#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read and validate the two release-candidate workflow run IDs in the record."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


class RunError(ValueError):
    """A candidate workflow run cannot be used for release verification."""


RUN_ID = re.compile(r"[1-9][0-9]*\Z")
SHA = re.compile(r"[0-9a-f]{40}\Z")
RUN_FIELDS = {"databaseId", "workflowName", "event", "status", "conclusion", "headSha"}
WORKFLOW_NAME = "Release candidate"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RunError(message)


def read_run_ids(document: Path) -> tuple[int, int]:
    try:
        lines = document.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise RunError(f"cannot read version result document: {error}") from error
    values: dict[str, list[str]] = {"candidate_run_id": [], "reproduction_run_id": []}
    for line in lines:
        for key in values:
            if line.startswith(key + ":"):
                values[key].append(line.split(":", 1)[1].strip())
    result = []
    for key in values:
        require(len(values[key]) == 1, f"{key} must appear exactly once")
        raw = values[key][0]
        require(RUN_ID.fullmatch(raw) is not None, f"{key} is unset or invalid")
        result.append(int(raw))
    require(result[0] != result[1], "candidate and reproduction run IDs must differ")
    return result[0], result[1]


def parse_run(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, ValueError) as error:
        raise RunError(f"invalid workflow run metadata: {error}") from error
    require(isinstance(value, dict) and set(value) == RUN_FIELDS,
            "workflow run metadata fields are incomplete or unexpected")
    return value


def verify_run(value: dict, expected_id: int, candidate_sha: str) -> None:
    require(SHA.fullmatch(candidate_sha) is not None,
            "candidate SHA must be canonical lowercase 40-character hex")
    require(type(value.get("databaseId")) is int and value["databaseId"] == expected_id,
            "workflow run ID does not match the version result document")
    require(value.get("workflowName") == WORKFLOW_NAME and
            value.get("event") == "workflow_dispatch" and
            value.get("status") == "completed" and value.get("conclusion") == "success" and
            value.get("headSha") == candidate_sha,
            "workflow/artifact run is not a successful candidate for the fixed source SHA")


def verify_pair(candidate: dict, reproduction: dict,
                candidate_id: int, reproduction_id: int) -> str:
    candidate_sha = candidate.get("headSha")
    require(isinstance(candidate_sha, str) and SHA.fullmatch(candidate_sha) is not None,
            "candidate run head SHA must be canonical lowercase 40-character hex")
    verify_run(candidate, candidate_id, candidate_sha)
    verify_run(reproduction, reproduction_id, candidate_sha)
    return candidate_sha


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    read = subparsers.add_parser("read")
    read.add_argument("--document", type=Path, required=True)
    verify = subparsers.add_parser("verify")
    verify.add_argument("--document", type=Path, required=True)
    verify.add_argument("--candidate-run", type=Path, required=True)
    verify.add_argument("--reproduction-run", type=Path, required=True)
    args = parser.parse_args()
    try:
        candidate_id, reproduction_id = read_run_ids(args.document)
        if args.command == "verify":
            candidate_sha = verify_pair(parse_run(args.candidate_run),
                                        parse_run(args.reproduction_run),
                                        candidate_id, reproduction_id)
            print(f"{candidate_id} {reproduction_id} {candidate_sha}")
        else:
            print(f"{candidate_id} {reproduction_id}")
    except RunError as error:
        print(f"release candidate runs: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
