/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Factory/grants/FW-manifest bridge through dtv_l3_open_with_args. */
#include "siano-l3.h"
#include "siano-l3-test-host.h"

#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IMAGE_SIZE (12U + 85828U)

static uint8_t g_firmware[IMAGE_SIZE];
static const uint8_t g_good_sha[32] = {
    0xf3U, 0xb3U, 0xb6U, 0xfdU, 0xebU, 0x5fU, 0x6fU, 0xb7U,
    0x58U, 0x15U, 0x3bU, 0xacU, 0xe5U, 0xc5U, 0xf7U, 0xabU,
    0x9cU, 0x84U, 0x1bU, 0xf5U, 0xa9U, 0xb5U, 0xe5U, 0xceU,
    0xd7U, 0x7dU, 0xb7U, 0x91U, 0xcbU, 0x9cU, 0x67U, 0x8cU,
};

static void fill_firmware(void)
{
    size_t index;
    memset(g_firmware, 0, sizeof(g_firmware));
    g_firmware[0] = 0xadU;
    g_firmware[4] = 0x44U;
    g_firmware[5] = 0x4fU;
    g_firmware[6] = 0x01U;
    g_firmware[8] = 0x60U;
    g_firmware[9] = 0x02U;
    g_firmware[10] = 0x04U;
    for (index = 12U; index < sizeof(g_firmware); ++index) {
        g_firmware[index] = (uint8_t)(index & 0xffU);
    }
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

static int close_owner(dtv_l3_context *owner)
{
    dtv_l3_close_status status;
    dtv_l3_result released = DTV_L3_BUSY;
    unsigned int attempt;
    if (dtv_l3_close(owner, 1000U) != DTV_L3_OK &&
        dtv_l3_close(owner, 1000U) != DTV_L3_ACCEPTED) {
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
        return 0;
    }
    for (attempt = 0; attempt < 2000000U && released == DTV_L3_BUSY;
         ++attempt) {
        released = dtv_l3_context_release(owner);
        sched_yield();
    }
    return released == DTV_L3_OK;
}

static dtv_l3_open_result open_factory(siano_l3_factory *factory,
                                       const dtv_l3_grant *grants,
                                       const dtv_l3_firmware *firmware)
{
    dtv_l3_open_args args;
    memset(&args, 0, sizeof(args));
    args.struct_size = sizeof(args);
    args.usb_provider = siano_l3_factory_usb_provider(factory);
    args.factory = siano_l3_factory_core(factory);
    args.grants = grants;
    args.grant_count = grants == NULL ? 0U : 1U;
    args.firmware = firmware;
    return dtv_l3_open_with_args(&args);
}

int main(void)
{
    siano_l3_test_host *host = NULL;
    siano_l3_factory *factory = NULL;
    siano_l3_options options;
    dtv_l3_grant_descriptor descriptor;
    dtv_l3_grant grant;
    dtv_l3_firmware_manifest manifest;
    dtv_l3_firmware firmware;
    dtv_l3_open_result opened;
    char error[128] = {0};
    size_t baseline = 0;
    int ok = 1;

    fill_firmware();
    memset(&options, 0, sizeof(options));
    if (!siano_l3_test_host_create(SIANO_L3_FIXTURE_DIR "/cold.jsonl", &host,
                                   error, sizeof(error))) {
        fprintf(stderr, "factory fixture: %s\n", error);
        return 1;
    }
    if (siano_l3_factory_create(siano_l3_test_host_provider(host),
                                siano_l3_test_host_port(host), &options,
                                &factory) != DTV_L3_OK) {
        return 1;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.vendor_id = 0x3275U;
    descriptor.product_id = 0x0080U;
    descriptor.num_interfaces = 1U;
    descriptor.num_endpoints = 2U;
    grant.token = 1U;
    grant.descriptor = &descriptor;

    memset(&manifest, 0, sizeof(manifest));
    manifest.length_bytes = sizeof(g_firmware);
    manifest.sha256 = g_good_sha;
    memset(&firmware, 0, sizeof(firmware));
    firmware.source = DTV_L3_FIRMWARE_BYTES;
    firmware.manifest = &manifest;
    firmware.bytes = g_firmware;
    firmware.size = sizeof(g_firmware);

    opened = open_factory(factory, &grant, &firmware);
    if (opened.result != DTV_L3_ACCEPTED || opened.owner == NULL ||
        opened.op == NULL || !wait_open(opened.op)) {
        fprintf(stderr, "factory open failed result=%d\n", (int)opened.result);
        ok = 0;
    } else {
        (void)dtv_l3_op_release(opened.op);
        if (siano_l3_test_host_out_count(host) != 363U ||
            !close_owner(opened.owner)) {
            fprintf(stderr, "factory cold sequence/close failed\n");
            ok = 0;
        }
    }
    baseline = siano_l3_test_host_out_count(host);

    if (ok != 0) {
        manifest.length_bytes = sizeof(g_firmware) - 1U;
        opened = open_factory(factory, &grant, &firmware);
        if (opened.result == DTV_L3_ACCEPTED ||
            siano_l3_test_host_out_count(host) != baseline) {
            fprintf(stderr, "manifest length rejection failed\n");
            ok = 0;
        }
        manifest.length_bytes = sizeof(g_firmware);
    }
    if (ok != 0) {
        uint8_t bad_sha[32] = {0};
        manifest.sha256 = bad_sha;
        opened = open_factory(factory, &grant, &firmware);
        if (opened.result == DTV_L3_ACCEPTED ||
            siano_l3_test_host_out_count(host) != baseline) {
            fprintf(stderr, "manifest sha rejection failed\n");
            ok = 0;
        }
        manifest.sha256 = g_good_sha;
    }
    if (ok != 0) {
        descriptor.vendor_id = 0x0000U;
        opened = open_factory(factory, &grant, &firmware);
        if (opened.result == DTV_L3_ACCEPTED ||
            siano_l3_test_host_out_count(host) != baseline) {
            fprintf(stderr, "grant rejection failed\n");
            ok = 0;
        }
    }
    siano_l3_factory_destroy(factory);
    siano_l3_test_host_destroy(host);
    if (ok != 0) {
        printf("factory/grants/manifest: PASS (valid cold + 3 pre-claim rejects)\n");
    }
    return ok != 0 ? 0 : 1;
}
