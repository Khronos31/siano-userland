#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Cross-compile siano-ts for Termux (Bionic). ANDROID_ABI is aarch64
# (linker64) or armv7a (linker). This is not a musl/glibc Linux binary.
# Host pkg-config, LIBRARY_PATH, LD_RUN_PATH, and shared libusb are kept
# out of the link so /usr and the NDK sysroot never become DT_NEEDED/RPATH.
# Prefix maps make the whole ELF reproducible with respect to the checkout,
# temporary build tree, libusb source, and NDK installation paths.
set -eu

api=${ANDROID_API:-24}
abi=${ANDROID_ABI:-aarch64}
case "$abi" in
aarch64)
	clang_triple=aarch64-linux-android${api}
	autotools_host=aarch64-linux-android
	want_interp=/system/bin/linker64
	;;
armv7a|armeabi-v7a|arm)
	abi=armv7a
	clang_triple=armv7a-linux-androideabi${api}
	autotools_host=armv7a-linux-androideabi
	want_interp=/system/bin/linker
	;;
*)
	echo "ANDROID_ABI must be aarch64 or armv7a, got: $abi" >&2
	exit 1
	;;
esac
libusb_ver=${LIBUSB_VERSION:-1.0.28}
libusb_sha256=966bb0d231f94a474eaae2e67da5ec844d3527a1f386456394ff432580634b29
libusb_url="https://github.com/libusb/libusb/releases/download/v${libusb_ver}/libusb-${libusb_ver}.tar.bz2"

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
out=$root/build/android-${abi}
prefix=$out/prefix
src=$out/src
pc_wrap=$out/pkg-config-libusb

ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${NDK:-}}}
if [ -z "$ndk" ] || [ ! -d "$ndk" ]; then
	echo "Set ANDROID_NDK_HOME to an Android NDK (r27)." >&2
	exit 1
fi

ndk_properties=$ndk/source.properties
ndk_notice=$ndk/NOTICE
ndk_toolchain_notice=$ndk/NOTICE.toolchain
if [ ! -f "$ndk_properties" ] || [ ! -f "$ndk_notice" ] || [ ! -f "$ndk_toolchain_notice" ]; then
	echo "NDK source.properties, NOTICE, and NOTICE.toolchain are required for provenance." >&2
	exit 1
fi
ndk_revision=$(awk -F'= *' '$1 == "Pkg.Revision " { print $2; exit }' "$ndk_properties")
case "$ndk_revision" in
27.*) ;;
*)
	echo "candidate Android builds require NDK r27, got: $ndk_revision" >&2
	exit 1
	;;
esac

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
cc=$toolchain/bin/${clang_triple}-clang
cxx=$toolchain/bin/${clang_triple}-clang++
ar=$toolchain/bin/llvm-ar
ranlib=$toolchain/bin/llvm-ranlib
nm=$toolchain/bin/llvm-nm
strip=$toolchain/bin/llvm-strip
# Host PATH only: NDK clang is invoked by absolute path so a random
# `clang`/`pkg-config` on PATH cannot mix glibc objects into the ELF.
host_path=/usr/bin:/bin

prefix_maps="-fdebug-compilation-dir=."
for prefix_path in "$root" "$out" "$src" "$prefix" "$ndk"; do
	prefix_maps="$prefix_maps -ffile-prefix-map=$prefix_path=."
	prefix_maps="$prefix_maps -fdebug-prefix-map=$prefix_path=."
	prefix_maps="$prefix_maps -fmacro-prefix-map=$prefix_path=."
done

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

if command -v sha256sum >/dev/null 2>&1; then
	sha256_file() {
		sha256sum "$1" | awk '{print $1}'
	}
elif command -v shasum >/dev/null 2>&1; then
	sha256_file() {
		shasum -a 256 "$1" | awk '{print $1}'
	}
else
	echo "sha256sum or shasum is required for provenance checks" >&2
	exit 1
fi

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
	CFLAGS="-O2 -fPIC $prefix_maps" \
	LDFLAGS="-fPIC" \
	/bin/sh -c "
		set -eu
		cd \"$libusb_src\"
		./configure \
			--host=${autotools_host} \
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
	CFLAGS="-O2 -fPIE $prefix_maps" \
	LDFLAGS="-pie -Wl,-z,relro -Wl,-z,now -Wl,-z,max-page-size=16384 -Wl,--no-undefined -Wl,-Map=$out/siano-ts.map" \
	make -C "$root" clean siano-ts

mkdir -p "$out"
cp -f "$root/siano-ts" "$out/siano-ts"
"$strip" --strip-unneeded "$out/siano-ts"
make -C "$root" clean

# Prefer host GNU readelf because the interpreter/RPATH parser is written
# against binutils output; use the NDK's llvm-readelf when BSD/macOS has no
# host readelf available.
if command -v readelf >/dev/null 2>&1; then
	readelf_bin=$(command -v readelf)
elif [ -x "$toolchain/bin/llvm-readelf" ]; then
	readelf_bin=$toolchain/bin/llvm-readelf
else
	echo "readelf or NDK llvm-readelf not found" >&2
	exit 1
fi
PATH="$host_path" \
	READELF="$readelf_bin" \
	ANDROID_ABI="$abi" \
	ANDROID_PATH_MARKERS="$root $out $src $prefix $ndk" \
	"$root/scripts/verify-android-elf.sh" "$out/siano-ts" "$want_interp"

python3 "$root/scripts/android-link-inventory.py" \
	--map "$out/siano-ts.map" --output "$out/static-link-inventory.tsv"

mkdir -p "$out/ndk"
cp -f "$ndk_properties" "$out/ndk/source.properties"
cp -f "$ndk_notice" "$out/ndk/NOTICE"
cp -f "$ndk_toolchain_notice" "$out/ndk/NOTICE.toolchain"
ndk_properties_sha256=$(sha256_file "$out/ndk/source.properties")
ndk_notice_sha256=$(sha256_file "$out/ndk/NOTICE")
ndk_notice_toolchain_sha256=$(sha256_file "$out/ndk/NOTICE.toolchain")

cat > "$out/build.properties" <<EOF
android_abi=$abi
android_api=$api
ndk_revision=$ndk_revision
libusb_version=$libusb_ver
libusb_source_url=$libusb_url
libusb_source_sha256=$libusb_sha256
ndk_source_properties_sha256=$ndk_properties_sha256
ndk_notice_sha256=$ndk_notice_sha256
ndk_notice_toolchain_sha256=$ndk_notice_toolchain_sha256
EOF

echo "built $out/siano-ts"
