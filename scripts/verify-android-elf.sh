#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Fail if a "Termux" binary is actually a glibc/musl ELF, dynamically
# linked against libusb, carrying a host RPATH/RUNPATH, or retaining an
# absolute build/source/NDK path in any string-bearing ELF section.
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
	echo "usage: $0 <siano-ts> [abi] [linker64|linker]" >&2
	exit 2
fi

bin=$1
abi=${ANDROID_ABI:-aarch64}
want_interp=
if [ "$#" -eq 2 ]; then
	case "$2" in
	aarch64|x86_64|armv7a|armeabi-v7a|arm)
		abi=$2
		;;
	linker64|/system/bin/linker64|linker|/system/bin/linker)
		want_interp=$2
		;;
	*)
		echo "second argument must be an ABI or interpreter, got: $2" >&2
		exit 2
		;;
	esac
elif [ "$#" -eq 3 ]; then
	abi=$2
	want_interp=$3
fi

case "$abi" in
aarch64)
	expected_machine='AArch64|AARCH64|ARM aarch64'
	expected_interp=/system/bin/linker64
	;;
x86_64)
	expected_machine='Advanced Micro Devices X86-64|AMD x86-64|X86-64|x86-64'
	expected_interp=/system/bin/linker64
	;;
armv7a|armeabi-v7a|arm)
	expected_machine='ARM|Arm'
	expected_interp=/system/bin/linker
	;;
*)
	echo "unknown ANDROID_ABI=$abi (expected aarch64, x86_64, or armv7a)" >&2
	exit 2
	;;
esac

if [ -z "$want_interp" ]; then
	want_interp=$expected_interp
else
	case "$want_interp" in
	linker64|/system/bin/linker64) want_interp=/system/bin/linker64 ;;
	linker|/system/bin/linker) want_interp=/system/bin/linker ;;
	*)
		echo "interpreter must be linker64 or linker, got: $want_interp" >&2
		exit 2
		;;
	esac
fi
if [ "$want_interp" != "$expected_interp" ]; then
	echo "$abi requires interpreter $expected_interp, got: $want_interp" >&2
	exit 2
fi

if [ ! -f "$bin" ]; then
	echo "missing binary: $bin" >&2
	exit 1
fi


# GNU readelf first: DT_RPATH tags are `(RPATH)` there, not `RPATH` as in
# llvm-readelf. Callers building with an NDK on BSD/macOS may provide the
# NDK's llvm-readelf explicitly because those systems do not ship readelf.
if [ -n "${READELF:-}" ]; then
	readelf_bin=$READELF
elif command -v readelf >/dev/null 2>&1; then
	readelf_bin=$(command -v readelf)
elif command -v llvm-readelf >/dev/null 2>&1; then
	readelf_bin=$(command -v llvm-readelf)
else
	echo "readelf not found" >&2
	exit 1
fi
if [ ! -x "$readelf_bin" ]; then
	echo "readelf is not executable: $readelf_bin" >&2
	exit 1
fi
if ! command -v strings >/dev/null 2>&1; then
	echo "strings not found" >&2
	exit 1
fi

dump_hdr=$("$readelf_bin" -h "$bin")
dump_prog=$("$readelf_bin" -lW "$bin")
dump_dyn=$("$readelf_bin" -d "$bin")
dump_sections=$("$readelf_bin" -SW "$bin")

# Release ELF policy: retain runtime/dynamic and unwind metadata, but never
# ship DWARF or the regular linker's symbol table.  The same rule is enforced
# by audit-artifact.py for archive inputs.
if printf '%s\n' "$dump_sections" | awk '
/^[[:space:]]*\[[[:space:]]*[0-9]+\][[:space:]]+/ {
	name=$3
	if (name ~ /^\.debug/ || name ~ /^\.zdebug/ || name == ".symtab") {
		print name
		bad=1
	}
}
END { exit bad }
'; then
	:
else
	echo "Android release ELF contains forbidden debug/symbol section" >&2
	exit 1
fi

echo "$dump_hdr"
echo "$dump_prog"
echo "$dump_dyn"

machine=$(printf '%s\n' "$dump_hdr" | awk -F: '/Machine:/ {gsub(/^[ \t]+/, "", $2); print $2; exit}')
if ! printf '%s\n' "$machine" | grep -E -x -- "$expected_machine" >/dev/null; then
	echo "expected $abi machine, got: $machine" >&2
	exit 1
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

# Android 15 and newer x86_64 devices may use 16 KiB pages.  Release ELFs
# must be link-compatible with that loader; require every loadable segment to
# advertise the 16 KiB maximum page size recorded by the linker.
load_alignments=$(printf '%s\n' "$dump_prog" | awk '$1 == "LOAD" { print $NF }')
if [ -z "$load_alignments" ]; then
	echo "no LOAD segments found" >&2
	exit 1
fi
for alignment in $load_alignments; do
	case "$alignment" in
	0x4000|0x00004000)
		;;
	*)
		echo "Android LOAD alignment must be 16 KiB (0x4000), got: $alignment" >&2
		exit 1
		;;
	esac
done

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

# The build script supplies every absolute input path it used. This catches
# paths in .debug_*, .comment, string literals, and other non-dynamic sections
# even after the dynamic-section checks above pass.
for marker in ${ANDROID_PATH_MARKERS:-}; do
	if strings -a "$bin" | grep -F -- "$marker" >/dev/null; then
		echo "absolute build/source/NDK path found in ELF: $marker" >&2
		exit 1
	fi
done
if strings -a "$bin" | grep -E '(^|[[:space:]])/(home|root|tmp|opt|config|workspace|usr/src|var/tmp)/' >/dev/null; then
	echo "absolute host path found in ELF string data" >&2
	exit 1
fi

echo "ok: $bin"
echo "  interpreter: $interp"
echo "  NEEDED: $(printf '%s' "$needed" | tr '\n' ' ')"
echo "  RPATH/RUNPATH: none"
