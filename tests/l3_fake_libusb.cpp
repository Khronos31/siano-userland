/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Test-only libusb replacement for the L3 daemon offline integration.  It
 * reuses the existing USB-only replay provider and never touches a device.
 */
#include "device-selector.h"
#include "siano-l3-libusb.h"
#include "siano-l3-test-host.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

constexpr uint16_t kFixturePort[] = {1U, 2U};
siano_l3_adapter* g_adapter = nullptr;
siano_l3_test_host* g_host = nullptr;
siano_l3_port g_port{};
bool g_diagnostics_emitted = false;

void fill_packet(uint8_t* packet, uint8_t cc)
{
    std::memset(packet, 0xff, 188U);
    packet[0] = 0x47U;
    packet[1] = 0x01U;
    packet[2] = 0x00U;
    packet[3] = static_cast<uint8_t>(0x10U | (cc & 0x0fU));
}

}  // namespace

struct siano_l3_libusb_session {
    siano_l3_test_host* host = nullptr;
    siano_l3_adapter* adapter = nullptr;
    siano_l3_port port{};
    std::atomic<bool> stop{false};
    std::thread feeder;
};

extern "C" {

void siano_l3_offline_diagnostics(void);

size_t siano_l3_libusb_device_count(void) { return 1U; }

int siano_l3_libusb_port(unsigned index, siano_l3_port* port)
{
    if (index != 0U || port == nullptr) return 0;
    *port = g_port;
    return 1;
}

dtv_l3_result siano_l3_libusb_session_open(
    unsigned index, const siano_l3_firmware* firmware,
    const siano_l3_options* options, siano_l3_libusb_session** session_out,
    char* error, size_t error_size)
{
    if (index != 0U || session_out == nullptr) return DTV_L3_UNSUPPORTED;
    g_diagnostics_emitted = false;
    const char* fixture = std::getenv("SIANO_L3_OFFLINE_FIXTURE");
    if (fixture == nullptr) fixture = SIANO_L3_OFFLINE_FIXTURE_DEFAULT;
    if (!siano_l3_test_host_create(fixture, &g_host, error, error_size))
        return DTV_L3_FAILED;
    siano_l3_adapter* adapter = nullptr;
    if (siano_l3_adapter_create(siano_l3_test_host_provider(g_host),
                                siano_l3_test_host_port(g_host), firmware,
                                options, &adapter) != DTV_L3_OK) {
        siano_l3_test_host_destroy(g_host);
        g_host = nullptr;
        return DTV_L3_FAILED;
    }
    g_adapter = adapter;
    auto* session = new (std::nothrow) siano_l3_libusb_session();
    if (session == nullptr) {
        siano_l3_adapter_destroy(adapter);
        g_adapter = nullptr;
        siano_l3_test_host_destroy(g_host);
        g_host = nullptr;
        return DTV_L3_FAILED;
    }
    session->host = g_host;
    session->adapter = adapter;
    session->port = *siano_l3_test_host_port(g_host);
    session->feeder = std::thread([session]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        uint8_t packet[188U * 10U];
        uint8_t cc = 0U;
        while (!session->stop.load()) {
            for (unsigned i = 0; i < 10U; ++i) {
                fill_packet(packet + i * 188U, cc++);
            }
            (void)siano_l3_test_host_feed_ts(session->host, packet, sizeof(packet));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    *session_out = session;
    return DTV_L3_OK;
}

dtv_l3_result siano_l3_libusb_session_open_fd(
    int, const siano_l3_firmware*, const siano_l3_options*,
    siano_l3_libusb_session**, char*, size_t)
{
    return DTV_L3_UNSUPPORTED;
}

dtv_l3_result siano_l3_libusb_find_selector(
    const struct siano_device_selector*, const char* selector_text,
    unsigned* index_out, char* error, size_t error_size)
{
    if (selector_text == nullptr || index_out == nullptr) return DTV_L3_INVALID;
    if (std::strcmp(selector_text, "usb:5-1.2") != 0) {
        if (error != nullptr && error_size != 0U)
            std::snprintf(error, error_size, "offline fixture only accepts usb:5-1.2");
        return DTV_L3_UNSUPPORTED;
    }
    *index_out = 0U;
    return DTV_L3_OK;
}

const siano_l3_port* siano_l3_libusb_session_port(siano_l3_libusb_session* session)
{
    return session == nullptr ? nullptr : &session->port;
}

void siano_l3_libusb_session_end_initialization_trace(
    siano_l3_libusb_session* session)
{
    (void)session;
}

siano_l3_adapter* siano_l3_libusb_session_adapter(siano_l3_libusb_session* session)
{
    return session == nullptr ? nullptr : session->adapter;
}

void siano_l3_libusb_session_close(siano_l3_libusb_session* session)
{
    if (session == nullptr) return;
    session->stop.store(true);
    if (session->feeder.joinable()) session->feeder.join();
    siano_l3_offline_diagnostics();
    siano_l3_adapter_destroy(session->adapter);
    if (g_adapter == session->adapter) g_adapter = nullptr;
    siano_l3_test_host_destroy(session->host);
    if (g_host == session->host) g_host = nullptr;
    delete session;
}

siano_l3_adapter* siano_l3_offline_adapter(void) { return g_adapter; }

void siano_l3_offline_diagnostics(void)
{
    if (g_diagnostics_emitted) return;
    g_diagnostics_emitted = true;
    if (g_adapter != nullptr) {
        siano_l3_stats stats{};
        if (siano_l3_adapter_stats(g_adapter, &stats) == DTV_L3_OK) {
            std::fprintf(stderr,
                         "FAKE_STATS last=%d boot=%u version=%u fw=%u pid=%u "
                         "signal=%u tsmsg=%llu tspkt=%llu owner_in_sub=%llu "
                         "owner_in_comp=%llu owner_out_sub=%llu "
                         "owner_out_comp=%llu direct_sub=%llu\n",
                         (int)stats.last_error, stats.boot_mode,
                         stats.version_length, stats.firmware_downloaded,
                         stats.pid_filter_sent, stats.signal_detected,
                         (unsigned long long)stats.ts_messages,
                         (unsigned long long)stats.ts_packets,
                         (unsigned long long)stats.owner_in_submits,
                         (unsigned long long)stats.owner_in_completions,
                         (unsigned long long)stats.owner_out_submits,
                         (unsigned long long)stats.owner_out_completions,
                         (unsigned long long)stats.direct_usb_submits);
        }
    }
    if (g_host != nullptr) {
        char error[256] = {0};
        if (!siano_l3_test_host_error(g_host, error, sizeof(error)))
            std::fprintf(stderr, "FAKE_HOST_ERROR=%s\n", error);
        const size_t count = siano_l3_test_host_out_count(g_host);
        std::fprintf(stderr, "FAKE_OUT_COUNT=%zu\n", count);
        for (size_t i = 0; i < count && i < 32U; ++i)
            std::fprintf(stderr, "FAKE_OUT[%zu]=%u len=%u\n", i,
                         (unsigned)siano_l3_test_host_out_type(g_host, i),
                         (unsigned)siano_l3_test_host_out_len(g_host, i));
    }
}

}  // extern "C"

// C++ accessor for the test translation unit without exposing globals.
namespace siano::offline {
siano_l3_adapter* adapter() { return g_adapter; }
}
