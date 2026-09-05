#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fail-closed audits for siano-ts binaries and release archives."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
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

VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SOURCE_REF_RE = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")
PLATFORMS = {
    # The glibc name is an audit-only CI target; it is never a candidate archive.
    "linux-glibc-x86_64": "linux-glibc",
    "linux-x86_64": "linux-musl",
    "darwin-arm64": "darwin",
    "android-aarch64": "android",
    "android-armv7a": "android",
    "windows-x64": "windows",
}
PACKAGE_PLATFORMS = {
    "linux-x86_64", "darwin-arm64", "android-aarch64", "android-armv7a", "windows-x64"
}
COMMON = {
    "COPYING", "LICENCE.siano", "README.md", "REBUILD.md", "DEPENDENCY-NOTICE.txt",
    "firmware/isdbt_rio.inp", "manifest.json", "SHA256SUMS", "evidence/binary-audit.json",
}
SOURCE_REQUIRED = {
    "BUILD-RELINK.md", "DEPENDENCY-NOTICE.txt", "README.md", "COPYING",
    "LICENCE.siano", "source-manifest.json", "SHA256SUMS",
    "third_party/libusb-1.0.28.tar.bz2",
}
SOURCE_FORBIDDEN = re.compile(
    r"(^|/)(?:\.git|build(?:-[^/.]+)?|out|dist|firmware|windows|win32|vendor|drivers?|"
    r"dkms|kernel|apk|addon|add-on)(?:/|$)"
    r"|(?:\.o$|\.a$|\.so(?:\.|$)|\.dylib$|\.apk$|\.ko$|\.sys$|\.inf$|\.dll$|\.exe$|\.bin$)",
    re.IGNORECASE,
)


class AuditError(Exception):
    pass


def fail(message: str) -> None:
    raise AuditError(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_version(version: str) -> None:
    if not VERSION_RE.fullmatch(version):
        fail(f"version must be strict N.N.N, got {version!r}")


def validate_source_ref(source_ref: str) -> None:
    if not SOURCE_REF_RE.fullmatch(source_ref):
        fail(f"source_ref must be a canonical commit ID, got {source_ref!r}")


def safe_member(name: str) -> None:
    if not name or "\\" in name or name.startswith("/"):
        fail(f"unsafe archive member: {name!r}")
    if ".." in PurePosixPath(name).parts:
        fail(f"unsafe archive member: {name!r}")


def archive_payloads(path: Path, modes: dict[str, int] | None = None) -> dict[str, bytes]:
    if not path.is_file() or path.is_symlink():
        fail(f"archive is not an ordinary file: {path}")
    payloads: dict[str, bytes] = {}
    try:
        if path.name.endswith(".zip"):
            with zipfile.ZipFile(path) as archive:
                for info in archive.infolist():
                    safe_member(info.filename)
                    mode = (info.external_attr >> 16) & 0xFFFF
                    if info.is_dir() or stat.S_ISLNK(mode) or (mode and not stat.S_ISREG(mode)):
                        fail(f"archive member is not a regular file: {info.filename}")
                    if info.filename in payloads:
                        fail(f"duplicate archive member: {info.filename}")
                    payloads[info.filename] = archive.read(info)
                    if modes is not None:
                        modes[info.filename] = mode
        else:
            with tarfile.open(path, "r:*") as archive:
                for info in archive.getmembers():
                    safe_member(info.name)
                    if not info.isfile() or info.name in payloads:
                        fail(f"archive member is not a unique regular file: {info.name}")
                    payload = archive.extractfile(info)
                    if payload is None:
                        fail(f"cannot read archive member: {info.name}")
                    payloads[info.name] = payload.read()
                    if modes is not None:
                        modes[info.name] = info.mode
    except (OSError, tarfile.TarError, zipfile.BadZipFile) as error:
        fail(f"cannot read archive {path}: {error}")
    return payloads


def reject_duplicate_json_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_nonfinite(value: str) -> object:
    raise ValueError(f"non-finite JSON constant: {value}")


def parse_json(data: bytes, name: str) -> object:
    try:
        return json.loads(data, object_pairs_hook=reject_duplicate_json_keys,
                          parse_constant=reject_nonfinite)
    except (UnicodeDecodeError, ValueError, json.JSONDecodeError) as error:
        fail(f"invalid {name}: {error}")


def notice_fields(data: bytes) -> dict[str, str]:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        fail(f"dependency notice is not UTF-8: {error}")
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            if re.fullmatch(r"[a-z0-9_.-]+", key):
                if key in fields:
                    fail(f"duplicate dependency notice field: {key}")
                fields[key] = value
    return fields


def verify_checksums(payloads: dict[str, bytes]) -> None:
    if "SHA256SUMS" not in payloads:
        fail("SHA256SUMS is missing")
    try:
        text = payloads["SHA256SUMS"].decode("ascii")
    except UnicodeDecodeError as error:
        fail(f"SHA256SUMS is not ASCII: {error}")
    listed: dict[str, str] = {}
    for line in text.splitlines():
        if not line:
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  ([^\r\n]+)", line)
        if not match:
            fail(f"invalid SHA256SUMS line: {line!r}")
        digest, name = match.groups()
        safe_member(name)
        if name in listed:
            fail(f"duplicate checksum entry: {name}")
        listed[name] = digest
    expected = set(payloads) - {"SHA256SUMS"}
    if set(listed) != expected:
        fail("SHA256SUMS must list exactly every other archive member")
    for name, digest in listed.items():
        if sha256_bytes(payloads[name]) != digest:
            fail(f"checksum mismatch: {name}")


def exact_firmware_fields(fields: dict[str, str]) -> None:
    expected = {
        "firmware.url": FIRMWARE_URL,
        "firmware.sha256": FIRMWARE_SHA256,
        "firmware.license.url": FIRMWARE_LICENSE_URL,
        "firmware.license.sha256": FIRMWARE_LICENSE_SHA256,
    }
    for key, value in expected.items():
        if fields.get(key) != value:
            fail(f"dependency notice field mismatch: {key}")


def verify_manifest(payloads: dict[str, bytes], platform: str) -> dict:
    manifest = parse_json(payloads.get("manifest.json", b""), "manifest.json")
    if not isinstance(manifest, dict) or manifest.get("schema") != 1 or manifest.get("platform") != platform:
        fail("manifest schema/platform mismatch")
    version = manifest.get("version")
    if not isinstance(version, str):
        fail("manifest version is missing")
    validate_version(version)
    if not isinstance(manifest.get("source_ref"), str):
        fail("manifest source_ref is missing")
    validate_source_ref(manifest["source_ref"])
    binary_name = "siano-ts.exe" if platform == "windows-x64" else "siano-ts"
    if manifest.get("programs") != [binary_name]:
        fail("manifest program list is wrong")
    inventory = manifest.get("files")
    if not isinstance(inventory, dict):
        fail("manifest files inventory is missing")
    expected = set(payloads) - {"manifest.json", "SHA256SUMS"}
    if set(inventory) != expected:
        fail("manifest files inventory does not match archived payload")
    for name in sorted(expected):
        record = inventory[name]
        if (not isinstance(record, dict) or set(record) != {"size", "sha256"} or
                not isinstance(record["size"], int) or isinstance(record["size"], bool) or
                record["size"] < 0 or not isinstance(record["sha256"], str) or
                not re.fullmatch(r"[0-9a-f]{64}", record["sha256"])):
            fail(f"invalid manifest record: {name}")
        if record["size"] != len(payloads[name]) or record["sha256"] != sha256_bytes(payloads[name]):
            fail(f"manifest record mismatch: {name}")
    firmware = manifest.get("firmware")
    expected_firmware = {"url": FIRMWARE_URL, "sha256": FIRMWARE_SHA256,
                         "license_url": FIRMWARE_LICENSE_URL,
                         "license_sha256": FIRMWARE_LICENSE_SHA256}
    if firmware != expected_firmware:
        fail("manifest firmware provenance mismatch")
    if platform.startswith("android") or platform == "windows-x64":
        libusb = manifest.get("libusb")
        if not isinstance(libusb, dict) or libusb.get("version") != LIBUSB_VERSION or \
                libusb.get("source_sha256") != LIBUSB_SOURCE_SHA256:
            fail("manifest libusb provenance mismatch")
        if platform == "windows-x64" and libusb.get("package_sha256") != WINDOWS_LIBUSB_PACKAGE_SHA256:
            fail("manifest Windows libusb package provenance mismatch")
    return manifest


def expected_members(platform: str) -> set[str]:
    if platform not in PACKAGE_PLATFORMS:
        fail(f"platform is not packageable: {platform}")
    result = set(COMMON) | ({"siano-ts.exe"} if platform == "windows-x64" else {"siano-ts"})
    if platform.startswith("android"):
        result |= {
            "libusb/COPYING", "libusb/libusb-1.0.28.tar.bz2",
            "evidence/static-link-inventory.tsv", "evidence/build.properties",
            "evidence/ndk/source.properties", "evidence/ndk/NOTICE",
            "evidence/ndk/NOTICE.toolchain",
        }
    elif platform == "windows-x64":
        result |= {
            "libusb/COPYING", "libusb/NOTICE.txt", "libusb-1.0.dll",
        }
    return result


def verify_binary_evidence(payloads: dict[str, bytes], manifest: dict) -> None:
    evidence = parse_json(payloads.get("evidence/binary-audit.json", b""), "binary-audit.json")
    platform = manifest["platform"]
    binary_name = "siano-ts.exe" if platform == "windows-x64" else "siano-ts"
    expected = {"schema": 2, "platform": platform, "source_ref": manifest["source_ref"],
                "binary": {"name": binary_name, "sha256": sha256_bytes(payloads[binary_name])}}
    if evidence != expected:
        fail("binary audit evidence is not bound to the archived binary/platform")


def verify_binary_archive_mode(modes: dict[str, int], binary_name: str, platform: str) -> None:
    if platform == "windows-x64":
        return
    mode = modes.get(binary_name)
    if mode is None or not mode & 0o111:
        fail(f"{platform} archive binary is not executable: {binary_name}")


def verify_android_provenance(payloads: dict[str, bytes], fields: dict[str, str], platform: str) -> None:
    inventory = payloads["evidence/static-link-inventory.tsv"].decode("utf-8")
    rows = inventory.splitlines()
    if not rows or rows[0] != "category\tarchive\tmember":
        fail("Android static-link inventory header is invalid")
    seen: set[tuple[str, str, str]] = set()
    for row in rows[1:]:
        parts = row.split("\t")
        if len(parts) != 3 or tuple(parts) in seen:
            fail("Android static-link inventory is malformed or duplicated")
        seen.add(tuple(parts))
        if parts[0] not in {"libusb", "ndk-runtime"} or not parts[1].endswith(".a") or not parts[2]:
            fail("Android static-link inventory contains an unclassified archive")
    if not any(row[0] == "libusb" and row[1] == "libusb-1.0.a" for row in seen):
        fail("Android static-link inventory has no libusb archive")
    properties = payloads["evidence/build.properties"].decode("utf-8")
    property_map: dict[str, str] = {}
    for line in properties.splitlines():
        if "=" not in line:
            fail("Android build.properties is malformed")
        key, value = line.split("=", 1)
        if key in property_map:
            fail(f"duplicate Android property: {key}")
        property_map[key] = value
    expected_abi = "aarch64" if platform == "android-aarch64" else "armv7a"
    if property_map.get("android_api") != "24" or property_map.get("android_abi") != expected_abi or \
            not property_map.get("ndk_revision", "").startswith("27.") or \
            property_map.get("libusb_version") != LIBUSB_VERSION or \
            property_map.get("libusb_source_url") != LIBUSB_SOURCE_URL or \
            property_map.get("libusb_source_sha256") != LIBUSB_SOURCE_SHA256:
        fail("Android build properties do not record the required ABI/API/NDK/libusb provenance")
    if fields.get("ndk_revision") != property_map["ndk_revision"]:
        fail("Android notice NDK revision mismatch")
    for name, field, property_name in (
            ("evidence/ndk/source.properties", "ndk.source_properties.sha256", "ndk_source_properties_sha256"),
            ("evidence/ndk/NOTICE", "ndk.notice.sha256", "ndk_notice_sha256"),
            ("evidence/ndk/NOTICE.toolchain", "ndk.notice_toolchain.sha256", "ndk_notice_toolchain_sha256")):
        digest = sha256_bytes(payloads[name])
        if fields.get(field) != digest or property_map.get(property_name) != digest:
            fail(f"Android NDK provenance mismatch: {name}")
    source_properties = payloads["evidence/ndk/source.properties"].decode("utf-8")
    if f"Pkg.Revision = {property_map['ndk_revision']}" not in source_properties:
        fail("Android NDK source.properties does not match build.properties")


def audit_binary_archive(path: Path, platform: str) -> dict:
    modes: dict[str, int] = {}
    payloads = archive_payloads(path, modes)
    expected = expected_members(platform)
    if set(payloads) != expected:
        fail(f"archive member allowlist mismatch; unexpected={sorted(set(payloads)-expected)}, missing={sorted(expected-set(payloads))}")
    manifest = verify_manifest(payloads, platform)
    expected_name = f"siano-ts-{manifest['version']}-{platform}.{'zip' if platform == 'windows-x64' else 'tar.gz'}"
    if path.name != expected_name:
        fail(f"archive name does not match platform/version: {path.name}")
    binary_name = "siano-ts.exe" if platform == "windows-x64" else "siano-ts"
    verify_binary_archive_mode(modes, binary_name, platform)
    if sha256_bytes(payloads["firmware/isdbt_rio.inp"]) != FIRMWARE_SHA256:
        fail("firmware SHA256 does not match the pinned input")
    if sha256_bytes(payloads["LICENCE.siano"]) != FIRMWARE_LICENSE_SHA256:
        fail("Siano firmware license does not match the pinned input")
    fields = notice_fields(payloads["DEPENDENCY-NOTICE.txt"])
    exact_firmware_fields(fields)
    if fields.get("source.archive") != f"siano-ts-{manifest['version']}-source.tar.gz":
        fail("dependency notice does not point to the corresponding project source archive")
    if platform.startswith("android"):
        expected_fields = {
            "dependency.libusb.version": LIBUSB_VERSION,
            "dependency.libusb.linkage": "static",
            "dependency.libusb.license": "LGPL-2.1-or-later",
            "corresponding-source": "libusb/libusb-1.0.28.tar.bz2",
            "corresponding-source.sha256": LIBUSB_SOURCE_SHA256,
        }
        for key, value in expected_fields.items():
            if fields.get(key) != value:
                fail(f"dependency notice field mismatch: {key}")
        if sha256_bytes(payloads["libusb/libusb-1.0.28.tar.bz2"]) != LIBUSB_SOURCE_SHA256:
            fail("Android corresponding libusb source checksum mismatch")
        verify_android_provenance(payloads, fields, platform)
    elif platform == "windows-x64":
        expected_fields = {
            "dependency.libusb.version": LIBUSB_VERSION,
            "dependency.libusb.linkage": "dynamic",
            "dependency.libusb.provider": "bundled",
            "libusb.package.url": WINDOWS_LIBUSB_PACKAGE_URL,
            "libusb.package.sha256": WINDOWS_LIBUSB_PACKAGE_SHA256,
            "corresponding-source.sha256": LIBUSB_SOURCE_SHA256,
        }
        for key, value in expected_fields.items():
            if fields.get(key) != value:
                fail(f"dependency notice field mismatch: {key}")
        if "libusb-1.0.dll" not in payloads["libusb/NOTICE.txt"].decode("utf-8").lower():
            fail("Windows libusb notice does not identify the bundled DLL")
        if f"siano-ts-{manifest['version']}-source.tar.gz" not in payloads["libusb/NOTICE.txt"].decode("utf-8"):
            fail("Windows libusb notice does not point to the corresponding project source archive")
        audit_pe_x64_bytes(payloads[binary_name], binary_name)
        audit_pe_x64_bytes(payloads["libusb-1.0.dll"], "libusb-1.0.dll")
    else:
        for key, value in {"dependency.libusb.linkage": "dynamic", "dependency.libusb.provider": "host"}.items():
            if fields.get(key) != value:
                fail(f"dependency notice field mismatch: {key}")
    verify_binary_evidence(payloads, manifest)
    verify_checksums(payloads)
    return {"archive": str(path.resolve()), "platform": platform, "members": sorted(payloads), "manifest": manifest}


def run(command: list[str], *, env: dict[str, str] | None = None) -> str:
    try:
        result = subprocess.run(command, check=True, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, env=env)
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"command failed: {' '.join(command)}: {getattr(error, 'stdout', '')}")
    return result.stdout


def audit_pe_x64_bytes(data: bytes, label: str) -> None:
    if len(data) < 0x40 or data[:2] != b"MZ":
        fail(f"Windows binary is not an MZ executable: {label}")
    pe_offset = int.from_bytes(data[0x3c:0x40], "little")
    if pe_offset < 0 or pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        fail(f"Windows binary has no PE signature: {label}")
    machine = int.from_bytes(data[pe_offset + 4:pe_offset + 6], "little")
    optional_magic = int.from_bytes(data[pe_offset + 24:pe_offset + 26], "little")
    if machine != 0x8664 or optional_magic != 0x20B:
        fail(f"Windows binary is not PE32+ x64: {label}")


def audit_pe_x64(path: Path) -> None:
    audit_pe_x64_bytes(path.read_bytes(), str(path))


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")


def audit_binary(path: Path, platform: str, source_ref: str, repo_root: Path,
                 evidence_output: Path | None = None) -> dict:
    if platform not in PLATFORMS:
        fail(f"unsupported platform: {platform}")
    validate_source_ref(source_ref)
    if not path.is_file() or path.is_symlink():
        fail(f"missing binary: {path}")
    if platform.startswith("android"):
        expected = "/system/bin/linker64" if platform.endswith("aarch64") else "/system/bin/linker"
        env = os.environ.copy()
        env["ANDROID_PATH_MARKERS"] = str(path.parent.resolve())
        run([str(repo_root / "scripts/verify-android-elf.sh"), str(path), expected], env=env)
    elif platform.startswith("linux"):
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        if not readelf:
            fail("readelf or llvm-readelf is required")
        header = run([readelf, "-h", str(path)])
        dynamic = run([readelf, "-d", str(path)])
        if "ELF" not in header or "libusb-1.0.so.0" not in dynamic:
            fail(f"Linux binary is not a dynamic libusb ELF: {path}")
        if not re.search(r"Class:\s+ELF64", header) or not re.search(
                r"Machine:\s+(?:Advanced Micro Devices X86-64|AMD x86-64)", header):
            fail(f"Linux binary is not x86-64 ELF: {path}")
        if platform == "linux-x86_64" and "/lib/ld-musl-x86_64.so.1" not in run([readelf, "-lW", str(path)]):
            fail(f"Linux musl interpreter missing: {path}")
        if platform == "linux-glibc-x86_64" and "libc.so.6" not in dynamic:
            fail(f"Linux glibc dependency missing: {path}")
    elif platform == "darwin-arm64":
        otool = shutil.which("otool")
        if not otool or "libusb-1.0" not in run([otool, "-L", str(path)]):
            fail(f"macOS dynamic libusb dependency missing: {path}")
        lipo = shutil.which("lipo")
        if not lipo or run([lipo, "-archs", str(path)]).split() != ["arm64"]:
            fail(f"macOS binary is not arm64-only: {path}")
    else:
        audit_pe_x64(path)
    evidence = {"schema": 2, "platform": platform, "source_ref": source_ref,
                "binary": {"name": path.name, "sha256": sha256_file(path)}}
    if evidence_output:
        write_json(evidence_output, evidence)
    return evidence


def audit_source_archive(path: Path) -> dict:
    payloads = archive_payloads(path)
    if not SOURCE_REQUIRED.issubset(payloads):
        fail(f"source archive is missing: {sorted(SOURCE_REQUIRED-set(payloads))}")
    for name in payloads:
        if SOURCE_FORBIDDEN.search(name):
            fail(f"forbidden source archive member: {name}")
        if not name.startswith(("repository/", "third_party/", "BUILD-RELINK.md", "DEPENDENCY-NOTICE.txt",
                                "README.md", "COPYING", "LICENCE.siano", "source-manifest.json", "SHA256SUMS")):
            fail(f"unexpected source archive member: {name}")
    if sha256_bytes(payloads["third_party/libusb-1.0.28.tar.bz2"]) != LIBUSB_SOURCE_SHA256:
        fail("source archive libusb checksum mismatch")
    manifest = parse_json(payloads["source-manifest.json"], "source-manifest.json")
    expected_provenance = {
        "schema": 1, "libusb_version": LIBUSB_VERSION,
        "libusb_source_sha256": LIBUSB_SOURCE_SHA256,
        "firmware_url": FIRMWARE_URL, "firmware_sha256": FIRMWARE_SHA256,
        "firmware_license_url": FIRMWARE_LICENSE_URL,
        "firmware_license_sha256": FIRMWARE_LICENSE_SHA256,
    }
    if not isinstance(manifest, dict) or any(manifest.get(key) != value for key, value in expected_provenance.items()):
        fail("invalid source archive provenance manifest")
    if not isinstance(manifest.get("version"), str) or not isinstance(manifest.get("source_ref"), str) or \
            not isinstance(manifest.get("resolved_commit"), str) or not isinstance(manifest.get("tree"), str):
        fail("source archive commit/tree provenance is missing")
    validate_version(manifest["version"])
    if not manifest["source_ref"] or not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", manifest["resolved_commit"]) or \
            not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", manifest["tree"]):
        fail("source archive commit/tree IDs are not canonical lowercase object IDs")
    if payloads.get("repository/VERSION") != (manifest["version"] + "\n").encode("ascii"):
        fail("repository/VERSION does not match the source manifest version")
    files = manifest.get("files")
    expected = set(payloads) - {"source-manifest.json", "SHA256SUMS"}
    if not isinstance(files, dict) or set(files) != expected:
        fail("source manifest does not match archived payload")
    for name, record in files.items():
        if not isinstance(record, dict) or set(record) != {"size", "sha256"} or \
                record.get("size") != len(payloads[name]) or record.get("sha256") != sha256_bytes(payloads[name]):
            fail(f"source manifest mismatch: {name}")
    fields = notice_fields(payloads["DEPENDENCY-NOTICE.txt"])
    for key, value in {
        "source.ref": manifest["source_ref"],
        "source.resolved_commit": manifest["resolved_commit"],
        "source.tree": manifest["tree"],
        "corresponding-source": "third_party/libusb-1.0.28.tar.bz2",
        "corresponding-source.sha256": LIBUSB_SOURCE_SHA256,
        "libusb.source.url": LIBUSB_SOURCE_URL,
        "firmware.url": FIRMWARE_URL, "firmware.sha256": FIRMWARE_SHA256,
        "firmware.license.url": FIRMWARE_LICENSE_URL,
        "firmware.license.sha256": FIRMWARE_LICENSE_SHA256,
    }.items():
        if fields.get(key) != value:
            fail(f"source dependency notice field mismatch: {key}")
    if sha256_bytes(payloads["LICENCE.siano"]) != FIRMWARE_LICENSE_SHA256:
        fail("source archive Siano license checksum mismatch")
    verify_checksums(payloads)
    return {"archive": str(path.resolve()), "kind": "source", "members": sorted(payloads), "manifest": manifest}


def self_test() -> None:
    safe_member("ok/name")
    for bad in ("../escape", "/absolute", "a\\b"):
        try:
            safe_member(bad)
        except AuditError:
            pass
        else:
            fail(f"unsafe member self-test did not fail: {bad}")
    with tempfile.TemporaryDirectory(prefix="siano-audit-test-") as temporary:
        root = Path(temporary)
        duplicate = root / "duplicate.tar"
        with tarfile.open(duplicate, "w") as archive:
            for _ in range(2):
                info = tarfile.TarInfo("same")
                info.size = 1
                archive.addfile(info, __import__("io").BytesIO(b"x"))
        try:
            archive_payloads(duplicate)
        except AuditError:
            pass
        else:
            fail("duplicate member self-test did not fail")
        link = root / "link.tar"
        with tarfile.open(link, "w") as archive:
            info = tarfile.TarInfo("link")
            info.type = tarfile.SYMTYPE
            info.linkname = "target"
            archive.addfile(info)
        try:
            archive_payloads(link)
        except AuditError:
            pass
        else:
            fail("non-regular member self-test did not fail")
        mode_archive = root / "mode.tar"
        with tarfile.open(mode_archive, "w") as archive:
            info = tarfile.TarInfo("siano-ts")
            info.mode = 0o644
            info.size = 1
            archive.addfile(info, __import__("io").BytesIO(b"x"))
        modes: dict[str, int] = {}
        archive_payloads(mode_archive, modes)
        for platform in sorted(PACKAGE_PLATFORMS - {"windows-x64"}):
            try:
                verify_binary_archive_mode(modes, "siano-ts", platform)
            except AuditError:
                pass
            else:
                fail(f"non-executable binary archive self-test did not fail: {platform}")
            verify_binary_archive_mode({"siano-ts": 0o755}, "siano-ts", platform)
        verify_binary_archive_mode({"siano-ts.exe": 0o644}, "siano-ts.exe", "windows-x64")
    try:
        parse_json(b'{"a":1,"a":2}', "duplicate-json")
    except AuditError:
        pass
    else:
        fail("duplicate JSON key self-test did not fail")
    try:
        parse_json(b'{"a":NaN}', "nonfinite-json")
    except AuditError:
        pass
    else:
        fail("non-finite JSON self-test did not fail")

    binary_payloads = {"siano-ts": b"binary"}
    binary_manifest = {"platform": "linux-x86_64", "source_ref": "a" * 40}
    binary_payloads["evidence/binary-audit.json"] = json.dumps(
        {"schema": 2, "platform": "linux-x86_64", "source_ref": "a" * 40,
         "binary": {"name": "siano-ts", "sha256": sha256_bytes(b"binary")}}
    ).encode("utf-8")
    verify_binary_evidence(binary_payloads, binary_manifest)
    binary_payloads["evidence/binary-audit.json"] = binary_payloads["evidence/binary-audit.json"].replace(
        b"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", b"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
    try:
        verify_binary_evidence(binary_payloads, binary_manifest)
    except AuditError:
        pass
    else:
        fail("mismatched source_ref evidence self-test did not fail")
    binary_payloads["evidence/binary-audit.json"] = json.dumps(
        {"schema": 2, "platform": "linux-x86_64", "source_ref": "a" * 40,
         "binary": {"name": "siano-ts", "sha256": sha256_bytes(b"binary")}}
    ).encode("utf-8")
    binary_payloads["evidence/binary-audit.json"] = binary_payloads["evidence/binary-audit.json"].replace(b"binary", b"tampered")
    try:
        verify_binary_evidence(binary_payloads, binary_manifest)
    except AuditError:
        pass
    else:
        fail("tampered binary evidence self-test did not fail")

    ndk_payloads = {
        "evidence/static-link-inventory.tsv": b"category\tarchive\tmember\nlibusb\tlibusb-1.0.a\tx.o\n",
        "evidence/build.properties": (
            b"android_abi=aarch64\nandroid_api=24\nndk_revision=27.3.13750724\n"
            b"libusb_version=1.0.28\nlibusb_source_url=" + LIBUSB_SOURCE_URL.encode() + b"\n"
            b"libusb_source_sha256=" + LIBUSB_SOURCE_SHA256.encode() + b"\n"
        ),
        "evidence/ndk/source.properties": b"Pkg.Revision = 27.3.13750724\n",
        "evidence/ndk/NOTICE": b"ndk notice\n",
        "evidence/ndk/NOTICE.toolchain": b"exact upstream toolchain notice\n",
    }
    ndk_fields = {
        "ndk_revision": "27.3.13750724",
        "ndk.source_properties.sha256": sha256_bytes(ndk_payloads["evidence/ndk/source.properties"]),
        "ndk.notice.sha256": sha256_bytes(ndk_payloads["evidence/ndk/NOTICE"]),
        "ndk.notice_toolchain.sha256": sha256_bytes(ndk_payloads["evidence/ndk/NOTICE.toolchain"]),
    }
    property_suffix = (
        b"ndk_source_properties_sha256=" + ndk_fields["ndk.source_properties.sha256"].encode() + b"\n"
        b"ndk_notice_sha256=" + ndk_fields["ndk.notice.sha256"].encode() + b"\n"
        b"ndk_notice_toolchain_sha256=" + ndk_fields["ndk.notice_toolchain.sha256"].encode() + b"\n"
    )
    ndk_payloads["evidence/build.properties"] += property_suffix
    verify_android_provenance(ndk_payloads, ndk_fields, "android-aarch64")
    ndk_fields["ndk_revision"] = "26.3.11579264"
    try:
        verify_android_provenance(ndk_payloads, ndk_fields, "android-aarch64")
    except AuditError:
        pass
    else:
        fail("Android NDK provenance mismatch self-test did not fail")
    print("archive safety/provenance self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--source-archive", action="store_true")
    parser.add_argument("--platform", choices=sorted(PLATFORMS))
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--source-ref")
    parser.add_argument("--evidence-output", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.source_archive:
        if not args.archive:
            parser.error("--source-archive requires --archive")
        result = audit_source_archive(args.archive.resolve())
    elif args.archive:
        if not args.platform:
            parser.error("--archive requires --platform")
        result = audit_binary_archive(args.archive.resolve(), args.platform)
    elif args.binary:
        if not args.platform:
            parser.error("--binary requires --platform")
        if not args.source_ref:
            parser.error("--binary requires --source-ref")
        result = audit_binary(args.binary.resolve(), args.platform, args.source_ref, args.repo_root.resolve(),
                              args.evidence_output.resolve() if args.evidence_output else None)
    else:
        parser.error("provide --archive, --source-archive, or --binary")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AuditError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"audit-artifact: {error}", file=sys.stderr)
        raise SystemExit(1)
