#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Fail if a "Termux" binary is actually a glibc/musl ELF, dynamically
# linked against libusb, or carrying a host RPATH/RUNPATH.
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
	echo "usage: $0 <siano-ts> [linker64|linker]" >&2
	exit 2
fi

bin=$1
want_interp=${2:-}
abi=${ANDROID_ABI:-aarch64}
case "$want_interp" in
'' )
	case "$abi" in
	aarch64) want_interp=/system/bin/linker64 ;;
	armv7a|armeabi-v7a|arm) want_interp=/system/bin/linker ;;
	*)
		echo "unknown ANDROID_ABI=$abi (expected aarch64 or armv7a)" >&2
		exit 2
		;;
	esac
	;;
linker64|/system/bin/linker64) want_interp=/system/bin/linker64 ;;
linker|/system/bin/linker) want_interp=/system/bin/linker ;;
*)
	echo "interpreter must be linker64 or linker, got: $want_interp" >&2
	exit 2
	;;
esac

if [ ! -f "$bin" ]; then
	echo "missing binary: $bin" >&2
	exit 1
fi

# GNU readelf first: DT_RPATH tags are `(RPATH)` there, not `RPATH` as in llvm-readelf.
if command -v readelf >/dev/null 2>&1; then
	readelf_bin=$(command -v readelf)
elif command -v llvm-readelf >/dev/null 2>&1; then
	readelf_bin=$(command -v llvm-readelf)
else
	echo "readelf not found" >&2
	exit 1
fi

dump_hdr=$("$readelf_bin" -h "$bin")
dump_prog=$("$readelf_bin" -l "$bin")
dump_dyn=$("$readelf_bin" -d "$bin")

echo "$dump_hdr"
echo "$dump_prog"
echo "$dump_dyn"

machine=$(printf '%s\n' "$dump_hdr" | awk -F: '/Machine:/ {gsub(/^[ \t]+/, "", $2); print $2; exit}')
if [ "$want_interp" = "/system/bin/linker64" ]; then
	case "$machine" in
	AArch64|AARCH64|"ARM aarch64")
		;;
	*)
		echo "expected AArch64, got: $machine" >&2
		exit 1
		;;
	esac
else
	case "$machine" in
	ARM|Arm)
		;;
	*)
		echo "expected 32-bit ARM, got: $machine" >&2
		exit 1
		;;
	esac
fi

type=$(printf '%s\n' "$dump_hdr" | awk -F: '/Type:/ {gsub(/^[ \t]+/, "", $2); print $2; exit}')
case "$type" in
DYN*|*"shared object"*)
	;;
*)
	echo "expected a PIE (DYN) ELF, got: $type" >&2
	exit 1
	;;
esac

interp=$(printf '%s\n' "$dump_prog" | sed -n 's/.*\[Requesting program interpreter: \(.*\)\]/\1/p' | head -n 1)
if [ -z "$interp" ]; then
	echo "no PT_INTERP; a static musl/glibc binary will not load on Android" >&2
	exit 1
fi
if [ "$interp" != "$want_interp" ]; then
	echo "interpreter must be $want_interp, got: $interp" >&2
	exit 1
fi

needed=$(printf '%s\n' "$dump_dyn" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
if [ -z "$needed" ]; then
	echo "no DT_NEEDED entries" >&2
	exit 1
fi

needed_sp=$(printf '%s' "$needed" | tr '\n' ' ')
for lib in $needed_sp; do
	case "$lib" in
	libc.so|libm.so|libdl.so|liblog.so)
		;;
	*)
		echo "unexpected DT_NEEDED: $lib" >&2
		echo "allowed: libc.so libm.so libdl.so liblog.so (Bionic)" >&2
		exit 1
		;;
	esac
done

case " $needed_sp " in
*" libc.so "*) ;;
*)
	echo "missing DT_NEEDED libc.so (Bionic)" >&2
	exit 1
	;;
esac
case " $needed_sp " in
*" liblog.so "*) ;;
*)
	echo "missing DT_NEEDED liblog.so (required by static libusb on Android)" >&2
	exit 1
	;;
esac

case "$needed_sp" in
*libusb*|*libudev*|*libc.so.6*|*ld-linux*|*ld-musl*|*libc.musl*)
	echo "host or libusb shared library leaked into DT_NEEDED:" >&2
	echo "$needed_sp" >&2
	exit 1
	;;
esac

rpath=$(printf '%s\n' "$dump_dyn" | sed -n 's/.*Library rpath: \[\(.*\)\]/\1/p')
runpath=$(printf '%s\n' "$dump_dyn" | sed -n 's/.*Library runpath: \[\(.*\)\]/\1/p')
if [ -n "$rpath" ] || [ -n "$runpath" ]; then
	echo "DT_RPATH/DT_RUNPATH must be empty (host or Termux prefix paths leak here)" >&2
	echo "  RPATH=$rpath" >&2
	echo "  RUNPATH=$runpath" >&2
	exit 1
fi
if printf '%s\n' "$dump_dyn" | grep -E '\(RPATH\)|\(RUNPATH\)|[[:space:]]RPATH[[:space:]]|[[:space:]]RUNPATH[[:space:]]' >/dev/null; then
	echo "DT_RPATH/DT_RUNPATH tag present without a parsed path" >&2
	exit 1
fi

if printf '%s\n' "$dump_dyn$dump_prog" | grep -E '/lib64/ld-linux|/lib/ld-musl|/lib/ld-linux-aarch64'; then
	echo "GNU/musl loader path found in ELF" >&2
	exit 1
fi

if printf '%s\n' "$dump_dyn" | grep -E '/usr/|/home/|/opt/|data/data/com.termux'; then
	echo "host or Termux prefix path found in dynamic section" >&2
	exit 1
fi

echo "ok: $bin"
echo "  interpreter: $interp"
echo "  NEEDED: $(printf '%s' "$needed" | tr '\n' ' ')"
echo "  RPATH/RUNPATH: none"
