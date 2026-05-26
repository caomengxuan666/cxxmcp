#!/usr/bin/env python3
"""Validate release-candidate evidence wiring without building the project."""

from __future__ import annotations

import argparse
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"release evidence check failed: {message}")


def read_text(path: Path) -> str:
    if not path.is_file():
        fail(f"missing required file: {path}")
    return path.read_text(encoding="utf-8")


def require_contains(path: Path, needle: str) -> None:
    text = read_text(path)
    if needle not in text:
        fail(f"{path} must contain {needle!r}")


def require_not_contains(path: Path, needle: str) -> None:
    text = read_text(path)
    if needle in text:
        fail(f"{path} must not contain {needle!r}")


def require_file(path: Path) -> None:
    if not path.is_file():
        fail(f"missing required file: {path}")


def check_source_tree(source: Path) -> None:
    required_files = [
        "README.md",
        "README_zh.md",
        "CHANGELOG.md",
        "CONTRIBUTING.md",
        "SECURITY.md",
        "CODE_OF_CONDUCT.md",
        "todo.md",
        "docs/compatibility_policy.md",
        "docs/dependency_policy.md",
        "docs/release_process.md",
        "docs/release_gates.md",
        "docs/release_candidate_checklist.md",
        "docs/release_notes_template.md",
        "docs/request_lifecycle.md",
        "docs/sdk_peer_service_migration.md",
        "docs/Doxyfile",
        "templates/external_consumer/CMakeLists.txt",
        "templates/external_consumer/main.cpp",
        "templates/external_consumer/README.md",
        ".github/workflows/release-gates.yml",
        "tests/CMakeLists.txt",
        "tests/package_smoke.cmake",
    ]
    for relative in required_files:
        require_file(source / relative)

    for relative in ["README.md", "README_zh.md"]:
        path = source / relative
        require_contains(path, "Peer")
        require_contains(path, "Service")
        require_contains(path, "Compatibility policy")

    require_contains(source / "CHANGELOG.md", "Peer")
    require_contains(source / "CHANGELOG.md", "Service")
    require_not_contains(source / "CHANGELOG.md", "facade")

    compatibility = source / "docs/compatibility_policy.md"
    require_contains(compatibility, "canonical SDK path")
    require_contains(compatibility, "compatibility or convenience APIs")
    require_contains(compatibility, "release evidence")

    release_notes = source / "docs/release_notes_template.md"
    require_contains(release_notes, "Canonical SDK Path")
    require_contains(release_notes, "Required Artifacts")
    require_contains(release_notes, "Checksums")

    release_process = source / "docs/release_process.md"
    for needle in [
        "semantic versioning",
        "Alpha",
        "Beta",
        "RC",
        "Stable",
        "versioned SDK source archive",
        "Public API Review",
        "Dependency Review",
        "Security And Advisories",
    ]:
        require_contains(release_process, needle)

    doxygen = source / "docs/Doxyfile"
    require_contains(doxygen, 'PROJECT_BRIEF          = "C++ MCP SDK"')
    require_contains(doxygen, "sdk/transport/include")
    for forbidden in [
        "runtime/include",
        "runtime/observability/include",
        "extensions/adapters/include",
        "extensions/plugin-sdk/include",
        "tools/cli/include",
    ]:
        require_not_contains(doxygen, forbidden)

    workflow = source / ".github/workflows/release-gates.yml"
    require_contains(workflow, "cxxmcp-release-evidence")
    require_contains(workflow, "if-no-files-found: error")
    require_contains(workflow, "typescript_sdk_reference=@modelcontextprotocol/sdk@1.29.0")
    require_contains(workflow, "python_sdk_reference=mcp==1.27.1")

    tests_cmake = source / "tests/CMakeLists.txt"
    require_contains(tests_cmake, "CXXMCP_INTEROP_TYPESCRIPT_SDK_VERSION")
    require_contains(tests_cmake, "CXXMCP_INTEROP_PYTHON_MCP_VERSION")
    require_contains(tests_cmake, "mcp==${CXXMCP_INTEROP_PYTHON_MCP_VERSION}")

    package_smoke = source / "tests/package_smoke.cmake"
    require_contains(package_smoke, "PACKAGE_SMOKE_GENERATOR")
    require_contains(package_smoke, "PACKAGE_SMOKE_CXX_COMPILER")
    require_contains(package_smoke, "templates/external_consumer")

    template_cmake = source / "templates/external_consumer/CMakeLists.txt"
    require_contains(template_cmake, "find_package(cxxmcp CONFIG REQUIRED)")
    require_contains(template_cmake, "cxxmcp::server")

    example_markers = {
        "examples/client_loopback.cpp": "Compatibility example",
        "examples/task_async_client_server.cpp": "Compatibility example",
        "examples/stdio_server.cpp": "Compatibility example",
        "examples/typed_stdio_server.cpp": "Compatibility example",
        "examples/gateway_runtime.cpp": "Runtime tooling example",
    }
    for relative, marker in example_markers.items():
        text = read_text(source / relative)
        header = "\n".join(text.splitlines()[:8])
        if marker not in header:
            fail(f"{relative} must be labelled with {marker!r} near the top")


def check_evidence_dir(evidence: Path) -> None:
    required_files = [
        "README.md",
        "README_zh.md",
        "CHANGELOG.md",
        "CONTRIBUTING.md",
        "SECURITY.md",
        "CODE_OF_CONDUCT.md",
        "todo.md",
        "MANIFEST.txt",
        "docs/compatibility_policy.md",
        "docs/dependency_policy.md",
        "docs/release_process.md",
        "docs/release_gates.md",
        "docs/release_candidate_checklist.md",
        "docs/release_notes_template.md",
        "docs/request_lifecycle.md",
        "docs/sdk_peer_service_migration.md",
        "examples/CMakeLists.txt",
        "examples/server_peer.cpp",
        "examples/client_peer.cpp",
        "examples/process_stdio_client.cpp",
        "templates/external_consumer/CMakeLists.txt",
        "templates/external_consumer/main.cpp",
        "templates/external_consumer/README.md",
    ]
    for relative in required_files:
        require_file(evidence / relative)

    manifest = evidence / "MANIFEST.txt"
    for key in [
        "commit=",
        "workflow=",
        "run_id=",
        "run_attempt=",
        "mcp_protocol_snapshot=2025-11-25",
        "rmcp_reference_commit=c330fede90e4729c234f8e87fdbc5ea27a1dd10c",
        "typescript_sdk_reference=@modelcontextprotocol/sdk@1.29.0",
        "python_sdk_reference=mcp==1.27.1",
    ]:
        require_contains(manifest, key)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository source root")
    parser.add_argument(
        "--evidence",
        help="optional collected release evidence directory to validate",
    )
    args = parser.parse_args()

    check_source_tree(Path(args.source).resolve())
    if args.evidence:
        check_evidence_dir(Path(args.evidence).resolve())


if __name__ == "__main__":
    main()
