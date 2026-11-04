/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Real-device probe for the independent Siano L3 adapter.  It is not started
 * automatically.  The owner must arrange exclusive access to the PX-S1UD and
 * confirm the firmware/runtime before running it.
 */
#include "siano-l3-libusb.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

bool read_file(const char *path, std::vector<uint8_t> *bytes, std::string *error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        *error = std::string("cannot open firmware: ") + path;
        return false;
    }
    bytes->assign(std::istreambuf_iterator<char>(file),
                  std::istreambuf_iterator<char>());
    if (bytes->empty()) {
        *error = "firmware is empty";
        return false;
    }
    return true;
}

bool wait_open(dtv_l3_op *op, dtv_l3_op_result *result)
{
    for (unsigned int attempt = 0; attempt < 6000000U; ++attempt) {
        if (dtv_l3_op_poll(op, result) != DTV_L3_OK) {
            return false;
        }
        if (result->state == DTV_L3_OP_DONE) {
            return result->result == DTV_L3_OK;
        }
        if (result->state == DTV_L3_OP_FAILED) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool finish_stream(dtv_l3_stream *stream)
{
    uint8_t packet[188];
    size_t read = 0;
    dtv_l3_read_state state = DTV_L3_READ_WAIT;
    if (dtv_l3_stream_stop(stream, 2000U) != DTV_L3_OK) {
        return false;
    }
    for (unsigned int attempt = 0; attempt < 2000000U; ++attempt) {
        if (dtv_l3_stream_read_ts(stream, packet, sizeof(packet), &read,
                                  &state) != DTV_L3_OK) {
            return false;
        }
        if (state == DTV_L3_READ_EOF) {
            return dtv_l3_stream_release(
                       stream, DTV_L3_STREAM_RELEASE_DRAINED) == DTV_L3_OK &&
                   dtv_l3_stream_destroy(stream) == DTV_L3_OK;
        }
        if (state == DTV_L3_READ_WAIT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return false;
}

void usage(const char *name)
{
    std::fprintf(stderr,
                 "usage: %s [--list] [--index N] [--firmware PATH] "
                 "[--warm|--cold] [--pid N] [--hz N] [--seconds N] "
                 "[--stats-ms N] [--output PATH]\n",
                 name);
}

} // namespace

int main(int argc, char **argv)
{
    bool list = false;
    unsigned index = 0;
    unsigned seconds = 60;
    unsigned stats_ms = 1000;
    unsigned long hz = 527142857UL;
    unsigned long pid = 0;
    unsigned boot_mode = SIANO_L3_BOOT_AUTO;
    const char *firmware_path = nullptr;
    const char *output_path = nullptr;

    for (int arg = 1; arg < argc; ++arg) {
        if (std::strcmp(argv[arg], "--list") == 0) {
            list = true;
        } else if (std::strcmp(argv[arg], "--index") == 0 && arg + 1 < argc) {
            index = static_cast<unsigned>(std::strtoul(argv[++arg], nullptr, 0));
        } else if (std::strcmp(argv[arg], "--firmware") == 0 && arg + 1 < argc) {
            firmware_path = argv[++arg];
        } else if (std::strcmp(argv[arg], "--warm") == 0) {
            boot_mode = SIANO_L3_BOOT_WARM;
        } else if (std::strcmp(argv[arg], "--cold") == 0) {
            boot_mode = SIANO_L3_BOOT_COLD;
        } else if (std::strcmp(argv[arg], "--pid") == 0 && arg + 1 < argc) {
            pid = std::strtoul(argv[++arg], nullptr, 0);
        } else if (std::strcmp(argv[arg], "--hz") == 0 && arg + 1 < argc) {
            hz = std::strtoul(argv[++arg], nullptr, 0);
        } else if (std::strcmp(argv[arg], "--seconds") == 0 && arg + 1 < argc) {
            seconds = static_cast<unsigned>(
                std::strtoul(argv[++arg], nullptr, 0));
        } else if (std::strcmp(argv[arg], "--stats-ms") == 0 && arg + 1 < argc) {
            stats_ms = static_cast<unsigned>(
                std::strtoul(argv[++arg], nullptr, 0));
        } else if (std::strcmp(argv[arg], "--output") == 0 && arg + 1 < argc) {
            output_path = argv[++arg];
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (list) {
        const size_t count = siano_l3_libusb_device_count();
        std::printf("matching Siano devices: %zu\n", count);
        for (unsigned device = 0; device < count; ++device) {
            siano_l3_port port;
            if (siano_l3_libusb_port(device, &port)) {
                std::printf(
                    "  [%u] %04x:%04x intf=%u in=0x%02x out=0x%02x align=%u\n",
                    device, port.vendor_id, port.product_id,
                    port.interface_number, port.in_endpoint,
                    port.out_endpoint, port.response_alignment);
            }
        }
        return 0;
    }

    std::vector<uint8_t> firmware;
    siano_l3_firmware firmware_input{};
    if (firmware_path != nullptr) {
        std::string error;
        if (!read_file(firmware_path, &firmware, &error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }
        firmware_input.data = firmware.data();
        firmware_input.size = firmware.size();
    }

    siano_l3_options options{};
    options.boot_mode = boot_mode;
    options.pid = static_cast<uint32_t>(pid);
    options.stats_poll_ms = stats_ms;
    siano_l3_libusb_session *session = nullptr;
    char error[256] = {0};
    dtv_l3_result result = siano_l3_libusb_session_open(
        index, firmware_path != nullptr ? &firmware_input : nullptr, &options,
        &session, error, sizeof(error));
    if (result != DTV_L3_OK || session == nullptr) {
        std::fprintf(stderr, "session open failed: %d %s\n", (int)result, error);
        return 1;
    }
    siano_l3_adapter *adapter = siano_l3_libusb_session_adapter(session);
    const dtv_l3_provider *provider = siano_l3_adapter_provider(adapter);
    dtv_l3_open_result opened = dtv_l3_open(provider, 1U);
    dtv_l3_op_result open_result{};
    if (opened.result != DTV_L3_ACCEPTED || opened.owner == nullptr ||
        opened.op == nullptr || !wait_open(opened.op, &open_result)) {
        siano_l3_stats stats;
        siano_l3_adapter_stats(adapter, &stats);
        std::fprintf(stderr, "open failed: result=%d last=%d\n",
                     (int)open_result.result, (int)stats.last_error);
        siano_l3_libusb_session_close(session);
        return 1;
    }
    (void)dtv_l3_op_release(opened.op);

    siano_l3_stats stats;
    siano_l3_adapter_stats(adapter, &stats);
    std::printf("ready: boot=%u version=%u supported=0x%02x fw=%u\n",
                stats.boot_mode, stats.version_length,
                stats.supported_protocols, stats.firmware_downloaded);

    dtv_l3_lease *lease = nullptr;
    dtv_l3_stream *stream = nullptr;
    uint64_t programmed = 0;
    uint64_t revision = 0;
    result = dtv_l3_lease_acquire(opened.owner, 0U, &lease);
    if (result == DTV_L3_OK) {
        result = dtv_l3_lease_tune(lease, hz, &programmed, &revision);
    }
    if (result == DTV_L3_OK) {
        result = dtv_l3_lease_start(lease);
    }
    if (result == DTV_L3_OK) {
        result = dtv_l3_lease_attach(lease, &stream);
    }
    if (result != DTV_L3_OK || stream == nullptr) {
        std::fprintf(stderr, "tune/attach failed: %d\n", (int)result);
        (void)dtv_l3_close(opened.owner, 2000U);
        siano_l3_libusb_session_close(session);
        return 1;
    }
    std::printf("tuned: requested=%lu programmed=%llu revision=%llu\n", hz,
                (unsigned long long)programmed,
                (unsigned long long)revision);

    std::ofstream output;
    if (output_path != nullptr) {
        output.open(output_path, std::ios::binary);
        if (!output) {
            std::fprintf(stderr, "cannot open output %s\n", output_path);
            return 1;
        }
    }
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    uint64_t packets = 0;
    uint64_t bytes = 0;
    uint64_t bad_sync = 0;
    uint64_t tei = 0;
    uint8_t buffer[188U * 64U];
    while (std::chrono::steady_clock::now() < deadline) {
        size_t read = 0;
        dtv_l3_read_state state = DTV_L3_READ_WAIT;
        const dtv_l3_result read_result = dtv_l3_stream_read_ts_wait(
            stream, buffer, sizeof(buffer), &read, &state, 1000U);
        if (read_result == DTV_L3_CLOSING || state == DTV_L3_READ_EOF) {
            break;
        }
        if (read_result != DTV_L3_OK || state != DTV_L3_READ_DATA) {
            continue;
        }
        if (output) {
            output.write(reinterpret_cast<const char *>(buffer),
                         static_cast<std::streamsize>(read));
        }
        for (size_t offset = 0; offset + 188U <= read; offset += 188U) {
            const uint8_t *packet = buffer + offset;
            ++packets;
            bytes += 188U;
            if (packet[0] != 0x47U) {
                ++bad_sync;
            }
            if ((packet[1] & 0x80U) != 0U) {
                ++tei;
            }
        }
    }
    if (output) {
        output.close();
    }
    siano_l3_adapter_stats(adapter, &stats);
    std::printf("stream: packets=%llu bytes=%llu bad_sync=%llu tei=%llu "
                "adapter_tei=%llu adapter_cc=%llu filtered=%llu signal=%u\n",
                (unsigned long long)packets, (unsigned long long)bytes,
                (unsigned long long)bad_sync, (unsigned long long)tei,
                (unsigned long long)stats.ts_tei_errors,
                (unsigned long long)stats.ts_continuity_errors,
                (unsigned long long)stats.ts_filtered_packets,
                stats.signal_detected);

    const bool stream_ok = finish_stream(stream);
    const dtv_l3_result lease_result = dtv_l3_lease_release(lease);
    const dtv_l3_result close_result = dtv_l3_close(opened.owner, 2000U);
    dtv_l3_close_status close_status{};
    for (unsigned int attempt = 0; attempt < 2000000U; ++attempt) {
        if (dtv_l3_close_status_get(opened.owner, &close_status) != DTV_L3_OK) {
            break;
        }
        if (close_status.cleanup_complete && close_status.all_ops_recovered) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("close: result=%d quiesced=%u ops=%u cleanup=%u releasable=%u "
                "disconnected=%u\n",
                (int)close_result, close_status.physical_quiesced,
                close_status.all_ops_recovered, close_status.cleanup_complete,
                close_status.releasable, close_status.disconnected);
    dtv_l3_result release_result = DTV_L3_BUSY;
    for (unsigned int attempt = 0; attempt < 2000000U &&
                                   release_result == DTV_L3_BUSY;
         ++attempt) {
        release_result = dtv_l3_context_release(opened.owner);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("release: result=%d\n", (int)release_result);
    siano_l3_libusb_session_close(session);
    return stream_ok && lease_result == DTV_L3_OK &&
                   release_result == DTV_L3_OK
               ? 0
               : 1;
}
