/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Independent Siano L3 product adapter.  The message numbers, lengths, and
 * sequence points come from the observation/fact files named in siano-l3.h.
 * The USB transport is supplied as a USB-only dtv_l3_provider; this file owns
 * tune, stream, PID, statistics, firmware download, and close semantics.
 */
#include "siano-l3.h"
extern "C" {
#include "protocol.h"
}

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr uint64_t kInternalTransferId = 0x8000000000000000ULL;
constexpr uint32_t kDefaultControlTimeoutMs = SIANO_L3_DEFAULT_CONTROL_TIMEOUT_MS;
constexpr uint32_t kDefaultVersionTimeoutMs = SIANO_L3_DEFAULT_VERSION_TIMEOUT_MS;
constexpr uint32_t kDefaultDownloadTimeoutMs = 2000U;
constexpr uint32_t kInTransferTimeoutMs = 2000U;

struct PendingCompletion {
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
    dtv_l3_result result = DTV_L3_FAILED;
    std::vector<uint8_t> message;
};

struct ResponseSlot {
    bool valid = false;
    std::vector<uint8_t> data;
};

struct StreamEntry {
    dtv_l3_ts_deliver_fn deliver = nullptr;
    void *context = nullptr;
};

struct InSlot {
    void *adapter = nullptr;
    uint32_t index = 0;
    uint64_t id = 0;
    bool active = false;
    bool done = false;
    dtv_l3_result result = DTV_L3_FAILED;
    size_t actual = 0;
    std::vector<uint8_t> data;
};

#ifdef SIANO_L3_HAS_CATALOG
const dtv_l3_receiver_capability kSianoReceivers[] = {
    {0U, DTV_L3_SYSTEM_ISDB_T, DTV_L3_PID_FILTER_HARDWARE,
     DTV_L3_SUPPORT_SUPPORTED},
};
const dtv_l3_shared_scope_capability kSianoSharedScopes[] = {
    {0U, DTV_L3_LEASE_SHAREABLE, DTV_L3_SUPPORT_SUPPORTED},
};
const dtv_l3_capability_catalog kSianoCatalog = {
    kSianoReceivers,
    1U,
    nullptr,
    0U,
    nullptr,
    0U,
    kSianoSharedScopes,
    1U,
    DTV_L3_SUPPORT_SUPPORTED,
};
#endif

} // namespace

struct siano_l3_adapter {
    dtv_l3_provider host{};
    siano_l3_port port{};
    siano_l3_options options{};
    siano_l3_firmware firmware{};

    std::mutex control_mutex;
    std::mutex response_mutex;
    std::condition_variable response_condition;
    std::map<uint16_t, ResponseSlot> responses;

    std::mutex in_mutex;
    std::condition_variable in_condition;
    std::thread in_thread;
    bool in_started = false;
    bool in_outstanding = false;
    bool in_complete = false;
    bool closing = false;
    dtv_l3_result in_result = DTV_L3_FAILED;
    size_t in_actual = 0;
    std::vector<uint8_t> in_copy;
    uint64_t in_transfer_id = 0;
    std::vector<InSlot> in_slots;
    std::vector<uint32_t> in_done_queue;
    uint32_t in_slots_done = 0;
    uint32_t in_flight_current = 0;
    uint32_t in_flight_max = 0;
    uint64_t in_submits = 0;
    uint64_t in_completions = 0;
    uint64_t owner_in_submits = 0;
    uint64_t owner_in_completions = 0;
    dtv_l3_context *owner = nullptr;
    dtv_l3_op *owner_op = nullptr;
    bool use_owner_io = false;
    std::array<uint8_t, SIANO_L3_USB2_BUFFER_SIZE> in_buffer{};

    std::mutex state_mutex;
    dtv_l3_provider_binding *binding = nullptr;
    bool claimed = false;
    bool ready = false;
    bool tuned = false;
    bool streaming = false;
    bool pid_sent = false;
    dtv_l3_result last_error = DTV_L3_OK;
    uint32_t chosen_boot_mode = SIANO_L3_BOOT_AUTO;
    uint32_t version_length = 0;
    uint32_t supported_protocols = 0;
    uint32_t firmware_id = 0;
    uint32_t firmware_downloaded = 0;
    uint32_t close_cancelled = 0;
    uint32_t signal_detected = 0;

    std::mutex stream_mutex;
    std::map<uint64_t, StreamEntry> streams;

    std::mutex ts_mutex;
    std::vector<uint8_t> ts_pending;
    std::array<uint8_t, 8192> last_cc{};
    std::array<uint8_t, 8192> cc_valid{};
    uint64_t ts_messages = 0;
    uint64_t ts_bytes = 0;
    uint64_t ts_packets = 0;
    uint64_t ts_bad_sync = 0;
    uint64_t ts_tei_errors = 0;
    uint64_t ts_continuity_errors = 0;
    uint64_t ts_filtered_packets = 0;
    uint64_t ts_delivered_packets = 0;

    std::thread stats_thread;
    std::atomic<bool> stats_stop{false};

    dtv_l3_provider exported{};
#ifdef SIANO_L3_SPLIT_API
    dtv_l3_usb_provider usb_view{};
    dtv_l3_product_adapter product_view{};
#endif

    std::mutex abandoned_mutex;
    std::vector<std::shared_ptr<PendingCompletion>> abandoned;

    std::mutex id_mutex;
    uint64_t next_internal_id = kInternalTransferId;
#ifdef SIANO_L3_FACTORY
    siano_l3_factory *factory_owner = nullptr;
#endif
};

#ifdef SIANO_L3_FACTORY
struct siano_l3_factory {
    dtv_l3_provider host{};
    siano_l3_port port{};
    siano_l3_options options{};
    std::mutex mutex;
    dtv_l3_provider_binding *binding = nullptr;
    siano_l3_adapter *adapter = nullptr;
    std::vector<uint8_t> firmware_copy;
    dtv_l3_usb_provider usb_view{};
    dtv_l3_factory core_view{};
};
#endif

namespace {

uint32_t first_nonzero(uint32_t value, uint32_t fallback)
{
    return value == 0 ? fallback : value;
}

uint32_t effective_alignment(const siano_l3_adapter *adapter)
{
    if (adapter->options.response_alignment != 0) {
        return adapter->options.response_alignment;
    }
    if (adapter->port.response_alignment != 0) {
        return adapter->port.response_alignment;
    }
    return SIANO_L3_DEFAULT_RESPONSE_ALIGNMENT;
}

uint32_t target_pid(const siano_l3_adapter *adapter)
{
    return adapter->options.pid == 0 ? SIANO_L3_DEFAULT_PID : adapter->options.pid;
}

bool pid_allowed(const siano_l3_adapter *adapter, uint16_t pid)
{
    if (adapter->options.pid_count == 0U) {
        return pid == target_pid(adapter);
    }
    const uint32_t count =
        adapter->options.pid_count > 64U ? 64U : adapter->options.pid_count;
    for (uint32_t index = 0; index < count; ++index) {
        if (adapter->options.pids[index] == pid) {
            return true;
        }
    }
    return false;
}

bool software_filter_enabled(const siano_l3_adapter *adapter)
{
    if (adapter->options.software_filter == SIANO_L3_FILTER_FORCE) {
        return true;
    }
    if (adapter->options.software_filter == SIANO_L3_FILTER_DISABLE) {
        return false;
    }
    return adapter->options.pid_count != 0U ||
           target_pid(adapter) != SIANO_L3_DEFAULT_PID;
}

bool port_supported(const siano_l3_port *port)
{
    const bool known_id =
        (port->vendor_id == 0x3275U && port->product_id == 0x0080U) ||
        (port->vendor_id == 0x187fU && port->product_id == 0x0600U);
    return known_id && port->interface_number == 0U &&
           port->in_endpoint != 0U && port->out_endpoint != 0U;
}

dtv_l3_provider_binding *current_binding(siano_l3_adapter *adapter)
{
    std::lock_guard<std::mutex> lock(adapter->state_mutex);
    return adapter->binding;
}

uint64_t allocate_internal_id(siano_l3_adapter *adapter)
{
    std::lock_guard<std::mutex> lock(adapter->id_mutex);
    return adapter->next_internal_id++;
}

uint8_t source_for(uint16_t type)
{
    switch (type) {
    case MSG_SMS_ADD_PID_FILTER_REQ:
    case MSG_SMS_GET_STATISTICS_EX_REQ:
    case MSG_SMS_ISDBT_TUNE_REQ:
        return 201U;
    default:
        return 0U;
    }
}

void set_error(siano_l3_adapter *adapter, dtv_l3_result result)
{
    std::lock_guard<std::mutex> lock(adapter->state_mutex);
    adapter->last_error = result;
}

void store_response(siano_l3_adapter *adapter, uint16_t type,
                    const uint8_t *data, size_t size)
{
    std::lock_guard<std::mutex> lock(adapter->response_mutex);
    ResponseSlot &slot = adapter->responses[type];
    slot.valid = true;
    slot.data.assign(data, data + size);
    adapter->response_condition.notify_all();
}

void clear_response(siano_l3_adapter *adapter, uint16_t type)
{
    std::lock_guard<std::mutex> lock(adapter->response_mutex);
    ResponseSlot &slot = adapter->responses[type];
    slot.valid = false;
    slot.data.clear();
}

dtv_l3_result wait_response(siano_l3_adapter *adapter, uint16_t type,
                            std::vector<uint8_t> *out, uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(adapter->response_mutex);
    const auto ready = [adapter, type] {
        return adapter->responses[type].valid;
    };
    if (!adapter->response_condition.wait_for(
            lock, std::chrono::milliseconds(timeout_ms), ready)) {
        return DTV_L3_TIMEOUT;
    }
    ResponseSlot &slot = adapter->responses[type];
    if (out != nullptr) {
        *out = slot.data;
    }
    slot.valid = false;
    slot.data.clear();
    return DTV_L3_OK;
}

void out_complete(dtv_l3_provider_binding *binding, uint64_t transfer_id,
                  const dtv_l3_transfer_completion *completion,
                  void *callback_context)
{
    (void)binding;
    (void)transfer_id;
    auto *pending = static_cast<PendingCompletion *>(callback_context);
    if (pending == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(pending->mutex);
    pending->result = completion == nullptr ? DTV_L3_FAILED : completion->result;
    pending->done = true;
    pending->condition.notify_all();
}

void abandon_pending(siano_l3_adapter *adapter,
                     const std::shared_ptr<PendingCompletion> &pending)
{
    std::lock_guard<std::mutex> lock(adapter->abandoned_mutex);
    adapter->abandoned.push_back(pending);
}

dtv_l3_result submit_message(siano_l3_adapter *adapter, uint16_t type,
                             const uint8_t *payload, size_t payload_len,
                             uint32_t timeout_ms)
{
    std::vector<uint8_t> message(SMS_HEADER_SIZE + payload_len);
    sms_pack_header(message.data(), type, source_for(type), SMS_HIF_TASK,
                    static_cast<uint16_t>(message.size()), 0U);
    if (payload_len != 0U) {
        std::memcpy(message.data() + SMS_HEADER_SIZE, payload, payload_len);
    }

    dtv_l3_provider_binding *binding = current_binding(adapter);
    if (binding == nullptr) {
        return DTV_L3_FAILED;
    }
    auto pending = std::make_shared<PendingCompletion>();
    pending->message = std::move(message);
    dtv_l3_transfer_request request{};
    request.kind = DTV_L3_TRANSFER_BULK;
    request.endpoint = adapter->port.out_endpoint;
    request.timeout_ms = timeout_ms;
    request.out_data = pending->message.data();
    request.out_size = pending->message.size();
    request.claim_index = 0U;
    const uint64_t id = allocate_internal_id(adapter);

    dtv_l3_result result = adapter->host.submit(
        adapter->host.provider_context, binding, id, &request, out_complete,
        pending.get());
    if (result != DTV_L3_ACCEPTED && result != DTV_L3_OK) {
        return result;
    }

    {
        std::unique_lock<std::mutex> lock(pending->mutex);
        pending->condition.wait_for(lock,
                                    std::chrono::milliseconds(timeout_ms + 1000U),
                                    [&pending] { return pending->done; });
    }
    if (!pending->done) {
        (void)adapter->host.cancel(adapter->host.provider_context, binding, id);
        std::unique_lock<std::mutex> lock(pending->mutex);
        pending->condition.wait_for(lock, std::chrono::milliseconds(1000U),
                                    [&pending] { return pending->done; });
    }
    if (!pending->done) {
        abandon_pending(adapter, pending);
        return DTV_L3_TIMEOUT;
    }
    return pending->result;
}

dtv_l3_result control_request(siano_l3_adapter *adapter, uint16_t type,
                              const uint8_t *payload, size_t payload_len,
                              uint16_t response_type, uint32_t timeout_ms,
                              std::vector<uint8_t> *response)
{
    std::lock_guard<std::mutex> lock(adapter->control_mutex);
    if (response_type != 0U) {
        clear_response(adapter, response_type);
    }
    dtv_l3_result result = submit_message(adapter, type, payload, payload_len,
                                          timeout_ms);
    if (result != DTV_L3_OK) {
        set_error(adapter, result);
        return result;
    }
    if (response_type == 0U) {
        return DTV_L3_OK;
    }
    result = wait_response(adapter, response_type, response, timeout_ms);
    if (result != DTV_L3_OK) {
        set_error(adapter, result);
    }
    return result;
}

int read_firmware(siano_l3_adapter *adapter, size_t offset, uint8_t *buffer,
                  size_t capacity, size_t *read_size)
{
    *read_size = 0;
    if (adapter->firmware.data != nullptr) {
        if (offset > adapter->firmware.size ||
            capacity > adapter->firmware.size - offset) {
            return -1;
        }
        std::memcpy(buffer, adapter->firmware.data + offset, capacity);
        *read_size = capacity;
        return 0;
    }
    if (adapter->firmware.read == nullptr) {
        return -1;
    }
    if (adapter->firmware.read(adapter->firmware.read_context, offset, buffer,
                               capacity, read_size) == 0) {
        return -1;
    }
    return *read_size == capacity ? 0 : -1;
}

dtv_l3_result version_probe(siano_l3_adapter *adapter)
{
    std::vector<uint8_t> response;
    dtv_l3_result result = control_request(
        adapter, MSG_SMS_GET_VERSION_EX_REQ, nullptr, 0U,
        MSG_SMS_GET_VERSION_EX_RES, adapter->options.version_timeout_ms,
        &response);
    if (result != DTV_L3_OK) {
        return result;
    }
    std::lock_guard<std::mutex> lock(adapter->state_mutex);
    adapter->version_length = static_cast<uint32_t>(response.size());
    adapter->supported_protocols =
        response.size() >= 14U ? static_cast<uint32_t>(response[13]) : 0U;
    adapter->firmware_id =
        response.size() >= 13U ? static_cast<uint32_t>(response[12]) : 0U;
    return DTV_L3_OK;
}

dtv_l3_result cold_init(siano_l3_adapter *adapter)
{
    if (adapter->firmware.size < 12U ||
        (adapter->firmware.data == nullptr && adapter->firmware.read == nullptr)) {
        return DTV_L3_UNSUPPORTED;
    }

    uint8_t header_bytes[12];
    size_t read_size = 0;
    if (read_firmware(adapter, 0U, header_bytes, sizeof(header_bytes),
                      &read_size) != 0) {
        return DTV_L3_FAILED;
    }
    struct sms_firmware_header header;
    header.check_sum = sms_get_le32(header_bytes + 0);
    header.length = sms_get_le32(header_bytes + 4);
    header.start_address = sms_get_le32(header_bytes + 8);
    const uint64_t download_size = 12ULL + header.length;
    if (download_size > adapter->firmware.size) {
        return DTV_L3_INVALID;
    }

    for (uint64_t offset = 0; offset < download_size;
         offset += SIANO_L3_FIRMWARE_CHUNK) {
        const size_t chunk = static_cast<size_t>(
            std::min<uint64_t>(SIANO_L3_FIRMWARE_CHUNK, download_size - offset));
        std::vector<uint8_t> payload(4U + chunk);
        sms_put_le32(payload.data(),
                     static_cast<uint32_t>(header.start_address + offset));
        if (read_firmware(adapter, static_cast<size_t>(offset),
                          payload.data() + 4U, chunk, &read_size) != 0) {
            return DTV_L3_FAILED;
        }
        dtv_l3_result result = control_request(
            adapter, MSG_SMS_DATA_DOWNLOAD_REQ, payload.data(), payload.size(),
            MSG_SMS_DATA_DOWNLOAD_RES, adapter->options.download_timeout_ms,
            nullptr);
        if (result != DTV_L3_OK) {
            return result;
        }
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->firmware_downloaded = 1U;
    }

    uint8_t validity[12];
    sms_put_le32(validity + 0, header.check_sum);
    sms_put_le32(validity + 4, header.length);
    sms_put_le32(validity + 8, header.start_address);
    dtv_l3_result result = control_request(
        adapter, MSG_SMS_DATA_VALIDITY_REQ, validity, sizeof(validity),
        MSG_SMS_DATA_VALIDITY_RES, adapter->options.control_timeout_ms,
        nullptr);
    if (result != DTV_L3_OK) {
        return result;
    }

    uint8_t trigger[20] = {0};
    result = control_request(
        adapter, MSG_SMS_SWDOWNLOAD_TRIGGER_REQ, trigger, sizeof(trigger),
        MSG_SMS_SWDOWNLOAD_TRIGGER_RES, adapter->options.control_timeout_ms,
        nullptr);
    if (result != DTV_L3_OK) {
        return result;
    }

    result = version_probe(adapter);
    if (result != DTV_L3_OK) {
        return result;
    }

    uint8_t mode[4];
    sms_put_le32(mode, SIANO_L3_ISDBT_BDA_MODE);
    return control_request(adapter, MSG_SMS_INIT_DEVICE_REQ, mode, sizeof(mode),
                           MSG_SMS_INIT_DEVICE_RES,
                           adapter->options.control_timeout_ms, nullptr);
}

void deliver_to_streams(siano_l3_adapter *adapter, const uint8_t *data,
                        size_t size)
{
    std::vector<StreamEntry> entries;
    {
        std::lock_guard<std::mutex> lock(adapter->stream_mutex);
        entries.reserve(adapter->streams.size());
        for (const auto &entry : adapter->streams) {
            entries.push_back(entry.second);
        }
    }
    for (const StreamEntry &entry : entries) {
        if (entry.deliver != nullptr) {
            entry.deliver(entry.context, data, size);
        }
    }
}

void handle_ts(siano_l3_adapter *adapter, const uint8_t *payload, size_t size)
{
    std::lock_guard<std::mutex> lock(adapter->ts_mutex);
    ++adapter->ts_messages;
    adapter->ts_bytes += size;
    adapter->ts_pending.insert(adapter->ts_pending.end(), payload,
                               payload + size);

    std::vector<uint8_t> output;
    size_t position = 0;
    while (adapter->ts_pending.size() - position >= 188U) {
        const uint8_t *packet = adapter->ts_pending.data() + position;
        if (packet[0] != 0x47U) {
            ++adapter->ts_bad_sync;
            ++position;
            continue;
        }
        ++adapter->ts_packets;
        const bool transport_error = (packet[1] & 0x80U) != 0U;
        if (transport_error) {
            ++adapter->ts_tei_errors;
            position += 188U;
            continue;
        }
        const uint16_t pid = static_cast<uint16_t>(
            (static_cast<uint16_t>(packet[1] & 0x1fU) << 8) | packet[2]);
        if (software_filter_enabled(adapter) && !pid_allowed(adapter, pid)) {
            ++adapter->ts_filtered_packets;
            position += 188U;
            continue;
        }
        if (pid != 0x1fffU) {
            const uint8_t afc = static_cast<uint8_t>((packet[3] >> 4) & 0x03U);
            const uint8_t cc = static_cast<uint8_t>(packet[3] & 0x0fU);
            if (afc == 2U) {
                /* Adaptation-only packets do not advance the counter. */
                adapter->last_cc[pid] = cc;
                adapter->cc_valid[pid] = 1U;
            } else {
                if (adapter->cc_valid[pid] != 0U &&
                    cc != static_cast<uint8_t>(
                              (adapter->last_cc[pid] + 1U) & 0x0fU)) {
                    ++adapter->ts_continuity_errors;
                }
                adapter->last_cc[pid] = cc;
                adapter->cc_valid[pid] = 1U;
            }
        }
        output.insert(output.end(), packet, packet + 188U);
        ++adapter->ts_delivered_packets;
        position += 188U;
    }
    adapter->ts_pending.erase(adapter->ts_pending.begin(),
                              adapter->ts_pending.begin() +
                                  static_cast<std::ptrdiff_t>(position));
    if (!output.empty()) {
        deliver_to_streams(adapter, output.data(), output.size());
    }
}

void route_message(siano_l3_adapter *adapter, const uint8_t *data, size_t size)
{
    struct sms_frame frame;
    if (sms_frame_message(data, size, effective_alignment(adapter), &frame) != 0) {
        return;
    }
    const uint8_t *message = data + frame.offset;
    const uint8_t *payload = data + frame.payload_offset;
    switch (frame.type) {
    case MSG_SMS_DVBT_BDA_DATA:
        handle_ts(adapter, payload, frame.payload_length);
        break;
    case MSG_SMS_GET_VERSION_EX_RES:
        store_response(adapter, frame.type, message, frame.length);
        break;
    case MSG_SMS_SIGNAL_DETECTED_IND:
        {
            std::lock_guard<std::mutex> lock(adapter->state_mutex);
            adapter->signal_detected = 1U;
        }
        store_response(adapter, frame.type, message, frame.length);
        break;
    case MSG_SMS_NO_SIGNAL_IND:
        {
            std::lock_guard<std::mutex> lock(adapter->state_mutex);
            adapter->signal_detected = 0U;
        }
        store_response(adapter, frame.type, message, frame.length);
        break;
    case MSG_SMS_DATA_DOWNLOAD_RES:
    case MSG_SMS_DATA_VALIDITY_RES:
    case MSG_SMS_SWDOWNLOAD_TRIGGER_RES:
    case MSG_SMS_INIT_DEVICE_RES:
    case MSG_SMS_ISDBT_TUNE_RES:
    case MSG_SMS_ADD_PID_FILTER_RES:
    case MSG_SMS_GET_STATISTICS_RES:
    case MSG_SMS_GET_STATISTICS_EX_RES:
        store_response(adapter, frame.type, message, frame.length);
        break;
    default:
        break;
    }
}

void in_complete(dtv_l3_provider_binding *binding, uint64_t transfer_id,
                 const dtv_l3_transfer_completion *completion,
                 void *callback_context)
{
    (void)binding;
    (void)transfer_id;
    auto *adapter = static_cast<siano_l3_adapter *>(callback_context);
    std::lock_guard<std::mutex> lock(adapter->in_mutex);
    adapter->in_result =
        completion == nullptr ? DTV_L3_FAILED : completion->result;
    adapter->in_actual = completion == nullptr ? 0U : completion->size;
    adapter->in_copy.clear();
    if (completion != nullptr && completion->data != nullptr &&
        completion->size != 0U) {
        adapter->in_copy.assign(completion->data,
                                completion->data + completion->size);
    }
    adapter->in_complete = true;
    adapter->in_outstanding = false;
    adapter->in_condition.notify_all();
}

void in_loop(siano_l3_adapter *adapter)
{
    while (true) {
        uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            if (adapter->closing) {
                break;
            }
            adapter->in_outstanding = true;
            adapter->in_complete = false;
            adapter->in_actual = 0;
            adapter->in_result = DTV_L3_FAILED;
            adapter->in_copy.clear();
            id = allocate_internal_id(adapter);
            adapter->in_transfer_id = id;
        }

        dtv_l3_provider_binding *binding = current_binding(adapter);
        if (binding == nullptr) {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            adapter->in_outstanding = false;
            break;
        }
        dtv_l3_transfer_request request{};
        request.kind = DTV_L3_TRANSFER_BULK;
        request.endpoint = adapter->port.in_endpoint;
        request.timeout_ms = kInTransferTimeoutMs;
        request.in_capacity = adapter->in_buffer.size();
        request.claim_index = 0U;
        dtv_l3_result submitted = adapter->host.submit(
            adapter->host.provider_context, binding, id, &request, in_complete,
            adapter);
        if (submitted != DTV_L3_ACCEPTED && submitted != DTV_L3_OK) {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            adapter->in_outstanding = false;
            if (adapter->closing) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::vector<uint8_t> raw;
        dtv_l3_result result = DTV_L3_FAILED;
        size_t actual = 0;
        bool completed = false;
        {
            std::unique_lock<std::mutex> lock(adapter->in_mutex);
            adapter->in_condition.wait_for(
                lock, std::chrono::milliseconds(kInTransferTimeoutMs + 1000U),
                [adapter] { return adapter->in_complete; });
            if (adapter->in_complete) {
                completed = true;
                result = adapter->in_result;
                actual = adapter->in_actual;
                raw = adapter->in_copy;
                adapter->in_complete = false;
                adapter->in_outstanding = false;
            }
        }
        if (!completed) {
            (void)adapter->host.cancel(adapter->host.provider_context, binding,
                                       id);
            std::unique_lock<std::mutex> lock(adapter->in_mutex);
            adapter->in_condition.wait_for(
                lock, std::chrono::milliseconds(1000U),
                [adapter] { return adapter->in_complete; });
            if (adapter->in_complete) {
                completed = true;
                result = adapter->in_result;
                actual = adapter->in_actual;
                raw = adapter->in_copy;
                adapter->in_complete = false;
                adapter->in_outstanding = false;
            }
        }
        if (completed && result == DTV_L3_OK && actual != 0U) {
            route_message(adapter, raw.data(), actual);
        }
        {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            if (adapter->closing) {
                break;
            }
        }
    }
}

void in_slot_complete(dtv_l3_provider_binding *binding, uint64_t transfer_id,
                      const dtv_l3_transfer_completion *completion,
                      void *callback_context)
{
    (void)binding;
    (void)transfer_id;
    auto *slot = static_cast<InSlot *>(callback_context);
    auto *adapter = static_cast<siano_l3_adapter *>(slot->adapter);
    std::lock_guard<std::mutex> lock(adapter->in_mutex);
    slot->done = true;
    slot->active = false;
    slot->result = completion == nullptr ? DTV_L3_FAILED : completion->result;
    slot->actual = completion == nullptr ? 0U : completion->size;
    slot->data.clear();
    if (completion != nullptr && completion->data != nullptr &&
        completion->size != 0U) {
        slot->data.assign(completion->data,
                          completion->data + completion->size);
    }
    ++adapter->in_slots_done;
    ++adapter->in_completions;
    adapter->in_done_queue.push_back(slot->index);
    if (adapter->in_flight_current != 0U) {
        --adapter->in_flight_current;
    }
    adapter->in_condition.notify_all();
}

dtv_l3_result submit_in_slot(siano_l3_adapter *adapter,
                              dtv_l3_provider_binding *binding,
                              InSlot *slot)
{
    uint64_t id = allocate_internal_id(adapter);
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        slot->id = id;
        slot->done = false;
        slot->active = false;
        slot->result = DTV_L3_FAILED;
        slot->actual = 0;
        slot->data.clear();
    }
    dtv_l3_transfer_request request{};
    request.kind = DTV_L3_TRANSFER_BULK;
    request.endpoint = adapter->port.in_endpoint;
    request.timeout_ms = kInTransferTimeoutMs;
    request.in_capacity = adapter->in_buffer.size();
    request.claim_index = 0U;
    dtv_l3_result submitted = adapter->host.submit(
        adapter->host.provider_context, binding, id, &request,
        in_slot_complete, slot);
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        if (submitted == DTV_L3_ACCEPTED || submitted == DTV_L3_OK) {
            ++adapter->in_submits;
            if (!slot->done) {
                slot->active = true;
                ++adapter->in_flight_current;
                if (adapter->in_flight_current > adapter->in_flight_max) {
                    adapter->in_flight_max = adapter->in_flight_current;
                }
            }
        } else if (!slot->done) {
            slot->done = true;
            slot->result = submitted;
            adapter->in_done_queue.push_back(slot->index);
        }
    }
    return submitted;
}

void in_loop_multi(siano_l3_adapter *adapter)
{
    uint32_t count = adapter->options.in_flight;
    if (count < 2U) {
        count = 2U;
    }
    if (count > 16U) {
        count = 16U;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        if (adapter->closing) {
            return;
        }
        adapter->in_slots.resize(count);
        adapter->in_done_queue.clear();
        adapter->in_slots_done = 0;
        adapter->in_flight_current = 0;
        for (uint32_t index = 0; index < count; ++index) {
            InSlot &slot = adapter->in_slots[index];
            slot.adapter = adapter;
            slot.index = index;
            slot.id = 0;
            slot.active = false;
            slot.done = false;
            slot.result = DTV_L3_FAILED;
            slot.actual = 0;
            slot.data.clear();
        }
    }
    dtv_l3_provider_binding *binding = current_binding(adapter);
    if (binding == nullptr) {
        return;
    }
    for (uint32_t index = 0; index < count; ++index) {
        (void)submit_in_slot(adapter, binding, &adapter->in_slots[index]);
    }

    while (true) {
        uint32_t index = 0;
        bool have_completion = false;
        {
            std::unique_lock<std::mutex> lock(adapter->in_mutex);
            adapter->in_condition.wait_for(
                lock, std::chrono::milliseconds(kInTransferTimeoutMs + 1000U),
                [adapter] {
                    return adapter->closing ||
                           !adapter->in_done_queue.empty();
                });
            if (adapter->closing && adapter->in_done_queue.empty()) {
                break;
            }
            if (!adapter->in_done_queue.empty()) {
                index = adapter->in_done_queue.front();
                adapter->in_done_queue.erase(
                    adapter->in_done_queue.begin());
                have_completion = true;
            }
        }
        if (!have_completion) {
            /* No completion within the deadline; release handles the rest. */
            break;
        }
        InSlot slot;
        {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            if (index >= adapter->in_slots.size()) {
                continue;
            }
            slot = adapter->in_slots[index];
        }
        if (slot.result == DTV_L3_OK && slot.actual != 0U &&
            !slot.data.empty()) {
            route_message(adapter, slot.data.data(), slot.actual);
        }
        if (current_binding(adapter) == nullptr) {
            break;
        }
        binding = current_binding(adapter);
        if (binding == nullptr) {
            break;
        }
        {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            if (adapter->closing) {
                break;
            }
        }
        (void)submit_in_slot(adapter, binding, &adapter->in_slots[index]);
    }
}

void owner_in_loop(siano_l3_adapter *adapter)
{
    while (true) {
        dtv_l3_context *owner = nullptr;
        {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            if (adapter->closing) {
                break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(adapter->state_mutex);
            owner = adapter->owner;
        }
        if (owner == nullptr) {
            break;
        }
        dtv_l3_transfer_request request{};
        request.kind = DTV_L3_TRANSFER_BULK;
        request.endpoint = adapter->port.in_endpoint;
        request.timeout_ms = kInTransferTimeoutMs;
        request.in_capacity = adapter->in_buffer.size();
        request.claim_index = 0U;
        dtv_l3_op *op = nullptr;
        dtv_l3_result result = dtv_l3_transfer_start(owner, &request, &op);
        if (result != DTV_L3_ACCEPTED || op == nullptr) {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            if (adapter->closing) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            ++adapter->owner_in_submits;
            adapter->owner_op = op;
            adapter->in_condition.notify_all();
            if (adapter->closing) {
                adapter->owner_op = nullptr;
            }
        }
        result = dtv_l3_op_wait(op, kInTransferTimeoutMs + 1000U);
        if (result == DTV_L3_WAIT_TIMEOUT) {
            (void)dtv_l3_op_cancel(op);
            (void)dtv_l3_op_wait(op, 1000U);
        }
        bool closing = false;
        {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            closing = adapter->closing;
        }
        dtv_l3_op_result op_result{};
        if (dtv_l3_op_poll(op, &op_result) == DTV_L3_OK &&
            op_result.state == DTV_L3_OP_DONE &&
            op_result.result == DTV_L3_OK && op_result.data != nullptr &&
            op_result.data_size != 0U) {
            route_message(adapter, op_result.data, op_result.data_size);
        }
        (void)dtv_l3_op_release(op);
        {
            std::lock_guard<std::mutex> lock(adapter->in_mutex);
            adapter->owner_op = nullptr;
            ++adapter->owner_in_completions;
            adapter->in_condition.notify_all();
        }
        if (closing) {
            break;
        }
    }
}

void stop_owner_in(siano_l3_adapter *adapter)
{
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        if (!adapter->use_owner_io) {
            return;
        }
    }
    dtv_l3_op *op = nullptr;
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        adapter->closing = true;
        op = adapter->owner_op;
        adapter->in_condition.notify_all();
    }
    if (op != nullptr) {
        (void)dtv_l3_op_cancel(op);
    }
    if (adapter->in_thread.joinable()) {
        adapter->in_thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        adapter->owner_op = nullptr;
        adapter->closing = false;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->use_owner_io = false;
    }
}

void stop_reader(siano_l3_adapter *adapter)
{
    uint64_t id = 0;
    std::vector<uint64_t> ids;
    dtv_l3_op *owner_op = nullptr;
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        adapter->closing = true;
        id = adapter->in_transfer_id;
        owner_op = adapter->owner_op;
        for (const InSlot &slot : adapter->in_slots) {
            if (slot.active && slot.id != 0U) {
                ids.push_back(slot.id);
            }
        }
        adapter->in_condition.notify_all();
    }
    if (owner_op != nullptr) {
        (void)dtv_l3_op_cancel(owner_op);
    }
    dtv_l3_provider_binding *binding = current_binding(adapter);
    if (binding != nullptr) {
        if (id != 0U) {
            (void)adapter->host.cancel(adapter->host.provider_context, binding,
                                       id);
        }
        for (uint64_t slot_id : ids) {
            (void)adapter->host.cancel(adapter->host.provider_context, binding,
                                       slot_id);
        }
    }
}

void reset_ts_stats(siano_l3_adapter *adapter)
{
    std::lock_guard<std::mutex> lock(adapter->ts_mutex);
    adapter->ts_pending.clear();
    adapter->last_cc.fill(0U);
    adapter->cc_valid.fill(0U);
    adapter->ts_messages = 0;
    adapter->ts_bytes = 0;
    adapter->ts_packets = 0;
    adapter->ts_bad_sync = 0;
    adapter->ts_tei_errors = 0;
    adapter->ts_continuity_errors = 0;
    adapter->ts_filtered_packets = 0;
    adapter->ts_delivered_packets = 0;
}

void stats_loop(siano_l3_adapter *adapter)
{
    const uint32_t poll_ms = adapter->options.stats_poll_ms;
    while (!adapter->stats_stop.load()) {
        for (uint32_t slept = 0; slept < poll_ms && !adapter->stats_stop.load();
             ++slept) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (adapter->stats_stop.load()) {
            break;
        }
        bool should_poll = false;
        {
            std::lock_guard<std::mutex> lock(adapter->state_mutex);
            should_poll = adapter->tuned && adapter->ready && !adapter->closing;
        }
        if (should_poll) {
            (void)control_request(adapter, MSG_SMS_GET_STATISTICS_EX_REQ,
                                  nullptr, 0U, 0U,
                                  adapter->options.control_timeout_ms, nullptr);
        }
    }
}

void switch_to_owner_in(siano_l3_adapter *adapter)
{
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        if (adapter->owner == nullptr || adapter->use_owner_io) {
            return;
        }
    }
    uint64_t id = 0;
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        adapter->closing = true;
        id = adapter->in_transfer_id;
        for (const InSlot &slot : adapter->in_slots) {
            if (slot.active && slot.id != 0U) {
                ids.push_back(slot.id);
            }
        }
        adapter->in_condition.notify_all();
    }
    dtv_l3_provider_binding *binding = current_binding(adapter);
    if (binding != nullptr) {
        if (id != 0U) {
            (void)adapter->host.cancel(adapter->host.provider_context, binding,
                                       id);
        }
        for (uint64_t slot_id : ids) {
            (void)adapter->host.cancel(adapter->host.provider_context, binding,
                                       slot_id);
        }
    }
    if (adapter->in_thread.joinable()) {
        adapter->in_thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        adapter->in_slots.clear();
        adapter->in_done_queue.clear();
        adapter->in_complete = false;
        adapter->in_outstanding = false;
        adapter->closing = false;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->use_owner_io = true;
    }
    dtv_l3_context *owner = nullptr;
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        owner = adapter->owner;
    }
    if (owner != nullptr && !adapter->in_thread.joinable()) {
        adapter->in_thread = std::thread(owner_in_loop, adapter);
        std::unique_lock<std::mutex> lock(adapter->in_mutex);
        adapter->in_condition.wait_for(
            lock, std::chrono::milliseconds(500),
            [adapter] {
                return adapter->closing || adapter->owner_in_submits != 0U;
            });
    }
}

void start_reader(siano_l3_adapter *adapter)
{
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        adapter->closing = false;
        adapter->in_complete = false;
        adapter->in_outstanding = false;
    }
    if (!adapter->in_thread.joinable()) {
        bool use_owner = false;
        {
            std::lock_guard<std::mutex> lock(adapter->state_mutex);
            use_owner = adapter->owner != nullptr && adapter->use_owner_io;
        }
        if (use_owner) {
            adapter->in_thread = std::thread(owner_in_loop, adapter);
        } else if (adapter->options.in_flight > 1U) {
            adapter->in_thread = std::thread(in_loop_multi, adapter);
        } else {
            adapter->in_thread = std::thread(in_loop, adapter);
        }
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        adapter->in_started = true;
    }
}

dtv_l3_result start_product(siano_l3_adapter *adapter)
{
    dtv_l3_result result = version_probe(adapter);
    if (result != DTV_L3_OK) {
        set_error(adapter, result);
        return result;
    }

    uint32_t mode = adapter->options.boot_mode;
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        if (mode == SIANO_L3_BOOT_AUTO) {
            const bool rom = adapter->firmware_id == 0xffU;
            mode = (!rom && adapter->supported_protocols != 0U)
                       ? SIANO_L3_BOOT_WARM
                       : SIANO_L3_BOOT_COLD;
        }
        adapter->chosen_boot_mode = mode;
    }
    if (mode == SIANO_L3_BOOT_COLD) {
        result = cold_init(adapter);
        if (result != DTV_L3_OK) {
            set_error(adapter, result);
            return result;
        }
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->ready = true;
    }
    return DTV_L3_OK;
}

dtv_l3_claim_result claim_cb(void *provider_context,
                             dtv_l3_provider_binding *binding,
                             uint32_t claim_index)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    if (claim_index != 0U) {
        return dtv_l3_claim_result{DTV_L3_FAILED, DTV_L3_CLAIM_NONE};
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        if (adapter->claimed) {
            return dtv_l3_claim_result{DTV_L3_BUSY, DTV_L3_CLAIM_NONE};
        }
    }
    if (!port_supported(&adapter->port)) {
        return dtv_l3_claim_result{DTV_L3_UNSUPPORTED, DTV_L3_CLAIM_NONE};
    }

    dtv_l3_claim_result claimed = adapter->host.claim(
        adapter->host.provider_context, binding, claim_index);
    if (claimed.result != DTV_L3_OK ||
        claimed.residual_state != DTV_L3_CLAIM_RETAINED) {
        return claimed;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->binding = binding;
        adapter->claimed = true;
        adapter->ready = false;
        adapter->tuned = false;
        adapter->streaming = false;
        adapter->pid_sent = false;
        adapter->close_cancelled = 0U;
        adapter->firmware_downloaded = 0U;
        adapter->signal_detected = 0U;
        adapter->version_length = 0U;
        adapter->supported_protocols = 0U;
        adapter->firmware_id = 0U;
        adapter->last_error = DTV_L3_OK;
    }
    start_reader(adapter);
    const dtv_l3_result result = start_product(adapter);
    if (result != DTV_L3_OK) {
        return dtv_l3_claim_result{result, DTV_L3_CLAIM_RETAINED};
    }
    return dtv_l3_claim_result{DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
}

dtv_l3_release_result release_claim_cb(void *provider_context,
                                       dtv_l3_provider_binding *binding,
                                       uint32_t claim_index)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    if (claim_index != 0U) {
        return dtv_l3_release_result{DTV_L3_RELEASE_QUIESCED,
                                       DTV_L3_CLAIM_NONE,
                                       DTV_L3_RESTORE_NOT_ATTEMPTED,
                                       DTV_L3_OK};
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        if (!adapter->claimed) {
            return dtv_l3_release_result{DTV_L3_RELEASE_QUIESCED,
                                           DTV_L3_CLAIM_NONE,
                                           DTV_L3_RESTORE_NOT_ATTEMPTED,
                                           DTV_L3_OK};
        }
    }

    stop_reader(adapter);
    dtv_l3_release_result released = adapter->host.release_claim(
        adapter->host.provider_context, binding, claim_index);
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        adapter->closing = true;
    }
    if (adapter->in_thread.joinable()) {
        adapter->in_thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        adapter->in_started = false;
        adapter->in_outstanding = false;
        adapter->in_complete = false;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->claimed = false;
        adapter->ready = false;
        adapter->tuned = false;
        adapter->streaming = false;
        adapter->binding = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->abandoned_mutex);
        adapter->abandoned.clear();
    }
    {
        std::lock_guard<std::mutex> lock(adapter->response_mutex);
        adapter->responses.clear();
    }
    if (released.quiescence == DTV_L3_RELEASE_QUIESCED &&
        released.residual_state == DTV_L3_CLAIM_NONE) {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->close_cancelled = 1U;
    }
    return released;
}

dtv_l3_result submit_cb(void *provider_context,
                        dtv_l3_provider_binding *binding,
                        uint64_t transfer_id,
                        const dtv_l3_transfer_request *request,
                        dtv_l3_transfer_complete_fn complete,
                        void *callback_context)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    (void)binding;
    return adapter->host.submit(adapter->host.provider_context, binding,
                                transfer_id, request, complete, callback_context);
}

dtv_l3_result cancel_cb(void *provider_context,
                        dtv_l3_provider_binding *binding,
                        uint64_t transfer_id)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    (void)binding;
    return adapter->host.cancel(adapter->host.provider_context, binding,
                                transfer_id);
}

dtv_l3_result scope_query_cb(void *provider_context, uint32_t receiver_id,
                             dtv_l3_scope_info *scope)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    (void)adapter;
    if (scope == nullptr || receiver_id != 0U) {
        return DTV_L3_UNSUPPORTED;
    }
    scope->bridge_id = 0U;
    scope->compatibility = DTV_L3_LEASE_SHAREABLE;
    return DTV_L3_OK;
}

dtv_l3_result bridge_start_cb(void *provider_context,
                              dtv_l3_provider_binding *binding,
                              uint32_t bridge_id, uint32_t receiver_id)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    (void)binding;
    if (bridge_id != 0U || receiver_id != 0U) {
        return DTV_L3_UNSUPPORTED;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        if (!adapter->ready || !adapter->tuned) {
            return DTV_L3_FAILED;
        }
    }
    switch_to_owner_in(adapter);
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->streaming = true;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        if (adapter->pid_sent) {
            return DTV_L3_OK;
        }
    }
    dtv_l3_result result = DTV_L3_OK;
    const uint32_t count = adapter->options.pid_count == 0U
                               ? 1U
                               : (adapter->options.pid_count > 64U
                                      ? 64U
                                      : adapter->options.pid_count);
    for (uint32_t index = 0; index < count; ++index) {
        uint8_t pid[4];
        const uint16_t value = adapter->options.pid_count == 0U
                                   ? static_cast<uint16_t>(target_pid(adapter))
                                   : adapter->options.pids[index];
        sms_put_le32(pid, value);
        result = control_request(adapter, MSG_SMS_ADD_PID_FILTER_REQ, pid,
                                 sizeof(pid), 0U,
                                 adapter->options.control_timeout_ms, nullptr);
        if (result != DTV_L3_OK) {
            break;
        }
    }
    if (result == DTV_L3_OK) {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->pid_sent = true;
    }
    return result;
}

dtv_l3_result bridge_stop_cb(void *provider_context,
                             dtv_l3_provider_binding *binding,
                             uint32_t bridge_id)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    (void)binding;
    if (bridge_id != 0U) {
        return DTV_L3_UNSUPPORTED;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->streaming = false;
    }
    stop_owner_in(adapter);
    return DTV_L3_OK;
}

dtv_l3_result stream_attach_cb(void *provider_context,
                               dtv_l3_provider_binding *binding,
                               uint64_t stream_id,
                               dtv_l3_ts_deliver_fn deliver,
                               void *callback_context)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    (void)binding;
    if (deliver == nullptr) {
        return DTV_L3_INVALID;
    }
    std::lock_guard<std::mutex> lock(adapter->stream_mutex);
    StreamEntry &entry = adapter->streams[stream_id];
    entry.deliver = deliver;
    entry.context = callback_context;
    return DTV_L3_OK;
}

dtv_l3_result stream_stop_cb(void *provider_context,
                             dtv_l3_provider_binding *binding,
                             uint64_t stream_id)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    (void)binding;
    StreamEntry entry;
    {
        std::lock_guard<std::mutex> lock(adapter->stream_mutex);
        const auto found = adapter->streams.find(stream_id);
        if (found == adapter->streams.end()) {
            return DTV_L3_STALE;
        }
        entry = found->second;
        adapter->streams.erase(found);
    }
    if (entry.deliver != nullptr) {
        entry.deliver(entry.context, nullptr, 0U);
    }
    return DTV_L3_OK;
}

dtv_l3_result tune_cb(void *provider_context,
                      dtv_l3_provider_binding *binding,
                      uint32_t receiver_id, uint64_t requested_hz,
                      uint64_t *programmed_hz)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    (void)binding;
    if (receiver_id != 0U || programmed_hz == nullptr ||
        requested_hz == 0U || requested_hz > 0xffffffffULL) {
        return DTV_L3_INVALID;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        if (!adapter->ready) {
            return DTV_L3_FAILED;
        }
    }
    uint8_t payload[16];
    sms_put_le32(payload + 0, static_cast<uint32_t>(requested_hz));
    sms_put_le32(payload + 4, BW_ISDBT_13SEG);
    sms_put_le32(payload + 8, 12000000U);
    sms_put_le32(payload + 12, 0U);
    dtv_l3_result result = control_request(
        adapter, MSG_SMS_ISDBT_TUNE_REQ, payload, sizeof(payload),
        MSG_SMS_ISDBT_TUNE_RES, adapter->options.control_timeout_ms, nullptr);
    if (result == DTV_L3_OK) {
        reset_ts_stats(adapter);
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->tuned = true;
        *programmed_hz = requested_hz;
    }
    return result;
}

dtv_l3_result claim_cancel_cb(void *provider_context,
                              dtv_l3_provider_binding *binding,
                              uint32_t claim_index)
{
    (void)provider_context;
    (void)binding;
    (void)claim_index;
    return DTV_L3_UNSUPPORTED;
}

constexpr uint32_t kSha256Constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

uint32_t sha_rotr(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32U - count));
}

void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t words[64];
    for (uint32_t index = 0; index < 16U; ++index) {
        words[index] = (static_cast<uint32_t>(block[index * 4U]) << 24) |
                       (static_cast<uint32_t>(block[index * 4U + 1U]) << 16) |
                       (static_cast<uint32_t>(block[index * 4U + 2U]) << 8) |
                       static_cast<uint32_t>(block[index * 4U + 3U]);
    }
    for (uint32_t index = 16; index < 64U; ++index) {
        const uint32_t s0 = sha_rotr(words[index - 15U], 7U) ^
                            sha_rotr(words[index - 15U], 18U) ^
                            (words[index - 15U] >> 3U);
        const uint32_t s1 = sha_rotr(words[index - 2U], 17U) ^
                            sha_rotr(words[index - 2U], 19U) ^
                            (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];
    for (uint32_t index = 0; index < 64U; ++index) {
        const uint32_t s1 = sha_rotr(e, 6U) ^ sha_rotr(e, 11U) ^
                            sha_rotr(e, 25U);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + s1 + ch + kSha256Constants[index] +
                               words[index];
        const uint32_t s0 = sha_rotr(a, 2U) ^ sha_rotr(a, 13U) ^
                            sha_rotr(a, 22U);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256_bytes(const uint8_t *data, size_t size, uint8_t digest[32])
{
    uint32_t state[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                         0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    const size_t full = size / 64U;
    for (size_t index = 0; index < full; ++index) {
        sha256_transform(state, data + index * 64U);
    }
    uint8_t tail[128] = {0};
    const size_t remainder = size % 64U;
    std::memcpy(tail, data + full * 64U, remainder);
    tail[remainder] = 0x80U;
    const size_t tail_size = remainder < 56U ? 64U : 128U;
    const uint64_t bit_length = static_cast<uint64_t>(size) * 8U;
    for (uint32_t index = 0; index < 8U; ++index) {
        tail[tail_size - 1U - index] =
            static_cast<uint8_t>(bit_length >> (index * 8U));
    }
    for (size_t offset = 0; offset < tail_size; offset += 64U) {
        sha256_transform(state, tail + offset);
    }
    for (uint32_t index = 0; index < 8U; ++index) {
        digest[index * 4U] = static_cast<uint8_t>(state[index] >> 24U);
        digest[index * 4U + 1U] = static_cast<uint8_t>(state[index] >> 16U);
        digest[index * 4U + 2U] = static_cast<uint8_t>(state[index] >> 8U);
        digest[index * 4U + 3U] = static_cast<uint8_t>(state[index]);
    }
}

#ifdef SIANO_L3_FACTORY
bool grant_matches(const dtv_l3_grant *grants, uint32_t count,
                   const siano_l3_port *port)
{
    if (grants == nullptr || count == 0U) {
        return false;
    }
    for (uint32_t index = 0; index < count; ++index) {
        const dtv_l3_grant_descriptor *descriptor = grants[index].descriptor;
        if (descriptor != nullptr && descriptor->vendor_id == port->vendor_id &&
            descriptor->product_id == port->product_id) {
            return true;
        }
    }
    return false;
}

bool read_file(const char *path, std::vector<uint8_t> *bytes)
{
    if (path == nullptr) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    bytes->assign(std::istreambuf_iterator<char>(file),
                  std::istreambuf_iterator<char>());
    return !bytes->empty();
}

dtv_l3_result prepare_firmware(siano_l3_factory *factory,
                               const dtv_l3_firmware *source,
                               siano_l3_firmware *firmware)
{
    std::memset(firmware, 0, sizeof(*firmware));
    factory->firmware_copy.clear();
    if (source == nullptr || source->source == DTV_L3_FIRMWARE_NONE) {
        return DTV_L3_OK;
    }
    const uint8_t *data = nullptr;
    size_t size = 0;
    switch (source->source) {
    case DTV_L3_FIRMWARE_BYTES:
        data = source->bytes;
        size = source->size;
        break;
    case DTV_L3_FIRMWARE_PATH:
        if (!read_file(source->path, &factory->firmware_copy)) {
            return DTV_L3_FAILED;
        }
        data = factory->firmware_copy.data();
        size = factory->firmware_copy.size();
        break;
    case DTV_L3_FIRMWARE_READ_CALLBACK:
        if (source->read == nullptr ||
            source->read(source->read_context, &data, &size) != DTV_L3_OK ||
            data == nullptr || size == 0U) {
            return DTV_L3_FAILED;
        }
        break;
    default:
        return DTV_L3_FAILED;
    }
    if (data == nullptr || size == 0U) {
        return DTV_L3_FAILED;
    }
    if (source->source != DTV_L3_FIRMWARE_PATH) {
        factory->firmware_copy.assign(data, data + size);
    }
    if (source->manifest != nullptr) {
        if (source->manifest->length_bytes != 0U &&
            source->manifest->length_bytes != size) {
            return DTV_L3_FAILED;
        }
        if (source->manifest->sha256 != nullptr) {
            uint8_t digest[32];
            sha256_bytes(factory->firmware_copy.data(),
                         factory->firmware_copy.size(), digest);
            if (std::memcmp(digest, source->manifest->sha256, 32U) != 0) {
                return DTV_L3_FAILED;
            }
        }
    }
    firmware->data = factory->firmware_copy.data();
    firmware->size = factory->firmware_copy.size();
    return DTV_L3_OK;
}

dtv_l3_claim_result factory_usb_claim(void *provider_context,
                                       dtv_l3_provider_binding *binding,
                                       uint32_t claim_index)
{
    auto *factory = static_cast<siano_l3_factory *>(provider_context);
    {
        std::lock_guard<std::mutex> lock(factory->mutex);
        factory->binding = binding;
        if (factory->adapter != nullptr) {
            siano_l3_adapter_set_binding(factory->adapter, binding);
        }
    }
    return factory->host.claim(factory->host.provider_context, binding,
                               claim_index);
}

dtv_l3_release_result factory_usb_release_claim(
    void *provider_context, dtv_l3_provider_binding *binding,
    uint32_t claim_index)
{
    auto *factory = static_cast<siano_l3_factory *>(provider_context);
    dtv_l3_release_result result = factory->host.release_claim(
        factory->host.provider_context, binding, claim_index);
    if (claim_index == 0U) {
        std::lock_guard<std::mutex> lock(factory->mutex);
        factory->binding = nullptr;
    }
    return result;
}

dtv_l3_result factory_usb_submit(
    void *provider_context, dtv_l3_provider_binding *binding,
    uint64_t transfer_id, const dtv_l3_transfer_request *request,
    dtv_l3_transfer_complete_fn complete, void *callback_context)
{
    auto *factory = static_cast<siano_l3_factory *>(provider_context);
    return factory->host.submit(factory->host.provider_context, binding,
                                transfer_id, request, complete,
                                callback_context);
}

dtv_l3_result factory_usb_cancel(void *provider_context,
                                 dtv_l3_provider_binding *binding,
                                 uint64_t transfer_id)
{
    auto *factory = static_cast<siano_l3_factory *>(provider_context);
    return factory->host.cancel(factory->host.provider_context, binding,
                                transfer_id);
}

dtv_l3_result factory_create(void *factory_context,
                             const dtv_l3_open_args *args,
                             const dtv_l3_product_adapter **adapter_out,
                             uint32_t *claim_count_out,
                             void **adapter_owner_out)
{
    auto *factory = static_cast<siano_l3_factory *>(factory_context);
    if (factory == nullptr || args == nullptr || adapter_out == nullptr ||
        claim_count_out == nullptr || adapter_owner_out == nullptr) {
        return DTV_L3_INVALID;
    }
    {
        std::lock_guard<std::mutex> lock(factory->mutex);
        if (factory->adapter != nullptr) {
            return DTV_L3_BUSY;
        }
    }
    if (!grant_matches(args->grants, args->grant_count, &factory->port)) {
        return DTV_L3_FAILED;
    }
    siano_l3_firmware firmware;
    dtv_l3_result result = prepare_firmware(factory, args->firmware, &firmware);
    if (result != DTV_L3_OK) {
        return result;
    }
    siano_l3_adapter *adapter = nullptr;
    result = siano_l3_adapter_create(&factory->host, &factory->port, &firmware,
                                     &factory->options, &adapter);
    if (result != DTV_L3_OK) {
        return result;
    }
    const dtv_l3_product_adapter *product =
        siano_l3_adapter_product_adapter(adapter);
    if (product == nullptr) {
        siano_l3_adapter_destroy(adapter);
        return DTV_L3_FAILED;
    }
    adapter->factory_owner = factory;
    {
        std::lock_guard<std::mutex> lock(factory->mutex);
        factory->adapter = adapter;
    }
    *adapter_out = product;
    *claim_count_out = 1U;
    *adapter_owner_out = adapter;
    return DTV_L3_OK;
}

void factory_release(void *adapter_owner)
{
    if (adapter_owner == nullptr) {
        return;
    }
    auto *adapter = static_cast<siano_l3_adapter *>(adapter_owner);
    siano_l3_factory *factory = adapter->factory_owner;
    if (factory != nullptr) {
        std::lock_guard<std::mutex> lock(factory->mutex);
        if (factory->adapter == adapter) {
            factory->adapter = nullptr;
        }
        factory->binding = nullptr;
    }
    siano_l3_adapter_destroy(adapter);
}
#endif

} // namespace

extern "C" {

dtv_l3_result siano_l3_adapter_create(const dtv_l3_provider *host,
                                      const siano_l3_port *port,
                                      const siano_l3_firmware *firmware,
                                      const siano_l3_options *options,
                                      siano_l3_adapter **adapter_out)
{
    if (host == nullptr || port == nullptr || adapter_out == nullptr ||
        host->claim == nullptr || host->release_claim == nullptr ||
        host->submit == nullptr || host->cancel == nullptr) {
        return DTV_L3_INVALID;
    }
    try {
        auto *adapter = new siano_l3_adapter();
        adapter->host = *host;
        adapter->port = *port;
        if (firmware != nullptr) {
            adapter->firmware = *firmware;
        }
        if (options != nullptr) {
            adapter->options = *options;
        }
        adapter->options.control_timeout_ms = first_nonzero(
            adapter->options.control_timeout_ms, kDefaultControlTimeoutMs);
        adapter->options.version_timeout_ms = first_nonzero(
            adapter->options.version_timeout_ms, kDefaultVersionTimeoutMs);
        adapter->options.download_timeout_ms = first_nonzero(
            adapter->options.download_timeout_ms, kDefaultDownloadTimeoutMs);
        adapter->options.response_alignment = effective_alignment(adapter);
        if (adapter->options.boot_mode > SIANO_L3_BOOT_WARM) {
            adapter->options.boot_mode = SIANO_L3_BOOT_AUTO;
        }
        if (adapter->options.software_filter > SIANO_L3_FILTER_DISABLE) {
            adapter->options.software_filter = SIANO_L3_FILTER_AUTO;
        }
        adapter->options.in_flight =
            first_nonzero(adapter->options.in_flight, 1U);
        if (adapter->options.in_flight > 16U) {
            adapter->options.in_flight = 16U;
        }

        adapter->exported = dtv_l3_provider{};
        adapter->exported.provider_context = adapter;
        adapter->exported.claim = claim_cb;
        adapter->exported.release_claim = release_claim_cb;
        adapter->exported.submit = submit_cb;
        adapter->exported.cancel = cancel_cb;
        adapter->exported.scope_query = scope_query_cb;
        adapter->exported.bridge_start = bridge_start_cb;
        adapter->exported.bridge_stop = bridge_stop_cb;
        adapter->exported.stream_attach = stream_attach_cb;
        adapter->exported.stream_stop = stream_stop_cb;
        adapter->exported.tune = tune_cb;
        adapter->exported.claim_cancel = claim_cancel_cb;
        adapter->exported.cancel_scope = adapter->host.cancel_scope;
        adapter->exported.logger = adapter->host.logger;
        if (adapter->options.stats_poll_ms != 0U) {
            adapter->stats_thread = std::thread(stats_loop, adapter);
        }
        *adapter_out = adapter;
        return DTV_L3_OK;
    } catch (...) {
        return DTV_L3_FAILED;
    }
}

void siano_l3_adapter_stop(siano_l3_adapter *adapter)
{
    if (adapter == nullptr) {
        return;
    }
    adapter->stats_stop.store(true);
    if (adapter->stats_thread.joinable()) {
        adapter->stats_thread.join();
    }
    stop_reader(adapter);
    if (adapter->in_thread.joinable()) {
        adapter->in_thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->claimed = false;
        adapter->ready = false;
        adapter->tuned = false;
        adapter->streaming = false;
        adapter->binding = nullptr;
        adapter->owner = nullptr;
        adapter->use_owner_io = false;
    }
}

void siano_l3_adapter_destroy(siano_l3_adapter *adapter)
{
    if (adapter == nullptr) {
        return;
    }
    siano_l3_adapter_stop(adapter);
    delete adapter;
}

const dtv_l3_provider *siano_l3_adapter_provider(siano_l3_adapter *adapter)
{
    if (adapter == nullptr) {
        return nullptr;
    }
    return &adapter->exported;
}

dtv_l3_result siano_l3_adapter_stats(siano_l3_adapter *adapter,
                                     siano_l3_stats *stats)
{
    if (adapter == nullptr || stats == nullptr) {
        return DTV_L3_INVALID;
    }
    std::memset(stats, 0, sizeof(*stats));
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        stats->boot_mode = adapter->chosen_boot_mode;
        stats->version_length = adapter->version_length;
        stats->supported_protocols = adapter->supported_protocols;
        stats->firmware_downloaded = adapter->firmware_downloaded;
        stats->pid_filter_sent = adapter->pid_sent ? 1U : 0U;
        stats->signal_detected = adapter->signal_detected;
        stats->close_cancelled = adapter->close_cancelled;
        stats->last_error = adapter->last_error;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->ts_mutex);
        stats->ts_messages = adapter->ts_messages;
        stats->ts_bytes = adapter->ts_bytes;
        stats->ts_packets = adapter->ts_packets;
        stats->ts_bad_sync = adapter->ts_bad_sync;
        stats->ts_tei_errors = adapter->ts_tei_errors;
        stats->ts_continuity_errors = adapter->ts_continuity_errors;
        stats->ts_filtered_packets = adapter->ts_filtered_packets;
        stats->ts_delivered_packets = adapter->ts_delivered_packets;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->in_mutex);
        stats->in_submits = adapter->in_submits;
        stats->in_completions = adapter->in_completions;
        stats->owner_in_submits = adapter->owner_in_submits;
        stats->owner_in_completions = adapter->owner_in_completions;
        stats->in_flight_max = adapter->in_flight_max;
    }
    return DTV_L3_OK;
}

dtv_l3_result siano_l3_adapter_poll_stats(siano_l3_adapter *adapter)
{
    if (adapter == nullptr) {
        return DTV_L3_INVALID;
    }
    return control_request(adapter, MSG_SMS_GET_STATISTICS_EX_REQ, nullptr, 0U,
                           0U, adapter->options.control_timeout_ms, nullptr);
}

#ifdef SIANO_L3_SPLIT_API
dtv_l3_claim_result split_usb_claim(void *provider_context,
                                   dtv_l3_provider_binding *binding,
                                   uint32_t claim_index)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->binding = binding;
    }
    return adapter->host.claim(adapter->host.provider_context, binding,
                               claim_index);
}

dtv_l3_release_result split_usb_release_claim(
    void *provider_context, dtv_l3_provider_binding *binding,
    uint32_t claim_index)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    dtv_l3_release_result result = adapter->host.release_claim(
        adapter->host.provider_context, binding, claim_index);
    if (claim_index == 0U) {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->binding = nullptr;
    }
    return result;
}

dtv_l3_result split_usb_submit(void *provider_context,
                               dtv_l3_provider_binding *binding,
                               uint64_t transfer_id,
                               const dtv_l3_transfer_request *request,
                               dtv_l3_transfer_complete_fn complete,
                               void *callback_context)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    return adapter->host.submit(adapter->host.provider_context, binding,
                                transfer_id, request, complete,
                                callback_context);
}

dtv_l3_result split_usb_cancel(void *provider_context,
                               dtv_l3_provider_binding *binding,
                               uint64_t transfer_id)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    return adapter->host.cancel(adapter->host.provider_context, binding,
                                transfer_id);
}

#ifdef SIANO_L3_SPLIT_INIT_HOOKS
dtv_l3_result split_bind_cb(void *provider_context, dtv_l3_context *owner)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    std::lock_guard<std::mutex> lock(adapter->state_mutex);
    adapter->owner = owner;
    return DTV_L3_OK;
}

dtv_l3_result split_initialize_cb(void *provider_context,
                                  dtv_l3_context *owner)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        adapter->owner = owner;
    }
    dtv_l3_provider_binding *binding = nullptr;
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        binding = adapter->binding;
    }
    if (binding == nullptr) {
        return DTV_L3_FAILED;
    }
    return siano_l3_adapter_attach(adapter, binding);
}
#endif

dtv_l3_result siano_l3_adapter_attach(siano_l3_adapter *adapter,
                                      dtv_l3_provider_binding *binding)
{
    if (adapter == nullptr || binding == nullptr) {
        return DTV_L3_INVALID;
    }
    {
        std::lock_guard<std::mutex> lock(adapter->state_mutex);
        if (adapter->ready && adapter->claimed) {
            return DTV_L3_OK;
        }
        adapter->binding = binding;
        adapter->claimed = true;
        adapter->ready = false;
        adapter->tuned = false;
        adapter->streaming = false;
        adapter->pid_sent = false;
        adapter->close_cancelled = 0U;
        adapter->firmware_downloaded = 0U;
        adapter->signal_detected = 0U;
        adapter->version_length = 0U;
        adapter->supported_protocols = 0U;
        adapter->firmware_id = 0U;
        adapter->last_error = DTV_L3_OK;
    }
    start_reader(adapter);
    return start_product(adapter);
}

dtv_l3_result split_tune_cb(void *provider_context,
                            dtv_l3_provider_binding *binding,
                            uint32_t receiver_id, uint64_t requested_hz,
                            uint64_t *programmed_hz)
{
    auto *adapter = static_cast<siano_l3_adapter *>(provider_context);
    dtv_l3_result result = siano_l3_adapter_attach(adapter, binding);
    if (result != DTV_L3_OK) {
        return result;
    }
    return tune_cb(provider_context, binding, receiver_id, requested_hz,
                   programmed_hz);
}

const dtv_l3_usb_provider *siano_l3_adapter_usb_provider(
    siano_l3_adapter *adapter)
{
    if (adapter == nullptr) {
        return nullptr;
    }
    adapter->usb_view = dtv_l3_usb_provider{};
    adapter->usb_view.provider_context = adapter;
    adapter->usb_view.claim = split_usb_claim;
    adapter->usb_view.release_claim = split_usb_release_claim;
    adapter->usb_view.submit = split_usb_submit;
    adapter->usb_view.cancel = split_usb_cancel;
    adapter->usb_view.claim_cancel = adapter->host.claim_cancel;
    adapter->usb_view.cancel_scope = adapter->host.cancel_scope;
    adapter->usb_view.logger = adapter->host.logger;
    return &adapter->usb_view;
}

const dtv_l3_product_adapter *siano_l3_adapter_product_adapter(
    siano_l3_adapter *adapter)
{
    if (adapter == nullptr) {
        return nullptr;
    }
    adapter->product_view = dtv_l3_product_adapter{};
    adapter->product_view.product_context = adapter;
    adapter->product_view.capabilities.receiver_count = 1U;
    adapter->product_view.capabilities.reader_count = 0U;
    adapter->product_view.capabilities.cancel_scope =
        adapter->host.cancel_scope;
    adapter->product_view.firmware = nullptr;
#ifdef SIANO_L3_HAS_CATALOG
    adapter->product_view.catalog = &kSianoCatalog;
#endif
#ifdef SIANO_L3_SPLIT_INIT_HOOKS
    adapter->product_view.bind = split_bind_cb;
    adapter->product_view.initialize = split_initialize_cb;
#endif
    adapter->product_view.scope_query = scope_query_cb;
    adapter->product_view.tune = split_tune_cb;
    adapter->product_view.bridge_start = bridge_start_cb;
    adapter->product_view.bridge_stop = bridge_stop_cb;
    adapter->product_view.stream_attach = stream_attach_cb;
    adapter->product_view.stream_stop = stream_stop_cb;
    return &adapter->product_view;
}
#endif

#ifdef SIANO_L3_FACTORY
void siano_l3_adapter_set_binding(siano_l3_adapter *adapter,
                                  dtv_l3_provider_binding *binding)
{
    if (adapter == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(adapter->state_mutex);
    adapter->binding = binding;
}

dtv_l3_result siano_l3_factory_create(const dtv_l3_provider *host,
                                      const siano_l3_port *port,
                                      const siano_l3_options *options,
                                      siano_l3_factory **factory_out)
{
    if (host == nullptr || port == nullptr || factory_out == nullptr ||
        host->claim == nullptr || host->release_claim == nullptr ||
        host->submit == nullptr || host->cancel == nullptr ||
        !port_supported(port)) {
        return DTV_L3_INVALID;
    }
    try {
        auto *factory = new siano_l3_factory();
        factory->host = *host;
        factory->port = *port;
        if (options != nullptr) {
            factory->options = *options;
        }
        factory->usb_view.provider_context = factory;
        factory->usb_view.claim = factory_usb_claim;
        factory->usb_view.release_claim = factory_usb_release_claim;
        factory->usb_view.submit = factory_usb_submit;
        factory->usb_view.cancel = factory_usb_cancel;
        factory->usb_view.claim_cancel = factory->host.claim_cancel;
        factory->usb_view.cancel_scope = factory->host.cancel_scope;
        factory->usb_view.logger = factory->host.logger;
        factory->core_view.struct_size = sizeof(dtv_l3_factory);
        factory->core_view.factory_context = factory;
        factory->core_view.create = factory_create;
        factory->core_view.release = factory_release;
        *factory_out = factory;
        return DTV_L3_OK;
    } catch (...) {
        return DTV_L3_FAILED;
    }
}

void siano_l3_factory_destroy(siano_l3_factory *factory)
{
    if (factory == nullptr) {
        return;
    }
    siano_l3_adapter *adapter = nullptr;
    {
        std::lock_guard<std::mutex> lock(factory->mutex);
        adapter = factory->adapter;
        factory->adapter = nullptr;
    }
    if (adapter != nullptr) {
        siano_l3_adapter_destroy(adapter);
    }
    delete factory;
}

const dtv_l3_usb_provider *siano_l3_factory_usb_provider(
    siano_l3_factory *factory)
{
    return factory == nullptr ? nullptr : &factory->usb_view;
}

const dtv_l3_factory *siano_l3_factory_core(siano_l3_factory *factory)
{
    return factory == nullptr ? nullptr : &factory->core_view;
}
#endif

} // extern "C"
