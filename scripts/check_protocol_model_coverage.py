#!/usr/bin/env python3
"""Check protocol model JSON serializer/parser pairing."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


FROM_ONLY = {
    # Validation helper used by Resource and ResourceTemplate parsing.
    "resource_size",
}

TO_ONLY = {
    # Primitive schema variants serialize individually but parse through the
    # tagged PrimitiveSchema parser.
    "boolean_schema",
    "enum_schema",
    "integer_schema",
    "number_schema",
    "string_schema",
    # Docstring-only examples in custom_methods.hpp.
    "my_event",
    "my_params",
}


def fail(message: str) -> None:
    raise SystemExit(f"protocol model coverage check failed: {message}")


def collect_json_functions(protocol_include: Path) -> tuple[set[str], set[str]]:
    pattern = re.compile(r"\b([A-Za-z][A-Za-z0-9_]*)_(from|to)_json\s*\(")
    from_json: set[str] = set()
    to_json: set[str] = set()

    for path in sorted(protocol_include.glob("*.hpp")):
        text = path.read_text(encoding="utf-8")
        for match in pattern.finditer(text):
            name = match.group(1)
            direction = match.group(2)
            if direction == "from":
                from_json.add(name)
            else:
                to_json.add(name)

    return from_json, to_json


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository source root")
    args = parser.parse_args()

    source = Path(args.source).resolve()
    protocol_include = source / "sdk/protocol/include/cxxmcp/protocol"
    if not protocol_include.is_dir():
        fail(f"missing protocol include directory: {protocol_include}")

    from_json, to_json = collect_json_functions(protocol_include)
    missing_to = from_json - to_json - FROM_ONLY
    missing_from = to_json - from_json - TO_ONLY

    if missing_to:
        fail("missing *_to_json counterpart(s): " + ", ".join(sorted(missing_to)))
    if missing_from:
        fail(
            "missing *_from_json counterpart(s): "
            + ", ".join(sorted(missing_from))
        )

    print(
        "protocol model coverage: "
        f"{len(from_json)} parser base(s), {len(to_json)} serializer base(s)"
    )


if __name__ == "__main__":
    main()
