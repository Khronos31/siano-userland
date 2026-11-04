/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SIANO_L3_LIBUSB_H
#define SIANO_L3_LIBUSB_H

#include "siano-l3.h"
#include "device-selector.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only enumeration of devices matching the observed PX-S1UD/Rio IDs. */
size_t siano_l3_libusb_device_count(void);
int siano_l3_libusb_port(unsigned index, siano_l3_port *port);

typedef struct siano_l3_libusb_session siano_l3_libusb_session;

dtv_l3_result siano_l3_libusb_session_open(
    unsigned index, const siano_l3_firmware *firmware,
    const siano_l3_options *options, siano_l3_libusb_session **session_out,
    char *error, size_t error_size);
dtv_l3_result siano_l3_libusb_session_open_fd(
    int fd, const siano_l3_firmware *firmware,
    const siano_l3_options *options, siano_l3_libusb_session **session_out,
    char *error, size_t error_size);
dtv_l3_result siano_l3_libusb_find_selector(
    const struct siano_device_selector *selector, const char *selector_text,
    unsigned *index_out, char *error, size_t error_size);
const siano_l3_port *siano_l3_libusb_session_port(
    siano_l3_libusb_session *session);
siano_l3_adapter *siano_l3_libusb_session_adapter(
    siano_l3_libusb_session *session);
void siano_l3_libusb_session_close(siano_l3_libusb_session *session);

#ifdef __cplusplus
}
#endif

#endif
