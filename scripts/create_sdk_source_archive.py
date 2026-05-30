#!/usr/bin/env python3
"""Create the deterministic SDK source archive used by release tooling."""

from __future__ import annotations

import argparse
import io
import gzip
import hashlib
from pathlib import Path
import tarfile


ROOT = Path(__file__).resolve().parents[1]

SDK_SOURCE_INPUTS = [
    "CMakeLists.txt",
    "CMakePresets.json",
    "cmake",
    "sdk",
    "examples",
    "tests",
    "templates",
    "docs",
    "scripts",
    "packaging",
    "conanfile.py",
    "third_party/tl",
    "third_party/nlohmann",
    "third_party/httplib/httplib.h",
    "README.md",
    "README_zh.md",
    "CHANGELOG.md",
    "LICENSE",
    ".clang-format",
    ".clang-tidy",
]

# These files carry the hash of this archive for external package consumers.
# Including them in the archive would make the archive hash self-referential.
SDK_SOURCE_EXCLUDES = {
    "docs/package_consumption.md",
    "docs/package_consumption_zh.md",
    "docs/pages/cookbook.html",
    "packaging/xmake/packages/c/cxxmcp/xmake.lua",
}


def normalize_tag(value: str) -> str:
    tag = value.strip()
    if not tag.startswith("v"):
        tag = f"v{tag}"
    return tag


def relative_name(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def included_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for entry in SDK_SOURCE_INPUTS:
        path = root / entry
        if not path.exists():
            raise SystemExit(f"SDK source input is missing: {entry}")
        if path.is_file():
            relative = relative_name(path, root)
            if relative not in SDK_SOURCE_EXCLUDES:
                files.append(path)
            continue
        for child in sorted(path.rglob("*")):
            if not child.is_file():
                continue
            relative = relative_name(child, root)
            if relative in SDK_SOURCE_EXCLUDES:
                continue
            if relative.startswith("docs/doxygen/"):
                continue
            files.append(child)
    return sorted(files, key=lambda item: relative_name(item, root))


def create_sdk_source_archive(root: Path, tag: str, output: Path) -> Path:
    tag = normalize_tag(tag)
    prefix = f"cxxmcp-sdk-{tag}"
    output.parent.mkdir(parents=True, exist_ok=True)

    with output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz:
            with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for path in included_files(root):
                    relative = relative_name(path, root)
                    arcname = f"{prefix}/{relative}"
                    data = archive_file_bytes(path)
                    info = archive.gettarinfo(str(path), arcname)
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    info.mtime = 0
                    info.mode = 0o644
                    info.size = len(data)
                    with io.BytesIO(data) as handle:
                        archive.addfile(info, handle)
    return output


def archive_file_bytes(path: Path) -> bytes:
    data = path.read_bytes()
    if b"\0" in data:
        return data
    return data.replace(b"\r\n", b"\n")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", required=True, help="release tag, for example v1.1.3")
    parser.add_argument("--output", required=True, help="archive output path")
    parser.add_argument(
        "--print-sha256",
        action="store_true",
        help="print the archive SHA256 after writing it",
    )
    args = parser.parse_args()

    output = create_sdk_source_archive(ROOT, args.tag, Path(args.output))
    if args.print_sha256:
        print(sha256(output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
