#!/bin/sh
set -eu

./siano-ts --help | grep -q -- '--fd'
if ./siano-ts --list --fd 3 >/dev/null 2>&1; then
    echo "--list --fd should be rejected" >&2
    exit 1
fi
if list_output=$(./siano-ts --list 2>tests/.list-err); then
    printf '%s\n' "$list_output" | grep -Eq '0 devices|supported RIO device'
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
