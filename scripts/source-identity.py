#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
from __future__ import annotations

import json
from pathlib import Path
import re
import sys


def resolved_commit(manifest_path: Path) -> str:
    try:
        with manifest_path.open(encoding="utf-8") as handle:
            manifest = json.load(handle)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read source manifest: {error}") from error
    if not isinstance(manifest, dict):
        raise ValueError("source manifest identity is missing")
    source_ref = manifest.get("source_ref")
    commit = manifest.get("resolved_commit")
    tree = manifest.get("tree")
    if not isinstance(source_ref, str) or not source_ref.strip():
        raise ValueError("source manifest source_ref is missing")
    if not isinstance(commit, str) or not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", commit):
        raise ValueError("source manifest resolved_commit must be a full lowercase commit ID")
    if not isinstance(tree, str) or not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", tree):
        raise ValueError("source manifest tree must be a full lowercase tree ID")
    return commit


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: source-identity.py SOURCE-MANIFEST", file=sys.stderr)
        raise SystemExit(2)
    try:
        print(resolved_commit(Path(sys.argv[1])))
    except ValueError as error:
        print(f"source-identity: {error}", file=sys.stderr)
        raise SystemExit(1)
