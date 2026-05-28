#!/usr/bin/env python3
"""Validate release-candidate evidence wiring without building the project."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

MINGW_COMPAT_JOB_NAMES = [
    "windows-mingw-ucrt64-gcc",
    "windows-mingw-clang64-clang",
]

MINGW_POLICY_DOCS = [
    "README.md",
    "README_zh.md",
    "docs/compatibility_policy.md",
    "docs/release_gates.md",
    "docs/ecosystem_maturity_evidence.md",
]

TECHNICAL_AUDIT_EVIDENCE_IDS = [
    "C1",
    "C2",
    "C3",
    "H1",
    "H2",
    "H3",
    "H4",
    "H5",
    "M1",
    "M2",
    "M3",
    "M4",
    "M5",
    "M6",
    "L1",
    "L2",
    "L3",
    "L4",
    "L5",
    "L6",
    "H6",
    "H7",
    "H8",
    "H9",
    "H10",
    "H11",
    "M7",
    "M8",
    "M9",
    "M10",
    "M11",
    "M12",
    "M13",
    "M14",
    "M15",
    "M16",
    "L7",
    "L8",
    "L9",
    "L10",
    "Auth-F1",
    "Auth-F2",
    "Auth-F3",
    "Auth-F4",
    "Auth-F5",
    "Auth-F6",
    "Auth-F7",
    "EXT-H1",
    "EXT-H2",
    "EXT-H3",
    "EXT-M1",
    "EXT-M2",
    "EXT-M3",
    "EXT-M4",
    "EXT-L1",
    "EXT-L2",
]


def fail(message: str) -> None:
    raise SystemExit(f"release evidence check failed: {message}")


def read_text(path: Path) -> str:
    if not path.is_file():
        fail(f"missing required file: {path}")
    return path.read_text(encoding="utf-8")


def require_contains(path: Path, needle: str) -> None:
    import re
    text = read_text(path)
    normalized_text = re.sub(r'\s+', ' ', text)
    normalized_needle = re.sub(r'\s+', ' ', needle)
    if normalized_needle not in normalized_text:
        fail(f"{path} must contain {needle!r}")


def require_not_contains(path: Path, needle: str) -> None:
    text = read_text(path)
    if needle in text:
        fail(f"{path} must not contain {needle!r}")


def require_file(path: Path) -> None:
    if not path.is_file():
        fail(f"missing required file: {path}")


def require_mingw_provisional_policy(path: Path) -> None:
    text = read_text(path)
    for needle in [
        "MinGW UCRT64 GCC",
        "MinGW CLANG64 Clang",
        "provisional",
        "best-effort",
        "release-supported",
    ]:
        if needle not in text:
            fail(f"{path} must describe MinGW as provisional best-effort, not release-supported")
    if not re.search(
        r"(?i)(not\s+(?:be\s+presented\s+as\s+)?(?:a\s+)?release-supported|不是\s+release-supported)",
        text,
    ):
        fail(f"{path} must say MinGW is not release-supported")


def require_mingw_compiler_compat_workflow(source: Path) -> None:
    path = source / ".github/workflows/compiler-compat.yml"
    text = read_text(path)
    require_contains(path, "mingw-sdk")
    require_contains(path, "continue-on-error: true")
    for job_name in MINGW_COMPAT_JOB_NAMES:
        require_contains(path, job_name)
    if not re.search(
        r"(?ms)^\s*mingw-sdk:\s.*?^\s*continue-on-error:\s*true\s*$",
        text,
    ):
        fail(
            ".github/workflows/compiler-compat.yml must keep the MinGW "
            "compatibility job continue-on-error while MinGW is provisional"
        )


def require_technical_audit_evidence_map(path: Path) -> None:
    text = read_text(path)
    require_contains(path, "## Audit Evidence Map")
    require_contains(path, "FIXED_WITH_TRACKED_DEBT")
    require_contains(path, "TRANSPORT_SPECIFIC")
    require_contains(path, "package maturity")
    require_contains(path, "fact-standard status")
    require_contains(path, "docs/performance_debt.md")
    require_contains(path, "release notes")

    missing = []
    for audit_id in TECHNICAL_AUDIT_EVIDENCE_IDS:
        pattern = rf"(?m)^\|\s*{re.escape(audit_id)}\s*\|"
        if not re.search(pattern, text):
            missing.append(audit_id)
    if missing:
        fail(
            f"{path} Audit Evidence Map missing audit id(s): "
            + ", ".join(missing)
        )


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
        "docs/adoption_ledger.md",
        "docs/auth_design.md",
        "docs/auth_user_guide.md",
        "docs/compatibility_policy.md",
        "docs/dependency_policy.md",
        "docs/capability_lifecycles.md",
        "docs/ecosystem_maturity_evidence.md",
        "docs/examples.md",
        "docs/official_sdk_candidate_process.md",
        "docs/package_consumption.md",
        "docs/package_consumption_zh.md",
        "docs/performance_debt.md",
        "docs/protocol_model_audit.md",
        "docs/public_api_stability.md",
        "docs/release_process.md",
        "docs/release_gates.md",
        "docs/release_candidate_checklist.md",
        "docs/release_notes_template.md",
        "docs/request_lifecycle.md",
        "docs/rmcp_source_mapping.json",
        "docs/runtime_gateway.md",
        "docs/technical_audit.md",
        "docs/Doxyfile",
        "docs/pages/index.html",
        "docs/pages/getting-started.html",
        "docs/pages/auth.html",
        "docs/pages/stability.html",
        "scripts/check_package_auth_features.py",
        "scripts/check_release_artifacts.py",
        "scripts/check_release_evidence.py",
        "scripts/check_protocol_model_coverage.py",
        "scripts/check_p2_todo_status.py",
        "scripts/check_package_recipe_sync.py",
        "scripts/check_rmcp_source_drift.py",
        "scripts/check_sdk_header_boundaries.py",
        "scripts/check_source_markers.py",
        "scripts/compare_public_api_surface.py",
        "scripts/collect_public_api_surface.py",
        "scripts/collect_public_header_compile_evidence.py",
        "scripts/run_clang_tidy.py",
        "scripts/selftest_release_artifacts.py",
        "scripts/selftest_public_api_surface.py",
        "templates/external_consumer/CMakeLists.txt",
        "templates/external_consumer/main.cpp",
        "templates/external_consumer/README.md",
        ".github/workflows/release-gates.yml",
        ".github/workflows/compiler-compat.yml",
        "tests/CMakeLists.txt",
        "tests/package_smoke.cmake",
    ]
    for relative in required_files:
        require_file(source / relative)

    require_mingw_compiler_compat_workflow(source)
    for relative in MINGW_POLICY_DOCS:
        require_mingw_provisional_policy(source / relative)

    conservative_claim_docs = [
        "README.md",
        "README_zh.md",
        "todo.md",
        "docs/ecosystem_maturity_evidence.md",
        "docs/release_candidate_checklist.md",
        "docs/pages/stability.html",
    ]
    for relative in conservative_claim_docs:
        path = source / relative
        for forbidden in [
            "fact-standard-ready",
            "curated-registry-ready",
            "independent public downstream adoption exists",
            "published release evidence is complete",
        ]:
            require_not_contains(path, forbidden)

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
            "docs/compatibility_policy.md#http-transport-backend-evidence",
        ]:
            if needle not in text:
                fail(f"{path} must link {needle!r}")
    for relative in [
        "README.md",
        "README_zh.md",
        "docs/compatibility_policy.md",
        "docs/public_api_stability.md",
        "docs/pages/stability.html",
    ]:
        path = source / relative
        text = read_text(path)
        if "cxxmcp" not in text or "mcp" not in text:
            fail(f"{path} must document cxxmcp package naming and mcp namespace")

    examples_doc = source / "docs/examples.md"
    require_contains(examples_doc, "First-Choice SDK Examples")
    require_contains(examples_doc, "auth_bearer_http.cpp")
    require_contains(examples_doc, "auth_dpop_openssl.cpp")
    require_contains(examples_doc, "server_stdio_peer.cpp")
    require_contains(examples_doc, "External Gateway Boundary")

    runtime_gateway = source / "docs/runtime_gateway.md"
    require_contains(runtime_gateway, "not part of the core public SDK contract")
    require_contains(runtime_gateway, "not part of this SDK repository")

    compatibility = source / "docs/compatibility_policy.md"
    require_contains(compatibility, "Do not add another HTTP backend")
    require_contains(compatibility, "release-blocking `http_transport`")

    performance_debt = source / "docs/performance_debt.md"
    require_contains(performance_debt, "protocol_serialization_benchmark")
    require_contains(performance_debt, "package_smoke")
    require_contains(
        performance_debt, "cxxmcp-public-header-compile-evidence-linux-gcc-ninja"
    )
    require_contains(performance_debt, "json_fwd")
    require_contains(performance_debt, "extern template")

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

    dependency_policy = source / "docs/dependency_policy.md"
    for needle in [
        "Time-Sensitive Dependency Claims",
        "Dependency status statements are release-candidate evidence",
        "tl::expected",
        "jsonrpcpp",
        "cpp-httplib",
    ]:
        require_contains(dependency_policy, needle)

    adoption_ledger = source / "docs/adoption_ledger.md"
    for needle in [
        "No independent public downstream adoption is recorded yet",
        "Project-Owned Evidence",
        "Public Downstream Adoption",
        "Search Audit",
        "2026-05-28",
        "Follow-up web search",
        "Entry Requirements",
        "not independent public",
    ]:
        require_contains(adoption_ledger, needle)

    auth_page = source / "docs/pages/auth.html"
    for needle in [
        "CXXMCP_ENABLE_AUTH=OFF",
        "CXXMCP_AUTH_CRYPTO=OpenSSL",
        "cxxmcp::auth_openssl",
        "FetchingJwksJwtVerifier",
        "OpenSslDpopSigner",
        "OpenSslDpopVerifier",
        "Explicit Non-Goals",
        "built-in HTTP JWKS client",
    ]:
        require_contains(auth_page, needle)

    stability_page = source / "docs/pages/stability.html"
    for needle in [
        "Ecosystem Evidence",
        "docs/adoption_ledger.md",
        "independent public downstream adoption",
    ]:
        require_contains(stability_page, needle)

    changelog = source / "CHANGELOG.md"
    require_contains(changelog, f"## {version}")
    require_contains(changelog, "Peer")
    require_contains(changelog, "Service")
    require_not_contains(changelog, "facade")

    compatibility = source / "docs/compatibility_policy.md"
    require_contains(compatibility, "canonical SDK path")
    require_contains(compatibility, "compatibility or convenience APIs")
    require_contains(compatibility, "release evidence")

    release_candidate_checklist = source / "docs/release_candidate_checklist.md"
    for needle in [
        "Ecosystem And Registry Review",
        "docs/adoption_ledger.md",
        "project-owned examples are not counted as adoption",
        "vcpkg curated-registry claims are not made",
        "release artifact verifier self-test",
        "unresolved source-marker",
        "Dependency review followed `docs/dependency_policy.md`",
        "vendored `tl::expected`",
        "`jsonrpcpp`",
        "`cpp-httplib`",
        "release-gates run id",
        "release-artifact-review.md",
        "--review-output",
        "without `--review-output` still passes",
    ]:
        require_contains(release_candidate_checklist, needle)

    request_lifecycle = source / "docs/request_lifecycle.md"
    require_contains(request_lifecycle, "Initialization Boundary")
    require_contains(request_lifecycle, "Direct calls to `Server::handle_request()`")
    require_contains(request_lifecycle, "notifications/initialized")
    require_contains(request_lifecycle, "Reconnect And Retry Boundary")
    require_contains(request_lifecycle, "not a generic `Transport<Role>` contract")

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

    rmcp_source_mapping = source / "docs/rmcp_source_mapping.json"
    for needle in [
        "rmcp_reference_commit",
        "c330fede90e4729c234f8e87fdbc5ea27a1dd10c",
        "model.rs",
        "content.rs",
        "capabilities.hpp",
        "task.hpp",
    ]:
        require_contains(rmcp_source_mapping, needle)

    rmcp_source_drift = source / "scripts/check_rmcp_source_drift.py"
    for needle in [
        "RMCP_REFERENCE_COMMIT",
        "MAPPINGS",
        "--write-mapping",
        "check_reference_checkout",
        "check_protocol_audit_doc",
    ]:
        require_contains(rmcp_source_drift, needle)

    release_notes = source / "docs/release_notes_template.md"
    require_contains(release_notes, "Canonical SDK Path")
    require_contains(release_notes, "Required Artifacts")
    require_contains(release_notes, "Checksums")
    require_contains(release_notes, "Dependency review")
    require_contains(release_notes, "`tl::expected` fallback/package-manager route")
    require_contains(release_notes, "private `jsonrpcpp` route")
    require_contains(release_notes, "hidden `cpp-httplib` route")
    require_contains(release_notes, "Windows ClangCL Ninja static runtime")
    require_contains(release_notes, "cxxmcp-package-vcpkg-default")
    require_contains(release_notes, "cxxmcp-package-vcpkg-auth")
    require_contains(release_notes, "cxxmcp-package-conan-default")
    require_contains(release_notes, "cxxmcp-package-conan-auth")
    require_contains(release_notes, "cxxmcp-package-xmake-default")
    require_contains(release_notes, "cxxmcp-package-xmake-auth")
    require_contains(release_notes, "cxxmcp-auth-openssl-release-gate-linux-gcc-ninja")
    require_contains(release_notes, "cxxmcp-performance-evidence-linux-gcc-ninja")
    require_contains(
        release_notes, "cxxmcp-public-header-compile-evidence-linux-gcc-ninja"
    )
    require_contains(release_notes, "release-artifact-review.md")
    require_contains(release_notes, "--review-output")
    require_contains(release_notes, "Platform scope: Ubuntu Linux only")

    release_process = source / "docs/release_process.md"
    for needle in [
        "semantic versioning",
        "Alpha",
        "Beta",
        "RC",
        "Stable",
        "versioned SDK source archive",
        "release-artifact-review.md",
        "--review-output",
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
    require_contains(workflow, "release-evidence/scripts")
    require_contains(workflow, "if-no-files-found: error")
    require_contains(workflow, "check_protocol_model_coverage.py")
    require_contains(workflow, "check_p2_todo_status.py")
    require_contains(workflow, "check_sdk_header_boundaries.py")
    require_contains(workflow, "check_source_markers.py --source .")
    require_contains(workflow, "check_package_auth_features.py")
    require_contains(workflow, "check_package_recipe_sync.py")
    require_contains(workflow, "build-config-smoke")
    require_contains(workflow, "performance-evidence-linux-gcc-ninja")
    require_contains(workflow, "public-header-compile-evidence-linux-gcc-ninja")
    require_contains(workflow, "protocol_serialization_benchmark")
    require_contains(workflow, "cxxmcp-performance-evidence-linux-gcc-ninja")
    require_contains(workflow, "collect_public_header_compile_evidence.py")
    require_contains(workflow, "selftest_release_artifacts.py")
    require_contains(workflow, "cxxmcp-public-header-compile-evidence-linux-gcc-ninja")
    require_contains(workflow, "performance-evidence.xml")
    require_contains(workflow, "clang-tidy-public-headers")
    require_contains(workflow, "scripts/run_clang_tidy.py")
    require_contains(workflow, "Check RMCP source drift")
    require_contains(workflow, "scripts/check_rmcp_source_drift.py --source .")
    require_contains(workflow, "collect_public_api_surface.py")
    require_contains(workflow, "compare_public_api_surface.py")
    require_contains(workflow, "selftest_public_api_surface.py")
    require_contains(workflow, "public-api-surface.json")
    require_contains(workflow, "package-manager-vcpkg")
    require_contains(workflow, "package-manager-conan")
    require_contains(workflow, "package-manager-xmake")
    require_contains(workflow, "cxxmcp-package-vcpkg-${{ matrix.name }}")
    require_contains(workflow, "cxxmcp-package-conan-${{ matrix.name }}")
    require_contains(workflow, "cxxmcp-package-xmake-${{ matrix.name }}")
    require_contains(workflow, "vcpkg-install.log")
    require_contains(workflow, "conan-create.log")
    require_contains(workflow, "xmake-build.log")
    require_contains(workflow, "re.subn(")
    require_contains(workflow, "expected one add_urls rewrite")
    require_contains(workflow, "add_repositories(\"cxxmcp-local repo\")")
    require_contains(workflow, "cxxmcp-xmake-source-${GITHUB_SHA}.tar.gz")
    require_contains(workflow, "auth-openssl-release-gate")
    require_contains(workflow, "CXXMCP_AUTH_CRYPTO=OpenSSL")
    require_contains(workflow, "cxxmcp-auth-openssl-release-gate-linux-gcc-ninja")
    require_contains(workflow, "auth-openssl-release-gate.xml")
    require_contains(workflow, "windows-clangcl-ninja-static-runtime")
    require_contains(workflow, "Release")
    require_contains(workflow, "typescript_sdk_reference=@modelcontextprotocol/sdk@1.29.0")
    require_contains(workflow, "python_sdk_reference=mcp==1.27.1")

    release_sdk = source / ".github/workflows/release-sdk.yml"
    require_contains(release_sdk, "release_gates_run_id")
    require_contains(release_sdk, "Verify matching release-gates run")
    require_contains(release_sdk, "status=success&head_sha=${RELEASE_COMMIT}")
    require_contains(release_sdk, "cxxmcp-release-gates-linux-gcc-ninja")
    require_contains(release_sdk, "cxxmcp-release-gates-windows-clangcl-ninja-static-runtime")
    require_contains(release_sdk, "cxxmcp-performance-evidence-linux-gcc-ninja")
    require_contains(release_sdk, "cxxmcp-public-header-compile-evidence-linux-gcc-ninja")
    require_contains(release_sdk, "cxxmcp-auth-release-gate-linux-gcc-ninja")
    require_contains(release_sdk, "cxxmcp-auth-openssl-release-gate-linux-gcc-ninja")
    require_contains(release_sdk, "cxxmcp-package-vcpkg-default")
    require_contains(release_sdk, "cxxmcp-package-vcpkg-auth")
    require_contains(release_sdk, "cxxmcp-package-conan-default")
    require_contains(release_sdk, "cxxmcp-package-conan-auth")
    require_contains(release_sdk, "cxxmcp-package-xmake-default")
    require_contains(release_sdk, "cxxmcp-package-xmake-auth")
    require_contains(release_sdk, "cxxmcp-doxygen-html")
    require_contains(release_sdk, "cxxmcp-source")
    require_contains(release_sdk, "cxxmcp-release-evidence")
    require_contains(release_sdk, "Download release-gates artifacts")
    require_contains(release_sdk, "Verify release-gates artifact contents")
    require_contains(release_sdk, "Verify release artifact contents")
    require_contains(release_sdk, "Verify assembled release artifact review")
    require_contains(release_sdk, "scripts/check_release_artifacts.py")
    require_contains(release_sdk, "--review-output release-artifacts/release-artifact-review.md")
    require_contains(release_sdk, "--commit \"${{ steps.release_identity.outputs.commit }}\"")
    require_contains(release_sdk, "--run-url \"${{ steps.release_gates.outputs.run_url }}\"")
    require_contains(release_sdk, "--release-url")
    require_contains(release_sdk, "Package release-gates evidence")
    require_contains(release_sdk, "cxxmcp-sdk-source-${tag}.tar.gz")
    require_contains(release_sdk, "-czf \"release-artifacts/${package}\"")
    require_contains(release_sdk, "scripts \\")
    require_contains(release_sdk, "SHA256SUMS.txt")
    require_contains(release_sdk, "release-artifact-review.md")
    require_contains(release_sdk, "sha256sum *.tar.gz > SHA256SUMS.txt")
    require_contains(release_sdk, "RELEASE_NOTES.md")
    require_contains(release_sdk, "Supported Matrix")
    require_contains(release_sdk, "Windows ClangCL Ninja static runtime")
    require_contains(release_sdk, "Ubuntu Linux vcpkg overlay, Conan, and xmake")
    require_contains(release_sdk, "OpenSSL-backed auth evidence is currently a Linux GCC Ninja release")
    require_contains(release_sdk, "Performance evidence is currently limited to the Linux GCC Ninja")
    require_contains(release_sdk, "Public-header compile-time evidence is currently limited")
    require_contains(release_sdk, "unless matching platform artifacts are attached")
    require_contains(release_sdk, "gh release upload")
    require_contains(release_sdk, "gh release create")
    require_contains(release_sdk, "find release-artifacts -maxdepth 1 -type f")
    require_contains(release_sdk, "--notes-file")
    require_contains(release_sdk, "Compatibility Notes")
    require_contains(release_sdk, "Static-library releases do not claim ABI stability")

    tests_cmake = source / "tests/CMakeLists.txt"
    require_contains(tests_cmake, "CXXMCP_INTEROP_TYPESCRIPT_SDK_VERSION")
    require_contains(tests_cmake, "CXXMCP_INTEROP_PYTHON_MCP_VERSION")
    require_contains(tests_cmake, "mcp==${CXXMCP_INTEROP_PYTHON_MCP_VERSION}")
    require_contains(tests_cmake, "cxxmcp_mark_release_blocking(auth_openssl")

    release_gate_manifest = source / "tests/release_gate_manifest.cmake"
    require_contains(release_gate_manifest, "auth_openssl")
    require_contains(release_gate_manifest, "foreach(header_test")
    require_contains(release_gate_manifest, "protocol error config auth transport")

    package_smoke = source / "tests/package_smoke.cmake"
    require_contains(package_smoke, "PACKAGE_SMOKE_GENERATOR")
    require_contains(package_smoke, "PACKAGE_SMOKE_CXX_COMPILER")
    require_contains(package_smoke, "package_smoke_auth_enabled")
    require_contains(package_smoke, "default package smoke must not install optional auth headers")
    require_contains(package_smoke, "assert_optional_component_missing")
    require_contains(package_smoke, "find_package(cxxmcp CONFIG REQUIRED COMPONENTS ${component_name})")
    require_contains(package_smoke, "component '${component_name}' was requested but is not installed")
    require_contains(package_smoke, "default package smoke must not install optional plugin headers")
    require_contains(package_smoke, "default package smoke must not install optional adapter headers")
    require_contains(package_smoke, "templates/external_consumer")

    release_artifacts = source / "scripts/check_release_artifacts.py"
    for needle in [
        "require_junit_tests",
        "require_nonempty_file",
        "require_nonempty_glob",
        "checked_tar_member_name",
        "duplicate checksum entry",
        "unsafe archive member name",
        "RELEASE_EVIDENCE_REQUIRED_FILES",
        "check_release_review_file",
        "vcpkg-install.log",
        "conan-create.log",
        "xmake-build.log",
        "cxxmcp-xmake-source-*.tar.gz",
        "\"repo\" / \"packages\" / \"c\" / \"cxxmcp\" / \"xmake.lua\"",
        "public_header_protocol",
        "public_header_sdk",
        "auth_openssl",
        "cxxmcp-auth-openssl-release-gate-linux-gcc-ninja",
        "auth-openssl-release-gate.xml",
        "cxxmcp-performance-evidence-linux-gcc-ninja",
        "cxxmcp-public-header-compile-evidence-linux-gcc-ninja",
        "performance-evidence.xml",
        "public-header-compile-evidence.json",
        "protocol_serialization_benchmark",
        "check_sdk_source_tarball",
        "check_release_tarball_contents",
        "scripts/collect_public_header_compile_evidence.py",
        "scripts/check_source_markers.py",
        "runtime/src/gateway.cpp",
        "tools/cli/src/main.cpp",
        "cxxmcp-release-evidence/{relative}",
    ]:
        require_contains(release_artifacts, needle)

    release_gates_doc = source / "docs/release_gates.md"
    require_contains(release_gates_doc, "parses the uploaded JUnit XML")
    require_contains(release_gates_doc, "OpenSSL auth")
    require_contains(release_gates_doc, "scripts/selftest_release_artifacts.py")
    require_contains(release_gates_doc, "scripts/check_source_markers.py")
    require_contains(release_gates_doc, "FIXME`/`HACK`/`XXX")
    require_contains(release_gates_doc, "evidence verifier scripts")
    require_contains(release_gates_doc, "cxxmcp-performance-evidence-linux-gcc-ninja")
    require_contains(
        release_gates_doc, "cxxmcp-public-header-compile-evidence-linux-gcc-ninja"
    )
    require_contains(release_gates_doc, "release-artifact-review.md")
    require_contains(release_gates_doc, "--review-output")
    require_contains(release_gates_doc, "without `--review-output`")
    require_contains(release_gates_doc, "vcpkg install log")
    require_contains(release_gates_doc, "create/install logs")
    require_contains(release_gates_doc, "repo and build logs")

    compile_evidence = source / "scripts/collect_public_header_compile_evidence.py"
    require_contains(compile_evidence, "mcp_public_header_protocol")
    require_contains(compile_evidence, "mcp_public_header_auth")
    require_contains(compile_evidence, "elapsed_seconds")

    release_artifact_selftest = source / "scripts/selftest_release_artifacts.py"
    require_contains(release_artifact_selftest, "check_release_artifacts")
    require_contains(release_artifact_selftest, "write_release_artifact_review")
    require_contains(release_artifact_selftest, "check_release_review_file")
    require_contains(release_artifact_selftest, "missing release-artifact-review.md must fail")
    require_contains(release_artifact_selftest, "empty evidence log must fail")
    require_contains(release_artifact_selftest, "failing JUnit testcase must fail")
    require_contains(release_artifact_selftest, "duplicate checksum entry must fail")
    require_contains(release_artifact_selftest, "unsafe tar member name must fail")
    require_contains(release_artifact_selftest, "vcpkg-install.log")
    require_contains(release_artifact_selftest, "conan-create.log")
    require_contains(release_artifact_selftest, "xmake-build.log")
    require_contains(
        release_artifact_selftest,
        "cxxmcp-public-header-compile-evidence-linux-gcc-ninja",
    )
    require_contains(release_artifact_selftest, "release-artifact-review.md")
    require_contains(
        release_artifact_selftest, "release artifact verifier self-test passed"
    )

    public_api_surface = source / "scripts/collect_public_api_surface.py"
    for needle in [
        "STABLE_TARGETS",
        "OPTIONAL_TARGETS",
        "public_headers",
        "optional_headers",
        "stable_cpp_namespace",
        "C++17",
    ]:
        require_contains(public_api_surface, needle)

    public_api_surface_compare = source / "scripts/compare_public_api_surface.py"
    for needle in [
        "STABLE_SCALAR_FIELDS",
        "STABLE_LIST_FIELDS",
        "public API surface comparison passed",
        "removed",
        "language_standard",
    ]:
        require_contains(public_api_surface_compare, needle)

    public_api_surface_selftest = source / "scripts/selftest_public_api_surface.py"
    for needle in [
        "public API surface self-test passed",
        "removed_header",
        "removed_optional_header",
        "removed_include_root",
        "removed_target",
        "scalar_change",
        "malformed_list",
    ]:
        require_contains(public_api_surface_selftest, needle)

    technical_audit = source / "docs/technical_audit.md"
    for needle in [
        "STILL PRESENT CODE DEFECTS",
        "0 open",
        "ACCEPTED LIMITATION",
        "TRACKED DEBT",
        "fact-standard claims still depend",
        "docs/dependency_policy.md",
    ]:
        require_contains(technical_audit, needle)
    require_technical_audit_evidence_map(technical_audit)

    source_markers = source / "scripts/check_source_markers.py"
    for needle in [
        "FORBIDDEN_MARKERS",
        "FIXME|HACK|XXX",
        "DEFAULT_ROOTS",
        "source marker check passed",
    ]:
        require_contains(source_markers, needle)

    package_smoke_auth = source / "tests/fixtures/package_smoke/auth.cpp"
    require_contains(package_smoke_auth, "cxxmcp/auth.hpp")
    require_contains(package_smoke_auth, "mcp::auth::")

    package_smoke_cmake = source / "tests/fixtures/package_smoke/CMakeLists.txt"
    require_contains(package_smoke_cmake, "cxxmcp::auth")

    template_cmake = source / "templates/external_consumer/CMakeLists.txt"
    require_contains(template_cmake, "find_package(cxxmcp CONFIG REQUIRED)")
    require_contains(template_cmake, "cxxmcp::sdk")

    example_markers = {
        "examples/client_loopback.cpp": "Compatibility example",
        "examples/task_async_client_server.cpp": "Compatibility example",
        "examples/stdio_server.cpp": "Comprehensive stdio server example",
        "examples/typed_stdio_server.cpp": "Typed tool registration example",
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
        "public-api-surface.json",
        "docs/adoption_ledger.md",
        "docs/auth_design.md",
        "docs/auth_user_guide.md",
        "docs/compatibility_policy.md",
        "docs/dependency_policy.md",
        "docs/capability_lifecycles.md",
        "docs/ecosystem_maturity_evidence.md",
        "docs/examples.md",
        "docs/official_sdk_candidate_process.md",
        "docs/package_consumption.md",
        "docs/package_consumption_zh.md",
        "docs/performance_debt.md",
        "docs/protocol_model_audit.md",
        "docs/public_api_stability.md",
        "docs/release_process.md",
        "docs/release_gates.md",
        "docs/release_candidate_checklist.md",
        "docs/release_notes_template.md",
        "docs/request_lifecycle.md",
        "docs/rmcp_source_mapping.json",
        "docs/runtime_gateway.md",
        "docs/technical_audit.md",
        "docs/pages/index.html",
        "docs/pages/getting-started.html",
        "docs/pages/auth.html",
        "docs/pages/stability.html",
        "scripts/check_package_auth_features.py",
        "scripts/check_package_recipe_sync.py",
        "scripts/check_p2_todo_status.py",
        "scripts/check_protocol_model_coverage.py",
        "scripts/check_release_artifacts.py",
        "scripts/check_release_evidence.py",
        "scripts/check_rmcp_source_drift.py",
        "scripts/check_sdk_header_boundaries.py",
        "scripts/check_source_markers.py",
        "scripts/compare_public_api_surface.py",
        "scripts/collect_public_api_surface.py",
        "scripts/collect_public_header_compile_evidence.py",
        "scripts/selftest_release_artifacts.py",
        "scripts/selftest_public_api_surface.py",
        "examples/CMakeLists.txt",
        "examples/auth_bearer_http.cpp",
        "examples/auth_dpop_openssl.cpp",
        "examples/server_stdio_peer.cpp",
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
    require_technical_audit_evidence_map(evidence / "docs/technical_audit.md")


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
