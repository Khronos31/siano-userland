/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Test-only USB-only host provider.  It replays the control sequence described
 * by a fixture file and completes IN transfers in the order the fixture lists
 * them.  It is not a libusb binding and is not part of the product path.
 */
#include "siano-l3-test-host.h"
extern "C" {
#include "protocol.h"
}

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kResponseAlignment = SIANO_L3_DEFAULT_RESPONSE_ALIGNMENT;

struct FixtureEvent {
    bool in = false;
    uint16_t type = 0;
    uint16_t len = 0;
    uint32_t count = 1;
    std::vector<uint8_t> payload;
};

struct PendingIn {
    bool active = false;
    uint64_t id = 0;
    dtv_l3_provider_binding *binding = nullptr;
    uint8_t *buffer = nullptr;
    size_t capacity = 0;
    dtv_l3_transfer_complete_fn complete = nullptr;
    void *context = nullptr;
};

struct Injection {
    uint16_t type = 0;
    std::vector<uint8_t> raw;
};

} // namespace

struct siano_l3_test_host {
    std::mutex mutex;
    std::vector<FixtureEvent> out_events;
    std::vector<FixtureEvent> in_events;
    size_t out_cursor = 0;
    size_t in_cursor = 0;
    std::vector<Injection> injected;
    std::vector<PendingIn> pending;
    size_t max_pending = 0;
    bool waiting_in = false;
    uint16_t expected_in = 0;
    std::vector<uint16_t> out_types;
    std::vector<uint16_t> out_lengths;
    std::string error;
    bool claimed = false;
    siano_l3_port port{};
    dtv_l3_provider provider{};
    std::array<std::array<uint8_t, SIANO_L3_USB2_BUFFER_SIZE>, 16> in_buffers{};
};

namespace {

uint16_t expected_response(uint16_t type)
{
    switch (type) {
    case MSG_SMS_GET_VERSION_EX_REQ: return MSG_SMS_GET_VERSION_EX_RES;
    case MSG_SMS_DATA_DOWNLOAD_REQ: return MSG_SMS_DATA_DOWNLOAD_RES;
    case MSG_SMS_DATA_VALIDITY_REQ: return MSG_SMS_DATA_VALIDITY_RES;
    case MSG_SMS_SWDOWNLOAD_TRIGGER_REQ: return MSG_SMS_SWDOWNLOAD_TRIGGER_RES;
    case MSG_SMS_INIT_DEVICE_REQ: return MSG_SMS_INIT_DEVICE_RES;
    case MSG_SMS_ISDBT_TUNE_REQ: return MSG_SMS_ISDBT_TUNE_RES;
    case MSG_SMS_GET_STATISTICS_REQ: return MSG_SMS_GET_STATISTICS_RES;
    case MSG_SMS_GET_STATISTICS_EX_REQ: return MSG_SMS_GET_STATISTICS_EX_RES;
    default: return 0;
    }
}

int parse_field_int(const std::string &line, const char *name, long *value)
{
    const std::string key = std::string("\"") + name + "\":";
    const size_t at = line.find(key);
    if (at == std::string::npos) {
        return 0;
    }
    const char *start = line.c_str() + at + key.size();
    char *end = nullptr;
    long parsed = std::strtol(start, &end, 0);
    if (end == start) {
        return 0;
    }
    *value = parsed;
    return 1;
}

bool parse_line(const std::string &line, FixtureEvent *event, std::string *error)
{
    if (line.find("\"dir\":\"out\"") != std::string::npos) {
        event->in = false;
    } else if (line.find("\"dir\":\"in\"") != std::string::npos) {
        event->in = true;
    } else {
        *error = "fixture line has no dir";
        return false;
    }
    long value = 0;
    if (!parse_field_int(line, "type", &value)) {
        *error = "fixture line has no type";
        return false;
    }
    event->type = static_cast<uint16_t>(value);
    if (!parse_field_int(line, "len", &value)) {
        *error = "fixture line has no len";
        return false;
    }
    event->len = static_cast<uint16_t>(value);
    if (parse_field_int(line, "count", &value) && value > 0) {
        event->count = static_cast<uint32_t>(value);
    }
    if (event->in) {
        event->payload.assign(event->len, 0U);
        long offset = 0;
        long byte = 0;
        if (parse_field_int(line, "set_offset", &offset) &&
            parse_field_int(line, "set_byte", &byte) && offset >= 0 &&
            offset < static_cast<long>(event->len)) {
            event->payload[static_cast<size_t>(offset)] =
                static_cast<uint8_t>(byte);
        }
    }
    return true;
}

bool load_fixture(const char *path, std::vector<FixtureEvent> *out,
                  std::vector<FixtureEvent> *in, std::string *error)
{
    std::ifstream file(path);
    if (!file) {
        *error = std::string("cannot open fixture ") + path;
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        const size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] != '{') {
            continue;
        }
        FixtureEvent event;
        if (!parse_line(line, &event, error)) {
            return false;
        }
        if (event.in) {
            in->push_back(event);
        } else {
            out->push_back(event);
        }
    }
    return true;
}

void deliver_in(siano_l3_test_host *host)
{
    PendingIn pending;
    std::vector<uint8_t> raw;
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(host->mutex);
        size_t pending_index = host->pending.size();
        for (size_t index = 0; index < host->pending.size(); ++index) {
            if (host->pending[index].active) {
                pending_index = index;
                break;
            }
        }
        if (pending_index == host->pending.size()) {
            return;
        }
        if (!host->injected.empty()) {
            raw = host->injected.front().raw;
            host->injected.erase(host->injected.begin());
        } else if (host->waiting_in && host->in_cursor < host->in_events.size()) {
            FixtureEvent &event = host->in_events[host->in_cursor];
            raw = event.payload;
            if (raw.size() >= SMS_HEADER_SIZE) {
                sms_pack_header(raw.data(), event.type, 0U, SMS_HIF_TASK,
                                event.len, 0U);
            }
            if (event.count > 1U) {
                --event.count;
            } else {
                ++host->in_cursor;
            }
            if (event.type == host->expected_in) {
                host->waiting_in = false;
                host->expected_in = 0;
            }
        } else {
            return;
        }
        pending = host->pending[pending_index];
        host->pending[pending_index].active = false;
        have = true;
    }
    if (!have) {
        return;
    }
    const size_t copied = raw.size() < pending.capacity ? raw.size() : pending.capacity;
    if (copied != 0U) {
        std::memcpy(pending.buffer, raw.data(), copied);
    }
    dtv_l3_transfer_completion completion{};
    completion.result = DTV_L3_OK;
    completion.data = pending.buffer;
    completion.size = copied;
    completion.actual_length = copied;
    completion.actual_length_known = 1U;
    if (pending.complete != nullptr) {
        pending.complete(pending.binding, pending.id, &completion,
                         pending.context);
    }
}

dtv_l3_claim_result test_claim(void *context, dtv_l3_provider_binding *binding,
                               uint32_t claim_index)
{
    auto *host = static_cast<siano_l3_test_host *>(context);
    (void)binding;
    if (claim_index != 0U) {
        return dtv_l3_claim_result{DTV_L3_FAILED, DTV_L3_CLAIM_NONE};
    }
    std::lock_guard<std::mutex> lock(host->mutex);
    host->claimed = true;
    return dtv_l3_claim_result{DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
}

dtv_l3_release_result test_release_claim(void *context,
                                         dtv_l3_provider_binding *binding,
                                         uint32_t claim_index)
{
    auto *host = static_cast<siano_l3_test_host *>(context);
    (void)binding;
    if (claim_index != 0U) {
        return dtv_l3_release_result{DTV_L3_RELEASE_QUIESCED,
                                       DTV_L3_CLAIM_NONE,
                                       DTV_L3_RESTORE_SUCCEEDED, DTV_L3_OK};
    }
    std::vector<PendingIn> pending;
    {
        std::lock_guard<std::mutex> lock(host->mutex);
        host->claimed = false;
        pending.swap(host->pending);
    }
    for (const PendingIn &entry : pending) {
        if (entry.active && entry.complete != nullptr) {
            dtv_l3_transfer_completion completion{};
            completion.result = DTV_L3_CANCELLED;
            completion.data = nullptr;
            completion.size = 0;
            completion.actual_length = 0;
            completion.actual_length_known = 1U;
            entry.complete(entry.binding, entry.id, &completion, entry.context);
        }
    }
    return dtv_l3_release_result{DTV_L3_RELEASE_QUIESCED, DTV_L3_CLAIM_NONE,
                                   DTV_L3_RESTORE_SUCCEEDED, DTV_L3_OK};
}

dtv_l3_result test_submit(void *context, dtv_l3_provider_binding *binding,
                          uint64_t transfer_id,
                          const dtv_l3_transfer_request *request,
                          dtv_l3_transfer_complete_fn complete,
                          void *callback_context)
{
    auto *host = static_cast<siano_l3_test_host *>(context);
    (void)binding;
    (void)transfer_id;
    if (request == nullptr) {
        return DTV_L3_INVALID;
    }
    if (request->endpoint == host->port.in_endpoint) {
        {
            std::lock_guard<std::mutex> lock(host->mutex);
            if (request->in_capacity == 0U || request->in_capacity > SIANO_L3_USB2_BUFFER_SIZE) {
                return DTV_L3_INVALID;
            }
            size_t slot = host->pending.size();
            for (size_t index = 0; index < host->pending.size(); ++index) {
                if (!host->pending[index].active) {
                    slot = index;
                    break;
                }
            }
            if (slot == host->pending.size()) {
                if (slot >= host->in_buffers.size()) {
                    return DTV_L3_INVALID;
                }
                host->pending.resize(slot + 1);
            }
            PendingIn &entry = host->pending[slot];
            entry.active = true;
            entry.id = transfer_id;
            entry.binding = binding;
            entry.buffer = host->in_buffers[slot].data();
            entry.capacity = request->in_capacity;
            entry.complete = complete;
            entry.context = callback_context;
            unsigned int active = 0;
            for (const PendingIn &p : host->pending) {
                if (p.active) {
                    ++active;
                }
            }
            if (active > host->max_pending) {
                host->max_pending = active;
            }
        }
        deliver_in(host);
        return DTV_L3_ACCEPTED;
    }

    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(host->mutex);
        if (host->out_cursor >= host->out_events.size()) {
            host->error = "unexpected OUT transfer";
            return DTV_L3_FAILED;
        }
        FixtureEvent &event = host->out_events[host->out_cursor];
        if (event.type == 0U || event.len != request->out_size) {
            host->error = "OUT type/length mismatch";
            return DTV_L3_FAILED;
        }
        if (event.type != sms_get_le16(request->out_data)) {
            host->error = "OUT type mismatch";
            return DTV_L3_FAILED;
        }
        host->out_types.push_back(event.type);
        host->out_lengths.push_back(static_cast<uint16_t>(request->out_size));
        if (event.count > 1U) {
            --event.count;
        } else {
            ++host->out_cursor;
        }
        host->expected_in = expected_response(event.type);
        host->waiting_in = host->expected_in != 0U;
        ok = true;
    }
    if (!ok) {
        return DTV_L3_FAILED;
    }
    deliver_in(host);
    if (complete != nullptr) {
        dtv_l3_transfer_completion completion{};
        completion.result = DTV_L3_OK;
        completion.data = request->out_data;
        completion.size = request->out_size;
        completion.actual_length = request->out_size;
        completion.actual_length_known = 1U;
        complete(binding, transfer_id, &completion, callback_context);
    }
    return DTV_L3_ACCEPTED;
}

dtv_l3_result test_cancel(void *context, dtv_l3_provider_binding *binding,
                          uint64_t transfer_id)
{
    auto *host = static_cast<siano_l3_test_host *>(context);
    (void)binding;
    PendingIn pending;
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(host->mutex);
        for (PendingIn &entry : host->pending) {
            if (entry.active && entry.id == transfer_id) {
                pending = entry;
                entry.active = false;
                have = true;
                break;
            }
        }
    }
    if (have && pending.complete != nullptr) {
        dtv_l3_transfer_completion completion{};
        completion.result = DTV_L3_CANCELLED;
        completion.data = nullptr;
        completion.size = 0;
        completion.actual_length = 0;
        completion.actual_length_known = 1U;
        pending.complete(pending.binding, pending.id, &completion,
                         pending.context);
    }
    return DTV_L3_OK;
}

} // namespace

extern "C" {

int siano_l3_test_host_create(const char *fixture_path,
                              siano_l3_test_host **host_out,
                              char *error, size_t error_size)
{
    if (fixture_path == nullptr || host_out == nullptr) {
        return 0;
    }
    auto *host = new siano_l3_test_host();
    std::string load_error;
    std::vector<FixtureEvent> out_events;
    std::vector<FixtureEvent> in_events;
    if (!load_fixture(fixture_path, &out_events, &in_events, &load_error)) {
        if (error != nullptr && error_size != 0U) {
            std::snprintf(error, error_size, "%s", load_error.c_str());
        }
        delete host;
        return 0;
    }
    host->out_events = std::move(out_events);
    host->in_events = std::move(in_events);
    host->port.vendor_id = 0x3275U;
    host->port.product_id = 0x0080U;
    host->port.interface_number = 0U;
    host->port.in_endpoint = 0x81U;
    host->port.out_endpoint = 0x02U;
    host->port.response_alignment = kResponseAlignment;
    host->provider.provider_context = host;
    host->provider.claim = test_claim;
    host->provider.release_claim = test_release_claim;
    host->provider.submit = test_submit;
    host->provider.cancel = test_cancel;
    host->provider.cancel_scope = DTV_L3_CANCEL_SCOPE_PIPE;
    *host_out = host;
    return 1;
}

void siano_l3_test_host_destroy(siano_l3_test_host *host)
{
    delete host;
}

const dtv_l3_provider *siano_l3_test_host_provider(siano_l3_test_host *host)
{
    return host == nullptr ? nullptr : &host->provider;
}

const siano_l3_port *siano_l3_test_host_port(siano_l3_test_host *host)
{
    return host == nullptr ? nullptr : &host->port;
}

int siano_l3_test_host_feed(siano_l3_test_host *host, uint16_t type,
                            const uint8_t *payload, size_t payload_size)
{
    if (host == nullptr || (payload == nullptr && payload_size != 0U)) {
        return 0;
    }
    Injection injection;
    injection.type = type;
    injection.raw.assign(SMS_HEADER_SIZE + payload_size, 0U);
    sms_pack_header(injection.raw.data(), type, 0U, SMS_HIF_TASK,
                    static_cast<uint16_t>(SMS_HEADER_SIZE + payload_size), 0U);
    if (payload_size != 0U) {
        std::memcpy(injection.raw.data() + SMS_HEADER_SIZE, payload,
                    payload_size);
    }
    {
        std::lock_guard<std::mutex> lock(host->mutex);
        host->injected.push_back(std::move(injection));
    }
    deliver_in(host);
    return 1;
}

int siano_l3_test_host_feed_ts(siano_l3_test_host *host,
                               const uint8_t *packets, size_t size)
{
    if (host == nullptr || packets == nullptr || size == 0U) {
        return 0;
    }
    const size_t actual = kResponseAlignment + SMS_HEADER_SIZE + size;
    Injection injection;
    injection.type = MSG_SMS_DVBT_BDA_DATA;
    injection.raw.assign(actual, 0U);
    sms_pack_header(injection.raw.data(), MSG_SMS_DVBT_BDA_DATA, 0U,
                    SMS_HIF_TASK,
                    static_cast<uint16_t>(SMS_HEADER_SIZE + size),
                    SMS_MSG_HDR_FLAG_SPLIT_MSG);
    sms_pack_header(injection.raw.data() + kResponseAlignment,
                    MSG_SMS_DVBT_BDA_DATA, 0U, SMS_HIF_TASK,
                    static_cast<uint16_t>(SMS_HEADER_SIZE + size),
                    SMS_MSG_HDR_FLAG_SPLIT_MSG);
    std::memcpy(injection.raw.data() + kResponseAlignment + SMS_HEADER_SIZE,
                packets, size);
    {
        std::lock_guard<std::mutex> lock(host->mutex);
        host->injected.push_back(std::move(injection));
    }
    deliver_in(host);
    return 1;
}

size_t siano_l3_test_host_out_count(siano_l3_test_host *host)
{
    if (host == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(host->mutex);
    return host->out_types.size();
}

uint16_t siano_l3_test_host_out_type(siano_l3_test_host *host, size_t index)
{
    if (host == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(host->mutex);
    return index < host->out_types.size() ? host->out_types[index] : 0U;
}

uint16_t siano_l3_test_host_out_len(siano_l3_test_host *host, size_t index)
{
    if (host == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(host->mutex);
    return index < host->out_lengths.size() ? host->out_lengths[index] : 0U;
}

size_t siano_l3_test_host_max_pending(siano_l3_test_host *host)
{
    if (host == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(host->mutex);
    return host->max_pending;
}

int siano_l3_test_host_error(siano_l3_test_host *host, char *error,
                             size_t error_size)
{
    if (host == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(host->mutex);
    if (host->error.empty()) {
        return 1;
    }
    if (error != nullptr && error_size != 0U) {
        std::snprintf(error, error_size, "%s", host->error.c_str());
    }
    return 0;
}

} // extern "C"
