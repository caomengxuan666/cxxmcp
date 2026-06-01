#!/usr/bin/env python3
"""Self-test the release artifact verifier with synthetic artifacts."""

from __future__ import annotations

import hashlib
import io
from pathlib import Path
import tarfile
import tempfile

import check_release_artifacts


DEFAULT_TESTS = [
    "protocol",
    "sdk_boundary",
    "release_gate_manifest",
    "transport_contract",
    "transport_stdio_contract",
    "client_server",
    "stdio_transport",
    "transport_adapters",
    "http_transport",
    "websocket_transport",
    "rmcp_conformance",
    "sdk",
    "public_targets",
    "package_smoke",
    "interop_typescript_client_process_stdio",
    "interop_python_client_process_stdio",
    "interop_rmcp_client_process_stdio",
    "public_header_protocol",
    "public_header_error",
    "public_header_config",
    "public_header_transport",
    "public_header_websocket_transport",
    "public_header_client",
    "public_header_server",
    "public_header_peer",
    "public_header_handler",
    "public_header_service",
    "public_header_sdk",
]

PUBLIC_HEADER_TARGETS = [
    "mcp_public_header_protocol",
    "mcp_public_header_error",
    "mcp_public_header_config",
    "mcp_public_header_auth",
    "mcp_public_header_transport",
    "mcp_public_header_websocket_transport",
    "mcp_public_header_client",
    "mcp_public_header_server",
    "mcp_public_header_peer",
    "mcp_public_header_handler",
    "mcp_public_header_service",
    "mcp_public_header_sdk",
]


def write(path: Path, text: str = "x") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_junit(path: Path, names: list[str]) -> None:
    cases = "".join(f'<testcase name="{name}"/>' for name in names)
    write(path, f"<testsuite>{cases}</testsuite>")


def write_failing_junit(path: Path, name: str) -> None:
    write(path, f'<testsuite><testcase name="{name}"><failure/></testcase></testsuite>')


def add_gate_artifact(root: Path, name: str, xml_name: str, tests: list[str]) -> None:
    artifact = root / name
    write(artifact / "CMakeCache.txt", "cache")
    write(artifact / "Testing" / "Temporary" / "LastTest.log", "log")
    write_junit(artifact / "test-results" / xml_name, tests)


def add_package_artifact(root: Path, name: str, kind: str) -> None:
    artifact = root / name
    if kind == "xmake":
        write(artifact / "xmake.lua", 'target("consumer")')
        write(artifact / "main.cpp", "int main() { return 0; }")
        write(artifact / "repo" / "packages" / "c" / "cxxmcp" / "xmake.lua", "package")
        write(artifact / "cxxmcp-xmake-source-test.tar.gz", "archive")
        write(artifact / "xmake-repo.log", "repo")
        write(artifact / "xmake-build.log", "build")
        (artifact / "build" / "dummy").mkdir(parents=True, exist_ok=True)
        return
    write(artifact / "consumer" / "CMakeCache.txt", "cache")
    write(artifact / "consumer" / "CMakeConfigureLog.yaml", "log")
    write(artifact / "consumer" / "cmake-configure.log", "configure")
    write(artifact / "consumer" / "cmake-build.log", "build")
    if kind == "vcpkg":
        write(artifact / "consumer" / "vcpkg-install.log", "install")
        write(artifact / "vcpkg" / "installed" / "vcpkg" / "status", "status")
    if kind == "conan":
        write(artifact / "consumer" / "conan-create.log", "create")
        write(artifact / "consumer" / "conan-install.log", "install")


def add_tree_to_tar(archive: tarfile.TarFile, root: Path, name: str) -> None:
    archive.add(root / name, arcname=name)


def create_tar(path: Path, root: Path, names: list[str]) -> None:
    with tarfile.open(path, "w:gz") as archive:
        for name in names:
            add_tree_to_tar(archive, root, name)


def create_unsafe_tar(path: Path) -> None:
    payload = b"x"
    info = tarfile.TarInfo("../escape.txt")
    info.size = len(payload)
    with tarfile.open(path, "w:gz") as archive:
        archive.addfile(info, io.BytesIO(payload))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def create_gate_artifacts(root: Path) -> None:
    for name in [
        "linux-gcc-ninja",
        "linux-clang-ninja",
        "macos-appleclang-ninja",
        "windows-msvc-ninja-static-runtime",
        "windows-clangcl-ninja-static-runtime",
        "windows-msvc-vs-dynamic-runtime",
    ]:
        add_gate_artifact(
            root, f"cxxmcp-release-gates-{name}", "release-blocking.xml", DEFAULT_TESTS
        )

    for name in ["linux-gcc-ninja", "windows-msvc-ninja"]:
        add_gate_artifact(
            root,
            f"cxxmcp-auth-release-gate-{name}",
            "auth-release-gate.xml",
            ["auth", "public_header_auth", "package_smoke"],
        )

    add_gate_artifact(
        root,
        "cxxmcp-auth-openssl-release-gate-linux-gcc-ninja",
        "auth-openssl-release-gate.xml",
        ["auth", "auth_openssl", "public_header_auth", "package_smoke"],
    )
    add_gate_artifact(
        root,
        "cxxmcp-performance-evidence-linux-gcc-ninja",
        "performance-evidence.xml",
        ["protocol_serialization_benchmark"],
    )

    public_header = root / "cxxmcp-public-header-compile-evidence-linux-gcc-ninja"
    write(public_header / "CMakeCache.txt", "cache")
    entries = ",".join(
        f'{{"target":"{target}","elapsed_seconds":0.1,"returncode":0}}'
        for target in PUBLIC_HEADER_TARGETS
    )
    write(public_header / "public-header-compile-evidence.json", f'{{"targets":[{entries}]}}')

    for feature in [
        "default",
        "http",
        "websocket",
        "http-openssl",
        "websocket-openssl",
        "http-auth",
        "websocket-auth",
        "http-auth-openssl",
    ]:
        add_package_artifact(root, f"cxxmcp-package-vcpkg-{feature}", "vcpkg")

    for manager in ["conan", "xmake"]:
        for feature in ["default", "http", "websocket", "http-auth", "websocket-auth"]:
            add_package_artifact(root, f"cxxmcp-package-{manager}-{feature}", manager)

    write(root / "cxxmcp-doxygen-html" / "index.html", "<html></html>")
    write(root / "cxxmcp-source" / "cxxmcp-source-test.tar.gz", "tar")
    write(root / "cxxmcp-source" / "SHA256SUMS.txt", "sum cxxmcp-source-test.tar.gz")

    for relative in check_release_artifacts.RELEASE_EVIDENCE_REQUIRED_FILES:
        write(root / "cxxmcp-release-evidence" / relative)


def create_sdk_source_tarball(release: Path, root: Path, tag: str) -> None:
    prefix = root / f"cxxmcp-sdk-{tag}"
    for relative in [
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
    ]:
        write(prefix / relative)
    create_tar(release / f"cxxmcp-sdk-source-{tag}.tar.gz", root, [prefix.name])


def create_release_artifacts(release: Path, gate: Path, source_root: Path, tag: str) -> None:
    release.mkdir(parents=True, exist_ok=True)
    create_sdk_source_tarball(release, source_root, tag)
    create_tar(
        release / f"cxxmcp-release-gates-{tag}.tar.gz",
        gate,
        check_release_artifacts.RELEASE_GATE_BUNDLE_ARTIFACTS,
    )
    create_tar(release / f"cxxmcp-doxygen-html-{tag}.tar.gz", gate, ["cxxmcp-doxygen-html"])
    create_tar(release / f"cxxmcp-release-gate-source-{tag}.tar.gz", gate, ["cxxmcp-source"])
    create_tar(
        release / f"cxxmcp-release-evidence-{tag}.tar.gz",
        gate,
        ["cxxmcp-release-evidence"],
    )
    write(release / "RELEASE_NOTES.md", "notes")
    sums = []
    for tarball in sorted(release.glob("*.tar.gz")):
        sums.append(f"{sha256(tarball)} {tarball.name}")
    write(release / "SHA256SUMS.txt", "\n".join(sums) + "\n")


def main() -> None:
    tag = "vtest"
    with tempfile.TemporaryDirectory(prefix="cxxmcp-release-artifact-selftest-") as tmp:
        root = Path(tmp)
        gate = root / "gate"
        release = root / "release"
        source_root = root / "source"
        create_gate_artifacts(gate)
        create_release_artifacts(release, gate, source_root, tag)
        empty_log = root / "empty.log"
        write(empty_log, "")
        try:
            check_release_artifacts.require_nonempty_file(empty_log)
        except SystemExit as error:
            assert "required file is empty" in str(error)
        else:
            raise AssertionError("empty evidence log must fail")
        check_release_artifacts.check_gate_artifacts(gate)
        check_release_artifacts.check_release_artifacts(release, tag)

        failing_junit = root / "failing.xml"
        write_failing_junit(failing_junit, "protocol")
        try:
            check_release_artifacts.require_junit_tests(failing_junit, ["protocol"])
        except SystemExit as error:
            assert "contains failure" in str(error)
        else:
            raise AssertionError("failing JUnit testcase must fail")

        sums_path = release / "SHA256SUMS.txt"
        valid_sums = sums_path.read_text(encoding="utf-8")
        tarball = release / f"cxxmcp-sdk-source-{tag}.tar.gz"
        duplicate = f"{sha256(tarball)} {tarball.name}\n{sha256(tarball)} {tarball.name}\n"
        write(sums_path, duplicate)
        try:
            check_release_artifacts.check_sha256sums(release, [tarball])
        except SystemExit as error:
            assert "duplicate checksum entry" in str(error)
        else:
            raise AssertionError("duplicate checksum entry must fail")
        write(sums_path, valid_sums)

        unsafe_tar = root / "unsafe.tar.gz"
        create_unsafe_tar(unsafe_tar)
        try:
            check_release_artifacts.require_tar_members(unsafe_tar, ["escape.txt"])
        except SystemExit as error:
            assert "unsafe archive member name" in str(error)
        else:
            raise AssertionError("unsafe tar member name must fail")

        review = root / "release-artifact-review.md"
        try:
            check_release_artifacts.check_release_review_file(review, tag)
        except SystemExit as error:
            assert "missing required file" in str(error)
        else:
            raise AssertionError("missing release-artifact-review.md must fail")
        check_release_artifacts.write_release_artifact_review(
            review,
            gate,
            release,
            tag,
            "0123456789abcdef0123456789abcdef01234567",
            "https://github.com/example/cxxmcp/actions/runs/1",
            "https://github.com/example/cxxmcp/releases/tag/vtest",
        )
        text = review.read_text(encoding="utf-8")
        assert "Release Artifact Review" in text
        assert "cxxmcp-release-gates-linux-gcc-ninja" in text
        assert f"cxxmcp-sdk-source-{tag}.tar.gz" in text
        assert "Manual Review Required Before Publishing" in text
        check_release_artifacts.check_release_review_file(review, tag)
    print("release artifact verifier self-test passed")


if __name__ == "__main__":
    main()
