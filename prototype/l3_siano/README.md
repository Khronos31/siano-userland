<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Independent Siano L3 adapter prototype

This directory contains a new independent Siano product adapter and a USB-only
host provider. It is connected to the Core L3 candidate header, not to the
existing `siano-device.c`, host facade, or receiver engine.

Inputs are limited to the GPL-2.0-or-later `protocol.c/h` in this repository,
the observed facts in `evidence/siano-independent-protocol-facts-20261104.json`
and `evidence/siano-independent-protocol-observation-facts-20261104.json`, and
the Core L3 candidate at `8bf496c9`. The GPL-2.0-only `smscoreapi.c`,
`sms-cards.c/h`, and the existing backend are not inputs. This is an independent
reimplementation with recorded provenance, not a clean-room or legal claim.

## Targets

- `siano_l3` — product adapter static library (`siano-l3.h`, `siano-l3.cpp`).
- `siano_l3_test_host` — test-only scripted USB host and fixture replay.
- `siano_l3_libusb` — real libusb asynchronous host provider.
- `siano_l3_replay` — strict C consumer test through the Core owner.
- `siano_l3_probe` — real-device probe; it is never started automatically.

The top-level product build enables these targets with
`-DSIANO_BUILD_L3_ADAPTER=ON`. The prototype can also be built stand-alone:

```sh
cmake -S prototype/l3_siano -B build/l3-siano \
  -DSIANO_L3_CORE_INCLUDE_DIR=/path/to/core/prototype/l3_owner/include \
  -DSIANO_L3_CORE_SOURCE_DIR=/path/to/core/prototype/l3_owner
cmake --build build/l3-siano -j2
ctest --test-dir build/l3-siano --output-on-failure
```

Use `-DSIANO_L3_ENABLE_ASAN=ON` or `-DSIANO_L3_ENABLE_TSAN=ON` for one
sanitizer build at a time. With a Core tree that has the split
`dtv_l3_usb_provider` / `dtv_l3_product_adapter` candidate, also pass
`-DSIANO_L3_SPLIT_API=ON` to build the `dtv_l3_open_ex` bridge test. Add
`-DSIANO_L3_OWNER_INIT_FIXTURE=ON` with Core `01c9951` or later to build the
owner-transfer-in-initialize fixture. Queue capacity is a Core candidate value; the replay
test feeds four packets at a time so overflow backpressure is not mistaken for
an adapter failure.

## Real-device boundary

The real path is prepared but was not executed on this host: HAOS enumerates no
`3275:0080` device. The PX-S1UD is assigned to the HOME-PC. Before a device run,
the parent must confirm the exclusive USB allocation, host, firmware path and
artifact. A representative command is:

```sh
./siano_l3_probe --list
./siano_l3_probe --index 0 --firmware /path/to/isdbt_rio.inp \
  --hz 527142857 --seconds 60 --stats-ms 1000 --output /private/ts.bin
```

Run `--warm` for a warmed device and omit the firmware argument. The probe
prints ready mode, tune result, TS/TEI/CC counters, the three close flags, and
the final context-release result. Raw firmware, TS, and pcap data stay private.
