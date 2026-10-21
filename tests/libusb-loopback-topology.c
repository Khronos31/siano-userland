/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fixed USB topology for the loopback fixture: one function on bus 1 at
 * path 2.3.  The base loopback does not provide get_port_numbers; this file
 * adds only that observation so the canonical usb: selector and the public
 * identity path can be exercised without a real device. */
#include <libusb.h>

int LIBUSB_CALL libusb_get_port_numbers(libusb_device *device, uint8_t *ports,
                                        int port_count)
{
    (void)device;
    if (ports == NULL || port_count < 2) return 0;
    ports[0] = 2U;
    ports[1] = 3U;
    return 2;
}
