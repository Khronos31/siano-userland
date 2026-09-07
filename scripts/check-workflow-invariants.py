#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check invariants for Alpine environments that rebuild the Linux binary."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_BUILD_CONTAINERS = {"ci.yml": 2, "release.yml": 4}
STATIC_JOB_IDS = {"ci.yml": "linux-static-x86_64", "release.yml": "linux-static"}


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
    if errors:
        for error in errors:
            print(f"workflow invariant: {error}", file=sys.stderr)
        return 1
    print("workflow invariant: Alpine Linux build containers include linux-headers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
