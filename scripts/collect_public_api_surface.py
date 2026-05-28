#!/usr/bin/env python3
"""Collect a diffable public SDK surface manifest for release evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


PUBLIC_INCLUDE_ROOTS = [
    "sdk/include/cxxmcp",
    "sdk/core/include/cxxmcp",
    "sdk/protocol/include/cxxmcp",
    "sdk/transport/include/cxxmcp",
    "sdk/client/include/cxxmcp",
    "sdk/server/include/cxxmcp",
]

OPTIONAL_INCLUDE_ROOTS = [
    "sdk/auth/include/cxxmcp",
    "extensions/plugin-sdk/include/cxxmcp",
]

STABLE_TARGETS = [
    "cxxmcp::protocol",
    "cxxmcp::transport",
    "cxxmcp::handler",
    "cxxmcp::peer",
    "cxxmcp::service",
    "cxxmcp::client",
    "cxxmcp::server",
    "cxxmcp::sdk",
]

OPTIONAL_TARGETS = [
    "cxxmcp::auth",
    "cxxmcp::auth_openssl",
    "cxxmcp::plugin_sdk",
    "cxxmcp::adapters",
]

OUT_OF_SCOPE_SURFACES = [
    "runtime",
    "gateway",
    "cli",
    "app",
    "profile",
    "policy",
    "discovery",
]


def fail(message: str) -> None:
    raise SystemExit(f"public API surface collection failed: {message}")


def collect_headers(source: Path, roots: list[str]) -> list[str]:
    headers: list[str] = []
    for root_name in roots:
        root = source / root_name
        if not root.is_dir():
            fail(f"missing public include root: {root_name}")
        headers.extend(
            header.relative_to(source).as_posix()
            for header in sorted(root.rglob("*.hpp"))
        )
    return sorted(headers)


def collect_manifest(source: Path) -> dict[str, object]:
    public_headers = collect_headers(source, PUBLIC_INCLUDE_ROOTS)
    optional_headers = collect_headers(source, OPTIONAL_INCLUDE_ROOTS)
    return {
        "schema_version": 1,
        "language_standard": "C++17",
        "package_prefix": "cxxmcp",
        "stable_cpp_namespace": "mcp",
        "public_include_roots": PUBLIC_INCLUDE_ROOTS,
        "optional_include_roots": OPTIONAL_INCLUDE_ROOTS,
        "stable_targets": STABLE_TARGETS,
        "optional_targets": OPTIONAL_TARGETS,
        "out_of_scope_surfaces": OUT_OF_SCOPE_SURFACES,
        "public_headers": public_headers,
        "optional_headers": optional_headers,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository root")
    parser.add_argument("--output", required=True, help="manifest JSON path")
    args = parser.parse_args()

    source = Path(args.source).resolve()
    output = Path(args.output).resolve()
    manifest = collect_manifest(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
