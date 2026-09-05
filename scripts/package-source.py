#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Create a corresponding source archive from an exact Git tree snapshot."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import tempfile

from provenance import (
    FIRMWARE_LICENSE_SHA256,
    FIRMWARE_LICENSE_URL,
    FIRMWARE_SHA256,
    FIRMWARE_URL,
    LIBUSB_SOURCE_SHA256,
    LIBUSB_SOURCE_URL,
    LIBUSB_VERSION,
)

_audit_spec = importlib.util.spec_from_file_location("siano_audit", Path(__file__).with_name("audit-artifact.py"))
if _audit_spec is None or _audit_spec.loader is None:
    raise ImportError("cannot load audit-artifact.py")
_audit = importlib.util.module_from_spec(_audit_spec)
_audit_spec.loader.exec_module(_audit)
AuditError = _audit.AuditError
SOURCE_FORBIDDEN = _audit.SOURCE_FORBIDDEN
audit_source_archive = _audit.audit_source_archive
fail = _audit.fail


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_regular(source: Path, destination: Path) -> None:
    if source.is_symlink() or not source.is_file():
        fail(f"source input must be an ordinary file: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    shutil.copymode(source, destination)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    path.chmod(0o644)


def run_git(root: Path, *arguments: str, binary: bool = False) -> bytes:
    try:
        result = subprocess.run(["git", "-C", str(root), *arguments], check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"git command failed: {error}")
    return result.stdout if binary else result.stdout.decode("utf-8", "strict")


def snapshot_repository(root: Path, source_ref: str, stage: Path) -> tuple[str, str]:
    commit = str(run_git(root, "rev-parse", "--verify", f"{source_ref}^{{commit}}")).strip()
    tree = str(run_git(root, "rev-parse", "--verify", f"{commit}^{{tree}}")).strip()
    version_data = run_git(root, "show", f"{commit}:VERSION", binary=True)
    if not version_data or not version_data.endswith(b"\n"):
        fail("VERSION at --source-ref must end with one newline")
    try:
        version_data.decode("ascii")
    except UnicodeDecodeError as error:
        fail(f"VERSION at --source-ref is not ASCII: {error}")
    archive_path = stage.parent / "repository-snapshot.tar"
    run_git(root, "archive", "--format=tar", f"--output={archive_path}", commit)
    try:
        with tarfile.open(archive_path, "r:") as archive:
            for member in archive.getmembers():
                name = PurePosixPath(member.name)
                if name.is_absolute() or ".." in name.parts:
                    fail(f"unsafe source tree member: {member.name}")
                if member.issym() or member.islnk() or not (member.isfile() or member.isdir()):
                    fail(f"source tree contains non-regular member: {member.name}")
                if member.isfile() and SOURCE_FORBIDDEN.search(member.name):
                    fail(f"forbidden source tree member: {member.name}")
                if not member.isfile():
                    continue
                payload = archive.extractfile(member)
                if payload is None:
                    fail(f"cannot read source tree member: {member.name}")
                destination = stage / "repository" / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                with destination.open("wb") as output:
                    shutil.copyfileobj(payload, output)
                destination.chmod(member.mode & 0o7777)
    except (OSError, tarfile.TarError) as error:
        fail(f"cannot inspect Git source snapshot: {error}")
    return commit, tree


def write_tar(stage: Path, output: Path) -> None:
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


def write_checksums(stage: Path) -> None:
    rows = []
    for path in sorted(stage.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            rows.append(f"{sha256(path)}  {path.relative_to(stage).as_posix()}")
    write_text(stage / "SHA256SUMS", "\n".join(rows) + "\n")


def verify_libusb_archive(path: Path) -> None:
    if path.name != f"libusb-{LIBUSB_VERSION}.tar.bz2" or sha256(path) != LIBUSB_SOURCE_SHA256:
        fail("libusb source is not the pinned archive")
    try:
        with tarfile.open(path, "r:bz2") as archive:
            names: set[str] = set()
            has_copying = False
            for member in archive.getmembers():
                if member.name in names:
                    fail(f"duplicate libusb source member: {member.name}")
                names.add(member.name)
                if member.name != f"libusb-{LIBUSB_VERSION}" and not member.name.startswith(f"libusb-{LIBUSB_VERSION}/"):
                    fail(f"unexpected libusb source member: {member.name}")
                if not (member.isfile() or member.isdir()):
                    fail(f"libusb source member is not regular: {member.name}")
                if member.name == f"libusb-{LIBUSB_VERSION}/COPYING":
                    has_copying = True
            if not has_copying:
                fail("libusb source archive has no COPYING")
    except (OSError, tarfile.TarError) as error:
        fail(f"cannot inspect libusb source archive: {error}")


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="siano-source-test-") as temporary:
        root = Path(temporary)
        (root / "packaging").mkdir()
        write_text(root / "VERSION", "0.0.1\n")
        write_text(root / "packaging/REBUILD.md", "snapshot rebuild\n")
        write_text(root / "README.md", "committed README\n")
        write_text(root / "COPYING", "GPL\n")
        write_text(root / "LICENCE.siano", "firmware license\n")
        run_git(root, "init", "-q")
        run_git(root, "config", "user.name", "source-test")
        run_git(root, "config", "user.email", "source-test@example.invalid")
        run_git(root, "add", "VERSION", "packaging/REBUILD.md", "README.md", "COPYING", "LICENCE.siano")
        run_git(root, "commit", "-q", "-m", "snapshot")
        write_text(root / "README.md", "dirty README must not be archived\n")
        stage = root / "stage"
        stage.mkdir()
        snapshot_repository(root, "HEAD", stage)
        if (stage / "repository/README.md").read_text(encoding="utf-8") != "committed README\n":
            fail("source-ref self-test included dirty worktree content")
    print("source snapshot self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--version")
    parser.add_argument("--source-ref")
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--libusb-source-archive", type=Path)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.version, args.source_ref, args.source_root,
                args.libusb_source_archive, args.output_dir)):
        parser.error("normal mode requires --version, --source-ref, --source-root, --libusb-source-archive, and --output-dir")
    if not __import__("re").fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        fail("version must be strict N.N.N")
    root = args.source_root.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir.resolve() / f"siano-ts-{args.version}-source.tar.gz"
    if output.exists() or output.is_symlink():
        fail(f"refusing to overwrite existing archive: {output}")
    libusb = args.libusb_source_archive.resolve()
    verify_libusb_archive(libusb)
    with tempfile.TemporaryDirectory(prefix="siano-source-") as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        commit, tree = snapshot_repository(root, args.source_ref, stage)
        repository = stage / "repository"
        version_data = (repository / "VERSION").read_bytes()
        if version_data != (args.version + "\n").encode("ascii"):
            fail("--version does not match VERSION at --source-ref")
        for name in ("COPYING", "LICENCE.siano", "README.md"):
            copy_regular(repository / name, stage / name)
        copy_regular(repository / "packaging/REBUILD.md", stage / "BUILD-RELINK.md")
        if sha256(stage / "LICENCE.siano") != FIRMWARE_LICENSE_SHA256:
            fail("repository LICENCE.siano does not match the pinned firmware license")
        copy_regular(libusb, stage / f"third_party/libusb-{LIBUSB_VERSION}.tar.bz2")
        notice = (
            f"source.ref={args.source_ref}\n"
            f"source.resolved_commit={commit}\n"
            f"source.tree={tree}\n"
            f"dependency.libusb.version={LIBUSB_VERSION}\n"
            "dependency.libusb.license=LGPL-2.1-or-later\n"
            f"corresponding-source=third_party/libusb-{LIBUSB_VERSION}.tar.bz2\n"
            f"corresponding-source.sha256={LIBUSB_SOURCE_SHA256}\n"
            f"libusb.source.url={LIBUSB_SOURCE_URL}\n"
            f"firmware.url={FIRMWARE_URL}\n"
            f"firmware.sha256={FIRMWARE_SHA256}\n"
            f"firmware.license.url={FIRMWARE_LICENSE_URL}\n"
            f"firmware.license.sha256={FIRMWARE_LICENSE_SHA256}\n"
            "firmware=excluded-from-source-archive\n"
            "vendor-blobs=excluded-from-source-archive\n"
        )
        write_text(stage / "DEPENDENCY-NOTICE.txt", notice)
        manifest = {
            "schema": 1, "version": args.version, "source_ref": args.source_ref,
            "resolved_commit": commit, "tree": tree, "libusb_version": LIBUSB_VERSION,
            "libusb_source_sha256": LIBUSB_SOURCE_SHA256,
            "firmware_url": FIRMWARE_URL, "firmware_sha256": FIRMWARE_SHA256,
            "firmware_license_url": FIRMWARE_LICENSE_URL,
            "firmware_license_sha256": FIRMWARE_LICENSE_SHA256, "files": {},
        }
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                data = path.read_bytes()
                manifest["files"][path.relative_to(stage).as_posix()] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        with (stage / "source-manifest.json").open("w", encoding="utf-8", newline="\n") as handle:
            json.dump(manifest, handle, indent=2, sort_keys=True)
            handle.write("\n")
        write_checksums(stage)
        write_tar(stage, output)
    audit_source_archive(output)
    print(output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AuditError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"package-source: {error}", file=sys.stderr)
        raise SystemExit(1)
