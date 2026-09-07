#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Rebuild the exact corresponding-source archive and relink against a
# deliberately changed, harmless libusb source. This is an offline test.
set -eu

usage() {
	echo "usage: $0 --source-archive ARCHIVE --arch x86_64|aarch64" >&2
	exit 2
}

source_archive=
arch=
while [ "$#" -gt 0 ]; do
	case "$1" in
	--source-archive) [ "$#" -ge 2 ] || usage; source_archive=$2; shift 2 ;;
	--arch) [ "$#" -ge 2 ] || usage; arch=$2; shift 2 ;;
	*) usage ;;
	esac
done
[ -n "$source_archive" ] && [ -f "$source_archive" ] || usage
case "$arch" in x86_64|aarch64) ;; *) usage ;; esac

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH='' cd -- "$script_dir/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/source" "$tmp/modified"
"$root/scripts/audit-artifact.sh" --source-archive --archive "$source_archive" >/dev/null
tar -xzf "$source_archive" -C "$tmp/source"
source_root=$tmp/source/repository
libusb_archive=$tmp/source/third_party/libusb-1.0.30.tar.bz2
[ -x "$source_root/scripts/build-linux-static.sh" ] || { echo "source archive has no static build script" >&2; exit 1; }

source_ref=$(python3 - "$tmp/source/source-manifest.json" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as handle:
    value = json.load(handle)
print(value["source_ref"])
PY
)

build_one() {
	input_archive=$1
	out=$2
	SOURCE_REF="$source_ref" LINUX_ARCH="$arch" LINUX_BUILD_DIR="$out" \
		LIBUSB_SOURCE_ARCHIVE="$input_archive" ALLOW_UNPINNED_LIBUSB=1 \
		"$source_root/scripts/build-linux-static.sh"
}

build_one "$libusb_archive" "$tmp/baseline"
baseline=$tmp/baseline/siano-ts
if strings "$baseline" | grep -F "https://libusb.relink-test.invalid" >/dev/null 2>&1; then
	echo "baseline unexpectedly contains the modified libusb marker" >&2
	exit 1
fi

tar -xjf "$libusb_archive" -C "$tmp/modified"
modified_dir=$tmp/modified/libusb-1.0.30
marker=https://libusb.relink-test.invalid
python3 - "$modified_dir/libusb/core.c" "$marker" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
old = "https://libusb.info"
new = sys.argv[2]
data = path.read_text(encoding="utf-8")
if data.count(old) != 2:
    raise SystemExit("unexpected libusb core URL count")
path.write_text(data.replace(old, new), encoding="utf-8")
PY
modified_archive=$tmp/libusb-modified.tar.bz2
tar -cjf "$modified_archive" -C "$tmp/modified" libusb-1.0.30
build_one "$modified_archive" "$tmp/modified-build"
modified=$tmp/modified-build/siano-ts

if cmp -s "$baseline" "$modified"; then
	echo "modified libusb did not change the linked executable" >&2
	exit 1
fi
if strings "$modified" | grep -F "$marker" >/dev/null 2>&1; then
	echo "static relink test: PASS (modified libusb marker is present)"
else
	echo "modified libusb marker is absent from the linked executable" >&2
	exit 1
fi
