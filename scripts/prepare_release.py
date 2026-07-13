#!/usr/bin/env python3
"""Prepare local release metadata before tagging.

This script intentionally stops before committing, tagging, or pushing. It keeps
the repetitive version bump and release-precheck steps in one place so tag
creation stays a deliberate final action.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from create_sdk_source_archive import create_sdk_source_archive
from create_sdk_source_archive import sha256 as archive_sha256


ROOT = Path(__file__).resolve().parents[1]


def run(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    target = ROOT / path
    old = target.read_bytes() if target.exists() else b""
    newline = "\r\n" if b"\r\n" in old else "\n"
    if newline == "\r\n":
        text = text.replace("\n", "\r\n")
    data = text.encode("utf-8")
    if data == old:
        return
    target.write_bytes(data)


def replace(path: str, pattern: str, replacement: str, *, flags: int = 0) -> None:
    text = read(path)
    updated, count = re.subn(pattern, replacement, text, flags=flags)
    if count == 0:
        raise SystemExit(f"{path}: pattern did not match: {pattern}")
    write(path, updated)


def normalize_version(value: str) -> str:
    version = value.strip()
    if version.startswith("v"):
        version = version[1:]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit(f"expected semantic version like 1.2.3, got {value!r}")
    return version


def current_xmake_hash() -> str:
    text = read("packaging/xmake/packages/c/cxxmcp/xmake.lua")
    match = re.search(r'add_versions\("v\d+\.\d+\.\d+",\s*"([0-9a-fA-F]{64})"\)', text)
    if not match:
        raise SystemExit("could not find xmake SDK source archive hash")
    return match.group(1).lower()


def update_changelog(version: str) -> None:
    path = "CHANGELOG.md"
    text = read(path)
    if f"## {version}" in text:
        return
    marker = "# Changelog\n\n"
    if marker not in text:
        raise SystemExit("CHANGELOG.md missing '# Changelog' heading")
    entry = (
        f"## {version}\n\n"
        "- Prepared release metadata and package references.\n\n"
    )
    write(path, text.replace(marker, marker + entry, 1))


def update_package_docs(version: str, sha256: str) -> None:
    tag = f"v{version}"
    files = [
        "docs/package_consumption.md",
        "docs/package_consumption_zh.md",
        "docs/pages/cookbook.html",
        "docs/pages/troubleshooting.html",
    ]
    archive_pattern = re.compile(
        r"releases/download/v\d+\.\d+\.\d+/"
        r"(cxxmcp(?:-sdk-source)?-)v\d+\.\d+\.\d+\.tar\.gz"
    )
    for path in files:
        text = read(path)
        text = archive_pattern.sub(
            lambda match: f"releases/download/{tag}/cxxmcp-sdk-source-{tag}.tar.gz",
            text,
        )
        text = re.sub(r"`v\d+\.\d+\.\d+` URL", f"`{tag}` URL", text)
        text = re.sub(
            r"URL_HASH\s+SHA256=[0-9a-fA-F]{64}",
            f"URL_HASH SHA256={sha256}",
            text,
        )
        write(path, text)


def update_release_gates(version: str) -> None:
    path = ".github/workflows/release-gates.yml"
    text = read(path)
    text = re.sub(
        r"--requires=cxxmcp/\d+\.\d+\.\d+",
        '--requires="cxxmcp/${version}"',
        text,
    )
    text = re.sub(
        r'add_requires\("cxxmcp v\d+\.\d+\.\d+"',
        'add_requires("cxxmcp v${version}"',
        text,
    )
    write(path, text)


def update_versions(version: str, sha256: str) -> None:
    tag = f"v{version}"
    write("VERSION", version + "\n")
    replace("conanfile.py", r'version = "\d+\.\d+\.\d+"', f'version = "{version}"')
    replace("docs/Doxyfile", r"PROJECT_NUMBER\s+=\s+\d+\.\d+\.\d+", f"PROJECT_NUMBER         = {version}")

    replace(
        "packaging/vcpkg/ports/cxxmcp-sdk/vcpkg.json",
        r'"version-semver": "\d+\.\d+\.\d+"',
        f'"version-semver": "{version}"',
    )
    replace(
        "packaging/vcpkg/ports/cxxmcp-sdk/vcpkg.json",
        r'"port-version": \d+',
        '"port-version": 0',
    )

    replace(
        "packaging/xmake/packages/c/cxxmcp/xmake.lua",
        r'add_versions\("v\d+\.\d+\.\d+",\s*"[0-9a-fA-F]{64}"\)',
        f'add_versions("{tag}", "{sha256}")',
    )
    update_changelog(version)
    update_package_docs(version, sha256)
    update_release_gates(version)


def computed_sdk_source_sha256(version: str) -> str:
    tag = f"v{version}"
    with tempfile.TemporaryDirectory(prefix="cxxmcp-release-") as temp:
        archive = Path(temp) / f"cxxmcp-sdk-source-{tag}.tar.gz"
        create_sdk_source_archive(ROOT, tag, archive)
        return archive_sha256(archive)


def run_checks(skip_format: bool) -> None:
    if not skip_format:
        run(["pwsh", "-NoProfile", "-File", "scripts/format.ps1"])
        run(["pwsh", "-NoProfile", "-File", "scripts/format.ps1", "-Check"])
    for command in [
        [sys.executable, "-B", "scripts/check_package_recipe_sync.py", "--source", "."],
        [sys.executable, "-B", "scripts/check_release_evidence.py", "--source", "."],
        [sys.executable, "-B", "scripts/check_p2_todo_status.py", "--source", "."],
        [sys.executable, "-B", "scripts/selftest_release_artifacts.py"],
        [sys.executable, "-B", "scripts/selftest_public_api_surface.py"],
        [sys.executable, "-B", "scripts/check_source_markers.py", "--source", "."],
    ]:
        run(command)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("version", help="release version, for example 1.1.3 or v1.1.3")
    parser.add_argument(
        "--sdk-source-sha256",
        default=None,
        help=(
            "SDK source archive SHA256 to write into package examples; "
            "defaults to the deterministic SDK source archive hash"
        ),
    )
    parser.add_argument("--skip-format", action="store_true", help="do not run scripts/format.ps1")
    parser.add_argument("--skip-checks", action="store_true", help="only update files")
    args = parser.parse_args()

    version = normalize_version(args.version)
    if args.sdk_source_sha256:
        sha256 = args.sdk_source_sha256.lower()
        if not re.fullmatch(r"[0-9a-f]{64}", sha256):
            raise SystemExit("--sdk-source-sha256 must be a 64-character hex digest")
        update_versions(version, sha256)
    else:
        update_versions(version, current_xmake_hash())
        sha256 = computed_sdk_source_sha256(version)
        update_versions(version, sha256)

    if not args.skip_checks:
        run_checks(args.skip_format)
    print(f"Release metadata prepared for v{version}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
