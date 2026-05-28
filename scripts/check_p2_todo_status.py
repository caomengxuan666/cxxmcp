#!/usr/bin/env python3
"""Ensure local TODO work is closed except documented external evidence gates."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ALLOWED_UNCHECKED_PREFIXES = (
    "The SDK-first public surface is stable across releases.",
    "Core MCP capability parity is complete enough for most C++ consumers.",
    "Installed-package consumption works on every supported compiler,",
    "Public docs, examples, changelog, release artifacts, and compatibility",
    "Accumulate maturity evidence before resubmitting to the vcpkg curated",
    "Maintain an adoption ledger with real downstream repositories,",
    "Resubmit to the vcpkg curated registry only after the maturity evidence is",
    "Publish generated docs from an exact tagged release candidate run.",
    "Publish and review versioned release artifacts plus compatibility notes",
)

REQUIRED_EXTERNAL_GATE_MARKERS = (
    "Status notes that must stay true until exact release evidence says otherwise:",
    "Do not claim fact-standard status yet.",
    "Current open checkboxes are intentionally external-evidence gates.",
    "local source edits alone",
    "exact-commit GitHub",
    "tagged release artifacts",
    "downstream adoption",
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


def check_external_gate_boundary(source: Path) -> None:
    todo_text = read_text(source, Path("todo.md"))
    for marker in REQUIRED_EXTERNAL_GATE_MARKERS:
        if marker not in todo_text:
            fail(f"todo.md is missing external-evidence gate marker: {marker!r}")

    if "- [ ] Do not claim fact-standard status yet." in todo_text:
        fail("fact-standard warning must be a status note, not an open checkbox")


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
    check_external_gate_boundary(source)

    todo = source / "todo.md"
    lines = todo.read_text(encoding="utf-8").splitlines()

    failures: list[str] = []
    allowed_seen: set[str] = set()

    for number, line in enumerate(lines, start=1):
        stripped = line.lstrip()
        if not stripped.startswith("- [ ] "):
            continue

        item = stripped[len("- [ ] ") :]
        allowed = False
        for prefix in ALLOWED_UNCHECKED_PREFIXES:
            if item.startswith(prefix):
                allowed = True
                allowed_seen.add(prefix)
                break
        if not allowed:
            failures.append(f"{number}: {line}")

    if failures:
        fail("unexpected unchecked TODO item(s):\n" + "\n".join(failures))

    missing = set(ALLOWED_UNCHECKED_PREFIXES) - allowed_seen
    if missing:
        fail(
            "expected external evidence gate(s) to remain explicit: "
            + ", ".join(sorted(missing))
        )


if __name__ == "__main__":
    main()
