# Rebuild and relink

The binary in this archive was built from the source commit recorded in
`manifest.json`. The archive is a release artifact, not a substitute for the
corresponding source archive.

Native Linux and macOS builds use the platform's host `libusb-1.0` development
package and therefore relink against the host-provided shared library. Windows
uses the libusb 1.0.28 WinUSB package identified in `libusb/NOTICE.txt`; the
downloaded 7z is verified during the build and is not embedded in the ZIP.

Android uses libusb 1.0.28 statically. The exact source archive is included as
`libusb/libusb-1.0.28.tar.bz2`; verify it against the SHA256 in
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
