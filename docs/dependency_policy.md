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
- `jsonrpcpp` as a private protocol implementation header

Default source/archive builds may vendor header-only SDK dependencies for easy
FetchContent and CPM.cmake consumption. Registry builds should use
`CXXMCP_USE_SYSTEM_DEPS=ON` and depend on package-manager versions instead.

Runtime/tooling dependencies such as spdlog and CLI11 are outside the SDK
package contract and should not be required by SDK-only packages.

`cpp-httplib` remains an implementation dependency hidden behind transport
interfaces. The evidence and replacement trigger for considering another HTTP
backend are tracked in
[HTTP transport backend evidence](http_transport_backend_evidence.md).

Runtime and gateway state is also outside the public SDK header contract. The
canonical SDK include roots must not expose app-layer registry/discovery models,
runtime state, exposure profiles, trust policy, import/export services,
multi-profile hosting configuration, CLI defaults, or observability/logging
types. Those concepts may depend on the SDK, but the dependency must not point
back into SDK headers or package targets.

For vcpkg, the repository-hosted overlay port is the supported package-manager
path until the project has enough maturity evidence for a curated-registry PR.
That port must stay SDK-only: no runtime, gateway, CLI, examples, tests, docs,
spdlog, or CLI11 in the default package dependency closure.

`jsonrpcpp` is private to cxxmcp package builds. The bundled copy is used only
to compile `cxxmcp::protocol` and must not be installed as a public SDK header
or exported as a cxxmcp-owned third-party target. Once the vcpkg `jsonrpcpp`
port is accepted, curated-registry builds should enable
`CXXMCP_USE_SYSTEM_JSONRPCPP=ON` and depend on that port instead of the bundled
copy.

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
