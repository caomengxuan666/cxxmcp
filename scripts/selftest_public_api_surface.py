#!/usr/bin/env python3
"""Self-test public API surface manifest comparison rules."""

from __future__ import annotations

import copy

import compare_public_api_surface


BASE_MANIFEST = {
    "language_standard": "C++17",
    "package_prefix": "cxxmcp",
    "stable_cpp_namespace": "mcp",
    "stable_targets": ["cxxmcp::protocol", "cxxmcp::sdk"],
    "optional_targets": ["cxxmcp::auth"],
    "public_include_roots": ["sdk/include/cxxmcp"],
    "optional_include_roots": ["sdk/auth/include/cxxmcp"],
    "public_headers": ["sdk/include/cxxmcp/peer.hpp"],
    "optional_headers": ["sdk/auth/include/cxxmcp/auth.hpp"],
}


def expect_failure(previous: dict[str, object], current: dict[str, object]) -> None:
    failures = compare_public_api_surface.compare_manifests(previous, current)
    if not failures:
        raise AssertionError("public API surface comparison should fail")


def expect_system_exit(previous: dict[str, object], current: dict[str, object]) -> None:
    try:
        compare_public_api_surface.compare_manifests(previous, current)
    except SystemExit:
        return
    raise AssertionError("malformed public API surface manifest should fail")


def main() -> None:
    current = copy.deepcopy(BASE_MANIFEST)
    assert compare_public_api_surface.compare_manifests(BASE_MANIFEST, current) == []

    additive = copy.deepcopy(BASE_MANIFEST)
    additive["public_headers"] = [
        "sdk/include/cxxmcp/peer.hpp",
        "sdk/include/cxxmcp/service.hpp",
    ]
    additive["stable_targets"] = [
        "cxxmcp::protocol",
        "cxxmcp::sdk",
        "cxxmcp::service",
    ]
    additive["optional_headers"] = [
        "sdk/auth/include/cxxmcp/auth.hpp",
        "sdk/auth/include/cxxmcp/auth/metadata.hpp",
    ]
    assert compare_public_api_surface.compare_manifests(BASE_MANIFEST, additive) == []

    removed_header = copy.deepcopy(BASE_MANIFEST)
    removed_header["public_headers"] = []
    expect_failure(BASE_MANIFEST, removed_header)

    removed_optional_header = copy.deepcopy(BASE_MANIFEST)
    removed_optional_header["optional_headers"] = []
    expect_failure(BASE_MANIFEST, removed_optional_header)

    removed_include_root = copy.deepcopy(BASE_MANIFEST)
    removed_include_root["public_include_roots"] = []
    expect_failure(BASE_MANIFEST, removed_include_root)

    removed_target = copy.deepcopy(BASE_MANIFEST)
    removed_target["stable_targets"] = ["cxxmcp::protocol"]
    expect_failure(BASE_MANIFEST, removed_target)

    scalar_change = copy.deepcopy(BASE_MANIFEST)
    scalar_change["stable_cpp_namespace"] = "cxxmcp"
    expect_failure(BASE_MANIFEST, scalar_change)

    malformed_list = copy.deepcopy(BASE_MANIFEST)
    malformed_list["public_headers"] = "sdk/include/cxxmcp/peer.hpp"
    expect_system_exit(BASE_MANIFEST, malformed_list)

    print("public API surface self-test passed")


if __name__ == "__main__":
    main()
