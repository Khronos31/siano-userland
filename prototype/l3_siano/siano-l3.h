/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Independent Siano L3 product adapter.
 *
 * This file is written from the observed message numbers, lengths, and
 * ordering recorded in evidence/siano-independent-protocol-facts-20261104.json
 * and evidence/siano-independent-protocol-observation-facts-20261104.json,
 * plus the GPL-2.0-or-later protocol.c/h in this repository.  It is not a
 * clean-room claim and gives no legal guarantee.  The GPL-2.0-only kernel
 * sources and the existing siano-device.c/host facade/receiver engine are not
 * inputs.
 */
#ifndef SIANO_L3_H
#define SIANO_L3_H

#include <dtv/l3_owner.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct siano_l3_adapter siano_l3_adapter;

#define SIANO_L3_DEFAULT_RESPONSE_ALIGNMENT 504U
#define SIANO_L3_USB2_BUFFER_SIZE 0x2000U
#define SIANO_L3_FIRMWARE_CHUNK 240U
#define SIANO_L3_DEFAULT_PID 0x2000U
#define SIANO_L3_ISDBT_BDA_MODE 6U
#define SIANO_L3_DEFAULT_CONTROL_TIMEOUT_MS 2000U
#define SIANO_L3_DEFAULT_VERSION_TIMEOUT_MS 10000U

typedef struct siano_l3_port {
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t interface_number;
    uint8_t in_endpoint;
    uint8_t out_endpoint;
    uint8_t reserved;
    uint32_t response_alignment;
    uint8_t bus;
    uint8_t address;
    uint8_t port_count;
    uint8_t reserved2;
    uint8_t ports[8];
    uint8_t reserved3[4];
} siano_l3_port;

typedef enum siano_l3_boot_mode {
    SIANO_L3_BOOT_AUTO = 0,
    SIANO_L3_BOOT_COLD = 1,
    SIANO_L3_BOOT_WARM = 2
} siano_l3_boot_mode;

typedef enum siano_l3_software_filter {
    SIANO_L3_FILTER_AUTO = 0,
    SIANO_L3_FILTER_FORCE = 1,
    SIANO_L3_FILTER_DISABLE = 2
} siano_l3_software_filter;

typedef struct siano_l3_options {
    uint32_t boot_mode;
    uint32_t response_alignment;
    uint32_t pid;
    uint32_t software_filter;
    uint32_t control_timeout_ms;
    uint32_t version_timeout_ms;
    uint32_t download_timeout_ms;
    uint32_t stats_poll_ms;
    /* Number of simultaneous IN transfers; zero selects one. */
    uint32_t in_flight;
    /* Internal daemon PID list; zero uses pid/default catch-all. */
    uint32_t pid_count;
    uint16_t pids[64];
} siano_l3_options;

/*
 * A firmware source is either a resident byte range (data != NULL) or a
 * caller-owned reader.  The reader receives an absolute offset into the image
 * and returns the copied byte count through read_size.  A zero return is a
 * failure.  size is the full image size, including the 12-byte sms_firmware
 * header, because the observed download sends the whole image.
 */
typedef int (*siano_l3_firmware_read_fn)(void *context,
                                         size_t offset,
                                         uint8_t *buffer,
                                         size_t capacity,
                                         size_t *read_size);

typedef struct siano_l3_firmware {
    const uint8_t *data;
    size_t size;
    siano_l3_firmware_read_fn read;
    void *read_context;
} siano_l3_firmware;

typedef struct siano_l3_stats {
    uint32_t boot_mode;
    uint32_t version_length;
    uint32_t supported_protocols;
    uint32_t firmware_downloaded;
    uint32_t pid_filter_sent;
    uint32_t signal_detected;
    uint32_t close_cancelled;
    uint32_t reserved;
    dtv_l3_result last_error;
    uint64_t ts_messages;
    uint64_t ts_bytes;
    uint64_t ts_packets;
    uint64_t ts_bad_sync;
    uint64_t ts_tei_errors;
    uint64_t ts_continuity_errors;
    uint64_t ts_filtered_packets;
    uint64_t ts_delivered_packets;
    uint64_t in_submits;
    uint64_t in_completions;
    uint64_t owner_in_submits;
    uint64_t owner_in_completions;
    uint64_t owner_out_submits;
    uint64_t owner_out_completions;
    uint64_t direct_usb_submits;
    uint32_t in_flight_max;
    uint32_t reserved_in;
} siano_l3_stats;

/*
 * host is a USB-only dtv_l3_provider.  The adapter owns no USB transport and
 * never opens a device by itself.  The returned provider is passed to
 * dtv_l3_open()/dtv_l3_open_ex() by the caller.
 */
dtv_l3_result siano_l3_adapter_create(const dtv_l3_provider *host,
                                      const siano_l3_port *port,
                                      const siano_l3_firmware *firmware,
                                      const siano_l3_options *options,
                                      siano_l3_adapter **adapter_out);

void siano_l3_adapter_destroy(siano_l3_adapter *adapter);
void siano_l3_adapter_stop(siano_l3_adapter *adapter);
void siano_l3_adapter_quiesce_owner_reader(siano_l3_adapter *adapter);
const dtv_l3_provider *siano_l3_adapter_provider(siano_l3_adapter *adapter);

/*
 * Split-API bridge for Core's usb_provider/product_adapter candidate.  The
 * caller opens with dtv_l3_open_ex(), quiesces the owner reader before
 * context_release, and calls siano_l3_adapter_stop() after release.
 */
#ifdef SIANO_L3_FACTORY
typedef struct siano_l3_factory siano_l3_factory;

dtv_l3_result siano_l3_factory_create(const dtv_l3_provider *host,
                                      const siano_l3_port *port,
                                      const siano_l3_options *options,
                                      siano_l3_factory **factory_out);
void siano_l3_factory_destroy(siano_l3_factory *factory);
const dtv_l3_usb_provider *siano_l3_factory_usb_provider(
    siano_l3_factory *factory);
const dtv_l3_factory *siano_l3_factory_core(siano_l3_factory *factory);
void siano_l3_adapter_set_binding(siano_l3_adapter *adapter,
                                  dtv_l3_provider_binding *binding);
#endif

#ifdef SIANO_L3_SPLIT_API
dtv_l3_result siano_l3_adapter_attach(siano_l3_adapter *adapter,
                                      dtv_l3_provider_binding *binding);
const dtv_l3_usb_provider *siano_l3_adapter_usb_provider(
    siano_l3_adapter *adapter);
const dtv_l3_product_adapter *siano_l3_adapter_product_adapter(
    siano_l3_adapter *adapter);
#endif
dtv_l3_result siano_l3_adapter_stats(siano_l3_adapter *adapter,
                                     siano_l3_stats *stats);
dtv_l3_result siano_l3_adapter_poll_stats(siano_l3_adapter *adapter);

#ifdef __cplusplus
}
#endif

#endif
