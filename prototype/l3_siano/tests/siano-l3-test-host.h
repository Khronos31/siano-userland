/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SIANO_L3_TEST_HOST_H
#define SIANO_L3_TEST_HOST_H

#include "siano-l3.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct siano_l3_test_host siano_l3_test_host;

int siano_l3_test_host_create(const char *fixture_path,
                              siano_l3_test_host **host_out,
                              char *error, size_t error_size);
void siano_l3_test_host_destroy(siano_l3_test_host *host);
const dtv_l3_provider *siano_l3_test_host_provider(siano_l3_test_host *host);
const siano_l3_port *siano_l3_test_host_port(siano_l3_test_host *host);

/* Unsolicited control frame; the next pending IN transfer receives it. */
int siano_l3_test_host_feed(siano_l3_test_host *host, uint16_t type,
                            const uint8_t *payload, size_t payload_size);
/* Wraps payload in a 693 frame with the observed 504-byte alignment. */
int siano_l3_test_host_feed_ts(siano_l3_test_host *host,
                               const uint8_t *packets, size_t size);

size_t siano_l3_test_host_out_count(siano_l3_test_host *host);
uint16_t siano_l3_test_host_out_type(siano_l3_test_host *host, size_t index);
uint16_t siano_l3_test_host_out_len(siano_l3_test_host *host, size_t index);
int siano_l3_test_host_error(siano_l3_test_host *host, char *error,
                             size_t error_size);
size_t siano_l3_test_host_max_pending(siano_l3_test_host *host);

#ifdef __cplusplus
}
#endif

#endif
