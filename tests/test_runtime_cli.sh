#!/bin/sh
set -eu

sianod=$1
siano_ts=$2
sianoctl=$3
test_root=$(mktemp -d "${TMPDIR:-/tmp}/siano-runtime-cli.XXXXXX")
trap 'rm -rf "$test_root"' EXIT HUP INT TERM

daemon_version=$("$sianod" --version)
case "$daemon_version" in 'sianod '*) ;; *) echo 'sianod --version failed' >&2; exit 1 ;; esac
if "$sianod" --help | grep -q -- '-V'; then
    echo 'sianod help must not list -V' >&2
    exit 1
fi
if "$sianod" -V >/dev/null 2>"$test_root/sianod.err"; then
    echo 'sianod should reject -V' >&2
    exit 1
else
    status=$?
fi
test "$status" -eq 2 || { echo 'sianod -V should return 2' >&2; exit 1; }
grep -q 'argument error: unknown argument' "$test_root/sianod.err"
if "$sianod" --firmware /tmp/no-such-firmware >/dev/null 2>"$test_root/sianod-firmware.err"; then
    echo 'sianod without a device unexpectedly succeeded' >&2
    exit 1
else
    status=$?
fi
test "$status" -eq 2 || { echo 'sianod --firmware preflight should return 2' >&2; exit 1; }
grep -q -- '--device or --fd is required' "$test_root/sianod-firmware.err"

ts_version=$("$siano_ts" --version)
case "$ts_version" in 'siano-ts '*) ;; *) echo 'siano-ts --version failed' >&2; exit 1 ;; esac
if "$siano_ts" --help | grep -q -- '-V'; then
    echo 'siano-ts help must not list -V' >&2
    exit 1
fi
for args in '-V' '--channel 27 -V' '--control -V'; do
    if "$siano_ts" $args >/dev/null 2>"$test_root/siano-ts.err"; then
        echo "siano-ts should reject $args" >&2
        exit 1
    else
        status=$?
    fi
    test "$status" -eq 2 || { echo "siano-ts $args should return 2" >&2; exit 1; }
    grep -q 'unknown' "$test_root/siano-ts.err"
done

ctl_version=$("$sianoctl" --version)
case "$ctl_version" in 'siano-userland '*) ;; *) echo 'sianoctl --version failed' >&2; exit 1 ;; esac
if "$sianoctl" --help | grep -q -- '-V'; then
    echo 'sianoctl help must not list -V' >&2
    exit 1
fi
if "$sianoctl" -V >/dev/null 2>"$test_root/sianoctl.err"; then
    echo 'sianoctl should reject -V' >&2
    exit 1
else
    status=$?
fi
test "$status" -eq 2 || { echo 'sianoctl -V should return 2' >&2; exit 1; }
printf '%s\n' 'Siano device-free runtime CLI tests: PASS'
