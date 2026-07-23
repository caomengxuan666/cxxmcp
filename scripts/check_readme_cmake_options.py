#!/usr/bin/env python3
"""Validate README CMake option tables against public CMake cache options."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


CMAKE_OPTION_PATTERNS = [
    re.compile(r"\bcxxmcp_build_option\s*\(\s*(CXXMCP_[A-Z0-9_]+)"),
    re.compile(r"\boption\s*\(\s*(CXXMCP_[A-Z0-9_]+)"),
    re.compile(
        r"\bset\s*\(\s*(CXXMCP_[A-Z0-9_]+)\s+.*?\bCACHE\s+"
        r"(?:BOOL|FILEPATH|PATH|STRING)\b",
        re.DOTALL,
    ),
]

README_FILES = [
    "README.md",
    "README_zh.md",
]


def read_text(path: Path) -> str:
    if not path.is_file():
        raise SystemExit(f"README CMake option check failed: missing {path}")
    return path.read_text(encoding="utf-8")


def public_cmake_options(source: Path) -> list[str]:
    text = read_text(source / "CMakeLists.txt")
    options: set[str] = set()
    for pattern in CMAKE_OPTION_PATTERNS:
        options.update(match.group(1) for match in pattern.finditer(text))
    return sorted(options)


def cmake_options_section(text: str, path: Path) -> str:
    match = re.search(
        r"^## CMake Options\s*$([\s\S]*?)(?=^##\s|\Z)",
        text,
        re.MULTILINE,
    )
    if not match:
        raise SystemExit(
            f"README CMake option check failed: {path} has no "
            "`## CMake Options` section"
        )
    return match.group(1)


def readme_table_options(source: Path, relative: str) -> set[str]:
    path = source / relative
    section = cmake_options_section(read_text(path), path)
    return set(re.findall(r"\|\s*`(CXXMCP_[A-Z0-9_]+)`\s*\|", section))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository source root")
    args = parser.parse_args()

    source = Path(args.source).resolve()
    expected = public_cmake_options(source)

    errors: list[str] = []
    for relative in README_FILES:
        documented = readme_table_options(source, relative)
        missing = [option for option in expected if option not in documented]
        extra = sorted(documented - set(expected))
        if missing:
            errors.append(
                f"{relative}: missing CMake option(s): {', '.join(missing)}"
            )
        if extra:
            errors.append(
                f"{relative}: documents unknown CMake option(s): {', '.join(extra)}"
            )

    if errors:
        raise SystemExit(
            "README CMake option check failed:\n" + "\n".join(errors)
        )

    print(
        "README CMake option check passed "
        f"({len(expected)} public option(s), {len(README_FILES)} README files)"
    )


if __name__ == "__main__":
    main()
