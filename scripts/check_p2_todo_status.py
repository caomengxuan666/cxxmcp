#!/usr/bin/env python3
"""Ensure P2 local work is closed except documented external maturity gates."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ALLOWED_UNCHECKED_P2_PREFIXES = (
    "Accumulate maturity evidence before resubmitting to the vcpkg curated",
    "Resubmit to the vcpkg curated registry only after the maturity evidence is",
)

REQUIRED_EVIDENCE_DOCS = (
    Path("docs/examples.md"),
    Path("docs/runtime_gateway.md"),
    Path("docs/http_transport_backend_evidence.md"),
    Path("docs/ecosystem_maturity_evidence.md"),
)

README_FIRST_SCREEN_MARKERS = {
    Path("README.md"): "## Capability Snapshot",
    Path("README_zh.md"): "## 能力快照",
}

README_FIRST_SCREEN_FORBIDDEN = re.compile(r"\b(runtime|gateway|cli)\b", re.I)


def fail(message: str) -> None:
    raise SystemExit(f"P2 TODO status check failed: {message}")


def read_text(source: Path, relative: Path) -> str:
    path = source / relative
    if not path.is_file():
        fail(f"missing required evidence file: {relative}")
    return path.read_text(encoding="utf-8")


def check_required_evidence_docs(source: Path) -> None:
    for relative in REQUIRED_EVIDENCE_DOCS:
        read_text(source, relative)

    todo_text = read_text(source, Path("todo.md"))
    for relative in REQUIRED_EVIDENCE_DOCS:
        display = relative.as_posix()
        if display not in todo_text and f"docs\\{relative.name}" not in todo_text:
            fail(f"todo.md does not reference required evidence doc: {display}")


def check_readme_first_screen(source: Path) -> None:
    for relative, marker in README_FIRST_SCREEN_MARKERS.items():
        text = read_text(source, relative)
        marker_index = text.find(marker)
        if marker_index == -1:
            fail(f"{relative} is missing first-screen marker {marker!r}")
        first_screen = text[:marker_index]
        match = README_FIRST_SCREEN_FORBIDDEN.search(first_screen)
        if match:
            line = first_screen[: match.start()].count("\n") + 1
            fail(
                f"{relative}:{line} mentions {match.group(0)!r} before "
                f"{marker!r}"
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository root")
    args = parser.parse_args()

    source = Path(args.source).resolve()
    check_required_evidence_docs(source)
    check_readme_first_screen(source)

    todo = source / "todo.md"
    lines = todo.read_text(encoding="utf-8").splitlines()

    in_p2 = False
    failures: list[str] = []
    allowed_seen: set[str] = set()

    for number, line in enumerate(lines, start=1):
        if line.startswith("## "):
            in_p2 = line.startswith("## P2:")
            continue
        if not in_p2 or not line.startswith("- [ ] "):
            continue

        item = line[len("- [ ] ") :]
        allowed = False
        for prefix in ALLOWED_UNCHECKED_P2_PREFIXES:
            if item.startswith(prefix):
                allowed = True
                allowed_seen.add(prefix)
                break
        if not allowed:
            failures.append(f"{number}: {line}")

    if failures:
        fail("unexpected unchecked P2 item(s):\n" + "\n".join(failures))

    missing = set(ALLOWED_UNCHECKED_P2_PREFIXES) - allowed_seen
    if missing:
        fail(
            "expected external P2 maturity gate(s) to remain explicit: "
            + ", ".join(sorted(missing))
        )


if __name__ == "__main__":
    main()
