#!/usr/bin/env python3
"""Compare two public SDK surface manifests for release review."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


STABLE_SCALAR_FIELDS = [
    "language_standard",
    "package_prefix",
    "stable_cpp_namespace",
]

STABLE_LIST_FIELDS = [
    "stable_targets",
    "optional_targets",
    "public_include_roots",
    "optional_include_roots",
    "public_headers",
    "optional_headers",
]


def fail(message: str) -> None:
    raise SystemExit(f"public API surface comparison failed: {message}")


def load_manifest(path: Path) -> dict[str, object]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        fail(f"{path}: invalid JSON: {error}")
    if not isinstance(payload, dict):
        fail(f"{path}: expected JSON object")
    return payload


def values(payload: dict[str, object], field: str) -> set[str]:
    raw = payload.get(field)
    if not isinstance(raw, list) or not all(isinstance(item, str) for item in raw):
        fail(f"manifest field {field!r} must be a string array")
    return set(raw)


def compare_manifests(previous: dict[str, object], current: dict[str, object]) -> list[str]:
    failures: list[str] = []
    for field in STABLE_SCALAR_FIELDS:
        if previous.get(field) != current.get(field):
            failures.append(
                f"{field} changed from {previous.get(field)!r} to {current.get(field)!r}"
            )

    for field in STABLE_LIST_FIELDS:
        removed = sorted(values(previous, field) - values(current, field))
        if removed:
            failures.append(f"{field} removed: {', '.join(removed)}")

    return failures


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--previous", required=True, help="previous manifest JSON")
    parser.add_argument("--current", required=True, help="current manifest JSON")
    args = parser.parse_args()

    previous = load_manifest(Path(args.previous))
    current = load_manifest(Path(args.current))
    failures = compare_manifests(previous, current)
    if failures:
        fail("\n" + "\n".join(failures))

    print("public API surface comparison passed")


if __name__ == "__main__":
    main()
