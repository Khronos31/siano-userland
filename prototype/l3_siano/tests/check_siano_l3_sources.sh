#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
root=${1:?repository root required}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sources="
${here}/../siano-l3.h
${here}/../siano-l3.cpp
${here}/../siano-l3-libusb.h
${here}/../siano-l3-libusb.cpp
${here}/../tools/siano-l3-probe.cpp
${here}/siano-l3-test-host.h
${here}/siano-l3-test-host.cpp
${here}/test_siano_l3_replay.c
${here}/test_siano_l3_split.c
${here}/test_l3_owner_init_fixture.c
${here}/test_siano_l3_factory.c
"
for source in $sources; do
    if ! head -n 1 "$source" | grep -q 'SPDX-License-Identifier: GPL-2.0-or-later'; then
        echo "missing GPL-2.0-or-later SPDX: $source" >&2
        exit 1
    fi
done
if grep -nE '^[[:space:]]*#include.*(smscoreapi|sms-cards|siano-device|receiver_engine|host[_-]facade)' $sources; then
    echo "forbidden GPL-2.0-only or existing-backend include in new source" >&2
    exit 1
fi
if grep -nE 'SPDX-License-Identifier:[[:space:]]*GPL-2\.0-only' $sources; then
    echo "forbidden GPL-2.0-only SPDX in new source" >&2
    exit 1
fi
if [ -f "${here}/../CMakeLists.txt" ]; then
    if ! head -n 1 "${here}/../CMakeLists.txt" | grep -q 'SPDX-License-Identifier: GPL-2.0-or-later'; then
        echo "missing GPL-2.0-or-later SPDX: CMakeLists.txt" >&2
        exit 1
    fi
fi
if command -v nm >/dev/null 2>&1; then
    archive=$(find "${root}" -name 'libsiano_l3.a' -print -quit 2>/dev/null || true)
    if [ -n "$archive" ]; then
        if nm -C --defined-only "$archive" 2>/dev/null | grep -qE 'smscoreapi|sms_cards|siano_device|receiver_engine'; then
            echo "forbidden symbol in libsiano_l3.a" >&2
            exit 1
        fi
    fi
fi
echo "siano l3 source/SPDX check: PASS"
