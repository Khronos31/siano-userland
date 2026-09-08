#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# BusyBox/POSIX sh helper for the OpenRC coldplug scan.
set -eu

if [ "$#" -ne 1 ] || [ "$1" != "--scan" ]; then
    printf '%s\n' "usage: $0 --scan" >&2
    exit 2
fi

sysfs_root=${MDEV_SYSFS_ROOT-/sys}
dev_root=${MDEV_DEV_ROOT-/dev}

strip_zeroes()
{
    value=$1
    while [ "${value#0}" != "$value" ] && [ "${#value}" -gt 1 ]; do
        value=${value#0}
    done
    printf '%s\n' "$value"
}

format_component()
{
    value=$(strip_zeroes "$1")
    case "$value" in
        [0-9]) printf '00%s\n' "$value" ;;
        [0-9][0-9]) printf '0%s\n' "$value" ;;
        [0-9][0-9][0-9]) printf '%s\n' "$value" ;;
        *) return 1 ;;
    esac
}

set_permissions()
{
    node=$1
    if chown root:video "$node"; then
        :
    elif [ ! -e "$node" ]; then
        return 0
    else
        printf '%s\n' "mdev: chown failed for $node" >&2
        return 1
    fi

    if chmod 0660 "$node"; then
        return 0
    elif [ ! -e "$node" ]; then
        return 0
    else
        printf '%s\n' "mdev: chmod failed for $node" >&2
        return 1
    fi
}

# mdev -s does not provide DEVTYPE/PRODUCT to mdev.conf commands. Walk every
# USB device entry and act only after validating its sysfs identity.
for usb_device in "$sysfs_root"/bus/usb/devices/*; do
    [ -d "$usb_device" ] || continue
    [ -f "$usb_device/idVendor" ] && [ -f "$usb_device/idProduct" ] || continue
    [ -f "$usb_device/busnum" ] && [ -f "$usb_device/devnum" ] || continue

    vendor=$(cat "$usb_device/idVendor" 2>/dev/null) || continue
    product=$(cat "$usb_device/idProduct" 2>/dev/null) || continue
    case "$vendor:$product" in
        3275:0080|187f:0600|187f:0302) ;;
        *) continue ;;
    esac

    bus=$(cat "$usb_device/busnum" 2>/dev/null) || continue
    device=$(cat "$usb_device/devnum" 2>/dev/null) || continue
    bus_component=$(format_component "$bus") || continue
    device_component=$(format_component "$device") || continue
    node=$dev_root/bus/usb/$bus_component/$device_component
    [ -e "$node" ] || continue
    set_permissions "$node"
done
