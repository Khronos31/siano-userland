#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Create one deterministic siano-ts platform archive from verified inputs."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
from pathlib import PurePosixPath
import shutil
import stat
import sys
import tarfile
import tempfile
import zipfile

from provenance import (
    FIRMWARE_LICENSE_SHA256,
    FIRMWARE_LICENSE_URL,
    FIRMWARE_SHA256,
    FIRMWARE_URL,
    LIBUSB_SOURCE_SHA256,
    LIBUSB_SOURCE_URL,
    LIBUSB_VERSION,
    WINDOWS_LIBUSB_PACKAGE_SHA256,
    WINDOWS_LIBUSB_PACKAGE_URL,
)

_audit_spec = importlib.util.spec_from_file_location("siano_audit", Path(__file__).with_name("audit-artifact.py"))
if _audit_spec is None or _audit_spec.loader is None:
    raise ImportError("cannot load audit-artifact.py")
_audit = importlib.util.module_from_spec(_audit_spec)
_audit_spec.loader.exec_module(_audit)
AuditError = _audit.AuditError
PACKAGE_PLATFORMS = _audit.PACKAGE_PLATFORMS
audit_binary_archive = _audit.audit_binary_archive
fail = _audit.fail
validate_source_ref = _audit.validate_source_ref


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_regular(source: Path, destination: Path, *, mode: int | None = None) -> None:
    if source.is_symlink() or not source.is_file():
        fail(f"package input must be an ordinary file: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    if mode is None:
        shutil.copymode(source, destination)
    else:
        destination.chmod(mode)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    path.chmod(0o644)


def parse_properties(data: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in data.splitlines():
        if not line or "=" not in line:
            fail("Android build.properties is malformed")
        key, value = line.split("=", 1)
        if key in result:
            fail(f"duplicate Android build property: {key}")
        result[key] = value
    return result


def verify_source_archive(path: Path, destination: Path) -> None:
    if path.name != f"libusb-{LIBUSB_VERSION}.tar.bz2" or sha256(path) != LIBUSB_SOURCE_SHA256:
        fail("libusb source is not the pinned libusb 1.0.28 archive")
    copying: bytes | None = None
    try:
        with tarfile.open(path, "r:bz2") as archive:
            names: set[str] = set()
            for member in archive.getmembers():
                name = PurePosixPath(member.name)
                if member.name in names:
                    fail(f"duplicate libusb source member: {member.name}")
                names.add(member.name)
                if member.name != f"libusb-{LIBUSB_VERSION}" and not member.name.startswith(f"libusb-{LIBUSB_VERSION}/"):
                    fail(f"unexpected libusb source member: {member.name}")
                if not member.isfile() and not member.isdir():
                    fail(f"libusb source member is not a regular file: {member.name}")
            member = archive.getmember(f"libusb-{LIBUSB_VERSION}/COPYING")
            stream = archive.extractfile(member)
            if stream is None:
                fail("libusb source has no COPYING")
            copying = stream.read()
    except (OSError, tarfile.TarError, KeyError) as error:
        fail(f"cannot inspect libusb source: {error}")
    try:
        copying_text = copying.decode("utf-8") if copying is not None else ""
    except UnicodeDecodeError as error:
        fail(f"libusb COPYING is not UTF-8: {error}")
    write_text(destination / "COPYING", copying_text)


def write_deterministic_tar(stage: Path, output: Path) -> None:
    with output.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as archive:
                for path in sorted(stage.rglob("*")):
                    if path.is_symlink():
                        fail(f"symlink reached archive writer: {path}")
                    if not path.is_file():
                        continue
                    info = tarfile.TarInfo(path.relative_to(stage).as_posix())
                    info.size = path.stat().st_size
                    info.mode = path.stat().st_mode & 0o7777
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = 0
                    with path.open("rb") as handle:
                        archive.addfile(info, handle)


def write_deterministic_zip(stage: Path, output: Path) -> None:
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(stage.rglob("*")):
            if path.is_symlink():
                fail(f"symlink reached archive writer: {path}")
            if not path.is_file():
                continue
            name = path.relative_to(stage).as_posix()
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | (path.stat().st_mode & 0o7777)) << 16
            with path.open("rb") as handle:
                archive.writestr(info, handle.read())


def write_checksums(stage: Path) -> None:
    rows = []
    for path in sorted(stage.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            rows.append(f"{sha256(path)}  {path.relative_to(stage).as_posix()}")
    write_text(stage / "SHA256SUMS", "\n".join(rows) + "\n")


def dependency_notice(repo_root: Path, version: str, platform: str, source_ref: str,
                      build_properties: str | None = None, ndk_hashes: dict[str, str] | None = None) -> str:
    template = repo_root / "packaging/DEPENDENCY-NOTICE.txt.in"
    if not template.is_file():
        fail(f"missing dependency notice template: {template}")
    fields = [
        f"source.ref={source_ref}",
        f"source.archive=siano-ts-{version}-source.tar.gz",
        f"firmware.url={FIRMWARE_URL}",
        f"firmware.sha256={FIRMWARE_SHA256}",
        f"firmware.license.url={FIRMWARE_LICENSE_URL}",
        f"firmware.license.sha256={FIRMWARE_LICENSE_SHA256}",
    ]
    if platform.startswith("android"):
        properties = parse_properties(build_properties or "")
        if not properties.get("ndk_revision", "").startswith("27."):
            fail("Android package requires NDK r27 evidence")
        if not ndk_hashes or set(ndk_hashes) != {"source_properties", "notice", "notice_toolchain"}:
            fail("Android package requires all NDK notice hashes")
        fields += [
            f"ndk_revision={properties['ndk_revision']}",
            f"ndk.source_properties.sha256={ndk_hashes['source_properties']}",
            f"ndk.notice.sha256={ndk_hashes['notice']}",
            f"ndk.notice_toolchain.sha256={ndk_hashes['notice_toolchain']}",
            f"dependency.libusb.version={LIBUSB_VERSION}",
            "dependency.libusb.linkage=static",
            "dependency.libusb.license=LGPL-2.1-or-later",
            "corresponding-source=libusb/libusb-1.0.28.tar.bz2",
            f"corresponding-source.sha256={LIBUSB_SOURCE_SHA256}",
        ]
        explanation = "Android statically links libusb; see libusb/COPYING, libusb source, REBUILD.md, NDK notices, and evidence/."
    elif platform == "windows-x64":
        fields += [
            f"dependency.libusb.version={LIBUSB_VERSION}",
            "dependency.libusb.linkage=dynamic",
            "dependency.libusb.provider=bundled",
            f"libusb.package.url={WINDOWS_LIBUSB_PACKAGE_URL}",
            f"libusb.package.sha256={WINDOWS_LIBUSB_PACKAGE_SHA256}",
            f"corresponding-source.sha256={LIBUSB_SOURCE_SHA256}",
        ]
        explanation = "Windows bundles libusb-1.0.dll; see libusb/NOTICE.txt and the corresponding source archive."
    else:
        fields += ["dependency.libusb.linkage=dynamic", "dependency.libusb.provider=host"]
        explanation = "Native builds use the host-provided dynamic libusb library."
    body = "\n".join(fields)
    with template.open("r", encoding="utf-8", newline="") as handle:
        text = handle.read()
    return text.replace("@VERSION@", version).replace("@PLATFORM@", platform).replace(
        "@DEPENDENCY_TEXT@", body + "\n\n" + explanation).rstrip("\n") + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", required=True, choices=sorted(PACKAGE_PLATFORMS))
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-ref", required=True)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--libusb-source-archive", type=Path)
    parser.add_argument("--windows-package", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    if not _audit.VERSION_RE.fullmatch(args.version):
        fail("version must be strict N.N.N")
    validate_source_ref(args.source_ref)
    if args.platform.startswith("android") and not args.libusb_source_archive:
        fail("Android package requires --libusb-source-archive")
    if args.platform == "windows-x64" and (not args.windows_package or not args.libusb_source_archive):
        fail("Windows package requires --windows-package and --libusb-source-archive")
    firmware = args.firmware.resolve()
    if not firmware.is_file() or sha256(firmware) != FIRMWARE_SHA256:
        fail("firmware is not the pinned input")
    repo_root = args.repo_root.resolve()
    version_file = repo_root / "VERSION"
    if version_file.read_bytes() != (args.version + "\n").encode("ascii"):
        fail("--version does not match the exact VERSION file")
    license_file = repo_root / "LICENCE.siano"
    if not license_file.is_file() or sha256(license_file) != FIRMWARE_LICENSE_SHA256:
        fail("repository LICENCE.siano does not match the pinned firmware license")
    build = args.build_dir.resolve()
    binary_name = "siano-ts.exe" if args.platform == "windows-x64" else "siano-ts"
    binary = build / binary_name
    if not binary.is_file() or binary.is_symlink():
        fail(f"missing build output: {binary}")
    evidence_path = build / "evidence/binary-audit.json"
    if not evidence_path.is_file() or evidence_path.is_symlink():
        fail(f"missing build-host binary evidence: {evidence_path}")
    evidence = _audit.parse_json(evidence_path.read_bytes(), "binary-audit.json")
    expected_evidence = {"schema": 2, "platform": args.platform, "source_ref": args.source_ref,
                         "binary": {"name": binary_name, "sha256": sha256(binary)}}
    if evidence != expected_evidence:
        fail("build-host binary evidence is not bound to this binary/platform")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    extension = "zip" if args.platform == "windows-x64" else "tar.gz"
    output = args.output_dir.resolve() / f"siano-ts-{args.version}-{args.platform}.{extension}"
    if output.exists() or output.is_symlink():
        fail(f"refusing to overwrite existing archive: {output}")
    with tempfile.TemporaryDirectory(prefix="siano-package-") as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        for name in ("COPYING", "LICENCE.siano", "README.md", "REBUILD.md"):
            source = repo_root / ("packaging/REBUILD.md" if name == "REBUILD.md" else name)
            copy_regular(source, stage / name)
        # Artifact downloads may normalize executable files to 0644.  Tar
        # consumers execute the native and Android binaries directly, so set
        # their canonical mode explicitly.  Windows remains a ZIP package and
        # keeps the existing source-mode behavior for its .exe.
        copy_regular(binary, stage / binary_name,
                     mode=None if args.platform == "windows-x64" else 0o755)
        copy_regular(evidence_path, stage / "evidence/binary-audit.json")
        copy_regular(firmware, stage / "firmware/isdbt_rio.inp")
        source_archive = None
        if args.libusb_source_archive:
            source_archive = args.libusb_source_archive.resolve()
            verify_source_archive(source_archive, stage / "libusb")
            if args.platform.startswith("android"):
                copy_regular(source_archive, stage / "libusb/libusb-1.0.28.tar.bz2")
        build_properties = None
        ndk_hashes = None
        if args.platform.startswith("android"):
            properties = build / "build.properties"
            inventory = build / "static-link-inventory.tsv"
            ndk_dir = build / "ndk"
            copy_regular(inventory, stage / "evidence/static-link-inventory.tsv")
            copy_regular(properties, stage / "evidence/build.properties")
            for name in ("source.properties", "NOTICE", "NOTICE.toolchain"):
                copy_regular(ndk_dir / name, stage / f"evidence/ndk/{name}")
            build_properties = (stage / "evidence/build.properties").read_text(encoding="utf-8")
            ndk_hashes = {key: sha256(stage / f"evidence/ndk/{name}") for key, name in {
                "source_properties": "source.properties", "notice": "NOTICE",
                "notice_toolchain": "NOTICE.toolchain"}.items()}
        if args.platform == "windows-x64":
            package = args.windows_package.resolve()
            if not package.is_file() or sha256(package) != WINDOWS_LIBUSB_PACKAGE_SHA256:
                fail("Windows libusb package is not the pinned 1.0.28 package")
            dll = build / "libusb-1.0.dll"
            _audit.audit_pe_x64(dll)
            copy_regular(dll, stage / "libusb-1.0.dll")
            write_text(stage / "libusb/NOTICE.txt", (
                "Bundled DLL: libusb-1.0.dll\n"
                f"Version: {LIBUSB_VERSION}\n"
                f"Package URL: {WINDOWS_LIBUSB_PACKAGE_URL}\n"
                f"Package SHA256: {WINDOWS_LIBUSB_PACKAGE_SHA256}\n"
                f"Corresponding source URL: {LIBUSB_SOURCE_URL}\n"
                f"Corresponding source SHA256: {LIBUSB_SOURCE_SHA256}\n"
                f"Project source archive: siano-ts-{args.version}-source.tar.gz\n"
                "The downloaded 7z package is verified during the build and is not embedded here.\n"
                "License: LGPL-2.1-or-later; see COPYING.\n"
            ))
        write_text(stage / "DEPENDENCY-NOTICE.txt", dependency_notice(
            repo_root, args.version, args.platform, args.source_ref, build_properties, ndk_hashes))
        manifest = {
            "schema": 1, "version": args.version, "platform": args.platform,
            "source_ref": args.source_ref, "programs": [binary_name], "files": {},
            "firmware": {"url": FIRMWARE_URL, "sha256": FIRMWARE_SHA256,
                         "license_url": FIRMWARE_LICENSE_URL, "license_sha256": FIRMWARE_LICENSE_SHA256},
        }
        if args.platform.startswith("android") or args.platform == "windows-x64":
            manifest["libusb"] = {"version": LIBUSB_VERSION, "source_sha256": LIBUSB_SOURCE_SHA256}
        if args.platform == "windows-x64":
            manifest["libusb"]["package_sha256"] = WINDOWS_LIBUSB_PACKAGE_SHA256
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                data = path.read_bytes()
                manifest["files"][path.relative_to(stage).as_posix()] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        with (stage / "manifest.json").open("w", encoding="utf-8", newline="\n") as handle:
            json.dump(manifest, handle, indent=2, sort_keys=True)
            handle.write("\n")
        write_checksums(stage)
        if args.platform == "windows-x64":
            write_deterministic_zip(stage, output)
        else:
            write_deterministic_tar(stage, output)
    audit_binary_archive(output, args.platform)
    print(output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AuditError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"package-artifact: {error}", file=sys.stderr)
        raise SystemExit(1)
