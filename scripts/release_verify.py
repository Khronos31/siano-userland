#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify deterministic Siano candidate bundles against a tag rebuild."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import copy
import gzip
import hashlib
import json
from pathlib import Path
import re
import stat
import subprocess
import sys
import tarfile
import zipfile


class VerificationError(ValueError):
    """A release verification gate failed."""


PLATFORMS = (
    "linux-x86_64", "linux-aarch64", "darwin-arm64", "android-aarch64",
    "android-armv7a", "android-x86_64", "windows-x64",
)
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\Z")
MAX_ARCHIVE_BYTES = 1024 * 1024 * 1024
MAX_MEMBER_BYTES = 128 * 1024 * 1024
MAX_TOTAL_BYTES = 1024 * 1024 * 1024
MAX_MEMBERS = 50000
MAX_METADATA_BYTES = 8 * 1024 * 1024
CHUNK_BYTES = 1024 * 1024
EVIDENCE = (
    "evidence/binary-audit.json", "evidence/sianod-binary-audit.json",
    "evidence/sianoctl-binary-audit.json",
)
PX4_DOCUMENTS = (
    "README.md", "SPEC.md", "docs/platforms/validation-results.md",
    "docs/release-validation.md", "docs/releasing.md",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def sha256(path: Path) -> str:
    require(path.is_file() and not path.is_symlink(), f"not a regular file: {path}")
    require(path.stat().st_size <= MAX_ARCHIVE_BYTES, f"input size limit: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_json(data: bytes, name: str) -> dict:
    def pairs(items: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in items:
            require(key not in result, f"duplicate JSON key in {name}: {key}")
            result[key] = value
        return result

    def constant(value: str) -> object:
        raise VerificationError(f"invalid JSON constant in {name}: {value}")

    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=pairs,
                           parse_constant=constant)
    except (UnicodeError, ValueError, RecursionError) as error:
        raise VerificationError(f"invalid JSON {name}: {error}") from error
    require(isinstance(value, dict), f"{name} must be an object")
    return value


def canonical_json(value: dict) -> bytes:
    return json.dumps(value, indent=2, sort_keys=True).encode("utf-8")


def canonical_json_file(data: bytes, value: dict) -> bool:
    canonical = canonical_json(value)
    return data in (canonical, canonical + b"\n")


def matching_json_style(before: bytes, after: bytes, name: str) -> None:
    require(before.endswith(b"\n") == after.endswith(b"\n"),
            f"JSON line ending changed: {name}")


def safe_name(name: str) -> None:
    require(isinstance(name, str) and bool(name) and len(name) <= 1024,
            "invalid archive path length")
    require(not any(ord(char) < 32 or ord(char) == 127 for char in name) and
            "\\" not in name and ":" not in name and
            all(part not in {"", ".", ".."} for part in name.split("/")) and
            not name.startswith("/"), f"unsafe archive path: {name!r}")


@dataclass(frozen=True)
class Member:
    kind: str
    mode: int
    size: int
    digest: str
    data: bytes | None
    link_target: str | None = None


def read_member(stream, size: int, name: str, keep: bool) -> Member:
    require(0 <= size <= MAX_MEMBER_BYTES, f"archive member size limit: {name}")
    require(not keep or size <= MAX_METADATA_BYTES, f"metadata size limit: {name}")
    digest, chunks, seen = hashlib.sha256(), [], 0
    while True:
        chunk = stream.read(min(CHUNK_BYTES, size - seen + 1))
        if not chunk:
            break
        seen += len(chunk)
        require(seen <= size, f"member length mismatch: {name}")
        digest.update(chunk)
        if keep:
            chunks.append(chunk)
    require(seen == size, f"truncated archive member: {name}")
    return Member("file", 0, size, digest.hexdigest(), b"".join(chunks) if keep else None)


def read_archive(path: Path) -> dict[str, Member]:
    require(path.is_file() and not path.is_symlink(), f"not a regular archive: {path}")
    require(path.stat().st_size <= MAX_ARCHIVE_BYTES, f"archive size limit: {path}")
    members: dict[str, Member] = {}
    folded: set[str] = set()
    total = 0
    keep_names = {"README.md", "DEPENDENCY-NOTICE.txt", "manifest.json", "SHA256SUMS",
                  "repository/VERSION",
                  "source-manifest.json", "evidence/binary-audit.json",
                  "evidence/sianod-binary-audit.json", "evidence/sianoctl-binary-audit.json"}

    def add(name: str, mode: int, size: int, stream) -> None:
        nonlocal total
        safe_name(name)
        require(name not in members, f"duplicate archive member: {name}")
        require(name.casefold() not in folded, f"case-colliding archive member: {name}")
        require(len(members) < MAX_MEMBERS, "archive member count limit")
        require(0 <= mode <= 0o777, f"unsupported archive mode: {name}")
        folded.add(name.casefold())
        total += size
        require(total <= MAX_TOTAL_BYTES, "archive expanded size limit")
        item = read_member(stream, size, name, name in keep_names or
                           name.startswith("repository/docs/release-validation/"))
        members[name] = Member(item.kind, mode, item.size, item.digest, item.data)

    try:
        if path.name.endswith(".zip"):
            with zipfile.ZipFile(path) as archive:
                require(len(archive.infolist()) <= MAX_MEMBERS, "archive member count limit")
                for info in archive.infolist():
                    mode = info.external_attr >> 16
                    require(info.create_system == 3 and stat.S_ISREG(mode) and
                            not info.is_dir() and not info.flag_bits & 1 and
                            info.compress_type in {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED},
                            f"unsupported ZIP member: {info.filename}")
                    with archive.open(info) as stream:
                        add(info.filename, stat.S_IMODE(mode), info.file_size, stream)
        else:
            require(path.name.endswith(".tar.gz"), f"unsupported archive format: {path}")
            with tarfile.open(path, "r|gz") as archive:
                for info in archive:
                    require(info.isfile() and not info.sparse,
                            f"unsupported TAR member: {info.name}")
                    stream = archive.extractfile(info)
                    require(stream is not None, f"cannot read archive member: {info.name}")
                    with stream:
                        add(info.name, info.mode, info.size, stream)
    except (OSError, EOFError, gzip.BadGzipFile, tarfile.TarError,
            zipfile.BadZipFile, RuntimeError) as error:
        raise VerificationError(f"cannot read archive {path.name}: {error}") from error
    return members


def expected_assets(version: str) -> tuple[str, ...]:
    require(isinstance(version, str) and VERSION.fullmatch(version) is not None,
            "version must be strict X.Y.Z")
    return tuple(f"siano-ts-{version}-{platform}." +
                 ("zip" if platform == "windows-x64" else "tar.gz")
                 for platform in PLATFORMS) + (f"siano-ts-{version}-source.tar.gz",)


def parse_sums(data: bytes, expected: set[str], name: str) -> dict[str, str]:
    try:
        lines = data.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise VerificationError(f"{name} is not ASCII") from error
    result = {}
    for line in lines:
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_./-]+)", line)
        require(match is not None, f"invalid {name} line")
        digest, filename = match.groups()
        require(filename not in result, f"duplicate {name} entry: {filename}")
        result[filename] = digest
    require(set(result) == expected, f"{name} inventory mismatch")
    return result


def verify_bundle(directory: Path, version: str) -> tuple[dict[str, str], dict[str, dict[str, Member]]]:
    require(directory.is_dir() and not directory.is_symlink(),
            f"not a bundle directory: {directory}")
    expected = set(expected_assets(version))
    entries = {item.name for item in directory.iterdir()}
    require(entries == expected | {"SHA256SUMS"},
            f"bundle inventory mismatch: {directory}")
    sums_path = directory / "SHA256SUMS"
    require(sums_path.is_file() and not sums_path.is_symlink() and
            sums_path.stat().st_size <= MAX_METADATA_BYTES,
            f"invalid outer SHA256SUMS: {directory}")
    checksums = parse_sums(sums_path.read_bytes(), expected,
                           "outer SHA256SUMS")
    archives: dict[str, dict[str, Member]] = {}
    for name in sorted(expected):
        path = directory / name
        require(path.is_file() and not path.is_symlink(), f"missing regular archive: {name}")
        require(sha256(path) == checksums[name], f"outer checksum mismatch: {name}")
        archives[name] = read_archive(path)
        verify_inner_sums(archives[name], name)
    return checksums, archives


def verify_inner_sums(members: dict[str, Member], archive_name: str) -> None:
    require("SHA256SUMS" in members and members["SHA256SUMS"].data is not None,
            f"missing inner SHA256SUMS: {archive_name}")
    expected = set(members) - {"SHA256SUMS"}
    sums = parse_sums(members["SHA256SUMS"].data or b"", expected,
                      f"inner SHA256SUMS in {archive_name}")
    for name, digest in sums.items():
        require(members[name].digest == digest,
                f"inner checksum mismatch in {archive_name}: {name}")


def git_value(root: Path, *args: str) -> str:
    try:
        result = subprocess.run(["git", "-C", str(root), *args], check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=5)
    except (OSError, subprocess.SubprocessError) as error:
        raise VerificationError(f"git identity check failed: {error}") from error
    return result.stdout.decode("ascii", "strict").strip()


def candidate_document_paths(root: Path, version: str, tag: str,
                             candidate_sha: str) -> set[str]:
    paths = set(PX4_DOCUMENTS)
    paths.update({f"release-notes/{tag}.md", f"release-notes/{tag}.json",
                  f"docs/release-validation/{version}.md"})
    existing = set()
    for path in paths:
        try:
            result = subprocess.run(
                ["git", "-C", str(root), "cat-file", "-t", f"{candidate_sha}:{path}"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        except (OSError, subprocess.SubprocessError) as error:
            raise VerificationError(f"candidate document check failed: {error}") from error
        if result.returncode == 0 and result.stdout.strip() == b"blob":
            existing.add(path)
    result_doc = f"docs/release-validation/{version}.md"
    notes_doc = f"release-notes/{tag}.md"
    require(result_doc in existing and notes_doc in existing,
            "candidate must already contain the version record and release notes")
    return existing


def verify_git_identity(root: Path, version: str, candidate_sha: str,
                        tag_sha: str) -> tuple[str, str]:
    require(HEX40.fullmatch(candidate_sha) and HEX40.fullmatch(tag_sha),
            "source SHAs must be canonical 40-character object IDs")
    require(git_value(root, "rev-parse", "--verify", f"{candidate_sha}^{{commit}}") == candidate_sha,
            "candidate commit is not present in source checkout")
    require(git_value(root, "rev-parse", "--verify", f"{tag_sha}^{{commit}}") == tag_sha,
            "tag commit is not present in source checkout")
    require(git_value(root, "rev-parse", "--verify", "HEAD^{commit}") == tag_sha,
            "source checkout is not at the release tag commit")
    try:
        subprocess.run(["git", "-C", str(root), "merge-base", "--is-ancestor",
                        candidate_sha, tag_sha], check=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=5)
    except (OSError, subprocess.SubprocessError) as error:
        raise VerificationError("candidate commit is not an ancestor of tag commit") from error
    candidate_tree = git_value(root, "rev-parse", "--verify", f"{candidate_sha}^{{tree}}")
    tag_tree = git_value(root, "rev-parse", "--verify", f"{tag_sha}^{{tree}}")
    allowed = candidate_document_paths(root, version, f"v{version}", candidate_sha)
    changed_notes_at = None
    changed_record_at = None
    commits = git_value(root, "rev-list", "--first-parent", "--reverse",
                        f"{candidate_sha}..{tag_sha}").splitlines()
    require(bool(commits), "tag commit must follow the candidate commit")
    previous = candidate_sha
    notes_doc = f"release-notes/v{version}.md"
    result_doc = f"docs/release-validation/{version}.md"
    for index, commit in enumerate(commits):
        parents = git_value(root, "show", "-s", "--format=%P", commit).split()
        require(parents == [previous], "candidate-to-tag document commits must be a linear chain")
        changed = set(filter(None, git_value(
            root, "diff", "--name-only", "-z", previous, commit).split("\x00")))
        require(bool(changed) and changed.issubset(allowed),
                "candidate-to-tag commits may change only approved existing documents")
        if result_doc in changed and changed_record_at is None:
            changed_record_at = index
        if notes_doc in changed and changed_notes_at is None:
            changed_notes_at = index
        previous = commit
    require(previous == tag_sha and changed_record_at is not None and
            changed_notes_at is not None and changed_record_at < changed_notes_at,
            "tag history must update the version result before release notes")
    return candidate_tree, tag_tree


def notice_lines(data: bytes, name: str) -> list[tuple[str | None, str]]:
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise VerificationError(f"notice is not UTF-8: {name}") from error
    result = []
    seen = set()
    for line in lines:
        if "=" not in line:
            result.append((None, line))
            continue
        key, value = line.split("=", 1)
        if re.fullmatch(r"[a-z0-9_.-]+", key):
            require(key not in seen, f"duplicate notice field in {name}: {key}")
            seen.add(key)
            result.append((key, value))
        else:
            result.append((None, line))
    return result


def compare_notice(before: bytes, after: bytes, fields: dict[str, str], name: str) -> None:
    old, new = notice_lines(before, name), notice_lines(after, name)
    require(len(old) == len(new), f"notice line count changed: {name}")
    changed = set()
    for left, right in zip(old, new):
        if left == right:
            continue
        require(left[0] is not None and right[0] is not None and left[0] == right[0],
                f"notice content changed outside identity fields: {name}")
        require(left[0] in fields and right[1] == fields[left[0]],
                f"unexpected notice identity change: {name}:{left[0]}")
        changed.add(left[0])
    require(changed == set(fields), f"notice identity fields did not all change: {name}")


def compare_json_identity(before: bytes, after: bytes, *, name: str,
                          field: str, expected: str) -> tuple[dict, dict]:
    old, new = parse_json(before, name), parse_json(after, name)
    matching_json_style(before, after, name)
    require(canonical_json_file(before, old) and canonical_json_file(after, new),
            f"noncanonical JSON encoding: {name}")
    require(old.get(field) != new.get(field) and new.get(field) == expected,
            f"invalid identity field change: {name}:{field}")
    normalized = copy.deepcopy(old)
    normalized[field] = new[field]
    require(normalized == new, f"JSON field changed outside identity: {name}")
    return old, new


def parse_binary_manifest(members: dict[str, Member], archive_name: str,
                          source_sha: str) -> dict:
    require("manifest.json" in members and members["manifest.json"].data is not None,
            f"missing manifest.json: {archive_name}")
    manifest = parse_json(members["manifest.json"].data or b"", "manifest.json")
    require(canonical_json_file(members["manifest.json"].data or b"", manifest),
            f"noncanonical manifest.json: {archive_name}")
    platform = next((value for value in PLATFORMS if f"-{value}." in archive_name), None)
    require(platform is not None, f"unknown binary archive: {archive_name}")
    version = re.match(r"siano-ts-(\d+\.\d+\.\d+)-", archive_name)
    require(version is not None and manifest.get("schema") == 1 and
            manifest.get("version") == version.group(1) and
            manifest.get("platform") == platform and manifest.get("source_ref") == source_sha,
            f"binary manifest identity mismatch: {archive_name}")
    require(manifest.get("programs") == (["sianod.exe", "sianoctl.exe", "siano-ts.exe"]
            if platform == "windows-x64" else ["sianod", "sianoctl", "siano-ts"]),
            f"binary program inventory mismatch: {archive_name}")
    files = manifest.get("files")
    require(isinstance(files, dict) and set(files) == set(members) - {"manifest.json", "SHA256SUMS"},
            f"manifest file inventory mismatch: {archive_name}")
    for name, record in files.items():
        require(isinstance(record, dict) and set(record) == {"size", "sha256"} and
                type(record.get("size")) is int and record["size"] == members[name].size and
                record.get("sha256") == members[name].digest,
                f"manifest record mismatch: {archive_name}:{name}")
    require(set(EVIDENCE).issubset(members), f"binary evidence missing: {archive_name}")
    for name, program in zip(EVIDENCE, manifest["programs"]):
        data = members[name].data
        require(data is not None, f"binary evidence exceeds metadata limit: {name}")
        evidence = parse_json(data, name)
        require(canonical_json_file(data, evidence), f"noncanonical binary evidence: {name}")
        require(set(evidence) == {"schema", "platform", "source_ref", "binary"} and
                evidence.get("schema") == 2 and evidence.get("platform") == platform and
                evidence.get("source_ref") == source_sha and
                isinstance(evidence.get("binary"), dict) and
                set(evidence["binary"]) == {"name", "sha256"} and
                evidence["binary"].get("name") == program and
                evidence["binary"].get("sha256") == members[program].digest,
                f"binary evidence mismatch: {name}")
    return manifest


def parse_source_manifest(members: dict[str, Member], archive_name: str,
                          version: str, commit: str, tree: str) -> dict:
    require("source-manifest.json" in members and
            members["source-manifest.json"].data is not None,
            f"missing source-manifest.json: {archive_name}")
    manifest = parse_json(members["source-manifest.json"].data or b"", "source-manifest.json")
    require(canonical_json_file(members["source-manifest.json"].data or b"", manifest),
            "noncanonical source-manifest.json")
    require(manifest.get("version") == version and manifest.get("source_ref") == commit and
            manifest.get("resolved_commit") == commit and manifest.get("tree") == tree,
            f"source manifest identity mismatch: {archive_name}")
    files = manifest.get("files")
    require(isinstance(files, dict) and set(files) == set(members) -
            {"source-manifest.json", "SHA256SUMS"},
            f"source manifest inventory mismatch: {archive_name}")
    for name, record in files.items():
        require(isinstance(record, dict) and set(record) == {"size", "sha256"} and
                type(record.get("size")) is int and record["size"] == members[name].size and
                record.get("sha256") == members[name].digest,
                f"source manifest record mismatch: {name}")
    require(members.get("repository/VERSION") is not None and
            members["repository/VERSION"].data == (version + "\n").encode("ascii"),
            "source archive VERSION mismatch")
    return manifest


def compare_member_sets(before: dict[str, Member], after: dict[str, Member],
                        allowed: set[str], archive_name: str) -> None:
    require(set(before) == set(after), f"archive member inventory changed: {archive_name}")
    for name in before:
        left, right = before[name], after[name]
        require((left.kind, left.mode, left.link_target) ==
                (right.kind, right.mode, right.link_target),
                f"archive member type/mode/link changed: {archive_name}:{name}")
        if name not in allowed:
            require((left.size, left.digest) == (right.size, right.digest),
                    f"archive payload changed: {archive_name}:{name}")


def verify_binary_pair(before: dict[str, Member], after: dict[str, Member],
                       archive_name: str, candidate_sha: str, tag_sha: str) -> None:
    allowed = {"manifest.json", "DEPENDENCY-NOTICE.txt", "SHA256SUMS", *EVIDENCE}
    compare_member_sets(before, after, allowed, archive_name)
    old_manifest = parse_binary_manifest(before, archive_name, candidate_sha)
    new_manifest = parse_binary_manifest(after, archive_name, tag_sha)
    matching_json_style(before["manifest.json"].data or b"",
                        after["manifest.json"].data or b"", f"{archive_name}:manifest.json")
    manifest_expected = copy.deepcopy(old_manifest)
    manifest_expected["source_ref"] = tag_sha
    for name in ("DEPENDENCY-NOTICE.txt", *EVIDENCE):
        require(old_manifest["files"][name]["size"] == new_manifest["files"][name]["size"],
                f"binary identity member size changed: {archive_name}:{name}")
        manifest_expected["files"][name] = new_manifest["files"][name]
    require(manifest_expected == new_manifest,
            f"binary manifest changed outside approved identity: {archive_name}")
    require(before["DEPENDENCY-NOTICE.txt"].data is not None and
            after["DEPENDENCY-NOTICE.txt"].data is not None,
            f"missing dependency notice: {archive_name}")
    old_notice = dict(item for item in notice_lines(
        before["DEPENDENCY-NOTICE.txt"].data or b"", archive_name) if item[0] is not None)
    new_notice = dict(item for item in notice_lines(
        after["DEPENDENCY-NOTICE.txt"].data or b"", archive_name) if item[0] is not None)
    require(old_notice.get("source.ref") == candidate_sha and
            new_notice.get("source.ref") == tag_sha,
            f"binary dependency notice identity mismatch: {archive_name}")
    compare_notice(before["DEPENDENCY-NOTICE.txt"].data or b"",
                   after["DEPENDENCY-NOTICE.txt"].data or b"",
                   {"source.ref": tag_sha}, f"{archive_name}:DEPENDENCY-NOTICE.txt")
    for name in EVIDENCE:
        compare_json_identity(before[name].data or b"", after[name].data or b"",
                              name=f"{archive_name}:{name}", field="source_ref", expected=tag_sha)
    changed = {name for name in before if before[name].digest != after[name].digest}
    require(changed.issubset(allowed), f"binary changed outside allowlist: {archive_name}")


def source_document_members(members: dict[str, Member], version: str,
                            tag: str) -> set[str]:
    paths = {"README.md"}
    paths.update(f"repository/{path}" for path in PX4_DOCUMENTS)
    paths.update({f"repository/release-notes/{tag}.md",
                  f"repository/release-notes/{tag}.json",
                  f"repository/docs/release-validation/{version}.md"})
    result_doc = f"repository/docs/release-validation/{version}.md"
    notes_doc = f"repository/release-notes/{tag}.md"
    require(result_doc in members and notes_doc in members,
            "candidate source archive must contain the version record and release notes")
    return paths & set(members)


def verify_source_pair(before: dict[str, Member], after: dict[str, Member],
                       archive_name: str, version: str, candidate_sha: str,
                       tag_sha: str, candidate_tree: str, tag_tree: str,
                       tag: str) -> None:
    doc = f"repository/docs/release-validation/{version}.md"
    documents = source_document_members(before, version, tag)
    require(doc in after, "version result document is missing from tag source inventory")
    allowed = {"source-manifest.json", "DEPENDENCY-NOTICE.txt", "SHA256SUMS"} | documents
    compare_member_sets(before, after, allowed, archive_name)
    old = parse_source_manifest(before, archive_name, version, candidate_sha, candidate_tree)
    new = parse_source_manifest(after, archive_name, version, tag_sha, tag_tree)
    matching_json_style(before["source-manifest.json"].data or b"",
                        after["source-manifest.json"].data or b"", "source-manifest.json")
    expected = copy.deepcopy(old)
    expected["source_ref"] = tag_sha
    expected["resolved_commit"] = tag_sha
    expected["tree"] = tag_tree
    require(old["files"]["DEPENDENCY-NOTICE.txt"]["size"] ==
            new["files"]["DEPENDENCY-NOTICE.txt"]["size"],
            "source notice identity member size changed")
    expected["files"]["DEPENDENCY-NOTICE.txt"] = new["files"]["DEPENDENCY-NOTICE.txt"]
    for name in documents:
        expected["files"][name] = new["files"][name]
    require(expected == new, "source manifest changed outside approved identity/document")
    require(before["DEPENDENCY-NOTICE.txt"].data is not None and
            after["DEPENDENCY-NOTICE.txt"].data is not None,
            "source dependency notice is missing")
    old_notice = dict(item for item in notice_lines(
        before["DEPENDENCY-NOTICE.txt"].data or b"", archive_name) if item[0] is not None)
    new_notice = dict(item for item in notice_lines(
        after["DEPENDENCY-NOTICE.txt"].data or b"", archive_name) if item[0] is not None)
    require({key: old_notice.get(key) for key in
             ("source.ref", "source.resolved_commit", "source.tree")} ==
            {"source.ref": candidate_sha, "source.resolved_commit": candidate_sha,
             "source.tree": candidate_tree} and
            {key: new_notice.get(key) for key in
             ("source.ref", "source.resolved_commit", "source.tree")} ==
            {"source.ref": tag_sha, "source.resolved_commit": tag_sha,
             "source.tree": tag_tree},
            "source dependency notice identity mismatch")
    compare_notice(before["DEPENDENCY-NOTICE.txt"].data or b"",
                   after["DEPENDENCY-NOTICE.txt"].data or b"",
                   {"source.ref": tag_sha, "source.resolved_commit": tag_sha,
                    "source.tree": tag_tree}, f"{archive_name}:DEPENDENCY-NOTICE.txt")
    changed = {name for name in before if before[name].digest != after[name].digest}
    require(changed.issubset(allowed), "source archive changed outside allowlist")


def verify_release(candidate_dir: Path, reproduction_dir: Path, tag_dir: Path,
                   *, tag: str, candidate_sha: str, tag_sha: str,
                   source_root: Path) -> dict[str, object]:
    require(tag.startswith("v") and VERSION.fullmatch(tag[1:]) is not None,
            "tag must be strict vX.Y.Z")
    version = tag[1:]
    candidate_tree, tag_tree = verify_git_identity(source_root, version, candidate_sha, tag_sha)
    _, candidate = verify_bundle(candidate_dir, version)
    _, reproduction = verify_bundle(reproduction_dir, version)
    _, tagged = verify_bundle(tag_dir, version)
    candidate_sums = (candidate_dir / "SHA256SUMS").read_bytes()
    reproduction_sums = (reproduction_dir / "SHA256SUMS").read_bytes()
    require(candidate_sums == reproduction_sums,
            "independent candidate outer SHA256SUMS differ byte-for-byte")
    for name in expected_assets(version):
        with (candidate_dir / name).open("rb") as left, (reproduction_dir / name).open("rb") as right:
            while True:
                a, b = left.read(CHUNK_BYTES), right.read(CHUNK_BYTES)
                require(a == b, f"independent candidate archive differs: {name}")
                if not a:
                    break
    source_name = f"siano-ts-{version}-source.tar.gz"
    for name in expected_assets(version):
        if name == source_name:
            verify_source_pair(candidate[name], tagged[name], name, version,
                               candidate_sha, tag_sha, candidate_tree, tag_tree, tag)
        else:
            verify_binary_pair(candidate[name], tagged[name], name, candidate_sha, tag_sha)
    return {"tag": tag, "candidate_sha": candidate_sha, "tag_sha": tag_sha,
            "candidate_tree": candidate_tree, "tag_tree": tag_tree,
            "candidate_sha256sums_sha256": hashlib.sha256(candidate_sums).hexdigest(),
            "tag_sha256sums_sha256": hashlib.sha256((tag_dir / "SHA256SUMS").read_bytes()).hexdigest(),
            "assets": list(expected_assets(version))}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-dir", type=Path, required=True)
    parser.add_argument("--reproduction-dir", type=Path, required=True)
    parser.add_argument("--tag-dir", type=Path, required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--candidate-sha", required=True)
    parser.add_argument("--tag-sha", required=True)
    parser.add_argument("--source-root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        result = verify_release(args.candidate_dir, args.reproduction_dir, args.tag_dir,
                                tag=args.tag, candidate_sha=args.candidate_sha,
                                tag_sha=args.tag_sha, source_root=args.source_root)
    except VerificationError as error:
        print(f"release verification: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
