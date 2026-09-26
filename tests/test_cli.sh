#!/bin/sh
set -eu

./siano-ts --help | grep -q -- '--fd'
./siano-ts --help | grep -q -- '--control'
if ./siano-ts --list --fd 3 >/dev/null 2>&1; then
    echo "--list --fd should be rejected" >&2
    exit 1
fi
if ./siano-ts --list --detach-kernel-driver >/dev/null 2>&1; then
    echo "--list --detach-kernel-driver should be rejected" >&2
    exit 1
fi
if ! ./siano-ts --help | grep -q -- '--detach-kernel-driver'; then
    echo "--help should mention --detach-kernel-driver" >&2
    exit 1
fi
if ./siano-ts --control --list >/dev/null 2>&1; then
    echo "--control --list should be rejected" >&2
    exit 1
fi
if ./siano-ts --control --time 5 >/dev/null 2>&1; then
    echo "--control --time 5 should be rejected" >&2
    exit 1
fi
if ./siano-ts --control --channel 27 --freq 100 >/dev/null 2>&1; then
    echo "--control --channel 27 --freq 100 should be rejected" >&2
    exit 1
fi
if list_output=$(./siano-ts --list 2>tests/.list-err); then
    if [ -n "$list_output" ]; then
        printf '%s\n' "$list_output" | grep -Eq '^(model=|receiver=0 device=1 local=0 system=ISDB-T)'
    fi
else
    if grep -q 'libusb_init' tests/.list-err; then
        echo "CLI tests: --list skipped (no USB backend)"
    else
        cat tests/.list-err >&2
        exit 1
    fi
fi
rm -f tests/.list-err

if ./siano-ts --channel 27 --firmware /definitely/missing/isdbt_rio.inp \
    >/dev/null 2>tests/.cli-error; then
    echo "missing firmware unexpectedly succeeded" >&2
    exit 1
fi
grep -q 'Usage:' tests/.cli-error
rm -f tests/.cli-error
echo "CLI tests: PASS"
