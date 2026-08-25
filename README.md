# siano-ts

[![CI](https://github.com/Khronos31/siano-userland/actions/workflows/ci.yml/badge.svg)](https://github.com/Khronos31/siano-userland/actions/workflows/ci.yml)

`siano-ts` is a standalone user-space driver for Siano RIO-family USB tuners
such as the PLEX PX-S1UD. It uses only libusb-1.0, loads the ISDB-T firmware,
tunes one Japanese ISDB-T physical channel, and writes MPEG-TS to stdout (or
`--output`). Diagnostic output goes to stderr.

It talks to USB only through libusb-1.0 (no usbfs ioctls), builds on musl,
glibc, and Bionic, and can take an already-open USB file descriptor so
Android/Termux do not have to enumerate `/dev/bus/usb`.

## Build and test

Dependencies are a C11 compiler, `make`, `pkg-config`, and libusb-1.0
(>= 1.0.23 for `--fd`) development files. There is no glibc-only API.

```sh
make
make test
```

CI (GitHub Actions) builds and tests on Ubuntu, macOS, and Alpine (musl).
There is no tuner in CI; that job is compile/link plus offline protocol tests.

On Alpine:

```sh
apk add gcc make pkgconf musl-dev libusb-dev
make
```

Runtime needs `libusb` (`libusb-1.0.so.0`). `make test`'s `--list` step
needs `/dev/bus/usb`; a container without it can fail `libusb_init` even
though the musl binary is fine.

On Linux, the USB event thread asks for `SCHED_FIFO` and `mlockall`. Both
are optional; without `CAP_SYS_NICE` / `CAP_IPC_LOCK` the process continues
at normal priority. The in-flight USB ring is 32 × 16KiB.

The firmware is not part of this repository. Obtain `isdbt_rio.inp` from the
linux-firmware collection and pass it with `--firmware`, or place it at
`./firmware/isdbt_rio.inp` or `/lib/firmware/isdbt_rio.inp`. GitHub Releases
ship the binary next to `firmware/isdbt_rio.inp` (not embedded in the ELF).
The Siano firmware license in [LICENCE.siano](LICENCE.siano) permits binary
redistribution with its copyright notice and disclaimer; it prohibits reverse
engineering, decompilation, and disassembly. Do not add the firmware to git.

Tagged builds publish Linux (musl), macOS, and Windows zip/tar archives.
Windows needs WinUSB (Zadig) once; the zip includes `libusb-1.0.dll`. macOS
needs Homebrew `libusb`.

## Usage

The RIO USB IDs handled as ISDB-T are `3275:0080`, `187f:0600`, and
`187f:0302`. The last ID is deliberately treated as RIO here; do not use its
kernel-table Venice/CMMB firmware.

```sh
./siano-ts --list
./siano-ts --channel 27
./siano-ts -c 27 -t 30 -o /tmp/x.ts
./siano-ts --freq 557142857 --firmware /path/to/isdbt_rio.inp
./siano-ts -c 27 --pid 0 --pid 0x1fff
./siano-ts --channel 27 --fd 3
termux-usb -r -e './siano-ts --channel 27' /dev/bus/usb/001/004
```

`--fd` wraps that descriptor with `libusb_wrap_sys_device` and does not scan
for devices. A leftover integer argument is the same as `--fd` (`termux-usb
-e` appends the fd). `--device N` selects the Nth matching RIO device when
enumerating. `--list` enumerates descriptors without opening USB devices.

The default PID filter is `0x2000` (the Siano/DVB catch-all). Every `--pid`
value is added in addition to it. This firmware may not ACK `0x2000`; the
mux still flows.

A mirakc tuner command can be configured as:

```toml
command = ['/usr/local/bin/siano-ts', '--channel', '{{channel}}']
```

## Scope, license, and provenance

The source is GPL-2.0-or-later; see [COPYING](COPYING). The wire protocol was
rewritten for user space from the GPL Linux `smsusb`, `smscoreapi`, and
`smsdvb` sources in the supplied Linux v6.18 reference snapshot. Kernel DVB
core, usbfs ioctls, libudev, IR, debugfs, and sysfs code are not used. USB
access is libusb only, including the Android/Termux fd path.

Before relying on a real tuner, measure three five-minute captures and count
ffmpeg `corrupt` reports. The target is 0.33 reports/minute, matching the
kernel-side mitigation baseline. That measurement is intentionally not made
in this offline implementation pass.
