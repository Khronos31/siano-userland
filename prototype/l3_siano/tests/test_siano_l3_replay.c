/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Strict C consumer test for the independent Siano L3 adapter.  It drives the
 * candidate Core owner, the adapter, and a test-only USB host provider.
 */
#include "siano-l3.h"
#include "siano-l3-test-host.h"

#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define COLD_IMAGE_SIZE (12U + 85828U)
#define MAX_PACKET 188U
#define READ_PACKETS 64U
#ifndef SIANO_L3_OWNER_QUEUE_EXPECT
#define SIANO_L3_OWNER_QUEUE_EXPECT 4U
#endif

static uint8_t g_firmware[COLD_IMAGE_SIZE];

static void fill_firmware(void)
{
    size_t index;
    memset(g_firmware, 0, sizeof(g_firmware));
    g_firmware[0] = 0xadU;
    g_firmware[1] = 0x00U;
    g_firmware[2] = 0x00U;
    g_firmware[3] = 0x00U;
    g_firmware[4] = 0x44U;
    g_firmware[5] = 0x4fU;
    g_firmware[6] = 0x01U;
    g_firmware[7] = 0x00U;
    g_firmware[8] = 0x60U;
    g_firmware[9] = 0x02U;
    g_firmware[10] = 0x04U;
    g_firmware[11] = 0x00U;
    for (index = 12U; index < sizeof(g_firmware); ++index) {
        g_firmware[index] = (uint8_t)(index & 0xffU);
    }
}

static void fill_packet(uint8_t *packet, uint16_t pid, uint8_t cc, int tei)
{
    memset(packet, 0xff, MAX_PACKET);
    packet[0] = 0x47U;
    packet[1] = (uint8_t)((pid >> 8) & 0x1fU);
    if (tei != 0) {
        packet[1] = (uint8_t)(packet[1] | 0x80U);
    }
    packet[2] = (uint8_t)(pid & 0xffU);
    packet[3] = (uint8_t)(0x10U | (cc & 0x0fU));
}

static int wait_open(dtv_l3_op *op)
{
    unsigned int attempt;
    for (attempt = 0; attempt < 2000000U; ++attempt) {
        dtv_l3_op_result result;
        if (dtv_l3_op_poll(op, &result) != DTV_L3_OK) {
            return 0;
        }
        if (result.state == DTV_L3_OP_DONE) {
            return result.result == DTV_L3_OK;
        }
        if (result.state == DTV_L3_OP_FAILED) {
            return 0;
        }
        sched_yield();
    }
    return 0;
}

static void print_diagnostics(siano_l3_adapter *adapter,
                              siano_l3_test_host *host)
{
    siano_l3_stats stats;
    char error[160] = {0};
    if (adapter != NULL &&
        siano_l3_adapter_stats(adapter, &stats) == DTV_L3_OK) {
        fprintf(stderr,
                "stats boot=%u version=%u fw=%u pid=%u signal=%u close=%u "
                "last=%d tsmsg=%llu tspkt=%llu sync=%llu tei=%llu cc=%llu "
                "filt=%llu delivered=%llu\n",
                stats.boot_mode, stats.version_length,
                stats.firmware_downloaded, stats.pid_filter_sent,
                stats.signal_detected, stats.close_cancelled,
                (int)stats.last_error, (unsigned long long)stats.ts_messages,
                (unsigned long long)stats.ts_packets,
                (unsigned long long)stats.ts_bad_sync,
                (unsigned long long)stats.ts_tei_errors,
                (unsigned long long)stats.ts_continuity_errors,
                (unsigned long long)stats.ts_filtered_packets,
                (unsigned long long)stats.ts_delivered_packets);
    }
    if (host != NULL &&
        !siano_l3_test_host_error(host, error, sizeof(error))) {
        fprintf(stderr, "host error: %s\n", error);
    }
}

static int open_owner(siano_l3_test_host *host,
                      const siano_l3_options *options,
                      siano_l3_adapter **adapter_out,
                      dtv_l3_context **owner_out)
{
    siano_l3_firmware firmware;
    siano_l3_adapter *adapter = NULL;
    const dtv_l3_provider *provider;
    dtv_l3_open_result opened;

    memset(&firmware, 0, sizeof(firmware));
    firmware.data = g_firmware;
    firmware.size = sizeof(g_firmware);
    if (siano_l3_adapter_create(siano_l3_test_host_provider(host),
                                siano_l3_test_host_port(host), &firmware,
                                options, &adapter) != DTV_L3_OK) {
        return 0;
    }
    provider = siano_l3_adapter_provider(adapter);
    if (provider == NULL) {
        siano_l3_adapter_destroy(adapter);
        return 0;
    }
    opened = dtv_l3_open(provider, 1U);
    if (opened.result != DTV_L3_ACCEPTED || opened.owner == NULL ||
        opened.op == NULL || !wait_open(opened.op)) {
        if (opened.op != NULL) {
            (void)dtv_l3_op_release(opened.op);
        }
        siano_l3_adapter_destroy(adapter);
        return 0;
    }
    if (dtv_l3_op_release(opened.op) != DTV_L3_OK) {
        siano_l3_adapter_destroy(adapter);
        return 0;
    }
    *adapter_out = adapter;
    *owner_out = opened.owner;
    return 1;
}

static int arm_stream(dtv_l3_context *owner, dtv_l3_lease **lease_out,
                      dtv_l3_stream **stream_out)
{
    dtv_l3_lease *lease = NULL;
    dtv_l3_stream *stream = NULL;
    uint64_t programmed = 0;
    uint64_t revision = 0;
    if (dtv_l3_lease_acquire(owner, 0U, &lease) != DTV_L3_OK ||
        lease == NULL) {
        return 0;
    }
    if (dtv_l3_lease_tune(lease, 527142857ULL, &programmed, &revision) !=
            DTV_L3_OK ||
        programmed != 527142857ULL) {
        return 0;
    }
    if (dtv_l3_lease_start(lease) != DTV_L3_OK ||
        dtv_l3_lease_attach(lease, &stream) != DTV_L3_OK || stream == NULL) {
        return 0;
    }
    *lease_out = lease;
    *stream_out = stream;
    return 1;
}

static int read_packets(dtv_l3_stream *stream, uint8_t *packets,
                        size_t count)
{
    size_t index;
    for (index = 0; index < count; ++index) {
        size_t read = 0;
        dtv_l3_read_state state = DTV_L3_READ_WAIT;
        {
            dtv_l3_result wait_result = dtv_l3_stream_read_ts_wait(
                stream, packets + index * MAX_PACKET, MAX_PACKET, &read,
                &state, 2000U);
            if (wait_result != DTV_L3_OK) {
                fprintf(stderr, "read[%zu] result=%d state=%d read=%zu\n",
                        index, (int)wait_result, (int)state, read);
                return 0;
            }
        }
        if (state != DTV_L3_READ_DATA || read != MAX_PACKET) {
            fprintf(stderr, "read[%zu] state=%d read=%zu\n", index, (int)state,
                    read);
            return 0;
        }
    }
    return 1;
}

static int finish_stream(dtv_l3_stream *stream)
{
    uint8_t packet[MAX_PACKET];
    size_t read = 0;
    dtv_l3_read_state state = DTV_L3_READ_WAIT;
    unsigned int attempt;
    if (dtv_l3_stream_stop(stream, 0U) != DTV_L3_OK) {
        return 0;
    }
    for (attempt = 0; attempt < 1000000U; ++attempt) {
        if (dtv_l3_stream_read_ts(stream, packet, sizeof(packet), &read,
                                  &state) != DTV_L3_OK) {
            return 0;
        }
        if (state == DTV_L3_READ_EOF) {
            break;
        }
        if (state == DTV_L3_READ_WAIT) {
            sched_yield();
        }
    }
    if (state != DTV_L3_READ_EOF) {
        return 0;
    }
    return dtv_l3_stream_release(stream, DTV_L3_STREAM_RELEASE_DRAINED) ==
               DTV_L3_OK &&
           dtv_l3_stream_destroy(stream) == DTV_L3_OK;
}

static int close_status(dtv_l3_context *owner, int expect_flags)
{
    dtv_l3_close_status status;
    unsigned int attempt;
    const dtv_l3_result close_result = dtv_l3_close(owner, 1000U);
    if (close_result != DTV_L3_OK && close_result != DTV_L3_ACCEPTED) {
        fprintf(stderr, "close result=%d\n", (int)close_result);
        return 0;
    }
    for (attempt = 0; attempt < 2000000U; ++attempt) {
        memset(&status, 0, sizeof(status));
        if (dtv_l3_close_status_get(owner, &status) != DTV_L3_OK) {
            return 0;
        }
        if (status.cleanup_complete && status.all_ops_recovered) {
            break;
        }
        sched_yield();
    }
    if (attempt == 2000000U) {
        fprintf(stderr, "close status timeout result=%d quiesced=%u ops=%u cleanup=%u lanes=%u dis=%u\n",
                (int)status.result, status.physical_quiesced,
                status.all_ops_recovered, status.cleanup_complete,
                status.live_lanes, status.disconnected);
        return 0;
    }
    if (expect_flags != 0 && (status.physical_quiesced == 0U ||
                              status.all_ops_recovered == 0U ||
                              status.cleanup_complete == 0U)) {
        fprintf(stderr, "close flags result=%d quiesced=%u ops=%u cleanup=%u lanes=%u dis=%u\n",
                (int)status.result, status.physical_quiesced,
                status.all_ops_recovered, status.cleanup_complete,
                status.live_lanes, status.disconnected);
        return 0;
    }
    return 1;
}

static int release_context(dtv_l3_context *owner)
{
    dtv_l3_result released = DTV_L3_BUSY;
    unsigned int attempt;
    for (attempt = 0; attempt < 2000000U && released == DTV_L3_BUSY;
         ++attempt) {
        released = dtv_l3_context_release(owner);
        sched_yield();
    }
    if (released != DTV_L3_OK) {
        fprintf(stderr, "context release result=%d\n", (int)released);
        return 0;
    }
    return 1;
}

static int close_owner(dtv_l3_context *owner, int expect_flags)
{
    return close_status(owner, expect_flags) && release_context(owner);
}

static void sleep_ms(unsigned int ms)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(ms / 1000U);
    delay.tv_nsec = (long)(ms % 1000U) * 1000000L;
    nanosleep(&delay, NULL);
}

static int feed_clean_ts(siano_l3_test_host *host, uint16_t pid,
                         uint8_t first_cc, size_t count)
{
    uint8_t packets[20U * MAX_PACKET];
    size_t index;
    if (count > 20U) {
        return 0;
    }
    for (index = 0; index < count; ++index) {
        fill_packet(packets + index * MAX_PACKET, pid,
                    (uint8_t)(first_cc + index), 0);
    }
    return siano_l3_test_host_feed_ts(host, packets, count * MAX_PACKET);
}

static int scenario_cold(void)
{
    siano_l3_test_host *host = NULL;
    siano_l3_adapter *adapter = NULL;
    dtv_l3_context *owner = NULL;
    siano_l3_options options;
    siano_l3_stats stats;
    char error[128] = {0};

    memset(&options, 0, sizeof(options));
    options.boot_mode = SIANO_L3_BOOT_AUTO;
    if (!siano_l3_test_host_create(SIANO_L3_FIXTURE_DIR "/cold.jsonl", &host,
                                   error, sizeof(error))) {
        fprintf(stderr, "cold fixture: %s\n", error);
        return 0;
    }
    if (!open_owner(host, &options, &adapter, &owner)) {
        fprintf(stderr, "cold open failed\n");
        if (adapter != NULL) {
            print_diagnostics(adapter, host);
        }
        return 0;
    }
    if (siano_l3_adapter_stats(adapter, &stats) != DTV_L3_OK ||
        stats.boot_mode != SIANO_L3_BOOT_COLD ||
        stats.version_length != 100U || stats.firmware_downloaded != 1U ||
        siano_l3_test_host_out_count(host) != 363U ||
        siano_l3_test_host_out_type(host, 0U) != 668U ||
        siano_l3_test_host_out_type(host, 1U) != 660U ||
        siano_l3_test_host_out_len(host, 1U) != 252U ||
        siano_l3_test_host_out_type(host, 358U) != 660U ||
        siano_l3_test_host_out_len(host, 358U) != 172U ||
        siano_l3_test_host_out_type(host, 362U) != 578U) {
        fprintf(stderr, "cold sequence mismatch\n");
        return 0;
    }
    if (!close_owner(owner, 1)) {
        fprintf(stderr, "cold close failed\n");
        return 0;
    }
    siano_l3_adapter_destroy(adapter);
    siano_l3_test_host_destroy(host);
    return 1;
}

static int scenario_warm(void)
{
    siano_l3_test_host *host = NULL;
    siano_l3_adapter *adapter = NULL;
    dtv_l3_context *owner = NULL;
    siano_l3_options options;
    siano_l3_stats stats;
    char error[128] = {0};

    memset(&options, 0, sizeof(options));
    if (!siano_l3_test_host_create(SIANO_L3_FIXTURE_DIR "/warm.jsonl", &host,
                                   error, sizeof(error))) {
        return 0;
    }
    if (!open_owner(host, &options, &adapter, &owner)) {
        fprintf(stderr, "warm open failed\n");
        return 0;
    }
    if (siano_l3_adapter_stats(adapter, &stats) != DTV_L3_OK ||
        stats.boot_mode != SIANO_L3_BOOT_WARM ||
        stats.firmware_downloaded != 0U ||
        siano_l3_test_host_out_count(host) != 1U) {
        fprintf(stderr, "warm sequence mismatch\n");
        return 0;
    }
    if (!close_owner(owner, 1)) {
        return 0;
    }
    siano_l3_adapter_destroy(adapter);
    siano_l3_test_host_destroy(host);
    return 1;
}

static int scenario_tune(void)
{
    siano_l3_test_host *host = NULL;
    siano_l3_adapter *adapter = NULL;
    dtv_l3_context *owner = NULL;
    dtv_l3_lease *lease = NULL;
    dtv_l3_stream *stream = NULL;
    siano_l3_options options;
    siano_l3_stats stats;
    uint8_t packets[READ_PACKETS * MAX_PACKET];
    uint8_t signal[8] = {0};
    char error[128] = {0};

    memset(&options, 0, sizeof(options));
    if (!siano_l3_test_host_create(SIANO_L3_FIXTURE_DIR "/warm_tune.jsonl",
                                   &host, error, sizeof(error))) {
        return 0;
    }
    if (!open_owner(host, &options, &adapter, &owner) ||
        !arm_stream(owner, &lease, &stream)) {
        fprintf(stderr, "tune arm failed\n");
        print_diagnostics(adapter, host);
        return 0;
    }
    if (!siano_l3_test_host_feed(host, 827U, signal, sizeof(signal))) {
        return 0;
    }
    {
        size_t batch;
        for (batch = 0; batch < 10U; ++batch) {
            if (!feed_clean_ts(host, 0x0200U, (uint8_t)(batch * 4U), 4U) ||
                !read_packets(stream, packets, 4U)) {
                fprintf(stderr, "tune read failed\n");
                print_diagnostics(adapter, host);
                return 0;
            }
        }
    }
    sleep_ms(20);
    if (siano_l3_adapter_stats(adapter, &stats) != DTV_L3_OK ||
        stats.ts_packets != 40U || stats.ts_delivered_packets != 40U ||
        stats.ts_bad_sync != 0U || stats.ts_tei_errors != 0U ||
        stats.ts_continuity_errors != 0U || stats.signal_detected != 1U ||
        stats.pid_filter_sent != 1U) {
        fprintf(stderr, "tune stats mismatch\n");
        return 0;
    }
    if (!finish_stream(stream) || dtv_l3_lease_release(lease) != DTV_L3_OK ||
        !close_owner(owner, 1)) {
        fprintf(stderr, "tune finish failed\n");
        return 0;
    }
    siano_l3_adapter_destroy(adapter);
    siano_l3_test_host_destroy(host);
    return 1;
}

static int scenario_pid(void)
{
    siano_l3_test_host *host = NULL;
    siano_l3_adapter *adapter = NULL;
    dtv_l3_context *owner = NULL;
    dtv_l3_lease *lease = NULL;
    dtv_l3_stream *stream = NULL;
    siano_l3_options options;
    siano_l3_stats stats;
    uint8_t packets[6U * MAX_PACKET];
    char error[128] = {0};
    size_t index;

    memset(&options, 0, sizeof(options));
    options.pid = 0x0100U;
    options.software_filter = SIANO_L3_FILTER_AUTO;
    if (!siano_l3_test_host_create(SIANO_L3_FIXTURE_DIR "/warm_pid.jsonl",
                                   &host, error, sizeof(error))) {
        return 0;
    }
    if (!open_owner(host, &options, &adapter, &owner) ||
        !arm_stream(owner, &lease, &stream)) {
        fprintf(stderr, "pid arm failed\n");
        return 0;
    }
    for (index = 0; index < 6U; ++index) {
        fill_packet(packets + index * MAX_PACKET,
                    (index % 2U) == 0U ? 0x0100U : 0x0200U,
                    (uint8_t)(index / 2U), 0);
    }
    if (!siano_l3_test_host_feed_ts(host, packets, sizeof(packets))) {
        return 0;
    }
    if (!read_packets(stream, packets, 3U)) {
        fprintf(stderr, "pid read failed\n");
        print_diagnostics(adapter, host);
        return 0;
    }
    sleep_ms(20);
    if (siano_l3_adapter_stats(adapter, &stats) != DTV_L3_OK ||
        stats.ts_packets != 6U || stats.ts_delivered_packets != 3U ||
        stats.ts_filtered_packets != 3U || stats.ts_continuity_errors != 0U) {
        fprintf(stderr, "pid filter stats mismatch\n");
        return 0;
    }
    if (siano_l3_test_host_out_type(host,
                                    siano_l3_test_host_out_count(host) - 1U) !=
        601U) {
        fprintf(stderr, "601 was not the last control request\n");
        return 0;
    }
    if (!finish_stream(stream) || dtv_l3_lease_release(lease) != DTV_L3_OK ||
        !close_owner(owner, 1)) {
        return 0;
    }
    siano_l3_adapter_destroy(adapter);
    siano_l3_test_host_destroy(host);
    return 1;
}

static int scenario_close(void)
{
    siano_l3_test_host *host = NULL;
    siano_l3_adapter *adapter = NULL;
    dtv_l3_context *owner = NULL;
    dtv_l3_lease *lease = NULL;
    dtv_l3_stream *stream = NULL;
    siano_l3_options options;
    uint8_t packet[MAX_PACKET];
    char error[128] = {0};

    memset(&options, 0, sizeof(options));
    if (!siano_l3_test_host_create(SIANO_L3_FIXTURE_DIR "/warm_tune.jsonl",
                                   &host, error, sizeof(error))) {
        return 0;
    }
    if (!open_owner(host, &options, &adapter, &owner) ||
        !arm_stream(owner, &lease, &stream)) {
        return 0;
    }
    fill_packet(packet, 0x0200U, 0U, 0);
    if (!siano_l3_test_host_feed_ts(host, packet, sizeof(packet))) {
        return 0;
    }
    if (!close_status(owner, 1) ||
        dtv_l3_stream_destroy(stream) != DTV_L3_OK ||
        dtv_l3_lease_release(lease) != DTV_L3_OK ||
        !release_context(owner)) {
        fprintf(stderr, "close with active stream failed\n");
        return 0;
    }
    siano_l3_adapter_destroy(adapter);
    siano_l3_test_host_destroy(host);
    return 1;
}


static int scenario_owner_queue(void)
{
    siano_l3_test_host *host = NULL;
    siano_l3_adapter *adapter = NULL;
    dtv_l3_context *owner = NULL;
    dtv_l3_lease *lease = NULL;
    dtv_l3_stream *stream = NULL;
    siano_l3_options options;
    siano_l3_stats stats;
    dtv_l3_stream_status stream_status;
    uint8_t packets[20U * MAX_PACKET];
    uint8_t received[4U * MAX_PACKET];
    char error[128] = {0};
    size_t index;
    unsigned int attempt;

    memset(&options, 0, sizeof(options));
    if (!siano_l3_test_host_create(SIANO_L3_FIXTURE_DIR "/warm_tune.jsonl",
                                   &host, error, sizeof(error))) {
        return 0;
    }
    if (!open_owner(host, &options, &adapter, &owner) ||
        !arm_stream(owner, &lease, &stream)) {
        fprintf(stderr, "owner_queue arm failed\n");
        return 0;
    }
    for (index = 0; index < 20U; ++index) {
        fill_packet(packets + index * MAX_PACKET, 0x0200U,
                    (uint8_t)index, 0);
    }
    if (!siano_l3_test_host_feed_ts(host, packets, sizeof(packets))) {
        return 0;
    }
    memset(&stream_status, 0, sizeof(stream_status));
    for (attempt = 0; attempt < 1000000U; ++attempt) {
        if ( dtv_l3_stream_status_get(stream, &stream_status) != DTV_L3_OK) {
            return 0;
        }
        if (stream_status.queued_packets +
                stream_status.dropped_overflow_packets >= 20U) {
            break;
        }
        sched_yield();
    }
    if (attempt == 1000000U ||
        siano_l3_adapter_stats(adapter, &stats) != DTV_L3_OK) {
        fprintf(stderr, "owner_queue status timeout\n");
        return 0;
    }
    const dtv_l3_stream_status initial_status = stream_status;
    const size_t expect_queued = (size_t)SIANO_L3_OWNER_QUEUE_EXPECT;
    const size_t expect_dropped = 20U - expect_queued;
    if (stats.ts_messages != 1U || stats.ts_packets != 20U ||
        stats.ts_delivered_packets != 20U ||
        stats.ts_continuity_errors != 0U ||
        stream_status.queued_packets != expect_queued ||
        stream_status.dropped_overflow_packets != expect_dropped ||
        stream_status.staged_bytes != 0U) {
        fprintf(stderr,
                "owner_queue mismatch expect_queued=%zu expect_dropped=%zu "
                "messages=%llu raw=%llu delivered=%llu cc=%llu queued=%llu "
                "dropped=%llu staged=%llu\n",
                expect_queued, expect_dropped,
                (unsigned long long)stats.ts_messages,
                (unsigned long long)stats.ts_packets,
                (unsigned long long)stats.ts_delivered_packets,
                (unsigned long long)stats.ts_continuity_errors,
                (unsigned long long)stream_status.queued_packets,
                (unsigned long long)stream_status.dropped_overflow_packets,
                (unsigned long long)stream_status.staged_bytes);
        return 0;
    }
    for (index = 0; index < 4U; ++index) {
        size_t read = 0;
        dtv_l3_read_state state = DTV_L3_READ_WAIT;
        if (dtv_l3_stream_read_ts(stream, received + index * MAX_PACKET,
                                  MAX_PACKET, &read, &state) != DTV_L3_OK ||
            state != DTV_L3_READ_DATA || read != MAX_PACKET ||
            memcmp(received + index * MAX_PACKET,
                   packets + index * MAX_PACKET, MAX_PACKET) != 0) {
            fprintf(stderr, "owner_queue read[%zu] failed\n", index);
            return 0;
        }
    }
    if (dtv_l3_stream_status_get(stream, &stream_status) != DTV_L3_OK ||
        stream_status.queued_packets != 0U) {
        return 0;
    }
    if (!finish_stream(stream) || dtv_l3_lease_release(lease) != DTV_L3_OK ||
        !close_owner(owner, 1)) {
        return 0;
    }
    printf("owner_queue measured: raw=%llu delivered_to_owner=%llu raw_cc=%llu "
           "queued=%llu dropped=%llu staged=%llu\n",
           (unsigned long long)stats.ts_packets,
           (unsigned long long)stats.ts_delivered_packets,
           (unsigned long long)stats.ts_continuity_errors,
           (unsigned long long)initial_status.queued_packets,
           (unsigned long long)initial_status.dropped_overflow_packets,
           (unsigned long long)initial_status.staged_bytes);
    siano_l3_adapter_destroy(adapter);
    siano_l3_test_host_destroy(host);
    return 1;
}

static int scenario_quality(void)
{
    siano_l3_test_host *host = NULL;
    siano_l3_adapter *adapter = NULL;
    dtv_l3_context *owner = NULL;
    dtv_l3_lease *lease = NULL;
    dtv_l3_stream *stream = NULL;
    siano_l3_options options;
    siano_l3_stats stats;
    uint8_t packets[4U * MAX_PACKET];
    char error[128] = {0};

    memset(&options, 0, sizeof(options));
    if (!siano_l3_test_host_create(SIANO_L3_FIXTURE_DIR "/warm_tune.jsonl",
                                   &host, error, sizeof(error))) {
        return 0;
    }
    if (!open_owner(host, &options, &adapter, &owner) ||
        !arm_stream(owner, &lease, &stream)) {
        return 0;
    }
    fill_packet(packets + 0U * MAX_PACKET, 0x0300U, 0U, 0);
    fill_packet(packets + 1U * MAX_PACKET, 0x0300U, 1U, 1);
    fill_packet(packets + 2U * MAX_PACKET, 0x0300U, 5U, 0);
    fill_packet(packets + 3U * MAX_PACKET, 0x0300U, 6U, 0);
    if (!siano_l3_test_host_feed_ts(host, packets, sizeof(packets))) {
        return 0;
    }
    sleep_ms(20);
    if (siano_l3_adapter_stats(adapter, &stats) != DTV_L3_OK ||
        stats.ts_tei_errors != 1U || stats.ts_continuity_errors != 1U ||
        stats.ts_delivered_packets != 3U) {
        fprintf(stderr, "quality stats mismatch\n");
        print_diagnostics(adapter, host);
        return 0;
    }
    if (!finish_stream(stream) || dtv_l3_lease_release(lease) != DTV_L3_OK ||
        !close_owner(owner, 1)) {
        return 0;
    }
    siano_l3_adapter_destroy(adapter);
    siano_l3_test_host_destroy(host);
    return 1;
}

int main(int argc, char **argv)
{
    int ok = 0;
    fill_firmware();
    if (argc != 2) {
        fprintf(stderr, "usage: %s <cold|warm|tune|pid|close|quality>\n",
                argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "cold") == 0) {
        ok = scenario_cold();
    } else if (strcmp(argv[1], "warm") == 0) {
        ok = scenario_warm();
    } else if (strcmp(argv[1], "tune") == 0) {
        ok = scenario_tune();
    } else if (strcmp(argv[1], "pid") == 0) {
        ok = scenario_pid();
    } else if (strcmp(argv[1], "close") == 0) {
        ok = scenario_close();
    } else if (strcmp(argv[1], "quality") == 0) {
        ok = scenario_quality();
    } else if (strcmp(argv[1], "owner_queue") == 0) {
        ok = scenario_owner_queue();
    } else {
        fprintf(stderr, "unknown scenario %s\n", argv[1]);
        return 2;
    }
    if (!ok) {
        return 1;
    }
    printf("siano l3 replay %s: PASS\n", argv[1]);
    return 0;
}
