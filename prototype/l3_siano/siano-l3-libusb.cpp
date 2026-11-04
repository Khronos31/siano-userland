/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Real USB-only host provider for the independent Siano L3 adapter.  It uses
 * libusb asynchronous bulk transfers and implements only the transport and
 * port-identification responsibilities; tune/stream/FW semantics stay in
 * siano-l3.cpp.
 */
#include "siano-l3-libusb.h"

#include <libusb.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

struct LibusbHost {
    libusb_context *context = nullptr;
    libusb_device *device = nullptr;
    libusb_device_handle *handle = nullptr;
    std::string error;
    bool claimed = false;
    bool preopened = false;
    bool closing = false;
    std::thread event_thread;
    std::mutex mutex;
    std::condition_variable condition;
    std::map<uint64_t, libusb_transfer *> transfers;
    uint8_t in_endpoint = 0x81U;
    uint8_t out_endpoint = 0x02U;
    uint8_t interface_number = 0U;
    uint32_t response_alignment = SIANO_L3_DEFAULT_RESPONSE_ALIGNMENT;
    dtv_l3_provider provider{};
};

struct TransferContext {
    LibusbHost *host = nullptr;
    dtv_l3_provider_binding *binding = nullptr;
    uint64_t id = 0;
    dtv_l3_transfer_complete_fn complete = nullptr;
    void *callback_context = nullptr;
    bool in = false;
    uint8_t *buffer = nullptr;
    size_t capacity = 0;
};

} // namespace

struct siano_l3_libusb_session {
    LibusbHost host;
    libusb_device *device = nullptr;
    siano_l3_adapter *adapter = nullptr;
    siano_l3_port port{};
};

namespace {

bool known_id(uint16_t vendor_id, uint16_t product_id)
{
    return (vendor_id == 0x3275U && product_id == 0x0080U) ||
           (vendor_id == 0x187fU && product_id == 0x0600U);
}

std::vector<libusb_device *> matching_devices(libusb_context *context)
{
    std::vector<libusb_device *> result;
    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(context, &list);
    if (count < 0) {
        return result;
    }
    for (ssize_t index = 0; index < count; ++index) {
        struct libusb_device_descriptor descriptor;
        if (libusb_get_device_descriptor(list[index], &descriptor) != 0) {
            continue;
        }
        if (known_id(descriptor.idVendor, descriptor.idProduct)) {
            result.push_back(libusb_ref_device(list[index]));
        }
    }
    libusb_free_device_list(list, 1);
    return result;
}

bool read_port(libusb_device *device, siano_l3_port *port,
               std::string *error)
{
    struct libusb_device_descriptor descriptor;
    struct libusb_config_descriptor *config = nullptr;
    bool found = false;
    if (libusb_get_device_descriptor(device, &descriptor) != 0 ||
        libusb_get_active_config_descriptor(device, &config) != 0) {
        *error = "cannot read descriptors";
        return false;
    }
    port->vendor_id = descriptor.idVendor;
    port->product_id = descriptor.idProduct;
    port->interface_number = 0U;
    port->in_endpoint = 0U;
    port->out_endpoint = 0U;
    port->response_alignment = SIANO_L3_DEFAULT_RESPONSE_ALIGNMENT;
    port->bus = libusb_get_bus_number(device);
    port->address = libusb_get_device_address(device);
    port->port_count = 0U;
    {
        uint8_t ports[8] = {0};
        const int count = libusb_get_port_numbers(device, ports, sizeof(ports));
        if (count > 0 && count <= static_cast<int>(sizeof(ports))) {
            port->port_count = static_cast<uint8_t>(count);
            for (int index = 0; index < count; ++index) {
                port->ports[index] = ports[index];
            }
        }
    }
    for (uint8_t interface_index = 0;
         interface_index < config->bNumInterfaces && !found;
         ++interface_index) {
        const struct libusb_interface &interface =
            config->interface[interface_index];
        if (interface.num_altsetting == 0) {
            continue;
        }
        const struct libusb_interface_descriptor &altsetting =
            interface.altsetting[0];
        if (altsetting.bInterfaceNumber != 0U) {
            continue;
        }
        uint16_t packet_size = 0;
        for (uint8_t endpoint_index = 0;
             endpoint_index < altsetting.bNumEndpoints; ++endpoint_index) {
            const struct libusb_endpoint_descriptor &endpoint =
                altsetting.endpoint[endpoint_index];
            if ((endpoint.bmAttributes & 0x03U) != LIBUSB_TRANSFER_TYPE_BULK) {
                continue;
            }
            if ((endpoint.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                LIBUSB_ENDPOINT_IN) {
                port->in_endpoint = endpoint.bEndpointAddress;
                packet_size = endpoint.wMaxPacketSize;
            } else {
                port->out_endpoint = endpoint.bEndpointAddress;
            }
        }
        if (port->in_endpoint != 0U && port->out_endpoint != 0U) {
            port->interface_number = altsetting.bInterfaceNumber;
            if (packet_size > 8U) {
                port->response_alignment = packet_size - 8U;
            }
            found = true;
        }
    }
    libusb_free_config_descriptor(config);
    if (!found) {
        *error = "no bulk IN/OUT endpoint pair on interface 0";
    }
    return found;
}

dtv_l3_result map_status(enum libusb_transfer_status status)
{
    switch (status) {
    case LIBUSB_TRANSFER_COMPLETED: return DTV_L3_OK;
    case LIBUSB_TRANSFER_TIMED_OUT: return DTV_L3_TIMEOUT;
    case LIBUSB_TRANSFER_CANCELLED: return DTV_L3_CANCELLED;
    case LIBUSB_TRANSFER_STALL: return DTV_L3_FAILED;
    case LIBUSB_TRANSFER_NO_DEVICE: return DTV_L3_DISCONNECTED;
    case LIBUSB_TRANSFER_OVERFLOW: return DTV_L3_FAILED;
    case LIBUSB_TRANSFER_ERROR:
    default: return DTV_L3_FAILED;
    }
}

void LIBUSB_CALL transfer_complete(struct libusb_transfer *transfer)
{
    auto *context = static_cast<TransferContext *>(transfer->user_data);
    if (context == nullptr) {
        return;
    }
    dtv_l3_transfer_completion completion{};
    completion.result = map_status(transfer->status);
    completion.data = context->in ? transfer->buffer : nullptr;
    completion.size = context->in ? transfer->actual_length : 0U;
    completion.actual_length = transfer->actual_length;
    completion.actual_length_known = 1U;
    if (context->complete != nullptr) {
        context->complete(context->binding, context->id, &completion,
                          context->callback_context);
    }
    {
        std::lock_guard<std::mutex> lock(context->host->mutex);
        context->host->transfers.erase(context->id);
        context->host->condition.notify_all();
    }
    delete[] context->buffer;
    libusb_free_transfer(transfer);
    delete context;
}

void event_loop(LibusbHost *host)
{
    while (true) {
        {
            std::lock_guard<std::mutex> lock(host->mutex);
            if (host->closing && host->transfers.empty()) {
                break;
            }
        }
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        (void)libusb_handle_events_timeout_completed(host->context, &timeout,
                                                     nullptr);
    }
}

dtv_l3_claim_result host_claim(void *context, dtv_l3_provider_binding *binding,
                               uint32_t claim_index)
{
    auto *host = static_cast<LibusbHost *>(context);
    (void)binding;
    if (claim_index != 0U || host->handle != nullptr) {
        return dtv_l3_claim_result{DTV_L3_FAILED, DTV_L3_CLAIM_NONE};
    }
    if (!host->preopened) {
        if (libusb_open(host->device, &host->handle) != 0) {
            host->handle = nullptr;
            return dtv_l3_claim_result{DTV_L3_DISCONNECTED, DTV_L3_CLAIM_NONE};
        }
    }
    if (libusb_claim_interface(host->handle, host->interface_number) != 0) {
        libusb_close(host->handle);
        host->handle = nullptr;
        return dtv_l3_claim_result{DTV_L3_BUSY, DTV_L3_CLAIM_NONE};
    }
    (void)libusb_clear_halt(host->handle, host->in_endpoint);
    (void)libusb_clear_halt(host->handle, host->out_endpoint);
    host->claimed = true;
    host->closing = false;
    host->event_thread = std::thread(event_loop, host);
    return dtv_l3_claim_result{DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
}

dtv_l3_release_result host_release_claim(void *context,
                                         dtv_l3_provider_binding *binding,
                                         uint32_t claim_index)
{
    auto *host = static_cast<LibusbHost *>(context);
    (void)binding;
    if (claim_index != 0U || !host->claimed) {
        return dtv_l3_release_result{DTV_L3_RELEASE_QUIESCED,
                                       DTV_L3_CLAIM_NONE,
                                       DTV_L3_RESTORE_NOT_ATTEMPTED,
                                       DTV_L3_OK};
    }
    {
        std::lock_guard<std::mutex> lock(host->mutex);
        host->closing = true;
        for (const auto &entry : host->transfers) {
            (void)libusb_cancel_transfer(entry.second);
        }
    }
    host->condition.notify_all();
    if (host->event_thread.joinable()) {
        host->event_thread.join();
    }
    bool quiesced = false;
    {
        std::unique_lock<std::mutex> lock(host->mutex);
        quiesced = host->condition.wait_for(
            lock, std::chrono::milliseconds(2000),
            [host] { return host->transfers.empty(); });
    }
    if (host->handle != nullptr) {
        (void)libusb_release_interface(host->handle, host->interface_number);
        libusb_close(host->handle);
        host->handle = nullptr;
    }
    host->claimed = false;
    return dtv_l3_release_result{
        quiesced ? DTV_L3_RELEASE_QUIESCED : DTV_L3_RELEASE_QUIESCENCE_UNKNOWN,
        quiesced ? DTV_L3_CLAIM_NONE : DTV_L3_CLAIM_UNKNOWN,
        DTV_L3_RESTORE_SUCCEEDED, quiesced ? DTV_L3_OK : DTV_L3_OUTCOME_UNKNOWN};
}

dtv_l3_result host_submit(void *context, dtv_l3_provider_binding *binding,
                          uint64_t transfer_id,
                          const dtv_l3_transfer_request *request,
                          dtv_l3_transfer_complete_fn complete,
                          void *callback_context)
{
    auto *host = static_cast<LibusbHost *>(context);
    if (request == nullptr || host->handle == nullptr || complete == nullptr) {
        return DTV_L3_INVALID;
    }
    const bool in = request->endpoint == host->in_endpoint;
    const size_t length = in ? request->in_capacity : request->out_size;
    if (length == 0U) {
        return DTV_L3_INVALID;
    }
    auto *transfer = libusb_alloc_transfer(0);
    if (transfer == nullptr) {
        return DTV_L3_FAILED;
    }
    auto *transfer_context = new TransferContext();
    transfer_context->host = host;
    transfer_context->binding = binding;
    transfer_context->id = transfer_id;
    transfer_context->complete = complete;
    transfer_context->callback_context = callback_context;
    transfer_context->in = in;
    transfer_context->capacity = length;
    transfer_context->buffer = new (std::nothrow) uint8_t[length];
    if (transfer_context->buffer == nullptr) {
        libusb_free_transfer(transfer);
        delete transfer_context;
        return DTV_L3_FAILED;
    }
    if (!in) {
        std::memcpy(transfer_context->buffer, request->out_data, length);
    }
    libusb_fill_bulk_transfer(transfer, host->handle, request->endpoint,
                              transfer_context->buffer,
                              static_cast<int>(length), transfer_complete,
                              transfer_context, request->timeout_ms);
    {
        std::lock_guard<std::mutex> lock(host->mutex);
        if (host->closing) {
            delete[] transfer_context->buffer;
            libusb_free_transfer(transfer);
            delete transfer_context;
            return DTV_L3_CLOSING;
        }
        host->transfers[transfer_id] = transfer;
    }
    if (libusb_submit_transfer(transfer) != 0) {
        {
            std::lock_guard<std::mutex> lock(host->mutex);
            host->transfers.erase(transfer_id);
        }
        delete[] transfer_context->buffer;
        libusb_free_transfer(transfer);
        delete transfer_context;
        return DTV_L3_FAILED;
    }
    return DTV_L3_ACCEPTED;
}

dtv_l3_result host_cancel(void *context, dtv_l3_provider_binding *binding,
                          uint64_t transfer_id)
{
    auto *host = static_cast<LibusbHost *>(context);
    (void)binding;
    libusb_transfer *transfer = nullptr;
    {
        std::lock_guard<std::mutex> lock(host->mutex);
        const auto found = host->transfers.find(transfer_id);
        if (found == host->transfers.end()) {
            return DTV_L3_STALE;
        }
        transfer = found->second;
    }
    return libusb_cancel_transfer(transfer) == 0 ? DTV_L3_OK : DTV_L3_FAILED;
}

} // namespace

extern "C" {

size_t siano_l3_libusb_device_count(void)
{
    libusb_context *context = nullptr;
    if (libusb_init(&context) != 0) {
        return 0;
    }
    const std::vector<libusb_device *> devices = matching_devices(context);
    for (libusb_device *device : devices) {
        libusb_unref_device(device);
    }
    libusb_exit(context);
    return devices.size();
}

int siano_l3_libusb_port(unsigned index, siano_l3_port *port)
{
    if (port == nullptr) {
        return 0;
    }
    libusb_context *context = nullptr;
    if (libusb_init(&context) != 0) {
        return 0;
    }
    const std::vector<libusb_device *> devices = matching_devices(context);
    int ok = 0;
    std::string error;
    if (index < devices.size()) {
        ok = read_port(devices[index], port, &error) ? 1 : 0;
    }
    for (libusb_device *device : devices) {
        libusb_unref_device(device);
    }
    libusb_exit(context);
    return ok;
}

dtv_l3_result siano_l3_libusb_session_open(
    unsigned index, const siano_l3_firmware *firmware,
    const siano_l3_options *options, siano_l3_libusb_session **session_out,
    char *error, size_t error_size)
{
    if (session_out == nullptr) {
        return DTV_L3_INVALID;
    }
    *session_out = nullptr;
    auto *session = new (std::nothrow) siano_l3_libusb_session();
    if (session == nullptr) {
        return DTV_L3_FAILED;
    }
    if (libusb_init(&session->host.context) != 0) {
        delete session;
        return DTV_L3_FAILED;
    }
    const std::vector<libusb_device *> devices =
        matching_devices(session->host.context);
    if (index >= devices.size()) {
        if (error != nullptr && error_size != 0U) {
            std::snprintf(error, error_size, "no matching device at index %u",
                          index);
        }
        for (libusb_device *device : devices) {
            libusb_unref_device(device);
        }
        libusb_exit(session->host.context);
        delete session;
        return DTV_L3_UNSUPPORTED;
    }
    session->host.device = devices[index];
    for (size_t other = 0; other < devices.size(); ++other) {
        if (other != index) {
            libusb_unref_device(devices[other]);
        }
    }
    std::string port_error;
    if (!read_port(session->host.device, &session->port, &port_error)) {
        if (error != nullptr && error_size != 0U) {
            std::snprintf(error, error_size, "%s", port_error.c_str());
        }
        libusb_unref_device(session->host.device);
        libusb_exit(session->host.context);
        delete session;
        return DTV_L3_UNSUPPORTED;
    }
    session->host.interface_number = session->port.interface_number;
    session->host.in_endpoint = session->port.in_endpoint;
    session->host.out_endpoint = session->port.out_endpoint;
    session->host.response_alignment = session->port.response_alignment;
    session->host.provider.provider_context = &session->host;
    session->host.provider.claim = host_claim;
    session->host.provider.release_claim = host_release_claim;
    session->host.provider.submit = host_submit;
    session->host.provider.cancel = host_cancel;
    session->host.provider.cancel_scope = DTV_L3_CANCEL_SCOPE_PIPE;
    const dtv_l3_result result = siano_l3_adapter_create(
        &session->host.provider, &session->port, firmware, options,
        &session->adapter);
    if (result != DTV_L3_OK) {
        libusb_unref_device(session->host.device);
        libusb_exit(session->host.context);
        delete session;
        return result;
    }
    *session_out = session;
    return DTV_L3_OK;
}


dtv_l3_result siano_l3_libusb_session_open_fd(
    int fd, const siano_l3_firmware *firmware, const siano_l3_options *options,
    siano_l3_libusb_session **session_out, char *error, size_t error_size)
{
    if (session_out == nullptr || fd < 0) {
        return DTV_L3_INVALID;
    }
    *session_out = nullptr;
    auto *session = new (std::nothrow) siano_l3_libusb_session();
    if (session == nullptr) {
        return DTV_L3_FAILED;
    }
    if (libusb_init(&session->host.context) != 0) {
        delete session;
        return DTV_L3_FAILED;
    }
    if (libusb_wrap_sys_device(session->host.context,
                               static_cast<intptr_t>(fd),
                               &session->host.handle) != 0 ||
        session->host.handle == nullptr) {
        if (error != nullptr && error_size != 0U) {
            std::snprintf(error, error_size, "cannot wrap USB fd");
        }
        libusb_exit(session->host.context);
        delete session;
        return DTV_L3_FAILED;
    }
    session->host.preopened = true;
    session->host.device = libusb_ref_device(
        libusb_get_device(session->host.handle));
    std::string port_error;
    if (session->host.device == nullptr ||
        !read_port(session->host.device, &session->port, &port_error)) {
        if (error != nullptr && error_size != 0U) {
            std::snprintf(error, error_size, "%s", port_error.c_str());
        }
        if (session->host.handle != nullptr) {
            libusb_close(session->host.handle);
        }
        if (session->host.device != nullptr) {
            libusb_unref_device(session->host.device);
        }
        libusb_exit(session->host.context);
        delete session;
        return DTV_L3_UNSUPPORTED;
    }
    session->host.interface_number = session->port.interface_number;
    session->host.in_endpoint = session->port.in_endpoint;
    session->host.out_endpoint = session->port.out_endpoint;
    session->host.response_alignment = session->port.response_alignment;
    session->host.provider.provider_context = &session->host;
    session->host.provider.claim = host_claim;
    session->host.provider.release_claim = host_release_claim;
    session->host.provider.submit = host_submit;
    session->host.provider.cancel = host_cancel;
    session->host.provider.cancel_scope = DTV_L3_CANCEL_SCOPE_PIPE;
    const dtv_l3_result result = siano_l3_adapter_create(
        &session->host.provider, &session->port, firmware, options,
        &session->adapter);
    if (result != DTV_L3_OK) {
        libusb_close(session->host.handle);
        libusb_unref_device(session->host.device);
        libusb_exit(session->host.context);
        delete session;
        return result;
    }
    *session_out = session;
    return DTV_L3_OK;
}

dtv_l3_result siano_l3_libusb_find_selector(
    const struct siano_device_selector *selector, const char *selector_text,
    unsigned *index_out, char *error, size_t error_size)
{
    (void)selector_text;
    if (selector == nullptr || index_out == nullptr) {
        return DTV_L3_INVALID;
    }
    libusb_context *context = nullptr;
    if (libusb_init(&context) != 0) {
        return DTV_L3_FAILED;
    }
    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(context, &list);
    unsigned matched = 0U;
    unsigned selected = 0U;
    bool found = false;
    if (count >= 0) {
        for (ssize_t raw = 0; raw < count; ++raw) {
            struct libusb_device_descriptor descriptor;
            if (libusb_get_device_descriptor(list[raw], &descriptor) != 0 ||
                !known_id(descriptor.idVendor, descriptor.idProduct)) {
                continue;
            }
            siano_l3_port port{};
            std::string port_error;
            if (!read_port(list[raw], &port, &port_error)) {
                continue;
            }
            bool matches = false;
            switch (selector->kind) {
            case SIANO_DEVICE_SELECTOR_INDEX:
                matches = selector->index == matched;
                break;
            case SIANO_DEVICE_SELECTOR_BUS_ADDRESS:
                matches = selector->bus == port.bus &&
                          selector->address == port.address;
                break;
            case SIANO_DEVICE_SELECTOR_PORT:
                matches = selector->bus == port.bus &&
                          selector->port_count == port.port_count;
                if (matches) {
                    for (unsigned i = 0; i < selector->port_count; ++i) {
                        if (selector->ports[i] != port.ports[i]) {
                            matches = false;
                            break;
                        }
                    }
                }
                break;
            default:
                break;
            }
            if (matches) {
                selected = matched;
                found = true;
                break;
            }
            ++matched;
        }
        libusb_free_device_list(list, 1);
    }
    libusb_exit(context);
    if (!found) {
        if (error != nullptr && error_size != 0U) {
            std::snprintf(error, error_size, "no matching device");
        }
        return DTV_L3_UNSUPPORTED;
    }
    *index_out = selected;
    return DTV_L3_OK;
}

const siano_l3_port *siano_l3_libusb_session_port(
    siano_l3_libusb_session *session)
{
    return session == nullptr ? nullptr : &session->port;
}

siano_l3_adapter *siano_l3_libusb_session_adapter(
    siano_l3_libusb_session *session)
{
    return session == nullptr ? nullptr : session->adapter;
}

void siano_l3_libusb_session_close(siano_l3_libusb_session *session)
{
    if (session == nullptr) {
        return;
    }
    if (session->adapter != nullptr) {
        siano_l3_adapter_destroy(session->adapter);
    }
    if (session->host.claimed) {
        (void)host_release_claim(&session->host, nullptr, 0U);
    }
    if (session->host.device != nullptr) {
        libusb_unref_device(session->host.device);
    }
    if (session->host.context != nullptr) {
        libusb_exit(session->host.context);
    }
    delete session;
}

} // extern "C"
