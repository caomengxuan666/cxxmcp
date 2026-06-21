#!/usr/bin/env python3
"""Validate package recipe versions and source archive hashes."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


SOURCE_ARCHIVE_SHA256_RE = r"[A-Fa-f0-9]{64}"
SEMVER_RE = r"[0-9]+\.[0-9]+\.[0-9]+"


def read_text(path: Path, errors: list[str]) -> str:
    if not path.is_file():
        errors.append(f"missing required file: {path}")
        return ""
    return path.read_text(encoding="utf-8")


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def project_version(source: Path, errors: list[str]) -> str:
    path = source / "CMakeLists.txt"
    text = read_text(path, errors)
    match = re.search(
        rf"project\s*\(\s*cxxmcp\b(?:(?!\)).)*?\bVERSION\s+({SEMVER_RE})",
        text,
        re.DOTALL,
    )
    if match:
        return match.group(1)
    # Support VERSION loaded from a file variable: project(cxxmcp VERSION ${VAR})
    var_match = re.search(
        r"project\s*\(\s*cxxmcp\b(?:(?!\)).)*?\bVERSION\s+\$\{[^}]+\}",
        text,
        re.DOTALL,
    )
    if var_match:
        version_file = source / "VERSION"
        if version_file.is_file():
            return version_file.read_text(encoding="utf-8").strip()
    errors.append(
        f"{path}: must declare project(cxxmcp VERSION <major.minor.patch>)"
    )
    return ""


def require_equal(
    errors: list[str],
    path: Path,
    label: str,
    actual: str | None,
    expected: str,
) -> None:
    if actual is None:
        errors.append(f"{path}: missing {label}; expected {expected}")
    elif actual != expected:
        errors.append(f"{path}: {label} is {actual}; expected {expected}")


def check_conanfile(source: Path, version: str, errors: list[str]) -> None:
    path = source / "conanfile.py"
    text = read_text(path, errors)
    match = re.search(r'^\s*version\s*=\s*"([^"]+)"', text, re.MULTILINE)
    require_equal(
        errors,
        path,
        'Conan recipe version field `version = "..."`',
        match.group(1) if match else None,
        version,
    )


def check_vcpkg_json(source: Path, version: str, errors: list[str]) -> None:
    path = source / "packaging/vcpkg/ports/cxxmcp-sdk/vcpkg.json"
    text = read_text(path, errors)
    if not text:
        return
    try:
        manifest = json.loads(text)
    except json.JSONDecodeError as exc:
        errors.append(f"{path}: invalid JSON at line {exc.lineno}: {exc.msg}")
        return
    actual = manifest.get("version-semver")
    if not isinstance(actual, str):
        errors.append(f"{path}: missing string field `version-semver`; expected {version}")
        return
    require_equal(errors, path, "`version-semver`", actual, version)


def check_xmake_recipe(
    source: Path,
    version: str,
    expected_tag: str,
    hashes: list[tuple[Path, int, str]],
    errors: list[str],
) -> None:
    path = source / "packaging/xmake/packages/c/cxxmcp/xmake.lua"
    text = read_text(path, errors)
    if not text:
        return

    if "cxxmcp-sdk-source-$(version).tar.gz" not in text:
        errors.append(
            f"{path}: add_urls must use the SDK source archive "
            "`cxxmcp-sdk-source-$(version).tar.gz`"
        )

    versions = list(
        re.finditer(
            rf'add_versions\("([^"]+)",\s*"({SOURCE_ARCHIVE_SHA256_RE})"\)',
            text,
        )
    )
    if not versions:
        errors.append(
            f"{path}: missing add_versions(\"{expected_tag}\", "
            '"<64-character SHA256>")'
        )
        return

    matching = [match for match in versions if match.group(1) == expected_tag]
    if not matching:
        observed = ", ".join(match.group(1) for match in versions)
        errors.append(
            f"{path}: missing add_versions for {expected_tag}; observed {observed}"
        )
        return

    for match in matching:
        hashes.append((path, line_number(text, match.start(2)), match.group(2).lower()))


def check_package_docs(
    source: Path,
    version: str,
    expected_tag: str,
    hashes: list[tuple[Path, int, str]],
    errors: list[str],
) -> None:
    archive_url_re = re.compile(
        rf"https://github\.com/caomengxuan666/cxxmcp/releases/download/"
        rf"(v?{SEMVER_RE})/cxxmcp-sdk-source-(v?{SEMVER_RE})\.tar\.gz"
    )
    hash_re = re.compile(rf"URL_HASH\s+SHA256=({SOURCE_ARCHIVE_SHA256_RE})")

    for relative in ["docs/package_consumption.md", "docs/package_consumption_zh.md"]:
        path = source / relative
        text = read_text(path, errors)
        if not text:
            continue

        archive_matches = list(archive_url_re.finditer(text))
        if not archive_matches:
            errors.append(
                f"{path}: missing SDK source archive URL for {expected_tag}"
            )
        for match in archive_matches:
            tag, archive_name_tag = match.group(1), match.group(2)
            line = line_number(text, match.start())
            if tag != expected_tag:
                errors.append(
                    f"{path}:{line}: release URL tag is {tag}; expected {expected_tag}"
                )
            if archive_name_tag != expected_tag:
                errors.append(
                    f"{path}:{line}: archive filename tag is {archive_name_tag}; "
                    f"expected {expected_tag}"
                )

        hash_matches = list(hash_re.finditer(text))
        if not hash_matches:
            errors.append(
                f"{path}: missing `URL_HASH SHA256=<64-character hash>` for "
                f"{expected_tag}"
            )
        for match in hash_matches:
            hashes.append((path, line_number(text, match.start(1)), match.group(1).lower()))


def check_release_gates(
    source: Path,
    version: str,
    expected_tag: str,
    errors: list[str],
) -> None:
    path = source / ".github/workflows/release-gates.yml"
    text = read_text(path, errors)
    if not text:
        return

    if '--requires="cxxmcp/${version}"' not in text and "--requires=cxxmcp/${version}" not in text:
        conan_matches = list(re.finditer(rf"--requires=cxxmcp/({SEMVER_RE})", text))
        if not conan_matches:
            errors.append(
                f"{path}: missing Conan package requirement; expected dynamic VERSION usage"
            )
        for match in conan_matches:
            actual = match.group(1)
            if actual != version:
                errors.append(
                    f"{path}:{line_number(text, match.start(1))}: "
                    f"Conan package requirement is {actual}; expected {version}"
                )

    if 'add_requires("cxxmcp v${version}"' not in text:
        xmake_matches = list(
            re.finditer(rf'add_requires\("cxxmcp\s+v({SEMVER_RE})"', text)
        )
        if not xmake_matches:
            errors.append(
                f"{path}: missing xmake package requirement; expected dynamic VERSION usage"
            )
        for match in xmake_matches:
            actual = match.group(1)
            if actual != version:
                errors.append(
                    f"{path}:{line_number(text, match.start(1))}: "
                    f"xmake package requirement is {actual}; expected {version}"
                )

    archive_matches = list(
        re.finditer(rf"cxxmcp-sdk-source-(v{SEMVER_RE})\.tar\.gz", text)
    )
    for match in archive_matches:
        actual = match.group(1)
        if actual != expected_tag:
            errors.append(
                f"{path}:{line_number(text, match.start(1))}: "
                f"SDK source archive tag is {actual}; expected {expected_tag}"
            )


def check_hash_consistency(
    version: str,
    hashes: list[tuple[Path, int, str]],
    errors: list[str],
) -> None:
    if not hashes:
        errors.append(
            f"missing SDK source archive SHA256 entries for version {version}; "
            "expected xmake add_versions and package docs URL_HASH entries"
        )
        return

    unique_hashes = sorted({value for _, _, value in hashes})
    if len(unique_hashes) <= 1:
        return

    details = "\n".join(f"  - {path}:{line}: {value}" for path, line, value in hashes)
    errors.append(
        "SDK source archive SHA256 values differ; all package recipes and "
        f"docs for version {version} must use one hash:\n{details}"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository source root")
    args = parser.parse_args()

    source = Path(args.source).resolve()
    errors: list[str] = []
    hashes: list[tuple[Path, int, str]] = []

    version = project_version(source, errors)
    if version:
        expected_tag = f"v{version}"
        check_conanfile(source, version, errors)
        check_vcpkg_json(source, version, errors)
        check_xmake_recipe(source, version, expected_tag, hashes, errors)
        check_package_docs(source, version, expected_tag, hashes, errors)
        check_release_gates(source, version, expected_tag, errors)
        check_hash_consistency(version, hashes, errors)

    if errors:
        joined = "\n".join(f"- {error}" for error in errors)
        raise SystemExit(f"package recipe sync check failed:\n{joined}")

    source_hash = hashes[0][2] if hashes else "<none>"
    print(
        "package recipe sync check passed: "
        f"version {version}, SDK source archive SHA256 {source_hash}"
    )


if __name__ == "__main__":
    main()
