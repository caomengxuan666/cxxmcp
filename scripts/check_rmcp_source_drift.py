#!/usr/bin/env python3
"""Check the pinned RMCP model source mapping used by protocol audits."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Any


MCP_PROTOCOL_SNAPSHOT = "2025-11-25"
RMCP_REFERENCE_COMMIT = "c330fede90e4729c234f8e87fdbc5ea27a1dd10c"
RMCP_ROOT = Path("reference/rmcp")
RMCP_MODEL_ROOT = Path("reference/rmcp/crates/rmcp/src/model")
RMCP_PARENT_MODEL = Path("reference/rmcp/crates/rmcp/src/model.rs")
CXXMCP_MODEL_ROOT = Path("sdk/protocol/include/cxxmcp/protocol")
MAPPING_DOC = Path("docs/rmcp_source_mapping.json")
PROTOCOL_AUDIT_DOC = Path("docs/protocol_model_audit.md")

IGNORED_MODEL_FILES = {
    # Serde helper implementation details are covered through the public model
    # files that use them; they are not a separate MCP wire-shape family.
    "serde_impl.rs",
}

MAPPINGS: list[dict[str, Any]] = [
    {
        "rmcp_source": "model.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model.rs",
        "cxxmcp_headers": [
            "types.hpp",
            "initialize.hpp",
            "resource.hpp",
            "prompt.hpp",
            "logging.hpp",
            "sampling.hpp",
            "completion.hpp",
            "roots.hpp",
            "elicitation.hpp",
            "task.hpp",
            "tool.hpp",
        ],
    },
    {
        "rmcp_source": "annotated.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/annotated.rs",
        "cxxmcp_headers": ["tool.hpp", "prompt.hpp", "resource.hpp"],
    },
    {
        "rmcp_source": "capabilities.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/capabilities.rs",
        "cxxmcp_headers": ["capabilities.hpp"],
    },
    {
        "rmcp_source": "content.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/content.rs",
        "cxxmcp_headers": ["tool.hpp", "resource.hpp", "sampling.hpp"],
    },
    {
        "rmcp_source": "elicitation_schema.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/elicitation_schema.rs",
        "cxxmcp_headers": ["elicitation.hpp"],
    },
    {
        "rmcp_source": "extension.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/extension.rs",
        "cxxmcp_headers": ["all protocol family headers"],
    },
    {
        "rmcp_source": "meta.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/meta.rs",
        "cxxmcp_headers": ["all protocol family headers"],
    },
    {
        "rmcp_source": "prompt.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/prompt.rs",
        "cxxmcp_headers": ["prompt.hpp", "tool.hpp"],
    },
    {
        "rmcp_source": "resource.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/resource.rs",
        "cxxmcp_headers": ["resource.hpp", "tool.hpp"],
    },
    {
        "rmcp_source": "task.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/task.rs",
        "cxxmcp_headers": ["task.hpp"],
    },
    {
        "rmcp_source": "tool.rs",
        "rmcp_path": "reference/rmcp/crates/rmcp/src/model/tool.rs",
        "cxxmcp_headers": ["tool.hpp", "task.hpp"],
    },
]


def fail(message: str) -> None:
    raise SystemExit(f"RMCP source-drift audit failed: {message}")


def read_text(path: Path) -> str:
    if not path.is_file():
        fail(f"missing required file: {path}")
    return path.read_text(encoding="utf-8")


def run_git(rmcp_root: Path, *args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", "-C", str(rmcp_root), *args],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError:
        fail("git is required to verify the pinned RMCP reference checkout")
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.strip() or exc.stdout.strip()
        fail(f"git {' '.join(args)} failed for {rmcp_root}: {detail}")
    return completed.stdout.strip()


def mapping_payload() -> dict[str, Any]:
    return {
        "mcp_protocol_snapshot": MCP_PROTOCOL_SNAPSHOT,
        "rmcp_reference_commit": RMCP_REFERENCE_COMMIT,
        "rmcp_model_source_root": RMCP_MODEL_ROOT.as_posix(),
        "cxxmcp_model_source_root": CXXMCP_MODEL_ROOT.as_posix(),
        "ignored_rmcp_model_files": sorted(IGNORED_MODEL_FILES),
        "mappings": MAPPINGS,
    }


def canonical_json(payload: dict[str, Any]) -> str:
    return json.dumps(payload, indent=2, sort_keys=True) + "\n"


def check_reference_checkout(source: Path) -> None:
    rmcp_root = source / RMCP_ROOT
    model_root = source / RMCP_MODEL_ROOT
    parent_model = source / RMCP_PARENT_MODEL

    if not rmcp_root.is_dir():
        fail(f"missing pinned RMCP reference root: {rmcp_root}")
    if not model_root.is_dir():
        fail(f"missing pinned RMCP model source root: {model_root}")
    if not parent_model.is_file():
        fail(f"missing pinned RMCP parent model file: {parent_model}")

    commit = run_git(rmcp_root, "rev-parse", "HEAD")
    if commit != RMCP_REFERENCE_COMMIT:
        fail(
            "RMCP reference commit mismatch: "
            f"expected {RMCP_REFERENCE_COMMIT}, got {commit}"
        )

    status = run_git(rmcp_root, "status", "--short")
    if status:
        fail("pinned RMCP reference checkout has local changes:\n" + status)


def check_model_files(source: Path) -> None:
    cxxmcp_model_root = source / CXXMCP_MODEL_ROOT
    if not cxxmcp_model_root.is_dir():
        fail(f"missing cxxmcp model source root: {cxxmcp_model_root}")

    for mapping in MAPPINGS:
        path = source / mapping["rmcp_path"]
        if not path.is_file():
            fail(f"missing mapped RMCP model source: {path}")
        for header in mapping["cxxmcp_headers"]:
            if header == "all protocol family headers":
                continue
            header_path = cxxmcp_model_root / header
            if not header_path.is_file():
                fail(f"missing mapped cxxmcp protocol header: {header_path}")

    model_root = source / RMCP_MODEL_ROOT
    actual = {path.name for path in model_root.glob("*.rs")}
    expected = {
        mapping["rmcp_source"]
        for mapping in MAPPINGS
        if mapping["rmcp_source"] != "model.rs"
    }
    unmapped = actual - expected - IGNORED_MODEL_FILES
    stale = expected - actual
    if unmapped:
        fail("unmapped RMCP model source file(s): " + ", ".join(sorted(unmapped)))
    if stale:
        fail("stale RMCP model mapping(s): " + ", ".join(sorted(stale)))


def parse_audit_table(text: str) -> dict[str, list[str]]:
    section = re.search(
        r"## RMCP Model File Coverage\n\n(?P<body>.*?)(?:\n## |\Z)",
        text,
        re.DOTALL,
    )
    if not section:
        fail(f"{PROTOCOL_AUDIT_DOC} is missing RMCP Model File Coverage section")

    rows: dict[str, list[str]] = {}
    for line in section.group("body").splitlines():
        match = re.match(r"\|\s*`([^`]+)`\s*\|\s*(.*?)\s*\|", line)
        if not match:
            continue
        rmcp_source = match.group(1)
        coverage_cell = match.group(2)
        headers = re.findall(r"`([^`]+)`", coverage_cell)
        if not headers and "all protocol family headers" in coverage_cell:
            headers = ["all protocol family headers"]
        rows[rmcp_source] = headers
    return rows


def check_protocol_audit_doc(source: Path) -> None:
    text = read_text(source / PROTOCOL_AUDIT_DOC)
    for needle in [
        f"MCP protocol snapshot: `{MCP_PROTOCOL_SNAPSHOT}`",
        f"RMCP reference commit: `{RMCP_REFERENCE_COMMIT}`",
        f"RMCP model source root: `{RMCP_MODEL_ROOT.as_posix()}`",
        f"cxxmcp model source root: `{CXXMCP_MODEL_ROOT.as_posix()}`",
    ]:
        if needle not in text:
            fail(f"{PROTOCOL_AUDIT_DOC} must contain {needle!r}")

    rows = parse_audit_table(text)
    expected = {
        mapping["rmcp_source"]: mapping["cxxmcp_headers"] for mapping in MAPPINGS
    }
    if set(rows) != set(expected):
        missing = set(expected) - set(rows)
        extra = set(rows) - set(expected)
        parts = []
        if missing:
            parts.append("missing " + ", ".join(sorted(missing)))
        if extra:
            parts.append("extra " + ", ".join(sorted(extra)))
        fail(f"{PROTOCOL_AUDIT_DOC} RMCP coverage rows differ: {'; '.join(parts)}")

    mismatched = [
        rmcp_source
        for rmcp_source, headers in expected.items()
        if rows[rmcp_source] != headers
    ]
    if mismatched:
        fail(
            f"{PROTOCOL_AUDIT_DOC} cxxmcp coverage differs for: "
            + ", ".join(sorted(mismatched))
        )


def check_or_write_mapping_doc(source: Path, write_mapping: bool) -> None:
    path = source / MAPPING_DOC
    expected = canonical_json(mapping_payload())
    if write_mapping:
        path.write_text(expected, encoding="utf-8")
        return

    if not path.is_file():
        fail(f"missing generated RMCP source mapping: {path}")
    actual = path.read_text(encoding="utf-8")
    if actual != expected:
        fail(f"{path} is stale; rerun with --write-mapping")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository source root")
    parser.add_argument(
        "--write-mapping",
        action="store_true",
        help=f"regenerate {MAPPING_DOC.as_posix()} from the script mapping",
    )
    args = parser.parse_args()

    source = Path(args.source).resolve()
    check_reference_checkout(source)
    check_model_files(source)
    check_protocol_audit_doc(source)
    check_or_write_mapping_doc(source, args.write_mapping)

    action = "wrote" if args.write_mapping else "verified"
    print(
        "RMCP source-drift audit "
        f"{action}: {len(MAPPINGS)} mapped source file(s) at {RMCP_REFERENCE_COMMIT}"
    )


if __name__ == "__main__":
    main()
