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
import struct
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
    "linux-x86_64": "linux-musl",
    "linux-aarch64": "linux-musl",
    "darwin-arm64": "darwin",
    "android-aarch64": "android",
    "android-armv7a": "android",
    "android-x86_64": "android",
    "windows-x64": "windows",
}
PACKAGE_PLATFORMS = {
    "linux-x86_64", "linux-aarch64", "darwin-arm64", "android-aarch64",
    "android-armv7a", "android-x86_64", "windows-x64"
}
ANDROID_ABI_METADATA = {
    "android-aarch64": {
        "abi": "aarch64",
        "elf_class": "ELF64",
        "machine": r"AArch64|AARCH64|ARM aarch64",
        "interpreter": "/system/bin/linker64",
    },
    "android-armv7a": {
        "abi": "armv7a",
        "elf_class": "ELF32",
        "machine": r"ARM|Arm",
        "interpreter": "/system/bin/linker",
    },
    "android-x86_64": {
        "abi": "x86_64",
        "elf_class": "ELF64",
        "machine": r"Advanced Micro Devices X86-64|AMD x86-64|X86-64|x86-64",
        "interpreter": "/system/bin/linker64",
    },
}
COMMON = {
    "COPYING", "LICENCE.siano", "README.md", "REBUILD.md", "DEPENDENCY-NOTICE.txt",
    "firmware/isdbt_rio.inp", "manifest.json", "SHA256SUMS", "evidence/binary-audit.json",
}
LINUX_MDEV = {
    "mdev/siano-ts-mdev.conf", "mdev/siano-ts-mdev.sh", "mdev/siano-ts-mdev.start",
}
LINUX_ARCHITECTURES = {"linux-x86_64": "x86_64", "linux-aarch64": "aarch64"}
SOURCE_REQUIRED = {
    "BUILD-RELINK.md", "DEPENDENCY-NOTICE.txt", "README.md", "COPYING",
    "LICENCE.siano", "source-manifest.json", "SHA256SUMS",
    "third_party/libusb-1.0.30.tar.bz2",
    "repository/Makefile",
    "repository/scripts/build-linux-static.sh",
    "repository/scripts/test-static-relink.sh",
    "repository/packaging/REBUILD.md",
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
    if platform in LINUX_ARCHITECTURES:
        expected_metadata = {
            "architecture": LINUX_ARCHITECTURES[platform],
            "libc": "none",
            "build_libc": "musl",
            "linkage": "static",
        }
        for key, value in expected_metadata.items():
            if manifest.get(key) != value:
                fail(f"Linux static manifest metadata mismatch: {key}")
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
    if platform in LINUX_ARCHITECTURES:
        libusb = manifest.get("libusb")
        expected_libusb = {
            "version": LIBUSB_VERSION,
            "source_ref": LIBUSB_SOURCE_URL,
            "source_sha256": LIBUSB_SOURCE_SHA256,
            "linkage": "static",
            "udev": "disabled",
            "backend": "netlink",
        }
        if libusb != expected_libusb:
            fail("manifest Linux libusb provenance/linkage mismatch")
    elif platform.startswith("android") or platform == "windows-x64":
        expected_metadata = {
            "architecture": ANDROID_ABI_METADATA[platform]["abi"]
            if platform.startswith("android") else "x86_64",
            "linkage": "static" if platform.startswith("android") else "dynamic",
            "libc": "bionic" if platform.startswith("android") else "windows",
        }
        for key, value in expected_metadata.items():
            if manifest.get(key) != value:
                fail(f"manifest metadata mismatch: {key}")
        libusb = manifest.get("libusb")
        if not isinstance(libusb, dict) or libusb.get("version") != LIBUSB_VERSION or \
                libusb.get("source_ref") != LIBUSB_SOURCE_URL or \
                libusb.get("source_sha256") != LIBUSB_SOURCE_SHA256 or \
                libusb.get("linkage") != expected_metadata["linkage"]:
            fail("manifest libusb provenance mismatch")
        if platform == "windows-x64" and libusb.get("package_sha256") != WINDOWS_LIBUSB_PACKAGE_SHA256:
            fail("manifest Windows libusb package provenance mismatch")
    return manifest


def expected_members(platform: str) -> set[str]:
    if platform not in PACKAGE_PLATFORMS:
        fail(f"platform is not packageable: {platform}")
    result = set(COMMON) | ({"siano-ts.exe"} if platform == "windows-x64" else {"siano-ts"})
    if platform in LINUX_ARCHITECTURES:
        result |= {"libusb/COPYING", "evidence/build.properties"} | LINUX_MDEV
    elif platform.startswith("android"):
        result |= {
            "libusb/COPYING", f"libusb/libusb-{LIBUSB_VERSION}.tar.bz2",
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


def verify_linux_mdev_modes(modes: dict[str, int], platform: str) -> None:
    if platform not in LINUX_ARCHITECTURES:
        return
    expected_modes = {
        "mdev/siano-ts-mdev.conf": 0o644,
        "mdev/siano-ts-mdev.sh": 0o755,
        "mdev/siano-ts-mdev.start": 0o755,
    }
    for name, expected in expected_modes.items():
        mode = modes.get(name)
        if mode != expected:
            fail(f"{platform} mdev file has mode {mode!r}, expected {expected:o}: {name}")


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
    metadata = ANDROID_ABI_METADATA.get(platform)
    if metadata is None:
        fail(f"unsupported Android platform: {platform}")
    expected_abi = metadata["abi"]
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
    verify_linux_mdev_modes(modes, platform)
    if platform in LINUX_ARCHITECTURES or platform.startswith("android"):
        with tempfile.TemporaryDirectory(prefix="siano-archive-audit-") as temporary:
            binary = Path(temporary) / binary_name
            binary.write_bytes(payloads[binary_name])
            if platform in LINUX_ARCHITECTURES:
                audit_linux_static_binary(binary, platform)
            else:
                audit_android_binary(binary, platform, Path(__file__).resolve().parents[1])
    if sha256_bytes(payloads["firmware/isdbt_rio.inp"]) != FIRMWARE_SHA256:
        fail("firmware SHA256 does not match the pinned input")
    if sha256_bytes(payloads["LICENCE.siano"]) != FIRMWARE_LICENSE_SHA256:
        fail("Siano firmware license does not match the pinned input")
    fields = notice_fields(payloads["DEPENDENCY-NOTICE.txt"])
    exact_firmware_fields(fields)
    if fields.get("source.archive") != f"siano-ts-{manifest['version']}-source.tar.gz":
        fail("dependency notice does not point to the corresponding project source archive")
    if platform in LINUX_ARCHITECTURES:
        expected_fields = {
            "dependency.libusb.version": LIBUSB_VERSION,
            "dependency.libusb.linkage": "static",
            "dependency.libusb.license": "LGPL-2.1-or-later",
            "dependency.libusb.backend": "netlink",
            "dependency.libusb.udev": "disabled",
            "corresponding-source": f"siano-ts-{manifest['version']}-source.tar.gz",
            "corresponding-source.path": f"third_party/libusb-{LIBUSB_VERSION}.tar.bz2",
            "libusb.source.url": LIBUSB_SOURCE_URL,
            "libusb.source.sha256": LIBUSB_SOURCE_SHA256,
        }
        for key, value in expected_fields.items():
            if fields.get(key) != value:
                fail(f"dependency notice field mismatch: {key}")
        properties = payloads["evidence/build.properties"].decode("utf-8")
        required_properties = {
            "target_os": "linux", "target_arch": LINUX_ARCHITECTURES[platform],
            "libc": "none", "build_libc": "musl", "linkage": "static", "libusb_version": LIBUSB_VERSION,
            "libusb_source_url": LIBUSB_SOURCE_URL, "libusb_source_sha256": LIBUSB_SOURCE_SHA256,
            "libusb_backend": "netlink",
        }
        property_map = {}
        for line in properties.splitlines():
            if "=" not in line:
                fail("Linux build.properties is malformed")
            key, value = line.split("=", 1)
            if key in property_map:
                fail(f"duplicate Linux build property: {key}")
            property_map[key] = value
        for key, value in required_properties.items():
            if property_map.get(key) != value:
                fail(f"Linux build property mismatch: {key}")
    elif platform.startswith("android"):
        expected_fields = {
            "dependency.libusb.version": LIBUSB_VERSION,
            "dependency.libusb.linkage": "static",
            "dependency.libusb.license": "LGPL-2.1-or-later",
            "corresponding-source": f"libusb/libusb-{LIBUSB_VERSION}.tar.bz2",
            "corresponding-source.sha256": LIBUSB_SOURCE_SHA256,
        }
        for key, value in expected_fields.items():
            if fields.get(key) != value:
                fail(f"dependency notice field mismatch: {key}")
        if sha256_bytes(payloads[f"libusb/libusb-{LIBUSB_VERSION}.tar.bz2"]) != LIBUSB_SOURCE_SHA256:
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
        for name in payloads:
            if name.lower().endswith(".pdb"):
                fail(f"Windows package contains a bundled PDB: {name}")
        audit_pe_x64_bytes(payloads[binary_name], binary_name, reject_debug_payload=True)
        # The pinned upstream DLL is intentionally not rewritten or subjected
        # to the project EXE's CodeView policy.
        audit_pe_x64_bytes(payloads["libusb-1.0.dll"], "libusb-1.0.dll")
    else:
        if manifest.get("architecture") != "arm64" or manifest.get("linkage") != "dynamic" or \
                manifest.get("libc") != "darwin":
            fail("macOS manifest metadata mismatch")
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


def _pe_rva_to_file_offset(data: bytes, rva: int, sections: list[tuple[int, int, int, int]],
                           label: str) -> int:
    for virtual_address, virtual_size, raw_size, raw_offset in sections:
        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + span:
            offset = raw_offset + rva - virtual_address
            if offset < 0 or offset > len(data):
                break
            return offset
    fail(f"Windows PE RVA is not backed by file data: {label}")


def _pe_debug_types(data: bytes, pe_offset: int, optional_size: int, label: str) -> tuple[set[int], list[str]]:
    optional = pe_offset + 24
    if optional_size < 112 or optional + optional_size > len(data):
        fail(f"Windows PE optional header is truncated: {label}")
    number_of_directories = int.from_bytes(data[optional + 108:optional + 112], "little")
    coff = pe_offset + 4
    section_count = int.from_bytes(data[coff + 2:coff + 4], "little")
    section_table = optional + optional_size
    section_end = section_table + section_count * 40
    if section_end > len(data):
        fail(f"Windows PE section table is truncated: {label}")
    sections: list[tuple[int, int, int, int]] = []
    section_names: list[str] = []
    for index in range(section_count):
        section = section_table + index * 40
        raw_name = data[section:section + 8].split(b"\0", 1)[0]
        section_names.append(raw_name.decode("ascii", errors="replace"))
        sections.append((
            int.from_bytes(data[section + 12:section + 16], "little"),
            int.from_bytes(data[section + 8:section + 12], "little"),
            int.from_bytes(data[section + 16:section + 20], "little"),
            int.from_bytes(data[section + 20:section + 24], "little"),
        ))
    if number_of_directories <= 6:
        return set(), section_names
    directory = optional + 112 + 6 * 8
    if directory + 8 > optional + optional_size:
        fail(f"Windows PE debug data directory is truncated: {label}")
    debug_rva = int.from_bytes(data[directory:directory + 4], "little")
    debug_size = int.from_bytes(data[directory + 4:directory + 8], "little")
    if not debug_rva and not debug_size:
        return set(), section_names
    if not debug_rva or debug_size < 28 or debug_size % 28:
        fail(f"Windows PE debug directory is malformed: {label}")

    debug_offset = _pe_rva_to_file_offset(data, debug_rva, sections, label)
    debug_end = debug_offset + debug_size
    if debug_end > len(data):
        fail(f"Windows PE debug directory exceeds file: {label}")
    debug_types: set[int] = set()
    for offset in range(debug_offset, debug_end, 28):
        values = struct.unpack_from("<IIHHIIII", data, offset)
        debug_type, size_of_data, address_of_raw_data, pointer_to_raw_data = values[4:]
        debug_types.add(debug_type)
        if size_of_data:
            if pointer_to_raw_data + size_of_data > len(data):
                fail(f"Windows PE debug payload exceeds file: {label}")
            if address_of_raw_data:
                _pe_rva_to_file_offset(data, address_of_raw_data, sections, label)
    return debug_types, section_names


def audit_pe_x64_bytes(data: bytes, label: str, *, reject_debug_payload: bool = False) -> None:
    if len(data) < 0x40 or data[:2] != b"MZ":
        fail(f"Windows binary is not an MZ executable: {label}")
    pe_offset = int.from_bytes(data[0x3c:0x40], "little")
    if pe_offset < 0 or pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        fail(f"Windows binary has no PE signature: {label}")
    machine = int.from_bytes(data[pe_offset + 4:pe_offset + 6], "little")
    symbol_table_pointer = int.from_bytes(data[pe_offset + 12:pe_offset + 16], "little")
    symbol_count = int.from_bytes(data[pe_offset + 16:pe_offset + 20], "little")
    optional_magic = int.from_bytes(data[pe_offset + 24:pe_offset + 26], "little")
    if machine != 0x8664 or optional_magic != 0x20B:
        fail(f"Windows binary is not PE32+ x64: {label}")
    debug_types, section_names = _pe_debug_types(
        data, pe_offset, int.from_bytes(data[pe_offset + 20:pe_offset + 22], "little"), label
    )
    if reject_debug_payload:
        if symbol_table_pointer or symbol_count:
            fail(f"project Windows executable contains a COFF symbol table: {label}")
        if 2 in debug_types:
            fail(f"project Windows executable contains a CodeView/PDB debug record: {label}")
        if 17 in debug_types:
            fail(f"project Windows executable contains an embedded debug payload: {label}")
        if any(name.startswith(".debug$") for name in section_names):
            fail(f"project Windows executable contains embedded debug sections: {label}")


def audit_pe_x64(path: Path, *, reject_debug_payload: bool = False) -> None:
    if reject_debug_payload:
        bundled_pdbs = [candidate for candidate in path.parent.iterdir()
                        if candidate.is_file() and candidate.suffix.lower() == ".pdb"]
        if bundled_pdbs:
            fail(f"project Windows executable has bundled PDB files: {', '.join(map(str, bundled_pdbs))}")
    audit_pe_x64_bytes(path.read_bytes(), str(path), reject_debug_payload=reject_debug_payload)


def audit_elf_section_text(section_headers: str, label: str) -> None:
    if "Section Headers:" not in section_headers and "There are no sections in this file." not in section_headers:
        fail(f"ELF section-header output is malformed: {label}")
    names: list[str] = []
    for line in section_headers.splitlines():
        match = re.match(r"^\s*\[\s*\d+\]\s+(\S+)", line)
        if match:
            names.append(match.group(1))
    for name in names:
        if name.startswith((".debug", ".zdebug")) or name == ".symtab":
            fail(f"ELF release binary contains forbidden debug/symbol section {name}: {label}")


def audit_linux_elf_text(header: str, dynamic: str, program_headers: str,
                         platform: str, label: str, section_headers: str = "") -> None:
    machine_patterns = {
        "linux-x86_64": r"Machine:\s+(?:Advanced Micro Devices X86-64|AMD x86-64)",
        "linux-aarch64": r"Machine:\s+AArch64",
    }
    if platform not in machine_patterns:
        fail(f"unsupported Linux platform: {platform}")
    if "ELF" not in header:
        fail(f"Linux binary is not an ELF: {label}")
    if not re.search(r"Class:\s+ELF64", header) or not re.search(machine_patterns[platform], header):
        architecture = "aarch64" if platform == "linux-aarch64" else "x86-64"
        fail(f"Linux binary is not {architecture} ELF: {label}")
    if re.search(r"Requesting program interpreter:|INTERP", program_headers):
        fail(f"Linux static binary contains PT_INTERP: {label}")
    if re.search(r"\(NEEDED\)|Shared library:", dynamic):
        fail(f"Linux static binary contains DT_NEEDED: {label}")
    audit_elf_section_text(section_headers, label)


def audit_linux_static_binary(path: Path, platform: str) -> None:
    readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
    if not readelf:
        fail("readelf or llvm-readelf is required")
    header = run([readelf, "-h", str(path)])
    dynamic = run([readelf, "-d", str(path)])
    program_headers = run([readelf, "-lW", str(path)])
    sections = run([readelf, "-SW", str(path)])
    audit_linux_elf_text(header, dynamic, program_headers, platform, str(path), sections)


def audit_elf_release_sections(path: Path, label: str) -> None:
    readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
    if not readelf:
        fail("readelf or llvm-readelf is required")
    audit_elf_section_text(run([readelf, "-SW", str(path)]), label)


def audit_android_binary(path: Path, platform: str, repo_root: Path) -> None:
    metadata = ANDROID_ABI_METADATA.get(platform)
    if metadata is None:
        fail(f"unsupported Android platform: {platform}")
    readelf = os.environ.get("READELF") or shutil.which("readelf") or shutil.which("llvm-readelf")
    if not readelf:
        fail("readelf or llvm-readelf is required")
    header = run([readelf, "-h", str(path)])
    program_headers = run([readelf, "-lW", str(path)])
    if "ELF" not in header or not re.search(rf"Class:\s+{metadata['elf_class']}", header) or \
            not re.search(rf"Machine:\s+{metadata['machine']}", header):
        fail(f"Android binary has the wrong ELF class or machine: {path}")
    interpreter = re.search(r"Requesting program interpreter:\s*([^\]\n]+)", program_headers)
    if interpreter is None or interpreter.group(1) != metadata["interpreter"]:
        fail(f"Android binary has the wrong interpreter: {path}")
    alignments = re.findall(r"(?m)^\s*LOAD\s+.*\s(0x[0-9A-Fa-f]+)\s*$", program_headers)
    if not alignments or any(int(alignment, 16) != 0x4000 for alignment in alignments):
        fail(f"Android binary LOAD segments are not aligned to 16 KiB: {path}")
    env = os.environ.copy()
    env["ANDROID_PATH_MARKERS"] = str(path.parent.resolve())
    run([str(repo_root / "scripts/verify-android-elf.sh"), str(path),
         metadata["abi"], metadata["interpreter"]], env=env)


def audit_darwin_load_commands(load_commands: str, label: str) -> int:
    if re.search(r"(?m)^\s*segname\s+__DWARF\s*$", load_commands):
        fail(f"macOS binary contains DWARF sections: {label}")
    blocks = re.findall(r"(?ms)^Load command \d+\n.*?(?=^Load command \d+\n|\Z)", load_commands)
    dysymtab = [block for block in blocks if re.search(r"(?m)^\s*cmd\s+LC_DYSYMTAB\s*$", block)]
    if len(dysymtab) != 1:
        fail(f"macOS binary must contain exactly one LC_DYSYMTAB: {label}")
    match = re.search(r"(?m)^\s*nlocalsym\s+(\d+)\s*$", dysymtab[0])
    if match is None:
        fail(f"macOS LC_DYSYMTAB has no nlocalsym field: {label}")
    nlocalsym = int(match.group(1))
    if nlocalsym > 1:
        fail(f"macOS binary contains too many local symbols (nlocalsym={nlocalsym}): {label}")
    return nlocalsym


def audit_darwin_optional_symbol_text(nm_output: str, label: str) -> None:
    lines = [line for line in nm_output.splitlines() if line.strip()]
    radr_lines = [line for line in lines if "radr://" in line]
    if len(radr_lines) != 1:
        fail(f"macOS nlocalsym=1 must contain exactly one radr:// metadata symbol: {label}")
    tokens = radr_lines[0].split()
    if (not tokens or not re.fullmatch(r"radr://[0-9]+", tokens[-1]) or
            len(tokens) > 6 or
            any(not re.fullmatch(r"(?:[0-9A-Fa-f]+|[-?+]|OPT)", token)
                for token in tokens[:-1])):
        fail(f"macOS local symbol is not the permitted OPT radr:// metadata: {label}")
    for line in lines:
        tokens = line.split()
        if (len(tokens) >= 3 and re.fullmatch(r"[0-9A-Fa-f]+", tokens[0]) and
                len(tokens[1]) == 1 and tokens[1].islower()):
            fail(f"macOS nlocalsym=1 contains a local function/source/debug symbol: {label}")


def audit_darwin_binary(path: Path, otool: str, label: str) -> None:
    nlocalsym = audit_darwin_load_commands(run([otool, "-l", str(path)]), label)
    if nlocalsym == 1:
        nm = shutil.which("nm")
        if not nm:
            fail("nm is required to validate macOS nlocalsym=1 metadata")
        audit_darwin_optional_symbol_text(run([nm, "-ap", str(path)]), label)


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
        audit_android_binary(path, platform, repo_root)
    elif platform.startswith("linux"):
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        if not readelf:
            fail("readelf or llvm-readelf is required")
        header = run([readelf, "-h", str(path)])
        dynamic = run([readelf, "-d", str(path)])
        program_headers = run([readelf, "-lW", str(path)])
        sections = run([readelf, "-SW", str(path)])
        audit_linux_elf_text(header, dynamic, program_headers, platform, str(path), sections)
    elif platform == "darwin-arm64":
        otool = shutil.which("otool")
        if not otool or "libusb-1.0" not in run([otool, "-L", str(path)]):
            fail(f"macOS dynamic libusb dependency missing: {path}")
        lipo = shutil.which("lipo")
        if not lipo or run([lipo, "-archs", str(path)]).split() != ["arm64"]:
            fail(f"macOS binary is not arm64-only: {path}")
        audit_darwin_binary(path, otool, str(path))
    else:
        audit_pe_x64(path, reject_debug_payload=True)
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
    if sha256_bytes(payloads[f"third_party/libusb-{LIBUSB_VERSION}.tar.bz2"]) != LIBUSB_SOURCE_SHA256:
        fail("source archive libusb checksum mismatch")
    rebuild = payloads["BUILD-RELINK.md"].decode("utf-8")
    for required_text in ("--disable-shared", "--enable-static", "--disable-udev", "test-static-relink.sh"):
        if required_text not in rebuild:
            fail(f"BUILD-RELINK.md is missing the static/relink recipe: {required_text}")
    manifest = parse_json(payloads["source-manifest.json"], "source-manifest.json")
    expected_provenance = {
        "schema": 1, "libusb_version": LIBUSB_VERSION,
        "libusb_source_url": LIBUSB_SOURCE_URL,
        "libusb_source_sha256": LIBUSB_SOURCE_SHA256,
        "libusb_license": "LGPL-2.1-or-later",
        "linux_linkage": "static",
        "linux_libusb_backend": "netlink",
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
        "corresponding-source": f"third_party/libusb-{LIBUSB_VERSION}.tar.bz2",
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
        for platform in sorted(LINUX_ARCHITECTURES):
            if not LINUX_MDEV <= expected_members(platform):
                fail(f"Linux mdev members missing from allowlist self-test: {platform}")
            expected_mdev_modes = {
                "mdev/siano-ts-mdev.conf": 0o644,
                "mdev/siano-ts-mdev.sh": 0o755,
                "mdev/siano-ts-mdev.start": 0o755,
            }
            verify_linux_mdev_modes(expected_mdev_modes, platform)
            for name in expected_mdev_modes:
                invalid_modes = dict(expected_mdev_modes)
                invalid_modes[name] = 0o600 if name.endswith(".conf") else 0o744
                try:
                    verify_linux_mdev_modes(invalid_modes, platform)
                except AuditError:
                    pass
                else:
                    fail(f"invalid mdev mode self-test did not fail: {platform}: {name}")
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

    aarch64_header = "ELF Header:\n  Class: ELF64\n  Machine: AArch64\n"
    static_dynamic = "There is no dynamic section in this file.\n"
    static_program_headers = "Program Headers:\n"
    static_section_headers = "Section Headers:\n"
    audit_linux_elf_text(aarch64_header, static_dynamic, static_program_headers,
                         "linux-aarch64", "synthetic-aarch64", static_section_headers)
    try:
        audit_linux_elf_text(aarch64_header, "Dynamic section:\n  (NEEDED) Shared library: [libusb-1.0.so.0]\n",
                             static_program_headers, "linux-aarch64", "synthetic-dynamic")
    except AuditError:
        pass
    else:
        fail("dynamic Linux dependency self-test did not fail")
    try:
        audit_linux_elf_text(aarch64_header, static_dynamic,
                             "Requesting program interpreter: /lib/ld-linux-aarch64.so.1\n",
                             "linux-aarch64", "synthetic-wrong-interpreter")
    except AuditError:
        pass
    else:
        fail("aarch64 musl interpreter mismatch self-test did not fail")
    try:
        audit_linux_elf_text(
            "ELF Header:\n  Class: ELF64\n  Machine: Advanced Micro Devices X86-64\n",
            static_dynamic, static_program_headers, "linux-aarch64", "synthetic-wrong-arch")
    except AuditError:
        pass
    else:
        fail("aarch64 machine mismatch self-test did not fail")

    stripped_release_sections = (
        "Section Headers:\n"
        "  [ 1] .text PROGBITS 00000000 000040 000010 00 AX 0 0 1\n"
        "  [ 2] .dynsym DYNSYM 00000000 000050 000010 18 A 0 1 8\n"
        "  [ 3] .eh_frame PROGBITS 00000000 000060 000010 00 A 0 0 8\n"
    )
    audit_elf_section_text(stripped_release_sections, "synthetic-stripped-release")
    for forbidden in (".debug_info", ".zdebug_line", ".symtab"):
        try:
            audit_elf_section_text(
                stripped_release_sections.replace(".text", forbidden, 1),
                f"synthetic-{forbidden}",
            )
        except AuditError:
            pass
        else:
            fail(f"forbidden ELF section self-test did not fail: {forbidden}")

    compiler = shutil.which("cc") or shutil.which("clang")
    readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
    strip = shutil.which("strip") or shutil.which("llvm-strip")
    if compiler and readelf and strip:
        with tempfile.TemporaryDirectory(prefix="siano-elf-audit-test-") as temporary:
            fixture_root = Path(temporary)
            source = fixture_root / "fixture.c"
            unstripped = fixture_root / "unstripped"
            stripped = fixture_root / "stripped"
            source.write_text("int main(void) { return 0; }\n", encoding="ascii")
            subprocess.run([compiler, "-g", "-O0", str(source), "-o", str(unstripped)], check=True)
            if "ELF" in run([readelf, "-h", str(unstripped)]):
                try:
                    audit_elf_section_text(run([readelf, "-SW", str(unstripped)]), str(unstripped))
                except AuditError:
                    pass
                else:
                    fail("actual debug-bearing ELF self-test did not fail")
                shutil.copyfile(unstripped, stripped)
                subprocess.run([strip, "--strip-unneeded", str(stripped)], check=True)
                audit_elf_section_text(run([readelf, "-SW", str(stripped)]), str(stripped))

    with tempfile.TemporaryDirectory(prefix="siano-android-audit-test-") as temporary:
        fixture_root = Path(temporary)
        repo_root = Path(__file__).resolve().parents[1]
        fake_readelf = fixture_root / "readelf"
        fake_readelf.write_text(
            "#!/bin/sh\n"
            "set -eu\n"
            "case \"$2\" in\n"
            "*android-aarch64) elf_class=ELF64; machine='AArch64'; interp=/system/bin/linker64 ;;\n"
            "*android-armv7a) elf_class=ELF32; machine='ARM'; interp=/system/bin/linker ;;\n"
            "*android-x86_64) elf_class=ELF64; machine='Advanced Micro Devices X86-64'; interp=/system/bin/linker64 ;;\n"
            "*) exit 2 ;;\n"
            "esac\n"
            "elf_class=${ANDROID_TEST_ELF_CLASS_OVERRIDE:-$elf_class}\n"
            "case \"$1\" in\n"
            "-h) printf 'ELF Header:\\n  Class: %s\\n  Type: DYN (Position-Independent Executable file)\\n  Machine: %s\\n' \"$elf_class\" \"$machine\" ;;\n"
            "-l|-lW) printf 'Program Headers:\\n  LOAD 0x0 0x0 0x0 0x0 0x0 R E 0x4000\\n      [Requesting program interpreter: %s]\\n' \"$interp\" ;;\n"
            "-d) printf 'Dynamic section:\\n  (NEEDED) Shared library: [liblog.so]\\n  (NEEDED) Shared library: [libdl.so]\\n  (NEEDED) Shared library: [libc.so]\\n' ;;\n"
            "-SW) printf 'Section Headers:\\n' ;;\n"
            "*) exit 2 ;;\n"
            "esac\n",
            encoding="ascii",
        )
        fake_readelf.chmod(0o755)
        saved_android_abi = os.environ.pop("ANDROID_ABI", None)
        saved_test_elf_class = os.environ.pop("ANDROID_TEST_ELF_CLASS_OVERRIDE", None)
        saved_readelf = os.environ.get("READELF")
        os.environ["READELF"] = str(fake_readelf)
        try:
            fixtures = {}
            for platform, machine in (
                    ("android-aarch64", "AArch64"),
                    ("android-armv7a", "ARM"),
                    ("android-x86_64", "Advanced Micro Devices X86-64")):
                fixture = fixture_root / platform
                fixture.write_text(f"{machine} fixture\n", encoding="ascii")
                fixtures[platform] = fixture
                audit_binary(fixture, platform, "a" * 40, repo_root)
            for platform, wrong_class in (
                    ("android-armv7a", "ELF64"),
                    ("android-x86_64", "ELF32")):
                os.environ["ANDROID_TEST_ELF_CLASS_OVERRIDE"] = wrong_class
                try:
                    audit_binary(fixtures[platform], platform, "a" * 40, repo_root)
                except AuditError:
                    pass
                else:
                    fail(f"Android ELF class self-test did not fail: {platform} as {wrong_class}")
                finally:
                    os.environ.pop("ANDROID_TEST_ELF_CLASS_OVERRIDE", None)
        finally:
            if saved_android_abi is None:
                os.environ.pop("ANDROID_ABI", None)
            else:
                os.environ["ANDROID_ABI"] = saved_android_abi
            if saved_test_elf_class is None:
                os.environ.pop("ANDROID_TEST_ELF_CLASS_OVERRIDE", None)
            else:
                os.environ["ANDROID_TEST_ELF_CLASS_OVERRIDE"] = saved_test_elf_class
            if saved_readelf is None:
                os.environ.pop("READELF", None)
            else:
                os.environ["READELF"] = saved_readelf

    def synthetic_pe(debug_type: int | None = None, coff_symbols: bool = False) -> bytes:
        data = bytearray(0x500)
        data[0:2] = b"MZ"
        data[0x3C:0x40] = (0x80).to_bytes(4, "little")
        data[0x80:0x84] = b"PE\0\0"
        coff = 0x84
        data[coff:coff + 2] = (0x8664).to_bytes(2, "little")
        data[coff + 2:coff + 4] = (1).to_bytes(2, "little")
        if coff_symbols:
            data[coff + 8:coff + 12] = (0x400).to_bytes(4, "little")
            data[coff + 12:coff + 16] = (1).to_bytes(4, "little")
        data[coff + 16:coff + 18] = (0xF0).to_bytes(2, "little")
        optional = coff + 20
        data[optional:optional + 2] = (0x20B).to_bytes(2, "little")
        data[optional + 108:optional + 112] = (16).to_bytes(4, "little")
        if debug_type is not None:
            directory = optional + 112 + 6 * 8
            data[directory:directory + 4] = (0x1000).to_bytes(4, "little")
            data[directory + 4:directory + 8] = (28).to_bytes(4, "little")
        section = optional + 0xF0
        data[section:section + 6] = b".rdata"
        data[section + 8:section + 12] = (0x200).to_bytes(4, "little")
        data[section + 12:section + 16] = (0x1000).to_bytes(4, "little")
        data[section + 16:section + 20] = (0x200).to_bytes(4, "little")
        data[section + 20:section + 24] = (0x200).to_bytes(4, "little")
        if debug_type is not None:
            data[0x200 + 12:0x200 + 16] = debug_type.to_bytes(4, "little")
        return bytes(data)

    audit_pe_x64_bytes(synthetic_pe(), "synthetic-stripped-windows", reject_debug_payload=True)
    try:
        audit_pe_x64_bytes(synthetic_pe(17), "synthetic-embedded-debug", reject_debug_payload=True)
    except AuditError:
        pass
    else:
        fail("embedded Windows debug self-test did not fail")
    audit_pe_x64_bytes(synthetic_pe(2), "synthetic-upstream-dll-codeview")
    try:
        audit_pe_x64_bytes(synthetic_pe(2), "synthetic-project-codeview", reject_debug_payload=True)
    except AuditError:
        pass
    else:
        fail("project CodeView self-test did not fail")
    for field in ("pointer", "count"):
        try:
            audit_pe_x64_bytes(
                synthetic_pe(coff_symbols=True), f"synthetic-coff-{field}", reject_debug_payload=True
            )
        except AuditError:
            pass
        else:
            fail(f"project COFF {field} self-test did not fail")
    try:
        audit_darwin_load_commands(
            "Load command 1\n  segname __DWARF\n", "synthetic-dwarf"
        )
    except AuditError:
        pass
    else:
        fail("macOS DWARF self-test did not fail")
    audit_darwin_load_commands(
        "Load command 1\n  cmd LC_DYSYMTAB\n  nlocalsym 0\n"
        "Load command 2\n  segname __TEXT\n  sectname __text\n",
        "synthetic-symbol-free",
    )
    try:
        audit_darwin_load_commands(
            "Load command 1\n  cmd LC_DYSYMTAB\n  nlocalsym 2\n", "synthetic-symbols"
        )
    except AuditError:
        pass
    else:
        fail("macOS symbol-table self-test did not fail")
    audit_darwin_load_commands(
        "Load command 1\n  cmd LC_DYSYMTAB\n  nlocalsym 1\n", "synthetic-radr"
    )
    audit_darwin_optional_symbol_text(
        "0000000005614542 - 00 0000   OPT radr://5614542\n", "synthetic-radr"
    )
    for bad_nm in (
            "0000000005614542 - 00 0000   OPT radr://5614542\n"
            "0000000000000000 t _local_function\n",
            "0000000005614542 - 00 0000   OPT radr://5614542\n"
            "0000000005614542 - 00 0000   OPT radr://5678\n",
            "0000000005614542 - 00 0000   OPT radr://not-a-number\n",
            "0000000005614542 - 00 0000   N_OPT radr://5614542\n"):
        try:
            audit_darwin_optional_symbol_text(bad_nm, "synthetic-invalid-radr")
        except AuditError:
            pass
        else:
            fail("macOS optional local-symbol self-test did not fail")

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
            b"libusb_version=1.0.30\nlibusb_source_url=" + LIBUSB_SOURCE_URL.encode() + b"\n"
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
