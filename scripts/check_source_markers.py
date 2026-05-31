#!/usr/bin/env python3
"""Reject unresolved issue markers in first-party SDK source paths."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

DEFAULT_ROOTS = [
    "sdk",
    "examples",
    "templates",
    "tests",
]

SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".cmake",
    ".txt",
}

FORBIDDEN_MARKERS = re.compile(r"\b(FIXME|HACK|XXX)\b")


def fail(message: str) -> None:
    raise SystemExit(f"source marker check failed: {message}")


def iter_source_files(source: Path, roots: list[str]) -> list[Path]:
    files: list[Path] = []
    for root_name in roots:
        root = source / root_name
        if not root.exists():
            continue
        if root.is_file():
            candidates = [root]
        else:
            candidates = [path for path in root.rglob("*") if path.is_file()]
        for path in candidates:
            if path.suffix.lower() in SOURCE_SUFFIXES:
                files.append(path)
    return sorted(files)


def check_file(path: Path, source: Path) -> list[str]:
    findings: list[str] = []
    text = path.read_text(encoding="utf-8", errors="replace")
    relative = path.relative_to(source).as_posix()
    for line_number, line in enumerate(text.splitlines(), start=1):
        match = FORBIDDEN_MARKERS.search(line)
        if match:
            findings.append(f"{relative}:{line_number}: {match.group(1)}")
    return findings


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository source root")
    parser.add_argument(
        "--root",
        action="append",
        dest="roots",
        help="first-party path to scan; may be supplied multiple times",
    )
    args = parser.parse_args()

    source = Path(args.source).resolve()
    roots = args.roots or DEFAULT_ROOTS
    findings: list[str] = []
    for path in iter_source_files(source, roots):
        findings.extend(check_file(path, source))

    if findings:
        fail("unresolved marker(s):\n" + "\n".join(findings))
    print(f"source marker check passed ({len(iter_source_files(source, roots))} files)")


if __name__ == "__main__":
    main()
