#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract and classify static archive members from an Android LLD map."""

from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath
import re


ARCHIVE_MEMBER = re.compile(r"(?P<archive>[^\s()]+\.a)\((?P<member>[^()\r\n]+)\)")


def category(archive: str) -> str:
    name = PurePosixPath(archive).name
    if name == "libusb-1.0.a":
        return "libusb"
    if (name.startswith("libclang_rt.") or name in {
        "libc++_static.a", "libc++abi.a", "libunwind.a", "libgcc.a",
    }):
        return "ndk-runtime"
    return "other-static"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    text = args.map.read_text(encoding="utf-8", errors="strict")
    rows: set[tuple[str, str, str]] = set()
    for match in ARCHIVE_MEMBER.finditer(text):
        archive = match.group("archive")
        member = match.group("member").strip()
        if (not member or "\t" in member or "\\" in member or
                member.startswith("/") or ".." in PurePosixPath(member).parts):
            raise ValueError(f"invalid archive member in {args.map}")
        rows.add((category(archive), PurePosixPath(archive).name, member))
    if not rows:
        raise ValueError(f"no static archive members found in {args.map}")
    if not any(row[1] == "libusb-1.0.a" for row in rows):
        raise ValueError(f"libusb-1.0.a members missing from {args.map}")
    if any(row[0] == "other-static" for row in rows):
        unexpected = sorted({row[1] for row in rows if row[0] == "other-static"})
        raise ValueError(f"unclassified static archive in {args.map}: {unexpected}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        output.write("category\tarchive\tmember\n")
        for row in sorted(rows):
            output.write("\t".join(row) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
