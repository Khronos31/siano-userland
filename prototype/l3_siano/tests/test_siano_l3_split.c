/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Split-API consumer test: Core owns the USB provider and the product adapter
 * view; the Siano library supplies both views and initializes on first tune.
 */
#include "siano-l3.h"
#include "siano-l3-test-host.h"

#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_PACKET 188U
#define PACKETS 4U

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

static void fill_packet(uint8_t *packet, uint16_t pid, uint8_t cc)
{
    memset(packet, 0xff, MAX_PACKET);
    packet[0] = 0x47U;
    packet[1] = (uint8_t)((pid >> 8) & 0x1fU);
    packet[2] = (uint8_t)(pid & 0xffU);
    packet[3] = (uint8_t)(0x10U | (cc & 0x0fU));
}

int main(void)
{
    siano_l3_test_host *host = NULL;
    siano_l3_adapter *adapter = NULL;
    siano_l3_options options;
    const dtv_l3_usb_provider *usb;
    const dtv_l3_product_adapter *product;
    dtv_l3_open_result opened;
    dtv_l3_lease *lease = NULL;
    dtv_l3_stream *stream = NULL;
    dtv_l3_close_status close_status;
    dtv_l3_result released = DTV_L3_BUSY;
    uint64_t programmed = 0;
    uint64_t revision = 0;
    uint8_t packets[PACKETS * MAX_PACKET];
    char error[128] = {0};
    size_t index;
    int ok = 0;

    memset(&options, 0, sizeof(options));
    if (!siano_l3_test_host_create(SIANO_L3_FIXTURE_DIR "/warm_tune.jsonl",
                                   &host, error, sizeof(error))) {
        fprintf(stderr, "split fixture: %s\n", error);
        return 1;
    }
    if (siano_l3_adapter_create(siano_l3_test_host_provider(host),
                                siano_l3_test_host_port(host), NULL, &options,
                                &adapter) != DTV_L3_OK) {
        return 1;
    }
    usb = siano_l3_adapter_usb_provider(adapter);
    product = siano_l3_adapter_product_adapter(adapter);
    if (usb == NULL || product == NULL) {
        return 1;
    }
    opened = dtv_l3_open_ex(usb, product, 1U);
    if (opened.result != DTV_L3_ACCEPTED || opened.owner == NULL ||
        opened.op == NULL || !wait_open(opened.op)) {
        fprintf(stderr, "split open failed\n");
        return 1;
    }
    (void)dtv_l3_op_release(opened.op);

    if (dtv_l3_lease_acquire(opened.owner, 0U, &lease) != DTV_L3_OK ||
        dtv_l3_lease_tune(lease, 527142857ULL, &programmed, &revision) !=
            DTV_L3_OK ||
        dtv_l3_lease_start(lease) != DTV_L3_OK ||
        dtv_l3_lease_attach(lease, &stream) != DTV_L3_OK) {
        fprintf(stderr, "split arm failed\n");
        return 1;
    }
    for (index = 0; index < PACKETS; ++index) {
        fill_packet(packets + index * MAX_PACKET, 0x0200U, (uint8_t)index);
    }
    if (!siano_l3_test_host_feed_ts(host, packets, sizeof(packets))) {
        return 1;
    }
    for (index = 0; index < PACKETS; ++index) {
        uint8_t packet[MAX_PACKET];
        size_t read = 0;
        dtv_l3_read_state state = DTV_L3_READ_WAIT;
        if (dtv_l3_stream_read_ts_wait(stream, packet, sizeof(packet), &read,
                                       &state, 2000U) != DTV_L3_OK ||
            state != DTV_L3_READ_DATA || read != MAX_PACKET) {
            fprintf(stderr, "split read failed at %zu\n", index);
            return 1;
        }
    }
    if (dtv_l3_stream_stop(stream, 0U) != DTV_L3_OK) {
        return 1;
    }
    for (;;) {
        uint8_t packet[MAX_PACKET];
        size_t read = 0;
        dtv_l3_read_state state = DTV_L3_READ_WAIT;
        if (dtv_l3_stream_read_ts(stream, packet, sizeof(packet), &read,
                                  &state) != DTV_L3_OK) {
            return 1;
        }
        if (state == DTV_L3_READ_EOF) {
            break;
        }
        if (state == DTV_L3_READ_WAIT) {
            sched_yield();
        }
    }
    if (dtv_l3_stream_release(stream, DTV_L3_STREAM_RELEASE_DRAINED) !=
            DTV_L3_OK ||
        dtv_l3_stream_destroy(stream) != DTV_L3_OK ||
        dtv_l3_lease_release(lease) != DTV_L3_OK) {
        return 1;
    }

    if (dtv_l3_close(opened.owner, 1000U) != DTV_L3_OK &&
        dtv_l3_close(opened.owner, 1000U) != DTV_L3_ACCEPTED) {
        return 1;
    }
    for (index = 0; index < 2000000U; ++index) {
        memset(&close_status, 0, sizeof(close_status));
        if (dtv_l3_close_status_get(opened.owner, &close_status) != DTV_L3_OK) {
            return 1;
        }
        if (close_status.cleanup_complete && close_status.all_ops_recovered) {
            break;
        }
        sched_yield();
    }
    if (index == 2000000U || close_status.physical_quiesced == 0U) {
        return 1;
    }
    for (index = 0; index < 2000000U && released == DTV_L3_BUSY; ++index) {
        released = dtv_l3_context_release(opened.owner);
        sched_yield();
    }
    if (released != DTV_L3_OK) {
        fprintf(stderr, "split context release=%d\n", (int)released);
        return 1;
    }
    siano_l3_adapter_stop(adapter);
    siano_l3_adapter_destroy(adapter);
    siano_l3_test_host_destroy(host);
    printf("siano l3 split open_ex: PASS\n");
    ok = 1;
    return ok ? 0 : 1;
}
