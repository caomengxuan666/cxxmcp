# Dependency And Reference Policy

This document records how cxxmcp tracks SDK dependencies and protocol reference
versions.

## Reference Versions

Release evidence records these protocol/reference anchors:

- MCP protocol snapshot: `2025-11-25`
- RMCP reference commit: `c330fede90e4729c234f8e87fdbc5ea27a1dd10c`
- TypeScript SDK reference: `@modelcontextprotocol/sdk@1.29.0`
- Python SDK reference: `mcp==1.27.1`

The CTest interop fixtures and release evidence manifest must be updated
together when these values change.

## SDK Dependencies

SDK package dependencies are intentionally narrow:

- `tl-expected`
- `nlohmann-json`
- `cpp-httplib`

Default source/archive builds may vendor header-only SDK dependencies for easy
FetchContent and CPM.cmake consumption. Registry builds should use
`CXXMCP_USE_SYSTEM_DEPS=ON` and depend on package-manager versions instead.

Tooling dependencies such as spdlog and CLI11 are outside the SDK package
contract and should not be required by SDK-only packages.

`cpp-httplib` remains an implementation dependency hidden behind transport
interfaces. The evidence and replacement trigger for considering another HTTP
backend are tracked in `docs/compatibility_policy.md#http-transport-backend-evidence`.
OpenSSL is optional and must stay outside the default SDK package path. When it
is enabled, package recipes should expose one cross-cutting `openssl` feature
for HTTPS, WSS, and auth crypto instead of separate transport-specific OpenSSL
features.

Gateway/tooling state is outside the public SDK header contract and now lives
outside this SDK repository. The canonical SDK include roots must not expose
tooling registry/discovery models, exposure profiles, trust policy,
import/export services, multi-profile hosting configuration, CLI defaults, or
observability/logging types. Those concepts may depend on the SDK, but the
dependency must not point back into SDK headers or package targets.

For vcpkg, the repository-hosted overlay port is the supported package-manager
path until the project has enough maturity evidence for a curated-registry PR.
That port must stay SDK-only: no external gateway/tooling, examples, tests,
docs, spdlog, or CLI11 in the default package dependency closure.

## Update Cadence

Before each public release candidate:

- check whether MCP has published a newer protocol snapshot;
- refresh RMCP, TypeScript SDK, and Python SDK references deliberately, not as
  drive-by churn;
- run `python scripts/check_rmcp_source_drift.py` after any RMCP refresh to
  confirm the pinned checkout, RMCP model source files, and protocol-model audit
  mapping are still synchronized; regenerate `docs/rmcp_source_mapping.json`
  with `--write-mapping` only as part of a deliberate mapping update;
- run the release-blocking interop matrix after any reference update;
- review package-manager dependency versions and changelog/security notes;
- record reference or dependency changes in release notes.

Dependency updates that affect public headers, generated CMake package files, or
wire behavior require a public API review.

## Time-Sensitive Dependency Claims

Dependency status statements are release-candidate evidence, not permanent
facts. In particular, any claim that a vendored fallback matches the latest
upstream release must be rechecked during the dependency review for the exact
release commit and recorded in release notes.

For the current SDK surface:

- `tl::expected` is the public `mcp::core::Result` backend for this major
  release, not only a fallback. Release review must confirm the vendored
  version and the package-manager `tl-expected` route are both acceptable for
  the advertised package paths. Do not switch `Result` to `std::expected` based
  on the consumer's C++ language mode; that would break static-library symbol
  compatibility between C++17 SDK builds and C++23 consumers.
- `cpp-httplib` remains hidden behind transport interfaces; replacement claims
  require the load/reliability evidence tracked in
  `docs/compatibility_policy.md#http-transport-backend-evidence`.

The release candidate checklist must cite this dependency review before
publishing stable, curated-registry, or fact-standard claims.
