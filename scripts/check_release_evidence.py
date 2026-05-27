#!/usr/bin/env python3
"""Validate release-candidate evidence wiring without building the project."""

from __future__ import annotations

import argparse
import re
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


def project_version(source: Path) -> str:
    cmake = read_text(source / "CMakeLists.txt")
    match = re.search(
        r"project\s*\(\s*cxxmcp\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)",
        cmake,
    )
    if not match:
        fail("CMakeLists.txt must declare project(cxxmcp VERSION <version>)")
    return match.group(1)


def check_source_tree(source: Path) -> None:
    version = project_version(source)
    required_files = [
        "README.md",
        "README_zh.md",
        "CHANGELOG.md",
        "CONTRIBUTING.md",
        "SECURITY.md",
        "CODE_OF_CONDUCT.md",
        "todo.md",
        "docs/auth_design.md",
        "docs/compatibility_policy.md",
        "docs/dependency_policy.md",
        "docs/elicitation_lifecycle.md",
        "docs/ecosystem_maturity_evidence.md",
        "docs/examples.md",
        "docs/http_transport_backend_evidence.md",
        "docs/official_sdk_candidate_process.md",
        "docs/package_consumption.md",
        "docs/package_consumption_zh.md",
        "docs/protocol_model_audit.md",
        "docs/release_process.md",
        "docs/release_gates.md",
        "docs/release_candidate_checklist.md",
        "docs/release_notes_template.md",
        "docs/request_lifecycle.md",
        "docs/runtime_gateway.md",
        "docs/sdk_peer_service_migration.md",
        "docs/task_lifecycle.md",
        "docs/Doxyfile",
        "scripts/check_protocol_model_coverage.py",
        "scripts/check_p2_todo_status.py",
        "scripts/check_sdk_header_boundaries.py",
        "scripts/run_clang_tidy.py",
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
        text = read_text(path)
        if "Peer" not in text:
            fail(f"{path} must contain 'Peer'")
        if "Service" not in text:
            fail(f"{path} must contain 'Service'")
        if "Compatibility policy" not in text:
            fail(f"{path} must contain 'Compatibility policy'")
        first_screen = "\n".join(text.splitlines()[:70]).lower()
        if "gateway" in first_screen:
            fail(f"{path} must keep gateway out of the README first screen")
        for needle in [
            "docs/examples.md",
            "docs/runtime_gateway.md",
            "docs/http_transport_backend_evidence.md",
        ]:
            if needle not in text:
                fail(f"{path} must link {needle!r}")

    examples_doc = source / "docs/examples.md"
    require_contains(examples_doc, "First-Choice SDK Examples")
    require_contains(examples_doc, "Runtime Tooling Example")

    runtime_gateway = source / "docs/runtime_gateway.md"
    require_contains(runtime_gateway, "not part of the core public SDK contract")
    require_contains(runtime_gateway, "gateway_runtime.cpp")

    http_backend = source / "docs/http_transport_backend_evidence.md"
    require_contains(http_backend, "Do not add another HTTP backend")
    require_contains(http_backend, "release-blocking `http_transport`")

    ecosystem = source / "docs/ecosystem_maturity_evidence.md"
    for needle in [
        "Stable release history",
        "Green release gates over time",
        "Installed package evidence",
        "Downstream examples",
        "Changelog discipline",
        "Public user adoption",
        "Curated port shape",
        "Do not resubmit to the vcpkg curated registry yet",
    ]:
        require_contains(ecosystem, needle)

    changelog = source / "CHANGELOG.md"
    require_contains(changelog, f"## {version}")
    require_contains(changelog, "Peer")
    require_contains(changelog, "Service")
    require_not_contains(changelog, "facade")

    compatibility = source / "docs/compatibility_policy.md"
    require_contains(compatibility, "canonical SDK path")
    require_contains(compatibility, "compatibility or convenience APIs")
    require_contains(compatibility, "release evidence")

    protocol_audit = source / "docs/protocol_model_audit.md"
    require_contains(protocol_audit, "c330fede90e4729c234f8e87fdbc5ea27a1dd10c")
    require_contains(protocol_audit, "2025-11-25")
    for family in [
        "capabilities.hpp",
        "tool.hpp",
        "prompt.hpp",
        "resource.hpp",
        "completion.hpp",
        "logging.hpp",
        "sampling.hpp",
        "elicitation.hpp",
        "task.hpp",
    ]:
        require_contains(protocol_audit, family)

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
    require_contains(doxygen, f"PROJECT_NUMBER         = {version}")
    require_contains(doxygen, 'PROJECT_BRIEF          = "C++ MCP SDK"')
    require_contains(doxygen, "sdk/auth/include")
    require_contains(doxygen, "sdk/transport/include")
    require_contains(doxygen, "MARKDOWN_ID_STYLE      = GITHUB")
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
    require_contains(workflow, "check_protocol_model_coverage.py")
    require_contains(workflow, "check_p2_todo_status.py")
    require_contains(workflow, "check_sdk_header_boundaries.py")
    require_contains(workflow, "check_package_auth_features.py")
    require_contains(workflow, "build-config-smoke")
    require_contains(workflow, "clang-tidy-public-headers")
    require_contains(workflow, "scripts/run_clang_tidy.py")
    require_contains(workflow, "Release")
    require_contains(workflow, "typescript_sdk_reference=@modelcontextprotocol/sdk@1.29.0")
    require_contains(workflow, "python_sdk_reference=mcp==1.27.1")

    release_sdk = source / ".github/workflows/release-sdk.yml"
    require_contains(release_sdk, "cxxmcp-sdk-source-${tag}.tar.gz")
    require_contains(release_sdk, "-czf \"release-artifacts/${package}\"")
    require_contains(release_sdk, "SHA256SUMS.txt")
    require_contains(release_sdk, "sha256sum \"${package}\" > SHA256SUMS.txt")
    require_contains(release_sdk, "RELEASE_NOTES.md")
    require_contains(release_sdk, "gh release upload")
    require_contains(release_sdk, "gh release create")
    require_contains(release_sdk, "\"release-artifacts/cxxmcp-sdk-source-${tag}.tar.gz\"")
    require_contains(release_sdk, "\"release-artifacts/SHA256SUMS.txt\"")
    require_contains(release_sdk, "\"release-artifacts/RELEASE_NOTES.md\"")
    require_contains(release_sdk, "--notes-file")
    require_contains(release_sdk, "Compatibility Notes")
    require_contains(release_sdk, "Static-library releases do not claim ABI stability")

    tests_cmake = source / "tests/CMakeLists.txt"
    require_contains(tests_cmake, "CXXMCP_INTEROP_TYPESCRIPT_SDK_VERSION")
    require_contains(tests_cmake, "CXXMCP_INTEROP_PYTHON_MCP_VERSION")
    require_contains(tests_cmake, "mcp==${CXXMCP_INTEROP_PYTHON_MCP_VERSION}")

    package_smoke = source / "tests/package_smoke.cmake"
    require_contains(package_smoke, "PACKAGE_SMOKE_GENERATOR")
    require_contains(package_smoke, "PACKAGE_SMOKE_CXX_COMPILER")
    require_contains(package_smoke, "package_smoke_auth_enabled")
    require_contains(package_smoke, "default package smoke must not install optional auth headers")
    require_contains(package_smoke, "templates/external_consumer")

    package_smoke_auth = source / "tests/fixtures/package_smoke/auth.cpp"
    require_contains(package_smoke_auth, "cxxmcp/auth.hpp")
    require_contains(package_smoke_auth, "mcp::auth::")

    package_smoke_cmake = source / "tests/fixtures/package_smoke/CMakeLists.txt"
    require_contains(package_smoke_cmake, "cxxmcp::auth")

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
        "docs/auth_design.md",
        "docs/compatibility_policy.md",
        "docs/dependency_policy.md",
        "docs/elicitation_lifecycle.md",
        "docs/ecosystem_maturity_evidence.md",
        "docs/examples.md",
        "docs/http_transport_backend_evidence.md",
        "docs/official_sdk_candidate_process.md",
        "docs/package_consumption.md",
        "docs/package_consumption_zh.md",
        "docs/protocol_model_audit.md",
        "docs/release_process.md",
        "docs/release_gates.md",
        "docs/release_candidate_checklist.md",
        "docs/release_notes_template.md",
        "docs/request_lifecycle.md",
        "docs/runtime_gateway.md",
        "docs/sdk_peer_service_migration.md",
        "docs/task_lifecycle.md",
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
