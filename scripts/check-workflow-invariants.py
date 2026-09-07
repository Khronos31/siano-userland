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
WINDOWS_CI_JOB_ID = "windows"


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


def check_windows_ci(text: str) -> list[str]:
    errors: list[str] = []
    try:
        job = workflow_job(text, WINDOWS_CI_JOB_ID)
    except ValueError as error:
        return [f"ci.yml: {error}"]
    if "runs-on: windows-2022" not in job:
        errors.append("ci.yml: Windows job must use windows-2022")
    if "nmake /f Makefile.win" not in job:
        errors.append("ci.yml: Windows job must compile Makefile.win")
    if "libusb-1.0.30.7z" not in job:
        errors.append("ci.yml: Windows job must fetch pinned libusb 1.0.30 package")
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
        if "alpine:3.22" not in block:
            errors.append(f"{path}: Alpine build container {index} is not pinned to alpine:3.22")
    return errors


def main() -> int:
    errors: list[str] = []
    for workflow in EXPECTED_BUILD_CONTAINERS:
        errors.extend(check_workflow(ROOT / ".github/workflows" / workflow))
    errors.extend(check_windows_ci((ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")))
    if errors:
        for error in errors:
            print(f"workflow invariant: {error}", file=sys.stderr)
        return 1
    print("workflow invariant: Alpine Linux headers and Windows CI build are present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
