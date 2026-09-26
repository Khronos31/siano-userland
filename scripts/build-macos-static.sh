#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the macOS arm64 release binary with a pinned static libusb. Only the
# system libraries and frameworks (libSystem, IOKit, CoreFoundation, Security)
# remain dynamic, so the binary does not need Homebrew or any other libusb.
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH='' cd -- "$script_dir/.." && pwd -P)

[ "$#" -eq 0 ] || { echo "unexpected argument: $1" >&2; exit 2; }
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
	echo "build-macos-static.sh must run on arm64 macOS" >&2
	exit 1
fi

version=$(tr -d '\n' < "$root/VERSION")
source_ref=${SOURCE_REF:-${GITHUB_SHA:-}}
if ! printf '%s' "$source_ref" | grep -Eq '^[0-9a-f]{40}([0-9a-f]{24})?$'; then
	if git -C "$root" rev-parse --verify HEAD >/dev/null 2>&1; then
		source_ref=$(git -C "$root" rev-parse HEAD)
	else
		echo "SOURCE_REF or GITHUB_SHA is required outside a Git checkout" >&2
		exit 1
	fi
fi

libusb_version=1.0.30
libusb_sha256=fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf
libusb_url="https://github.com/libusb/libusb/releases/download/v${libusb_version}/libusb-${libusb_version}.tar.bz2"
libusb_options="--disable-shared --enable-static --disable-examples-build --disable-tests-build"
# The first macOS release for Apple silicon. libusb only needs the Objective-C
# runtime for deployment targets before 10.12, so -lobjc is not linked.
deployment_target=11.0
system_libs="-framework IOKit -framework CoreFoundation -framework Security"

# Keep a caller-provided libusb archive before the build directory is emptied;
# it may live inside the previous build.
provided_archive=
if [ -n "${LIBUSB_SOURCE_ARCHIVE:-}" ]; then
	if [ ! -f "$LIBUSB_SOURCE_ARCHIVE" ]; then
		echo "libusb source archive not found: $LIBUSB_SOURCE_ARCHIVE" >&2
		exit 1
	fi
	provided_archive=$(mktemp)
	trap 'rm -f "$provided_archive"' EXIT HUP INT TERM
	cp -f "$LIBUSB_SOURCE_ARCHIVE" "$provided_archive"
fi

# The build directory is emptied, so only the default directory or a new or
# empty one is accepted.
default_build_root=$root/build/darwin-arm64
build_root=${MACOS_BUILD_DIR:-$default_build_root}
case "$build_root" in
/*) ;;
*) build_root=$(pwd -P)/$build_root ;;
esac
if [ "$build_root" = "$default_build_root" ]; then
	rm -rf "$build_root"
elif [ -e "$build_root" ] && [ -n "$(ls -A "$build_root")" ]; then
	echo "refusing to use a non-empty macOS build directory: $build_root" >&2
	exit 1
fi
mkdir -p "$build_root"
build_root=$(CDPATH='' cd -- "$build_root" && pwd -P)
prefix=$build_root/prefix
src=$build_root/src
pc_wrap=$build_root/pkg-config-libusb
mkdir -p "$src" "$build_root/evidence"

archive=$build_root/libusb-${libusb_version}.tar.bz2
if [ -n "$provided_archive" ]; then
	cp -f "$provided_archive" "$archive"
else
	curl -fsSL --retry 2 -o "$archive" "$libusb_url"
fi
actual=$(shasum -a 256 "$archive" | awk '{print $1}')
if [ "$actual" != "$libusb_sha256" ] && [ "${ALLOW_UNPINNED_LIBUSB:-0}" != 1 ]; then
	echo "libusb checksum mismatch: $actual" >&2
	exit 1
fi

tar -xjf "$archive" -C "$src"
libusb_src=$src/libusb-${libusb_version}

cat > "$pc_wrap" <<EOF
#!/bin/sh
set -eu
for arg in "\$@"; do
 case "\$arg" in
  --cflags) printf '%s ' '-I${prefix}/include/libusb-1.0' ;;
  --libs|--static) printf '%s ' '${prefix}/lib/libusb-1.0.a ${system_libs} -pthread' ;;
  libusb-1.0|--exists|--print-errors|--short-errors|--modversion) ;;
  --atleast-version=*|--exact-version=*|--max-version=*) ;;
  -*) ;;
  *) echo "pkg-config-libusb: unexpected argument: \$arg" >&2; exit 1 ;;
 esac
done
printf '\\n'
EOF
chmod +x "$pc_wrap"

# A clean environment keeps Homebrew (pkg-config, headers, and libraries under
# /opt/homebrew) out of both builds.
build_cflags="-O2 -arch arm64 -ffile-prefix-map=$root=. -ffile-prefix-map=$build_root=. -ffile-prefix-map=$libusb_src=."
jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 2)
env -i \
	PATH=/usr/bin:/bin:/usr/sbin:/sbin \
	HOME="${HOME:-/tmp}" \
	LC_ALL=C \
	MACOSX_DEPLOYMENT_TARGET=$deployment_target \
	CC=cc \
	PKG_CONFIG=/usr/bin/false \
	CFLAGS="$build_cflags" \
	LDFLAGS="-arch arm64" \
	/bin/sh -c "
		set -eu
		cd '$libusb_src'
		./configure --prefix='$prefix' $libusb_options --disable-dependency-tracking
		make -j$jobs
		make install
	"

if [ ! -f "$prefix/lib/libusb-1.0.a" ] || ls "$prefix/lib"/libusb-1.0*.dylib >/dev/null 2>&1; then
	echo "static libusb installation is incomplete" >&2
	exit 1
fi

env -i \
	PATH=/usr/bin:/bin:/usr/sbin:/sbin \
	HOME="${HOME:-/tmp}" \
	LC_ALL=C \
	MACOSX_DEPLOYMENT_TARGET=$deployment_target \
	CC=cc \
	PKG_CONFIG="$pc_wrap" \
	CFLAGS="$build_cflags" \
	LDFLAGS="-arch arm64" \
	make -C "$root" clean siano-ts

cp -f "$root/siano-ts" "$build_root/siano-ts"
chmod 755 "$build_root/siano-ts"
make -C "$root" clean
# Apple strip -S -x removes debug and local symbols. -N is not used: it drops
# every nlist entry and is not needed to remove debug symbols.
strip_bin=$(xcrun --find strip)
"$strip_bin" -S -x "$build_root/siano-ts"
"$build_root/siano-ts" --help >/dev/null

cat > "$build_root/evidence/build.properties" <<EOF
target_os=darwin
target_arch=arm64
macos_deployment_target=$deployment_target
linkage=static
libusb_version=$libusb_version
libusb_source_url=$libusb_url
libusb_source_sha256=$actual
libusb_build_options=$libusb_options
libusb_backend=darwin
system_libraries=$system_libs
EOF

"$root/scripts/audit-artifact.sh" --platform darwin-arm64 \
	--binary "$build_root/siano-ts" --source-ref "$source_ref" \
	--evidence-output "$build_root/evidence/binary-audit.json"
echo "built $build_root/siano-ts (version $version, libusb $libusb_version, source $source_ref)"
