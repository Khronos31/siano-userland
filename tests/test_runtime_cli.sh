#!/bin/sh
set -eu

sianod=$1
siano_ts=$2
sianoctl=$3
sianod=$(cd "$(dirname "$sianod")" && pwd)/$(basename "$sianod")
siano_ts=$(cd "$(dirname "$siano_ts")" && pwd)/$(basename "$siano_ts")
sianoctl=$(cd "$(dirname "$sianoctl")" && pwd)/$(basename "$sianoctl")
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
if "$sianod" --device usb:1-2 --firmware '' --firmware /tmp/firmware \
    >/dev/null 2>"$test_root/sianod-duplicate-firmware.err"; then
    echo 'sianod should reject duplicate --firmware' >&2
    exit 1
else
    status=$?
fi
test "$status" -eq 2 || { echo 'sianod duplicate --firmware should return 2' >&2; exit 1; }

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

sentinel="$test_root/output.ts"
printf '%s' KEEP >"$sentinel"
for args in \
    "--control --instance RoomA --receiver 0 -o - --output $sentinel" \
    "--control --instance RoomA --receiver 0 --tune-timeout-ms 100 --tune-timeout-ms 200" \
    "--control --instance CON --receiver 0 -o $sentinel" \
    "--control --instance room/name --receiver 0 -o $sentinel" \
    "--control --instance RoomA. --receiver 0 -o $sentinel"; do
    if "$siano_ts" $args >/dev/null 2>"$test_root/siano-ts-contract.err"; then
        echo "siano-ts should reject $args" >&2
        exit 1
    else
        status=$?
    fi
    test "$status" -eq 2 || { echo "siano-ts $args should return 2" >&2; exit 1; }
    test "$(cat "$sentinel")" = KEEP || {
        echo "siano-ts changed output before rejecting $args" >&2
        exit 1
    }
done
if "$siano_ts" --control --runtime-dir "" --runtime-dir "$test_root" \
    --instance RoomA --receiver 0 -o "$sentinel" \
    >/dev/null 2>"$test_root/siano-ts-empty-runtime-duplicate.err"; then
    echo 'siano-ts should reject duplicate --runtime-dir after an empty value' >&2
    exit 1
else
    status=$?
fi
test "$status" -eq 2 || { echo 'siano-ts empty --runtime-dir duplicate should return 2' >&2; exit 1; }
if "$siano_ts" --control --instance "" --instance RoomA --receiver 0 \
    -o "$sentinel" >/dev/null 2>"$test_root/siano-ts-empty-instance-duplicate.err"; then
    echo 'siano-ts should reject duplicate --instance after an empty value' >&2
    exit 1
else
    status=$?
fi
test "$status" -eq 2 || { echo 'siano-ts empty --instance duplicate should return 2' >&2; exit 1; }
test "$(cat "$sentinel")" = KEEP || {
    echo 'siano-ts changed output before rejecting an empty-value duplicate' >&2
    exit 1
}

"$siano_ts" --device --output -h --version >"$test_root/siano-ts-metadata.out"
grep -q '^usage:' "$test_root/siano-ts-metadata.out" || {
    echo 'siano-ts metadata scan differs from the shared parser' >&2
    exit 1
}

(
    cd "$test_root"
    "$siano_ts" --control --instance RoomA --receiver 0 --output -h \
        </dev/null >/dev/null 2>"$test_root/siano-ts-output-help-value.err"
)
test -f "$test_root/-h" || {
    echo 'siano-ts did not treat -h as the output file value' >&2
    exit 1
}
if grep -q '^usage:' "$test_root/siano-ts-output-help-value.err"; then
    echo 'siano-ts treated the output value -h as --help' >&2
    exit 1
fi

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
