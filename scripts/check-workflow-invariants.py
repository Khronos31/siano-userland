#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check invariants for release workflows and Linux build environments."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_BUILD_CONTAINERS = {"ci.yml": 2, "release.yml": 4}
STATIC_JOB_IDS = {"ci.yml": "linux-static-x86_64", "release.yml": "linux-static"}
WINDOWS_JOB_ID = "windows"
CI_PACKAGING_JOB_ID = "package-linux"
CI_RUNTIME_JOB_IDS = {"packaged-runtime-x86_64", "packaged-runtime-aarch64"}
RC_RUNTIME_JOB_IDS = {"packaged-runtime-x86_64", "packaged-runtime-aarch64"}


def workflow_job(text: str, job_id: str) -> str:
    match = re.search(
        rf"(?ms)^  {re.escape(job_id)}:\n.*?(?=^  [^ \n][^:]*:|\Z)", text
    )
    if match is None:
        raise ValueError(f"missing workflow job: {job_id}")
    return match.group(0)


def build_container_blocks(text: str) -> list[str]:
    starts = list(re.finditer(r"(?m)^[ \t]+docker run --rm[^\n]*", text))
    blocks: list[str] = []
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        blocks.append(text[match.start():end])
    return blocks


def check_windows(text: str, path: Path) -> list[str]:
    errors: list[str] = []
    try:
        job = workflow_job(text, WINDOWS_JOB_ID)
    except ValueError as error:
        return [f"{path}: {error}"]
    if "runs-on: windows-2022" not in job:
        errors.append(f"{path}: Windows job must use windows-2022")
    if "reproducible-windows-build.ps1" not in job:
        errors.append(f"{path}: Windows job must run the reproducibility helper")
    if "siano-ts.exe --help" not in job:
        errors.append(f"{path}: Windows job must smoke-test the second build")
    if "audit-artifact.py --platform windows-x64" not in job:
        errors.append(f"{path}: Windows job must audit the second build")
    if "uses: actions/upload-artifact@" not in job or "name: bin-windows-x64" not in job:
        errors.append(f"{path}: Windows job must upload the audited binary artifact")
    helper_at = job.find("reproducible-windows-build.ps1")
    help_at = job.find("siano-ts.exe --help")
    audit_at = job.find("audit-artifact.py --platform windows-x64")
    upload_at = job.find("uses: actions/upload-artifact@")
    if not (0 <= helper_at < help_at < audit_at < upload_at):
        errors.append(f"{path}: Windows reproducibility, smoke test, audit, and upload must be ordered")
    if "libusb-1.0.30.7z" not in job:
        errors.append(f"{path}: Windows job must fetch pinned libusb 1.0.30 package")
    return errors


def check_windows_makefile(text: str) -> list[str]:
    errors: list[str] = []
    if not re.search(r"(?m)^CFLAGS\s*=.*\s/Brepro(?:\s|$)", text):
        errors.append("Makefile.win: CFLAGS must enable /Brepro")
    if not re.search(r"(?m)^\s*\$\(CC\).*?/link\s+/Brepro(?:\s|$)", text):
        errors.append("Makefile.win: link command must enable /Brepro")
    return errors


def check_macos_strip(text: str, path: Path) -> list[str]:
    errors: list[str] = []
    try:
        job = workflow_job(text, "macos")
    except ValueError as error:
        return [f"{path}: {error}"]
    if "xcrun --find strip" not in job or "-S -x ./siano-ts" not in job:
        errors.append(f"{path}: macOS job must use Apple strip -S -x")
    if "-N ./siano-ts" in job:
        errors.append(f"{path}: macOS job must not use Apple strip -N")
    if "./siano-ts --help" not in job:
        errors.append(f"{path}: macOS job must smoke-test the stripped binary")
    if ("command -v otool" not in job or "command -v lipo" not in job or
            "command -v nm" not in job):
        errors.append(f"{path}: macOS job must verify native otool/lipo/nm availability")
    return errors


def check_packaged_runtime(text: str, path: Path, job_id: str, artifact_name: str) -> list[str]:
    errors: list[str] = []
    try:
        job = workflow_job(text, job_id)
    except ValueError as error:
        return [f"{path}: {error}"]
    if "actions/download-artifact@" not in job or f"name: {artifact_name}" not in job:
        errors.append(f"{path}: {job_id} must download {artifact_name}")
    checkout_at = job.find("actions/checkout@")
    download_at = job.find("actions/download-artifact@")
    if checkout_at < 0 or download_at < 0 or checkout_at > download_at:
        errors.append(f"{path}: {job_id} must checkout before downloading its archive")
    if "scripts/test-packaged-linux.sh" not in job:
        errors.append(f"{path}: {job_id} must use the final-archive runtime helper")
    if "Ubuntu glibc" not in job:
        errors.append(f"{path}: {job_id} lacks Ubuntu glibc archive smoke")
    if "Alpine musl" not in job:
        errors.append(f"{path}: {job_id} lacks Alpine musl archive smoke")
    if "alpine:3.22" not in job:
        errors.append(f"{path}: {job_id} must pin Alpine runtime to alpine:3.22")
    if job_id.endswith("aarch64"):
        if "runs-on: ubuntu-24.04-arm" not in job:
            errors.append(f"{path}: {job_id} must use the native arm64 runner")
        if "native arm64" not in job:
            errors.append(f"{path}: {job_id} must identify native arm64 runtime smoke")
        if "qemu" in job.lower():
            errors.append(f"{path}: {job_id} must not use QEMU")
    else:
        if "runs-on: ubuntu-24.04" not in job:
            errors.append(f"{path}: {job_id} must use the x86_64 Ubuntu runner")
    if "HAOS" in job or "hardware" in job.lower():
        errors.append(f"{path}: {job_id} must not claim HAOS hardware validation")
    return errors


def check_ci_packaging(text: str, path: Path) -> list[str]:
    errors: list[str] = []
    try:
        job = workflow_job(text, CI_PACKAGING_JOB_ID)
    except ValueError as error:
        return [f"{path}: {error}"]
    if "pattern: bin-linux-*" not in job:
        errors.append(f"{path}: {CI_PACKAGING_JOB_ID} must download both Linux build artifacts")
    checkout_at = job.find("actions/checkout@")
    download_at = job.find("actions/download-artifact@")
    if checkout_at < 0 or download_at < 0 or checkout_at > download_at:
        errors.append(f"{path}: {CI_PACKAGING_JOB_ID} must checkout before downloading build artifacts")
    if job.count("scripts/package-artifact.sh --platform linux-") != 2:
        errors.append(f"{path}: {CI_PACKAGING_JOB_ID} must assemble both Linux archives")
    if "apt-get install" not in job or "binutils" not in job:
        errors.append(f"{path}: {CI_PACKAGING_JOB_ID} lacks archive audit binutils")
    if "name: packaged-linux" not in job:
        errors.append(f"{path}: {CI_PACKAGING_JOB_ID} must upload packaged-linux")
    return errors


def check_android_packageable(ci: str, release: str, audit: str, package: str) -> list[str]:
    errors: list[str] = []
    try:
        ci_job = workflow_job(ci, "android")
    except ValueError as error:
        return [f"ci.yml: {error}"]
    try:
        release_job = workflow_job(release, "android")
    except ValueError as error:
        return [f"release.yml: {error}"]
    try:
        package_job = workflow_job(release, "package")
    except ValueError as error:
        return [f"release.yml: {error}"]

    if not re.search(r"(?ms)^\s*- abi: x86_64\s*\n\s*packageable: true\s*$", ci_job):
        errors.append("ci.yml: Android x86_64 matrix entry must be packageable")
    if "scripts/build-android.sh" not in ci_job or \
            "scripts/audit-artifact.sh --platform android-${{ matrix.abi }}" not in ci_job:
        errors.append("ci.yml: Android matrix must build and run the full artifact audit")
    upload_at = ci_job.find("uses: actions/upload-artifact@")
    if upload_at < 0 or "if: matrix.packageable" not in ci_job[max(0, upload_at - 160):upload_at]:
        errors.append("ci.yml: Android artifact upload must be conditional on packageable")
    if "packageable: false" in ci_job:
        errors.append("ci.yml: Android matrix must not retain a build-only ABI")

    if not re.search(r"abi:\s*\[aarch64, armv7a, x86_64\]", release_job):
        errors.append("release.yml: Android release matrix must include all three ABIs")
    if "scripts/build-android.sh" not in release_job or \
            "scripts/audit-artifact.sh --platform android-${{ matrix.abi }}" not in release_job:
        errors.append("release.yml: Android release matrix must build and run the full artifact audit")
    release_upload_at = release_job.find("uses: actions/upload-artifact@")
    if release_upload_at < 0 or "name: bin-android-${{ matrix.abi }}" not in release_job[release_upload_at:]:
        errors.append("release.yml: Android release matrix must upload each audited artifact")

    if "android" not in package_job.split("needs:", 1)[-1].split("runs-on:", 1)[0]:
        errors.append("release.yml: package job must depend on the Android artifact job")
    if "actions/download-artifact@" not in package_job or "pattern: bin-*" not in package_job:
        errors.append("release.yml: package job must download the Android build artifacts")
    if package_job.count("scripts/package-artifact.sh --platform android-") != 3:
        errors.append("release.yml: package job must assemble all three Android archives")
    if "scripts/package-artifact.sh --platform android-x86_64" not in package_job:
        errors.append("release.yml: package job must assemble android-x86_64")
    if "siano-ts-$version-android-x86_64.tar.gz" not in package_job:
        errors.append("release.yml: expected asset list must include android-x86_64")
    if "-eq 8" not in package_job or "wc -l < candidate/SHA256SUMS)\" -eq 8" not in package_job:
        errors.append("release.yml: release candidate and SHA256SUMS must each contain eight assets")
    if "scripts/audit-artifact.sh --source-archive" not in package_job or "cmp -s" not in package_job:
        errors.append("release.yml: package job must audit source and compare deterministic archives")
    if "android-x86_64" not in audit:
        errors.append("audit-artifact.py: Android x86_64 mapping/package support is missing")
    if "android-x86_64" not in package:
        errors.append("package-artifact.py: Android x86_64 metadata/package support is missing")
    return errors


def check_workflow(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    errors: list[str] = []
    job_id = STATIC_JOB_IDS[path.name]
    try:
        job = workflow_job(text, job_id)
    except ValueError as error:
        return [f"{path}: {error}"]
    if "container: alpine:3.22" in job:
        errors.append(f"{path}: {job_id} must not use a job-level Alpine container")

    blocks = [block for block in build_container_blocks(text)
              if ("scripts/build-linux-static.sh" in block or
                  "test-static-relink.sh" in block)]
    expected = EXPECTED_BUILD_CONTAINERS[path.name]
    if len(blocks) != expected:
        errors.append(f"{path}: expected {expected} Alpine build containers, found {len(blocks)}")
    for index, block in enumerate(blocks, start=1):
        apk_lines = re.findall(r"(?m)^\s*apk add[^\n]*", block)
        if not any("linux-headers" in line.split() for line in apk_lines):
            errors.append(f"{path}: Alpine build container {index} lacks linux-headers")
        if not any("binutils" in line.split() for line in apk_lines):
            errors.append(f"{path}: Alpine build container {index} lacks binutils (readelf/strip)")
        if "alpine:3.22" not in block:
            errors.append(f"{path}: Alpine build container {index} is not pinned to alpine:3.22")
    return errors


def main() -> int:
    errors: list[str] = []
    for workflow in EXPECTED_BUILD_CONTAINERS:
        errors.extend(check_workflow(ROOT / ".github/workflows" / workflow))
    errors.extend(check_windows_makefile((ROOT / "Makefile.win").read_text(encoding="utf-8")))
    ci = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    errors.extend(check_windows(ci, ROOT / ".github/workflows/ci.yml"))
    errors.extend(check_macos_strip(ci, ROOT / ".github/workflows/ci.yml"))
    errors.extend(check_ci_packaging(ci, ROOT / ".github/workflows/ci.yml"))
    for job_id in CI_RUNTIME_JOB_IDS:
        errors.extend(check_packaged_runtime(ci, ROOT / ".github/workflows/ci.yml", job_id, "packaged-linux"))
    release = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
    errors.extend(check_windows(release, ROOT / ".github/workflows/release.yml"))
    errors.extend(check_android_packageable(
        ci, release,
        (ROOT / "scripts/audit-artifact.py").read_text(encoding="utf-8"),
        (ROOT / "scripts/package-artifact.py").read_text(encoding="utf-8")))
    errors.extend(check_macos_strip(release, ROOT / ".github/workflows/release.yml"))
    for job_id in RC_RUNTIME_JOB_IDS:
        errors.extend(check_packaged_runtime(release, ROOT / ".github/workflows/release.yml", job_id, "release-candidate"))
    if errors:
        for error in errors:
            print(f"workflow invariant: {error}", file=sys.stderr)
        return 1
    print("workflow invariant: Linux build, final archive runtime, and Windows CI checks are present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
