#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Cross-compile siano-ts for Termux (Android aarch64 / Bionic).
#
# This is not a musl/glibc aarch64 Linux binary. Termux loads ELF with
# /system/bin/linker64 and Bionic (libc.so, not libc.so.6). Host
# pkg-config, LIBRARY_PATH, LD_RUN_PATH, and shared libusb are kept out
# of the link so /usr and the NDK sysroot never become DT_NEEDED/RPATH.
set -eu

api=${ANDROID_API:-24}
abi=${ANDROID_ABI:-aarch64}
libusb_ver=${LIBUSB_VERSION:-1.0.28}
libusb_sha256=966bb0d231f94a474eaae2e67da5ec844d3527a1f386456394ff432580634b29
libusb_url="https://github.com/libusb/libusb/releases/download/v${libusb_ver}/libusb-${libusb_ver}.tar.bz2"

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
out=$root/build/android-${abi}
prefix=$out/prefix
src=$out/src
pc_wrap=$out/pkg-config-libusb

ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${NDK:-}}}
if [ -z "$ndk" ] || [ ! -d "$ndk" ]; then
	echo "Set ANDROID_NDK_HOME to an Android NDK (r26+)." >&2
	exit 1
fi

case "$(uname -s)-$(uname -m)" in
Linux-x86_64) prebuilt=linux-x86_64 ;;
Linux-aarch64) prebuilt=linux-aarch64 ;;
Darwin-arm64) prebuilt=darwin-arm64 ;;
Darwin-x86_64) prebuilt=darwin-x86_64 ;;
*)
	echo "unsupported NDK host: $(uname -s) $(uname -m)" >&2
	exit 1
	;;
esac

toolchain=$ndk/toolchains/llvm/prebuilt/$prebuilt
cc=$toolchain/bin/${abi}-linux-android${api}-clang
cxx=$toolchain/bin/${abi}-linux-android${api}-clang++
ar=$toolchain/bin/llvm-ar
ranlib=$toolchain/bin/llvm-ranlib
nm=$toolchain/bin/llvm-nm
strip=$toolchain/bin/llvm-strip
# Host PATH only: NDK clang is invoked by absolute path so a random
# `clang`/`pkg-config` on PATH cannot mix glibc objects into the ELF.
host_path=/usr/bin:/bin

if [ ! -x "$cc" ] || [ ! -x "$cxx" ]; then
	echo "NDK clang not found: $cc" >&2
	exit 1
fi

rm -rf "$out"
mkdir -p "$prefix" "$src" "$out"

# Isolated pkg-config: never consult the host database, never emit -L/usr.
cat > "$pc_wrap" <<EOF
#!/bin/sh
# Isolated from the host pkg-config database.
for arg in "\$@"; do
	case "\$arg" in
	--cflags)
		printf '%s ' '-I${prefix}/include/libusb-1.0'
		;;
	--libs)
		printf '%s ' '${prefix}/lib/libusb-1.0.a -llog'
		;;
	--exists|--print-errors|--short-errors|--modversion)
		;;
	--atleast-version=*|--exact-version=*|--max-version=*)
		;;
	libusb-1.0)
		;;
	-*)
		;;
	*)
		echo "pkg-config-libusb: unexpected: \$arg" >&2
		exit 1
		;;
	esac
done
echo
exit 0
EOF
chmod +x "$pc_wrap"

if [ ! -f "$src/libusb-${libusb_ver}.tar.bz2" ]; then
	curl -fsSL -o "$src/libusb-${libusb_ver}.tar.bz2" "$libusb_url"
fi
if command -v sha256sum >/dev/null 2>&1; then
	actual=$(sha256sum "$src/libusb-${libusb_ver}.tar.bz2" | awk '{print $1}')
else
	actual=$(shasum -a 256 "$src/libusb-${libusb_ver}.tar.bz2" | awk '{print $1}')
fi
if [ "$actual" != "$libusb_sha256" ]; then
	echo "libusb checksum mismatch: $actual" >&2
	exit 1
fi
jobs=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

tar -xjf "$src/libusb-${libusb_ver}.tar.bz2" -C "$src"
libusb_src=$src/libusb-${libusb_ver}

# --disable-udev: Android has no libudev; detecting the host one would
# pull a glibc libudev. --disable-shared: Termux's \$PREFIX/lib must
# not appear as DT_NEEDED. Direct path to libusb-1.0.a at link time.
env -i \
	PATH="$host_path" \
	LC_ALL=C \
	CC="$cc" \
	CXX="$cxx" \
	AR="$ar" \
	RANLIB="$ranlib" \
	NM="$nm" \
	STRIP="$strip" \
	PKG_CONFIG=/bin/false \
	CFLAGS="-O2 -fPIC" \
	LDFLAGS="-fPIC" \
	/bin/sh -c "
		set -eu
		cd \"$libusb_src\"
		./configure \
			--host=${abi}-linux-android \
			--prefix=\"$prefix\" \
			--libdir=\"$prefix/lib\" \
			--disable-shared \
			--enable-static \
			--with-pic \
			--disable-udev \
			--disable-examples-build \
			--disable-tests-build \
			--disable-dependency-tracking
		make -j$jobs
		make install
	"

if [ -e "$prefix/lib/libusb-1.0.so" ] || [ -e "$prefix/lib/libusb-1.0.so.0" ]; then
	echo "shared libusb was installed; refusing to link it" >&2
	exit 1
fi
if [ ! -f "$prefix/lib/libusb-1.0.a" ]; then
	echo "static libusb-1.0.a missing" >&2
	exit 1
fi

# env -i drops LIBRARY_PATH / LD_RUN_PATH / PKG_CONFIG_PATH / CPATH,
# which otherwise become DT_RPATH or cause the host libusb to be used.
env -i \
	PATH="$host_path" \
	LC_ALL=C \
	CC="$cc" \
	CXX="$cxx" \
	AR="$ar" \
	RANLIB="$ranlib" \
	NM="$nm" \
	PKG_CONFIG="$pc_wrap" \
	CFLAGS="-O2 -fPIE" \
	LDFLAGS="-pie -Wl,-z,relro -Wl,-z,now -Wl,-z,max-page-size=16384 -Wl,--no-undefined" \
	make -C "$root" clean siano-ts

mkdir -p "$out"
cp -f "$root/siano-ts" "$out/siano-ts"
"$strip" --strip-unneeded "$out/siano-ts"
make -C "$root" clean

# Host GNU readelf, not NDK llvm-readelf: the interpreter/RPATH parser
# is written against binutils output.
PATH="$host_path" \
	"$root/scripts/verify-android-elf.sh" "$out/siano-ts"

echo "built $out/siano-ts"
