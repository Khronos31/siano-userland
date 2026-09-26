/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../exit-codes.h"

#include <assert.h>

int main(void)
{
    assert(siano_exit_code(0) == 0);
    assert(siano_exit_code(-EINVAL) == 1);
    assert(siano_exit_code(-ENODEV) == 3);
    assert(siano_exit_code(-EBUSY) == 4);
    assert(siano_exit_code(LIBUSB_ERROR_BUSY) == 4);
    assert(siano_exit_code(-ETIMEDOUT) == 5);
    assert(siano_exit_code(LIBUSB_ERROR_NO_DEVICE) == 7);
    assert(siano_exit_code(-ENOENT) == 10);
    assert(siano_exit_code(-EBADMSG) == 10);
    assert(siano_exit_code(-ENOMEM) == 70);
    assert(siano_exit_code(-EAGAIN) == 70);
    return 0;
}
