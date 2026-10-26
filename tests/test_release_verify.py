import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release_verify


VERSION = "1.0.0"
TAG = "v1.0.0"
DOC = "repository/docs/release-validation/1.0.0.md"


def git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=5).stdout.decode("ascii").strip()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(value, newline=False):
    data = json.dumps(value, indent=2, sort_keys=True).encode("utf-8")
    return data + (b"\n" if newline else b"")


def checksums(payloads):
    return "".join(f"{digest(data)}  {name}\n" for name, data in sorted(payloads.items())).encode()


def binary_archive(version, platform, source_sha, *, firmware=b"fixed firmware", modes=None,
                   extra=None, evidence_extra=None):
    windows = platform == "windows-x64"
    programs = ["sianod.exe", "sianoctl.exe", "siano-ts.exe"] if windows else [
        "sianod", "sianoctl", "siano-ts"]
    payloads = {
        "README.md": b"candidate README\n",
        "DEPENDENCY-NOTICE.txt": f"source.ref={source_sha}\nsource.archive=siano-ts-{version}-source.tar.gz\n".encode(),
        "firmware/isdbt_rio.inp": firmware,
    }
    for program in programs:
        payloads[program] = ("binary " + program).encode()
    for path, program in zip(release_verify.EVIDENCE, programs):
        data = {"schema": 2, "platform": platform, "source_ref": source_sha,
                "binary": {"name": program, "sha256": digest(payloads[program])}}
        if evidence_extra and path == release_verify.EVIDENCE[0]:
            data["unexpected"] = "field"
        payloads[path] = write_json(data, newline=True)
    if extra:
        payloads.update(extra)
    manifest = {"schema": 1, "version": version, "platform": platform,
                "source_ref": source_sha, "programs": programs, "files": {}}
    for name, data in payloads.items():
        manifest["files"][name] = {"size": len(data), "sha256": digest(data)}
    payloads["manifest.json"] = write_json(manifest)
    payloads["SHA256SUMS"] = checksums(payloads)
    output = io.BytesIO()
    if windows:
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(payloads.items()):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | (modes or {}).get(name, 0o644)) << 16
                archive.writestr(info, data)
    else:
        with gzip.GzipFile(fileobj=output, mode="wb", filename="", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as archive:
                for name, data in sorted(payloads.items()):
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    info.mode = (modes or {}).get(name, 0o644)
                    info.uid = info.gid = 0
                    info.mtime = 0
                    archive.addfile(info, io.BytesIO(data))
    return output.getvalue()


def source_archive(version, source_sha, tree, doc_data, *, extra=None, modes=None):
    payloads = {
        "repository/VERSION": (version + "\n").encode(),
        DOC: doc_data,
        "repository/README.md": b"stable source readme\n",
        "README.md": b"stable archive readme\n",
        "BUILD-RELINK.md": b"stable rebuild instructions\n",
        "DEPENDENCY-NOTICE.txt": (
            f"source.ref={source_sha}\nsource.resolved_commit={source_sha}\n"
            f"source.tree={tree}\ncorresponding-source=third_party/libusb-1.0.30.tar.bz2\n"
        ).encode(),
    }
    if extra:
        payloads.update(extra)
    manifest = {"schema": 1, "version": version, "source_ref": source_sha,
                "resolved_commit": source_sha, "tree": tree, "files": {}}
    for name, data in payloads.items():
        manifest["files"][name] = {"size": len(data), "sha256": digest(data)}
    payloads["source-manifest.json"] = write_json(manifest)
    payloads["SHA256SUMS"] = checksums(payloads)
    output = io.BytesIO()
    with gzip.GzipFile(fileobj=output, mode="wb", filename="", mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as archive:
            for name, data in sorted(payloads.items()):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = (modes or {}).get(name, 0o644)
                info.uid = info.gid = 0
                info.mtime = 0
                archive.addfile(info, io.BytesIO(data))
    return output.getvalue()


def make_bundle(directory, version, source_sha, tree, doc_data, *, firmware=b"fixed firmware",
                extra=None, evidence_extra=False, modes=None):
    directory.mkdir()
    assets = {}
    for platform in release_verify.PLATFORMS:
        name = f"siano-ts-{version}-{platform}." + ("zip" if platform == "windows-x64" else "tar.gz")
        assets[name] = binary_archive(version, platform, source_sha, firmware=firmware,
                                      extra=extra if platform == "linux-x86_64" else None,
                                      evidence_extra=evidence_extra and platform == "linux-x86_64",
                                      modes=modes if platform == "linux-x86_64" else None)
    source_name = f"siano-ts-{version}-source.tar.gz"
    assets[source_name] = source_archive(version, source_sha, tree, doc_data)
    for name, data in assets.items():
        (directory / name).write_bytes(data)
    (directory / "SHA256SUMS").write_bytes(checksums(assets))


class ReleaseVerifyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        git(self.repo, "init", "-q")
        git(self.repo, "config", "user.name", "Fixture")
        git(self.repo, "config", "user.email", "fixture@example.invalid")
        (self.repo / "VERSION").write_text(VERSION + "\n", encoding="ascii")
        result_doc = self.repo / "docs/release-validation/1.0.0.md"
        result_doc.parent.mkdir(parents=True)
        result_doc.write_text("candidate record\n", encoding="utf-8")
        git(self.repo, "add", "VERSION", "docs/release-validation/1.0.0.md")
        git(self.repo, "commit", "-qm", "candidate")
        self.candidate_sha = git(self.repo, "rev-parse", "HEAD")
        self.candidate_tree = git(self.repo, "rev-parse", "HEAD^{tree}")
        self.candidate_doc = result_doc.read_bytes()
        result_doc.write_text("completed test record\n", encoding="utf-8")
        git(self.repo, "add", "docs/release-validation/1.0.0.md")
        git(self.repo, "commit", "-qm", "record result")
        self.tag_sha = git(self.repo, "rev-parse", "HEAD")
        self.tag_tree = git(self.repo, "rev-parse", "HEAD^{tree}")
        self.tag_doc = result_doc.read_bytes()
        self.candidate = self.root / "candidate"
        self.reproduction = self.root / "reproduction"
        self.tag = self.root / "tag"
        make_bundle(self.candidate, VERSION, self.candidate_sha, self.candidate_tree,
                    self.candidate_doc)
        shutil.copytree(self.candidate, self.reproduction)
        make_bundle(self.tag, VERSION, self.tag_sha, self.tag_tree, self.tag_doc)

    def tearDown(self):
        self.temporary.cleanup()

    def verify(self, candidate=None, reproduction=None, tag=None, **kwargs):
        return release_verify.verify_release(
            candidate or self.candidate, reproduction or self.reproduction,
            tag or self.tag, tag=TAG, candidate_sha=self.candidate_sha,
            tag_sha=self.tag_sha, source_root=self.repo, **kwargs)

    def copy_bundles(self):
        candidate = self.root / "candidate-copy"
        reproduction = self.root / "reproduction-copy"
        tag = self.root / "tag-copy"
        shutil.copytree(self.candidate, candidate)
        shutil.copytree(self.reproduction, reproduction)
        shutil.copytree(self.tag, tag)
        return candidate, reproduction, tag

    def mutate_bundle(self, directory, archive_name, mutator):
        path = directory / archive_name
        raw = read_archive_bytes(path)
        members = release_verify.read_archive(path)
        modes = {name: item.mode for name, item in members.items()}
        raw = mutator(raw)
        raw = rebuild_source(raw) if archive_name.endswith("-source.tar.gz") else rebuild_binary(raw)
        data = archive_from_raw(archive_name, raw, modes)
        path.write_bytes(data)
        assets = {item.name: item.read_bytes() for item in directory.iterdir()
                  if item.name != "SHA256SUMS"}
        (directory / "SHA256SUMS").write_bytes(checksums(assets))

    def test_approved_identity_and_version_document_pass(self):
        result = self.verify()
        self.assertEqual(result["candidate_sha"], self.candidate_sha)
        self.assertEqual(result["tag_sha"], self.tag_sha)
        self.assertEqual(len(result["assets"]), 8)

    def test_independent_candidate_byte_difference_fails(self):
        candidate, reproduction, tag = self.copy_bundles()
        name = release_verify.expected_assets(VERSION)[0]
        data = bytearray((reproduction / name).read_bytes())
        data[-1] ^= 1
        (reproduction / name).write_bytes(data)
        assets = {item.name: item.read_bytes() for item in reproduction.iterdir()
                  if item.name != "SHA256SUMS"}
        (reproduction / "SHA256SUMS").write_bytes(checksums(assets))
        with self.assertRaises(release_verify.VerificationError):
            self.verify(candidate, reproduction, tag)

    def test_binary_payload_difference_fails(self):
        candidate, reproduction, tag = self.copy_bundles()
        name = release_verify.expected_assets(VERSION)[0]
        self.mutate_bundle(tag, name, lambda raw: {**raw, "firmware/isdbt_rio.inp": b"changed"})
        with self.assertRaises(release_verify.VerificationError):
            self.verify(candidate, reproduction, tag)

    def test_executable_binary_difference_fails(self):
        candidate, reproduction, tag = self.copy_bundles()
        name = release_verify.expected_assets(VERSION)[0]
        self.mutate_bundle(tag, name, lambda raw: {**raw, "siano-ts": b"different binary"})
        with self.assertRaises(release_verify.VerificationError):
            self.verify(candidate, reproduction, tag)

    def test_unknown_evidence_field_fails(self):
        candidate, reproduction, tag = self.copy_bundles()
        name = release_verify.expected_assets(VERSION)[0]
        def add_field(raw):
            value = json.loads(raw[release_verify.EVIDENCE[0]])
            value["unexpected"] = "field"
            raw[release_verify.EVIDENCE[0]] = write_json(value, newline=True)
            return rebuild_binary(raw)
        self.mutate_bundle(tag, name, add_field)
        with self.assertRaises(release_verify.VerificationError):
            self.verify(candidate, reproduction, tag)

    def test_unknown_manifest_field_fails(self):
        candidate, reproduction, tag = self.copy_bundles()
        name = release_verify.expected_assets(VERSION)[0]
        def add_field(raw):
            value = json.loads(raw["manifest.json"])
            value["unexpected"] = "field"
            raw["manifest.json"] = write_json(value)
            return raw
        self.mutate_bundle(tag, name, add_field)
        with self.assertRaises(release_verify.VerificationError):
            self.verify(candidate, reproduction, tag)

    def test_non_field_notice_content_must_match(self):
        with self.assertRaises(release_verify.VerificationError):
            release_verify.compare_notice(
                b"source.ref=old\nannotation A\n",
                b"source.ref=new\nannotation B\n",
                {"source.ref": "new"}, "fixture notice")

    def test_member_mode_difference_fails(self):
        candidate, reproduction, tag = self.copy_bundles()
        name = release_verify.expected_assets(VERSION)[0]
        # Rebuild with a changed member mode while preserving valid file bytes.
        path = tag / name
        raw = read_archive_bytes(path)
        members = release_verify.read_archive(path)
        modes = {key: value.mode for key, value in members.items()}
        modes["firmware/isdbt_rio.inp"] = 0o600
        path.write_bytes(archive_from_raw(name, rebuild_binary(raw), modes))
        assets = {item.name: item.read_bytes() for item in tag.iterdir() if item.name != "SHA256SUMS"}
        (tag / "SHA256SUMS").write_bytes(checksums(assets))
        with self.assertRaises(release_verify.VerificationError):
            self.verify(candidate, reproduction, tag)

    def test_source_inventory_and_other_document_changes_fail(self):
        candidate, reproduction, tag = self.copy_bundles()
        name = f"siano-ts-{VERSION}-source.tar.gz"
        self.mutate_bundle(tag, name, lambda raw: {**raw, "repository/docs/helper.py": b"unexpected"})
        with self.assertRaises(release_verify.VerificationError):
            self.verify(candidate, reproduction, tag)

    def test_bundle_checksum_mismatch_fails(self):
        candidate, reproduction, tag = self.copy_bundles()
        sums = bytearray((tag / "SHA256SUMS").read_bytes())
        sums[0] = ord("0") if sums[0] != ord("0") else ord("1")
        (tag / "SHA256SUMS").write_bytes(sums)
        with self.assertRaises(release_verify.VerificationError):
            self.verify(candidate, reproduction, tag)

    def test_link_member_is_rejected_by_regular_file_archive_contract(self):
        candidate, reproduction, tag = self.copy_bundles()
        name = "siano-ts-1.0.0-windows-x64.zip"
        raw = read_archive_bytes(tag / name)
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for member, data in sorted(raw.items()):
                info = zipfile.ZipInfo(member, date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                mode = stat.S_IFLNK | 0o777 if member == "firmware/isdbt_rio.inp" else stat.S_IFREG | 0o644
                info.external_attr = mode << 16
                archive.writestr(info, b"../target" if member == "firmware/isdbt_rio.inp" else data)
        (tag / name).write_bytes(output.getvalue())
        assets = {item.name: item.read_bytes() for item in tag.iterdir()
                  if item.name != "SHA256SUMS"}
        (tag / "SHA256SUMS").write_bytes(checksums(assets))
        with self.assertRaises(release_verify.VerificationError):
            self.verify(candidate, reproduction, tag)

    def test_tag_commit_with_unapproved_path_fails(self):
        (self.repo / "README.md").write_text("changed\n", encoding="utf-8")
        git(self.repo, "add", "README.md")
        git(self.repo, "commit", "-qm", "unapproved")
        with self.assertRaises(release_verify.VerificationError):
            self.verify()

    def test_oversized_input_rejected_with_small_fixed_limit(self):
        path = self.root / "oversized"
        path.write_bytes(b"123456")
        original_limit = release_verify.MAX_ARCHIVE_BYTES
        try:
            release_verify.MAX_ARCHIVE_BYTES = 5
            with self.assertRaises(release_verify.VerificationError):
                release_verify.sha256(path)
        finally:
            release_verify.MAX_ARCHIVE_BYTES = original_limit

    def test_expanded_archive_size_rejected_with_small_fixed_limit(self):
        archive = self.tag / release_verify.expected_assets(VERSION)[0]
        original_limit = release_verify.MAX_TOTAL_BYTES
        try:
            release_verify.MAX_TOTAL_BYTES = 1
            with self.assertRaises(release_verify.VerificationError):
                release_verify.read_archive(archive)
        finally:
            release_verify.MAX_TOTAL_BYTES = original_limit


def rebuild_binary(raw):
    manifest = json.loads(raw["manifest.json"])
    for name in set(raw) - {"manifest.json", "SHA256SUMS"}:
        manifest["files"][name] = {"size": len(raw[name]), "sha256": digest(raw[name])}
    raw["manifest.json"] = write_json(manifest)
    raw["SHA256SUMS"] = checksums({name: data for name, data in raw.items()
                                   if name != "SHA256SUMS"})
    return raw


def rebuild_source(raw):
    manifest = json.loads(raw["source-manifest.json"])
    for name in set(raw) - {"source-manifest.json", "SHA256SUMS"}:
        manifest["files"][name] = {"size": len(raw[name]), "sha256": digest(raw[name])}
    manifest["files"] = {
        name: {"size": len(raw[name]), "sha256": digest(raw[name])}
        for name in set(raw) - {"source-manifest.json", "SHA256SUMS"}}
    raw["source-manifest.json"] = write_json(manifest)
    raw["SHA256SUMS"] = checksums({name: data for name, data in raw.items()
                                   if name != "SHA256SUMS"})
    return raw


def read_archive_bytes(path):
    if path.stat().st_size > 2 * 1024 * 1024:
        raise AssertionError("fixture archive exceeds the 2 MiB input limit")
    result = {}
    if path.name.endswith(".zip"):
        with zipfile.ZipFile(path) as archive:
            for item in archive.infolist():
                result[item.filename] = archive.read(item)
    else:
        with tarfile.open(path, "r:gz") as archive:
            for item in archive.getmembers():
                stream = archive.extractfile(item)
                result[item.name] = stream.read()
    return result


def archive_from_raw(name, raw, modes):
    output = io.BytesIO()
    if name.endswith(".zip"):
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for member, data in sorted(raw.items()):
                info = zipfile.ZipInfo(member, date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | modes.get(member, 0o644)) << 16
                archive.writestr(info, data)
    else:
        with gzip.GzipFile(fileobj=output, mode="wb", filename="", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as archive:
                for member, data in sorted(raw.items()):
                    info = tarfile.TarInfo(member)
                    info.size = len(data)
                    info.mode = modes.get(member, 0o644)
                    info.uid = info.gid = 0
                    info.mtime = 0
                    archive.addfile(info, io.BytesIO(data))
    return output.getvalue()


if __name__ == "__main__":
    unittest.main()
