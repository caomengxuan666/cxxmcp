#!/usr/bin/env python3
"""Run clang-tidy over selected translation units."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"clang-tidy check failed: {message}")


def collect_sources(source_root: Path, paths: list[str]) -> list[Path]:
    sources: list[Path] = []
    for item in paths:
        path = (source_root / item).resolve()
        if path.is_file():
            sources.append(path)
            continue
        if path.is_dir():
            sources.extend(sorted(path.rglob("*.cpp")))
            continue
        fail(f"missing clang-tidy input path: {path}")
    return sources


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository source root")
    parser.add_argument("--build-dir", required=True, help="CMake build dir")
    parser.add_argument(
        "paths",
        nargs="+",
        help="source files or directories to check",
    )
    args = parser.parse_args()

    clang_tidy = shutil.which("clang-tidy")
    if not clang_tidy:
        fail("clang-tidy was not found on PATH")

    source_root = Path(args.source).resolve()
    build_dir = (source_root / args.build_dir).resolve()
    compile_commands = build_dir / "compile_commands.json"
    if not compile_commands.is_file():
        fail(f"missing compile_commands.json: {compile_commands}")

    sources = collect_sources(source_root, args.paths)
    if not sources:
        fail("no clang-tidy source files were selected")

    command = [clang_tidy, "-p", str(build_dir), *[str(path) for path in sources]]
    subprocess.run(command, cwd=source_root, check=True)


if __name__ == "__main__":
    main()
