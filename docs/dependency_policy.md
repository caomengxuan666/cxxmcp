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
- the in-tree `jsonrpcpp` single-header implementation

Default source/archive builds may vendor header-only SDK dependencies for easy
FetchContent and CPM.cmake consumption. Registry builds should use
`CXXMCP_USE_SYSTEM_DEPS=ON` and depend on package-manager versions instead.

Runtime/tooling dependencies such as spdlog and CLI11 are outside the SDK
package contract and should not be required by SDK-only packages.

## Update Cadence

Before each public release candidate:

- check whether MCP has published a newer protocol snapshot;
- refresh RMCP, TypeScript SDK, and Python SDK references deliberately, not as
  drive-by churn;
- run the release-blocking interop matrix after any reference update;
- review package-manager dependency versions and changelog/security notes;
- record reference or dependency changes in release notes.

Dependency updates that affect public headers, generated CMake package files, or
wire behavior require a public API review.
