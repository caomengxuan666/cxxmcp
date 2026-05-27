#!/usr/bin/env python3
"""Validate package-manager auth features stay opt-in."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def require_contains(path: Path, needle: str) -> None:
    text = path.read_text(encoding="utf-8")
    if needle not in text:
        fail(f"{path} must contain {needle!r}")


def require_not_contains(path: Path, needle: str) -> None:
    text = path.read_text(encoding="utf-8")
    if needle in text:
        fail(f"{path} must not contain {needle!r}")


def dependency_name(dep: object) -> str:
    if isinstance(dep, str):
        return dep
    if isinstance(dep, dict):
        name = dep.get("name")
        if isinstance(name, str):
            return name
    return ""


def dependency_features(dep: object) -> set[str]:
    if not isinstance(dep, dict):
        return set()
    features = dep.get("features", [])
    if not isinstance(features, list):
        fail("vcpkg dependency features must be a list")
    return {feature for feature in features if isinstance(feature, str)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository root")
    args = parser.parse_args()
    source = Path(args.source).resolve()

    vcpkg_manifest = source / "packaging/vcpkg/ports/cxxmcp/vcpkg.json"
    data = json.loads(vcpkg_manifest.read_text(encoding="utf-8"))
    default_features = data.get("default-features", [])
    if "auth" in default_features:
        fail("vcpkg auth feature must not be enabled by default")
    features = data.get("features", {})
    if "auth" not in features:
        fail("vcpkg port must expose an opt-in auth feature")
    deps = data.get("dependencies", [])
    if any(dependency_name(dep) == "openssl" for dep in deps):
        fail("vcpkg default dependencies must not pull OpenSSL for auth")
    if any(dependency_name(dep) == "jsonrpcpp" for dep in deps):
        fail(
            "overlay vcpkg port must not depend on jsonrpcpp "
            "before the port is accepted"
        )
    for dep in deps:
        if dependency_name(dep) == "cpp-httplib":
            if dep.get("default-features", True) is not False:
                fail("vcpkg cpp-httplib dependency must keep default features disabled")
            if {"openssl", "ssl"} & dependency_features(dep):
                fail("vcpkg default cpp-httplib dependency must not enable OpenSSL")

    vcpkg_portfile = source / "packaging/vcpkg/ports/cxxmcp/portfile.cmake"
    require_contains(vcpkg_portfile, '"auth" IN_LIST FEATURES')
    require_contains(vcpkg_portfile, "-DCXXMCP_ENABLE_AUTH=${CXXMCP_VCPKG_ENABLE_AUTH}")
    require_contains(vcpkg_portfile, "vcpkg_check_linkage(ONLY_STATIC_LIBRARY)")
    require_not_contains(vcpkg_portfile, "-DBUILD_SHARED_LIBS=OFF")
    require_not_contains(vcpkg_portfile, "CXXMCP_USE_SYSTEM_JSONRPCPP")

    curated_portfile = source / "packaging/vcpkg/curated-portfile.future.cmake"
    require_contains(curated_portfile, "vcpkg_from_github(")
    require_contains(curated_portfile, "SHA512 @CXXMCP_RELEASE_ARCHIVE_SHA512@")
    require_contains(curated_portfile, "after-jsonrpcpp-vcpkg-acceptance")
    require_contains(curated_portfile, "-DCXXMCP_USE_SYSTEM_JSONRPCPP=ON")
    require_contains(curated_portfile, "vcpkg_check_linkage(ONLY_STATIC_LIBRARY)")
    require_not_contains(curated_portfile, "-DBUILD_SHARED_LIBS=OFF")

    conanfile = source / "conanfile.py"
    require_contains(conanfile, '"with_auth": [True, False]')
    require_contains(conanfile, '"with_auth": False')
    require_contains(conanfile, 'toolchain.variables["CXXMCP_ENABLE_AUTH"]')
    require_contains(conanfile, 'self.cpp_info.components["auth"]')
    require_not_contains(conanfile, "openssl/")
    require_not_contains(conanfile, '"openssl"')
    require_not_contains(conanfile, "'openssl'")

    xmake = source / "packaging/xmake/packages/c/cxxmcp/xmake.lua"
    require_contains(xmake, 'add_configs("auth"')
    require_contains(xmake, 'package:config("auth") and "ON" or "OFF"')
    require_contains(xmake, "#include <cxxmcp/auth.hpp>")
    require_not_contains(xmake, "openssl")
    require_not_contains(xmake, "OpenSSL")


if __name__ == "__main__":
    main()
