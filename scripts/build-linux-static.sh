#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the libc-independent Linux release binary with a pinned static libusb.
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH='' cd -- "$script_dir/.." && pwd -P)

reject_build_root() {
	echo "refusing unsafe Linux build directory: $1" >&2
	return 1
}

path_contains_symlink() {
	candidate=$1
	while [ "$candidate" != "/" ]; do
		if [ -L "$candidate" ]; then
			return 0
		fi
		next=$(dirname -- "$candidate")
		[ "$next" != "$candidate" ] || break
		candidate=$next
	done
	return 1
}

validate_build_root() {
	requested=$1
	if [ -z "$requested" ] || [ "$requested" = "/" ]; then
		reject_build_root "empty or root path"
		return 1
	fi
	case "$requested" in
	/*) input=$requested ;;
	*) input=$(pwd -P)/$requested ;;
	esac
	if path_contains_symlink "$input"; then
		reject_build_root "symlink: $requested"
		return 1
	fi
	if ! mkdir -p -- "$input"; then
		echo "cannot create Linux build directory: $requested" >&2
		return 1
	fi
	if path_contains_symlink "$input"; then
		reject_build_root "symlink: $requested"
		return 1
	fi
	canonical=$(CDPATH='' cd -- "$input" && pwd -P) || {
		echo "cannot resolve Linux build directory: $requested" >&2
		return 1
	}
	if [ "$canonical" = "/" ] || [ "$(dirname -- "$canonical")" = "/" ]; then
		reject_build_root "$canonical"
		return 1
	fi
	case "$root/" in
	"$canonical/"*)
		reject_build_root "$canonical (repository or its parent)"
		return 1
		;;
	esac
	case "$input/" in
	"$root/"*)
		case "$canonical/" in
		"$root/build/"*) ;;
		*)
			reject_build_root "$requested (path escapes repository build/ )"
			return 1
			;;
		esac
		;;
	esac
	case "$canonical/" in
	"$root/"*)
		case "$canonical/" in
		"$root/build/"*) ;;
		*)
			reject_build_root "$canonical (repository path outside build/ )"
			return 1
			;;
		esac
		;;
	esac
	if [ -e "$canonical/.git" ]; then
		reject_build_root "$canonical (Git working tree)"
		return 1
	fi
	printf '%s\n' "$canonical"
}

clean_build_root() {
	find "$1" -mindepth 1 -depth -delete
}

build_root_self_test() {
	for candidate in "" "/" "$root" "$root/.." "$root/build/.." "$root/.git" "$root/README.md" "/tmp"; do
		if validate_build_root "$candidate" >/dev/null 2>&1; then
			echo "build directory safety self-test accepted unsafe path: ${candidate:-<empty>}" >&2
			return 1
		fi
	done
	safety_tmp=$(mktemp -d "${TMPDIR:-/tmp}/siano-build-safety.XXXXXX")
	trap 'find "$safety_tmp" -depth -delete' EXIT HUP INT TERM
	allowed=$(validate_build_root "$safety_tmp/allowed")
	[ "$allowed" = "$safety_tmp/allowed" ]
	mkdir -p "$allowed/nested"
	touch "$allowed/nested/marker"
	clean_build_root "$allowed"
	[ -d "$allowed" ]
	[ ! -e "$allowed/nested" ]
	ln -s /tmp "$safety_tmp/link"
	if validate_build_root "$safety_tmp/link/build" >/dev/null 2>&1; then
		echo "build directory safety self-test accepted symlink path" >&2
		return 1
	fi
	trap - EXIT HUP INT TERM
	find "$safety_tmp" -depth -delete
	echo "build directory safety self-test: PASS"
}

if [ "${1:-}" = "--self-test" ]; then
	[ "$#" -eq 1 ] || { echo "--self-test takes no arguments" >&2; exit 2; }
	build_root_self_test
	exit 0
fi
[ "$#" -eq 0 ] || { echo "unexpected argument: $1" >&2; exit 2; }
arch=${LINUX_ARCH:-}
if [ -z "$arch" ]; then
	case "$(uname -m)" in
		x86_64) arch=x86_64 ;;
		aarch64) arch=aarch64 ;;
		*) echo "LINUX_ARCH must be x86_64 or aarch64" >&2; exit 1 ;;
	esac
fi
case "$arch" in
	x86_64|aarch64) ;;
	*) echo "LINUX_ARCH must be x86_64 or aarch64, got: $arch" >&2; exit 1 ;;
esac

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
requested_build_root=${LINUX_BUILD_DIR-$root/build/linux-$arch}
build_root=$(validate_build_root "$requested_build_root")
archive=${LIBUSB_SOURCE_ARCHIVE:-$build_root/libusb-${libusb_version}.tar.bz2}
prefix=$build_root/prefix
src=$build_root/src
pc_wrap=$build_root/pkg-config-libusb

compiler=${CC:-cc}
cxx_compiler=${CXX:-c++}
compiler=$(command -v "$compiler")
cxx_compiler=$(command -v "$cxx_compiler")
compiler=$(readlink -f "$compiler")
cxx_compiler=$(readlink -f "$cxx_compiler")
cmake_bin=$(command -v cmake)
compiler_target=$($compiler -dumpmachine 2>/dev/null || true)
case "$compiler_target" in
	*musl*) ;;
	*) echo "a musl-targeting compiler is required (got: $compiler_target)" >&2; exit 1 ;;
esac
compiler_version=$($compiler -dumpfullversion -dumpversion)
libc_file=$(readlink -f "$($compiler -print-file-name=libc.a)")
libgcc_file=$(readlink -f "$($compiler -print-file-name=libgcc.a)")
libstdcxx_file=$(readlink -f "$($cxx_compiler -print-file-name=libstdc++.a)")
musl_package=$(apk info -v -e musl | head -n 1)
case "$musl_package" in musl-1.2.5-r*) ;; *) echo "musl 1.2.5 is required for the bundled copyright" >&2; exit 1 ;; esac
for input in "$libc_file" "$libgcc_file" "$libstdcxx_file"; do
	[ -f "$input" ] || { echo "missing static runtime input: $input" >&2; exit 1; }
done
libc_owner=$(apk info -W "$libc_file" | awk -F' is owned by ' 'NF == 2 {print $2; exit}')
libgcc_owner=$(apk info -W "$libgcc_file" | awk -F' is owned by ' 'NF == 2 {print $2; exit}')
libstdcxx_owner=$(apk info -W "$libstdcxx_file" | awk -F' is owned by ' 'NF == 2 {print $2; exit}')
[ -n "$libc_owner" ] && [ -n "$libgcc_owner" ] && [ -n "$libstdcxx_owner" ] || {
	echo "static libc/libgcc/libstdc++ must have identified Alpine package owners" >&2; exit 1;
}
strip_bin=${STRIP:-strip}
strip_bin=$(command -v "$strip_bin" 2>/dev/null || true)
if [ -z "$strip_bin" ] || [ ! -x "$strip_bin" ]; then
	echo "target-native strip is required (set STRIP or install binutils)" >&2
	exit 1
fi

provided_archive=
if [ -n "${LIBUSB_SOURCE_ARCHIVE:-}" ]; then
	if [ ! -f "$archive" ]; then
		echo "libusb source archive not found: $archive" >&2
		exit 1
	fi
	provided_archive=$(mktemp)
	cp -f "$archive" "$provided_archive"
	trap 'rm -f "$provided_archive"' EXIT HUP INT TERM
fi

clean_build_root "$build_root"
mkdir -p "$build_root" "$src"
if [ -n "$provided_archive" ]; then
	cp -f "$provided_archive" "$build_root/libusb-${libusb_version}.tar.bz2"
	archive=$build_root/libusb-${libusb_version}.tar.bz2
else
	curl -fsSL --retry 2 -o "$archive" "$libusb_url"
fi
actual=$(sha256sum "$archive" | awk '{print $1}')
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
  --libs|--static) printf '%s ' '${prefix}/lib/libusb-1.0.a -pthread' ;;
  libusb-1.0|--exists|--print-errors|--short-errors|--modversion) ;;
  --atleast-version=*|--exact-version=*|--max-version=*) ;;
  -*) ;;
  *) echo "pkg-config-libusb: unexpected argument: \$arg" >&2; exit 1 ;;
 esac
done
printf '\\n'
EOF
chmod +x "$pc_wrap"

jobs=$(nproc 2>/dev/null || echo 2)
env -i \
	PATH=/usr/bin:/bin \
	HOME="${HOME:-/tmp}" \
	LC_ALL=C \
	CC="${CC:-cc}" \
	AR="${AR:-ar}" \
	RANLIB="${RANLIB:-ranlib}" \
	PKG_CONFIG=/bin/false \
	CFLAGS="-O2 -ffile-prefix-map=$root=. -ffile-prefix-map=$build_root=. -ffile-prefix-map=$libusb_src=." \
	LDFLAGS="-static -Wl,-z,relro,-z,now -Wl,--no-undefined" \
	/bin/sh -c "
		set -eu
		cd '$libusb_src'
		./configure --prefix='$prefix' --disable-shared --enable-static \\
			--disable-udev --disable-examples-build --disable-tests-build \\
			--disable-dependency-tracking
		make -j$jobs
		make install
	"

if [ ! -f "$prefix/lib/libusb-1.0.a" ] || [ -e "$prefix/lib/libusb-1.0.so" ]; then
	echo "static libusb installation is incomplete" >&2
	exit 1
fi

env -i \
	PATH=/usr/bin:/bin \
	HOME="${HOME:-/tmp}" \
	LC_ALL=C \
	CC="${CC:-cc}" \
	CXX="$cxx_compiler" \
	CMAKE="$cmake_bin" \
	DTV_USERLAND_CORE_SOURCE_DIR="${DTV_USERLAND_CORE_SOURCE_DIR:-}" \
	SIANO_PX4_USERLAND_SOURCE_DIR="${SIANO_PX4_USERLAND_SOURCE_DIR:-}" \
	CXXFLAGS="-O2 -ffile-prefix-map=$root=. -ffile-prefix-map=$build_root=. -ffile-prefix-map=$libusb_src=." \
	PKG_CONFIG="$pc_wrap" \
	CFLAGS="-O2 -ffile-prefix-map=$root=. -ffile-prefix-map=$build_root=. -ffile-prefix-map=$libusb_src=." \
	LDFLAGS="-static -Wl,-z,relro,-z,now -Wl,--no-undefined" \
	make -C "$root" clean all RUNTIME_BUILD_DIR="$build_root/runtime"

mkdir -p "$build_root/evidence"
for program in sianod sianoctl siano-ts; do
	cp -f "$root/$program" "$build_root/$program"
	chmod 755 "$build_root/$program"
	"$strip_bin" --strip-unneeded "$build_root/$program"
	"$build_root/$program" --help >/dev/null
done
make -C "$root" clean

cat > "$build_root/evidence/build.properties" <<EOF
target_os=linux
target_arch=$arch
libc=none
build_libc=musl
compiler_path=$compiler
compiler_sha256=$(sha256sum "$compiler" | awk '{print $1}')
compiler_version=$compiler_version
compiler_target=$compiler_target
musl_package=$musl_package
runtime=libgcc
libc_archive=$libc_file
libc_archive_sha256=$(sha256sum "$libc_file" | awk '{print $1}')
libc_archive_owner=$libc_owner
libgcc_archive=$libgcc_file
libgcc_archive_sha256=$(sha256sum "$libgcc_file" | awk '{print $1}')
libgcc_archive_owner=$libgcc_owner
libstdcxx=static
libstdcxx_archive=$libstdcxx_file
libstdcxx_archive_sha256=$(sha256sum "$libstdcxx_file" | awk '{print $1}')
libstdcxx_archive_owner=$libstdcxx_owner
linkage=static
libusb_version=$libusb_version
libusb_source_url=$libusb_url
libusb_source_sha256=$actual
libusb_build_options=--disable-shared --enable-static --disable-udev --disable-examples-build --disable-tests-build
libusb_backend=netlink
EOF

for program in sianod sianoctl siano-ts; do
	"$root/scripts/audit-artifact.sh" --platform "linux-$arch" \
		--binary "$build_root/$program" --source-ref "$source_ref" \
		--evidence-output "$build_root/evidence/$program-binary-audit.json"
done
cp "$build_root/evidence/siano-ts-binary-audit.json" "$build_root/evidence/binary-audit.json"
echo "built $build_root/siano-ts (version $version, libusb $libusb_version, source $source_ref)"
