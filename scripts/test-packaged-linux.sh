#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Start a downloaded, final Linux package archive without touching USB.
set -eu

usage() {
	echo "usage: $0 --archive ARCHIVE --architecture x86_64|aarch64" >&2
	exit 2
}

archive=
architecture=
while [ "$#" -gt 0 ]; do
	case "$1" in
	--archive) [ "$#" -ge 2 ] || usage; archive=$2; shift 2 ;;
	--architecture) [ "$#" -ge 2 ] || usage; architecture=$2; shift 2 ;;
	*) usage ;;
	esac
done
[ -n "$archive" ] && [ -f "$archive" ] || usage
case "$architecture" in x86_64|aarch64) ;; *) usage ;; esac

case "$architecture" in
x86_64) expected_machine=x86_64 ;;
aarch64) expected_machine=aarch64 ;;
esac
[ "$(uname -m)" = "$expected_machine" ] || {
	echo "runtime architecture mismatch: expected $expected_machine, got $(uname -m)" >&2
	exit 1
}
case "$archive" in
*-linux-"$architecture".tar.gz) ;;
*) echo "archive name does not match architecture: $archive" >&2; exit 1 ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
tar -xzf "$archive" -C "$tmp"
binary_count=$(find "$tmp" -type f -name siano-ts | wc -l)
[ "$binary_count" -eq 1 ] || {
	echo "expected one packaged siano-ts binary, found $binary_count" >&2
	exit 1
}
binary=$(find "$tmp" -type f -name siano-ts -print -quit)
[ -x "$binary" ] || { echo "packaged binary is not executable: $binary" >&2; exit 1; }
"$binary" --help >/dev/null
echo "packaged Linux $architecture archive startup: PASS"
