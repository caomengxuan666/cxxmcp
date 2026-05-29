#!/usr/bin/env python3
"""Validate release-gates and release-sdk artifact contents."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import tarfile
from textwrap import dedent
import xml.etree.ElementTree as ET

RELEASE_EVIDENCE_REQUIRED_FILES = [
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

RELEASE_MATRIX_ARTIFACTS = [
    "cxxmcp-release-gates-linux-gcc-ninja",
    "cxxmcp-release-gates-linux-clang-ninja",
    "cxxmcp-release-gates-macos-appleclang-ninja",
    "cxxmcp-release-gates-windows-msvc-ninja-static-runtime",
    "cxxmcp-release-gates-windows-clangcl-ninja-static-runtime",
    "cxxmcp-release-gates-windows-msvc-vs-dynamic-runtime",
]

AUTH_ARTIFACTS = [
    "cxxmcp-auth-release-gate-linux-gcc-ninja",
    "cxxmcp-auth-release-gate-windows-msvc-ninja",
]

PACKAGE_ARTIFACTS = [
    "cxxmcp-package-vcpkg-default",
    "cxxmcp-package-vcpkg-http-auth",
    "cxxmcp-package-conan-default",
    "cxxmcp-package-conan-http-auth",
    "cxxmcp-package-xmake-default",
    "cxxmcp-package-xmake-http-auth",
]

REQUIRED_GATE_ARTIFACTS = [
    *RELEASE_MATRIX_ARTIFACTS,
    *AUTH_ARTIFACTS,
    "cxxmcp-auth-openssl-release-gate-linux-gcc-ninja",
    "cxxmcp-performance-evidence-linux-gcc-ninja",
    "cxxmcp-public-header-compile-evidence-linux-gcc-ninja",
    *PACKAGE_ARTIFACTS,
    "cxxmcp-doxygen-html",
    "cxxmcp-source",
    "cxxmcp-release-evidence",
]


def fail(message: str) -> None:
    raise SystemExit(f"release artifact check failed: {message}")


def require_dir(path: Path) -> None:
    if not path.is_dir():
        fail(f"missing required directory: {path}")


def require_file(path: Path) -> None:
    if not path.is_file():
        fail(f"missing required file: {path}")


def require_nonempty_file(path: Path) -> None:
    require_file(path)
    if path.stat().st_size == 0:
        fail(f"required file is empty: {path}")


def require_glob(root: Path, pattern: str) -> None:
    if not any(root.glob(pattern)):
        fail(f"{root} must contain {pattern}")


def require_nonempty_glob(root: Path, pattern: str) -> None:
    matches = list(root.glob(pattern))
    if not matches:
        fail(f"{root} must contain {pattern}")
    for path in matches:
        if path.is_file() and path.stat().st_size == 0:
            fail(f"required file is empty: {path}")


def xml_local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def require_junit_tests(path: Path, required_tests: list[str]) -> None:
    require_file(path)
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as error:
        fail(f"{path}: invalid JUnit XML: {error}")

    observed = set()
    for testcase in root.iter():
        if xml_local_name(testcase.tag) != "testcase":
            continue
        for attribute in ["name", "classname"]:
            value = testcase.attrib.get(attribute)
            if value:
                observed.add(value)
        for result in testcase:
            result_name = xml_local_name(result.tag)
            if result_name in {"failure", "error"}:
                name = testcase.attrib.get("name") or testcase.attrib.get("classname")
                fail(f"{path}: test case {name!r} contains {result_name}")

    missing = [name for name in required_tests if name not in observed]
    if missing:
        fail(f"{path}: missing expected test case(s): {', '.join(missing)}")


def artifact_root(gate_artifacts: Path, name: str) -> Path:
    path = gate_artifacts / name
    require_dir(path)
    return path


def check_release_gate_matrix_artifact(
    root: Path, junit_name: str, required_tests: list[str]
) -> None:
    # Support both flat layout and nested build-release-gates/ layout.
    candidate = root / "build-release-gates"
    if candidate.is_dir():
        root = candidate
    require_file(root / "CMakeCache.txt")
    require_junit_tests(root / "test-results" / junit_name, required_tests)
    require_glob(root, "Testing/Temporary/*.log")


def check_package_artifact(root: Path, kind: str) -> None:
    if kind in {"vcpkg", "conan"}:
        require_glob(root, "**/CMakeCache.txt")
        require_glob(root, "**/CMakeConfigureLog.yaml")
        require_nonempty_glob(root, "**/cmake-configure.log")
        require_nonempty_glob(root, "**/cmake-build.log")
        if kind == "vcpkg":
            require_nonempty_glob(root, "**/vcpkg-install.log")
            require_nonempty_glob(root, "**/status")
        else:
            require_nonempty_glob(root, "**/conan-create.log")
            require_nonempty_glob(root, "**/conan-install.log")
        return
    if kind == "xmake":
        require_file(root / "xmake.lua")
        require_file(root / "main.cpp")
        require_file(root / "repo" / "packages" / "c" / "cxxmcp" / "xmake.lua")
        require_nonempty_glob(root, "cxxmcp-xmake-source-*.tar.gz")
        require_nonempty_file(root / "xmake-repo.log")
        require_nonempty_file(root / "xmake-build.log")
        require_glob(root, "build/**")
        return
    fail(f"unknown package artifact kind: {kind}")


def check_public_header_compile_artifact(root: Path) -> None:
    require_file(root / "CMakeCache.txt")
    evidence = root / "public-header-compile-evidence.json"
    require_file(evidence)
    try:
        payload = json.loads(evidence.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        fail(f"{evidence}: invalid JSON: {error}")

    required_targets = {
        "mcp_public_header_protocol",
        "mcp_public_header_error",
        "mcp_public_header_config",
        "mcp_public_header_auth",
        "mcp_public_header_transport",
        "mcp_public_header_client",
        "mcp_public_header_server",
        "mcp_public_header_peer",
        "mcp_public_header_handler",
        "mcp_public_header_service",
        "mcp_public_header_sdk",
    }
    observed_targets = set()
    for entry in payload.get("targets", []):
        target = entry.get("target")
        if target:
            observed_targets.add(target)
        if entry.get("returncode") != 0:
            fail(f"{evidence}: target {target!r} did not build successfully")
        elapsed = entry.get("elapsed_seconds")
        if not isinstance(elapsed, (int, float)) or elapsed < 0:
            fail(f"{evidence}: target {target!r} has invalid elapsed_seconds")

    missing = sorted(required_targets - observed_targets)
    if missing:
        fail(f"{evidence}: missing target timing(s): {', '.join(missing)}")


def check_gate_artifacts(gate_artifacts: Path) -> None:
    default_required_tests = [
        "protocol",
        "sdk_boundary",
        "release_gate_manifest",
        "transport_contract",
        "transport_stdio_contract",
        "client_server",
        "stdio_transport",
        "transport_adapters",
        "http_transport",
        "rmcp_conformance",
        "sdk",
        "public_targets",
        "package_smoke",
        "process_stdio_transport",
        "interop_typescript_client_process_stdio",
        "interop_python_client_process_stdio",
        "interop_rmcp_client_process_stdio",
        "public_header_protocol",
        "public_header_error",
        "public_header_config",
        "public_header_transport",
        "public_header_client",
        "public_header_server",
        "public_header_peer",
        "public_header_handler",
        "public_header_service",
        "public_header_sdk",
    ]
    auth_required_tests = [
        "auth",
        "public_header_auth",
        "package_smoke",
    ]
    auth_openssl_required_tests = [
        "auth",
        "auth_openssl",
        "public_header_auth",
        "package_smoke",
    ]
    performance_required_tests = [
        "protocol_serialization_benchmark",
    ]

    for artifact_name in RELEASE_MATRIX_ARTIFACTS:
        check_release_gate_matrix_artifact(
            artifact_root(gate_artifacts, artifact_name),
            "release-blocking.xml",
            default_required_tests,
        )

    for artifact_name in AUTH_ARTIFACTS:
        check_release_gate_matrix_artifact(
            artifact_root(gate_artifacts, artifact_name),
            "auth-release-gate.xml",
            auth_required_tests,
        )

    check_release_gate_matrix_artifact(
        artifact_root(
            gate_artifacts, "cxxmcp-auth-openssl-release-gate-linux-gcc-ninja"
        ),
        "auth-openssl-release-gate.xml",
        auth_openssl_required_tests,
    )

    check_release_gate_matrix_artifact(
        artifact_root(gate_artifacts, "cxxmcp-performance-evidence-linux-gcc-ninja"),
        "performance-evidence.xml",
        performance_required_tests,
    )

    check_public_header_compile_artifact(
        artifact_root(
            gate_artifacts, "cxxmcp-public-header-compile-evidence-linux-gcc-ninja"
        )
    )

    for artifact_name in PACKAGE_ARTIFACTS:
        if "-vcpkg-" in artifact_name:
            check_package_artifact(artifact_root(gate_artifacts, artifact_name), "vcpkg")
        elif "-conan-" in artifact_name:
            check_package_artifact(artifact_root(gate_artifacts, artifact_name), "conan")
        elif "-xmake-" in artifact_name:
            check_package_artifact(artifact_root(gate_artifacts, artifact_name), "xmake")

    require_file(artifact_root(gate_artifacts, "cxxmcp-doxygen-html") / "index.html")

    source = artifact_root(gate_artifacts, "cxxmcp-source")
    require_glob(source, "cxxmcp-source-*.tar.gz")
    require_file(source / "SHA256SUMS.txt")

    evidence = artifact_root(gate_artifacts, "cxxmcp-release-evidence")
    for relative in RELEASE_EVIDENCE_REQUIRED_FILES:
        require_file(evidence / relative)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_sha256sums(release_artifacts: Path, tarballs: list[Path]) -> None:
    sums_path = release_artifacts / "SHA256SUMS.txt"
    require_file(sums_path)
    observed: dict[str, str] = {}
    for line_number, line in enumerate(
        sums_path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        parts = line.split()
        if len(parts) != 2:
            fail(f"{sums_path}:{line_number}: expected '<sha256> <filename>'")
        digest, filename = parts
        if len(digest) != 64 or any(ch not in "0123456789abcdefABCDEF" for ch in digest):
            fail(f"{sums_path}:{line_number}: invalid sha256 digest")
        if filename != Path(filename).name:
            fail(f"{sums_path}:{line_number}: checksum filename must be a basename")
        if filename in observed:
            fail(f"{sums_path}:{line_number}: duplicate checksum entry for {filename}")
        observed[filename] = digest.lower()

    expected_names = {path.name for path in tarballs}
    if set(observed) != expected_names:
        fail(
            f"{sums_path}: checksum entries {sorted(observed)} do not match "
            f"release tarballs {sorted(expected_names)}"
        )

    for path in tarballs:
        actual = sha256(path)
        expected = observed[path.name]
        if actual != expected:
            fail(f"{sums_path}: checksum mismatch for {path.name}")


def strip_archive_prefix(name: str) -> str:
    parts = name.split("/", 1)
    return parts[1] if len(parts) == 2 else name


def checked_tar_member_name(path: Path, name: str) -> str:
    normalized = name.removeprefix("./")
    if (
        not normalized
        or normalized.startswith("/")
        or "\\" in normalized
        or any(part in {"", ".", ".."} for part in normalized.split("/"))
    ):
        fail(f"{path}: unsafe archive member name: {name}")
    return normalized


def check_sdk_source_tarball(path: Path) -> None:
    try:
        with tarfile.open(path, "r:gz") as archive:
            names = {
                strip_archive_prefix(checked_tar_member_name(path, member.name))
                for member in archive.getmembers()
            }
    except tarfile.TarError as error:
        fail(f"{path}: invalid tar.gz archive: {error}")

    required = [
        "CMakeLists.txt",
        "CMakePresets.json",
        "sdk/include/cxxmcp/peer.hpp",
        "sdk/protocol/include/cxxmcp/protocol/types.hpp",
        "docs/release_gates.md",
        "docs/release_candidate_checklist.md",
        "scripts/check_release_artifacts.py",
        "scripts/check_release_evidence.py",
        "scripts/check_rmcp_source_drift.py",
        "scripts/check_source_markers.py",
        "scripts/compare_public_api_surface.py",
        "scripts/collect_public_api_surface.py",
        "scripts/collect_public_header_compile_evidence.py",
        "scripts/selftest_public_api_surface.py",
        "tests/package_smoke.cmake",
        "templates/external_consumer/CMakeLists.txt",
        "third_party/jsonrpcpp/jsonrpcpp.hpp",
    ]
    for relative in required:
        if relative not in names:
            fail(f"{path}: source archive missing {relative}")

    forbidden = [
        "docs/doxygen/html/index.html",
        "runtime/src/gateway.cpp",
        "tools/cli/src/main.cpp",
    ]
    for relative in forbidden:
        if relative in names:
            fail(f"{path}: source archive must not contain {relative}")


def tar_member_names(path: Path) -> set[str]:
    try:
        with tarfile.open(path, "r:gz") as archive:
            return {
                checked_tar_member_name(path, member.name)
                for member in archive.getmembers()
            }
    except tarfile.TarError as error:
        fail(f"{path}: invalid tar.gz archive: {error}")


def require_tar_members(path: Path, required: list[str]) -> None:
    names = tar_member_names(path)
    for member in required:
        if member not in names:
            fail(f"{path}: archive missing {member}")


def check_release_tarball_contents(release_artifacts: Path, tag: str) -> None:
    require_tar_members(
        release_artifacts / f"cxxmcp-release-gates-{tag}.tar.gz",
        REQUIRED_GATE_ARTIFACTS,
    )
    require_tar_members(
        release_artifacts / f"cxxmcp-doxygen-html-{tag}.tar.gz",
        ["cxxmcp-doxygen-html/index.html"],
    )
    require_tar_members(
        release_artifacts / f"cxxmcp-release-gate-source-{tag}.tar.gz",
        ["cxxmcp-source/SHA256SUMS.txt"],
    )
    require_tar_members(
        release_artifacts / f"cxxmcp-release-evidence-{tag}.tar.gz",
        [f"cxxmcp-release-evidence/{relative}" for relative in RELEASE_EVIDENCE_REQUIRED_FILES],
    )


def check_release_artifacts(release_artifacts: Path, tag: str) -> None:
    require_dir(release_artifacts)
    expected = [
        f"cxxmcp-sdk-source-{tag}.tar.gz",
        f"cxxmcp-release-gates-{tag}.tar.gz",
        f"cxxmcp-doxygen-html-{tag}.tar.gz",
        f"cxxmcp-release-gate-source-{tag}.tar.gz",
        f"cxxmcp-release-evidence-{tag}.tar.gz",
    ]
    tarballs: list[Path] = []
    for name in expected:
        path = release_artifacts / name
        require_file(path)
        tarballs.append(path)

    require_file(release_artifacts / "RELEASE_NOTES.md")
    check_sha256sums(release_artifacts, tarballs)
    check_sdk_source_tarball(release_artifacts / f"cxxmcp-sdk-source-{tag}.tar.gz")
    check_release_tarball_contents(release_artifacts, tag)


def check_release_review_file(path: Path, tag: str | None = None) -> None:
    require_file(path)
    text = path.read_text(encoding="utf-8")
    required = [
        "Release Artifact Review",
        "Gate Artifacts Reviewed",
        "Manual Review Required Before Publishing",
        "cxxmcp-release-gates-linux-gcc-ninja",
        "cxxmcp-public-header-compile-evidence-linux-gcc-ninja",
        "Release-gates run",
    ]
    if tag:
        required.append(tag)
    for needle in required:
        if needle not in text:
            fail(f"{path}: missing expected review text: {needle}")


def review_line(label: str, value: str | None) -> str:
    if value:
        return f"- {label}: `{value}`"
    return f"- {label}: _fill before publishing_"


def write_release_artifact_review(
    output: Path,
    gate_artifacts: Path,
    release_artifacts: Path | None,
    tag: str | None,
    commit: str | None,
    run_url: str | None,
    release_url: str | None,
) -> None:
    """Write a human review record after artifact validation succeeds."""

    lines = [
        "# Release Artifact Review",
        "",
        "This review record was generated after `scripts/check_release_artifacts.py`",
        "validated the downloaded release-gates artifacts. It is evidence that the",
        "listed artifact set was present and structurally checked; it is not a",
        "substitute for a green workflow run from the exact release commit.",
        "",
        "## Identity",
        "",
        review_line("Tag", tag),
        review_line("Commit SHA", commit),
        review_line("Release-gates run", run_url),
        review_line("Release URL", release_url),
        review_line("Downloaded gate artifacts", str(gate_artifacts)),
    ]
    if release_artifacts:
        lines.append(review_line("Final release artifacts", str(release_artifacts)))

    lines.extend(
        [
            "",
            "## Gate Artifacts Reviewed",
            "",
        ]
    )
    for artifact in REQUIRED_GATE_ARTIFACTS:
        lines.append(f"- [x] `{artifact}`")

    if release_artifacts and tag:
        lines.extend(
            [
                "",
                "## Final Release Artifacts Reviewed",
                "",
                f"- [x] `cxxmcp-sdk-source-{tag}.tar.gz`",
                f"- [x] `cxxmcp-release-gates-{tag}.tar.gz`",
                f"- [x] `cxxmcp-doxygen-html-{tag}.tar.gz`",
                f"- [x] `cxxmcp-release-gate-source-{tag}.tar.gz`",
                f"- [x] `cxxmcp-release-evidence-{tag}.tar.gz`",
                "- [x] `SHA256SUMS.txt`",
                "- [x] `RELEASE_NOTES.md`",
            ]
        )

    lines.extend(
        dedent(
            """

            ## Manual Review Required Before Publishing

            - [ ] The tag and commit above match the advertised release candidate.
            - [ ] The linked workflow run is green and was produced from that commit.
            - [ ] README, changelog, compatibility policy, release notes, generated
                  docs, and `todo.md` describe the same public SDK surface.
            - [ ] Public-header compile-time evidence was reviewed before making
                  `json_fwd` or `extern template` decisions.
            - [ ] Package-manager artifacts match the compiler/generator/runtime
                  matrix claimed in the release notes.
            - [ ] Adoption and vcpkg curated-registry claims remain conservative
                  unless `docs/adoption_ledger.md` has independent public downstream
                  evidence.
            """
        ).strip().splitlines()
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    check_release_review_file(output, tag)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--gate-artifacts",
        required=True,
        help="directory produced by gh run download for release-gates artifacts",
    )
    parser.add_argument(
        "--release-artifacts",
        help="optional final release-artifacts directory produced by release-sdk",
    )
    parser.add_argument(
        "--tag",
        help="release tag such as v2.0.2; required with --release-artifacts",
    )
    parser.add_argument(
        "--commit",
        help="optional exact release commit SHA to record in --review-output",
    )
    parser.add_argument(
        "--run-url",
        help="optional release-gates workflow URL to record in --review-output",
    )
    parser.add_argument(
        "--release-url",
        help="optional GitHub release URL to record in --review-output",
    )
    parser.add_argument(
        "--review-output",
        help="optional markdown path for a human release artifact review record",
    )
    args = parser.parse_args()

    gate_artifacts = Path(args.gate_artifacts).resolve()
    require_dir(gate_artifacts)
    check_gate_artifacts(gate_artifacts)

    release_artifacts: Path | None = None
    if args.release_artifacts:
        if not args.tag:
            fail("--tag is required when --release-artifacts is set")
        release_artifacts = Path(args.release_artifacts).resolve()
        check_release_artifacts(release_artifacts, args.tag)

    if args.review_output:
        write_release_artifact_review(
            Path(args.review_output).resolve(),
            gate_artifacts,
            release_artifacts,
            args.tag,
            args.commit,
            args.run_url,
            args.release_url,
        )
    elif release_artifacts:
        check_release_review_file(
            release_artifacts / "release-artifact-review.md", args.tag
        )

    print("release artifact check passed")


if __name__ == "__main__":
    main()
