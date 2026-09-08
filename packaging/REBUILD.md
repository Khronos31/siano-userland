# Rebuild and relink

The binary in this archive was built from the source commit recorded in
`manifest.json`. The corresponding source archive contains that exact Git
tree under `repository/`, the pinned libusb source under `third_party/`, and
this recipe as `BUILD-RELINK.md`.

## Linux static binaries

The Linux release binary is built in Alpine with the pinned libusb 1.0.30
source. libusb is configured with udev disabled, so its Linux netlink backend
is used. `repository/scripts/build-linux-static.sh` is the exact build script.
The Alpine `binutils` package supplies the target-native `strip` and `readelf`
tools required by the release policy:

```sh
apk add --no-cache gcc make pkgconf musl-dev linux-headers python3 binutils curl tar bzip2
cd repository
SOURCE_REF=<source-ref> LINUX_ARCH=x86_64 \
  LIBUSB_SOURCE_ARCHIVE=../third_party/libusb-1.0.30.tar.bz2 \
  scripts/build-linux-static.sh
```

Use `LINUX_ARCH=aarch64` for the aarch64 binary. Verify the archive checksum
before building. The script passes `-static` to the final link and records the
actual libusb source checksum and build options in
`build/<target>/evidence/build.properties`. A release build must have no
`PT_INTERP` and no `DT_NEEDED`; use `repository/scripts/audit-artifact.sh` to
check the result. After the static-link and source-provenance checks, the
script applies target-native `strip --strip-unneeded`, audits the result, and
runs `siano-ts --help` before packaging.

The static Linux executable includes libusb under the LGPL-2.1-or-later. The
corresponding source archive includes the complete application source, the
complete pinned libusb source, `libusb/COPYING` in the binary archive, and this
relink recipe. These materials are provided so libusb can be modified and the
executable relinked as required by LGPL section 6(a).

## Clean relink test

Run the following from an Alpine environment. It extracts the corresponding
source archive, rebuilds the unmodified source, then rebuilds against a
harmlessly changed libusb source. The test requires the changed marker to be
present in the resulting executable, so a successful compiler invocation alone
does not pass the test.

```sh
repository/scripts/test-static-relink.sh \
  --source-archive siano-ts-<version>-source.tar.gz \
  --arch x86_64
```

## Other targets

macOS uses the platform's host `libusb-1.0` shared library. Windows uses the
libusb 1.0.30 WinUSB package identified in `libusb/NOTICE.txt`; the downloaded
7z is verified during the build and is not embedded in the ZIP.

Android uses libusb 1.0.30 statically. The exact source archive is included as
`libusb/libusb-1.0.30.tar.bz2`; verify it against the SHA256 in
`DEPENDENCY-NOTICE.txt`, unpack it, and build with:

```sh
./configure --host=aarch64-linux-android --disable-shared --enable-static \
  --with-pic --disable-udev --disable-examples-build --disable-tests-build
make
```

Use the Android NDK API 24 clang target for the archive's ABI, pass the
resulting `libusb-1.0.a` directly to the final link, and retain `-llog`.
The exact NDK revision is in `evidence/build.properties` and
`DEPENDENCY-NOTICE.txt`. The matching NDK `source.properties`, `NOTICE`, and
`NOTICE.toolchain` are copied byte-for-byte into `evidence/ndk/`. The
static-link inventory in `evidence/static-link-inventory.tsv` is the expected audit record; an
unclassified static archive is a release failure.

The firmware is intentionally not part of a source archive. Obtain the
separately licensed input from the pinned URL and verify its SHA256 before
passing it to `--firmware`. The license text and its pinned URL/SHA256 are
recorded in `LICENCE.siano` and `DEPENDENCY-NOTICE.txt`.

The macOS distribution workflow applies Apple `strip -S -x` followed by
`strip -N` to `siano-ts`,
smoke-tests `siano-ts --help`, and then audits the Mach-O load commands for
DWARF and local symbols.
