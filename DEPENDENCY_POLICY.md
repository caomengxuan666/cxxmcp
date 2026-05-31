# Dependency Policy

This document describes how cxxmcp manages its dependencies.

## Direct Dependencies

| Dependency | Version | License | Purpose |
|---|---|---|---|
| nlohmann/json | ^3.11 | MIT | JSON parsing and serialization |
| cpp-httplib | ^0.15 | MIT | HTTP client/server and WebSocket transport |
| OpenSSL | ^3.0 | Apache-2.0 | TLS and cryptographic operations (optional) |

All direct dependencies are vendored under `third_party/` or resolved via vcpkg when building with `CXXMCP_USE_SYSTEM_DEPS=ON`.

## Update Policy

- Security patches for dependencies are applied within 7 days of disclosure.
- Non-security dependency updates are evaluated monthly and included in the next minor or patch release.
- Dependencies are pinned to known-good versions in CMakeLists.txt. Renovate or Dependabot automation is planned but not yet enabled.

## Adding New Dependencies

New dependencies require a justification in the pull request describing:

1. Why the functionality cannot be implemented in-tree.
2. The dependency's license compatibility (must be MIT, Apache-2.0, BSD, or ISC).
3. The dependency's maintenance status and known CVE history.
4. Whether the dependency can be vendored or must be fetched at build time.
