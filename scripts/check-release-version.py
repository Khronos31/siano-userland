#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject release candidates that reuse a version tag for different source."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys


def check_version_tag(tag_commit: str | None, source_commit: str) -> str:
    if tag_commit is None:
        return "unused"
    if tag_commit.lower() == source_commit.lower():
        return "same-commit"
    raise ValueError(f"version tag already points to {tag_commit}, source is {source_commit}")


def resolve_tag_commit(tag_ref: str) -> str | None:
    exists = subprocess.run(
        ["git", "show-ref", "--verify", "--quiet", tag_ref], check=False
    )
    if exists.returncode == 1:
        return None
    if exists.returncode != 0:
        raise RuntimeError(f"git show-ref failed for {tag_ref}")
    result = subprocess.run(
        ["git", "rev-parse", "--verify", f"{tag_ref}^{{commit}}"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def self_test() -> None:
    source = "a" * 40
    other = "b" * 40
    assert check_version_tag(None, source) == "unused"
    assert check_version_tag(source, source) == "same-commit"
    try:
        check_version_tag(other, source)
    except ValueError:
        pass
    else:
        raise AssertionError("different-commit tag was accepted")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--version")
    parser.add_argument("--source-commit")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("release version guard self-test: PASS")
        return 0
    if not args.version or not args.source_commit:
        parser.error("--version and --source-commit are required")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("VERSION must use numeric major.minor.patch form")
    if not re.fullmatch(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})", args.source_commit):
        parser.error("source commit must be a full hexadecimal object id")
    tag_ref = f"refs/tags/v{args.version}"
    try:
        tag_commit = resolve_tag_commit(tag_ref)
        result = check_version_tag(tag_commit, args.source_commit)
    except (OSError, subprocess.CalledProcessError, RuntimeError, ValueError) as error:
        print(f"release version guard: {error}", file=sys.stderr)
        return 1
    if result == "unused":
        print(f"release version guard: v{args.version} is unused")
    else:
        print(f"release version guard: v{args.version} resolves to source commit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
