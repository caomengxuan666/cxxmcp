# Release Notes Template

Use this template for every public release candidate and stable release. Fill
it from the artifacts produced by `.github/workflows/release-gates.yml`.

## Identity

- Release tag:
- Commit SHA:
- Release stage: alpha / beta / rc / stable
- MCP protocol snapshot:
- RMCP reference commit:
- TypeScript SDK reference:
- Python SDK reference:

## Supported Matrix

List only matrix entries whose `release-blocking` CTest label set passed for
this exact commit.

- Windows MSVC Ninja static runtime:
- Windows MSVC Visual Studio dynamic runtime:
- Linux GCC Ninja:
- Linux Clang Ninja:
- macOS AppleClang Ninja:

## Required Artifacts

Attach or link these artifacts for the same commit:

- `cxxmcp-release-gates-linux-gcc-ninja`
- `cxxmcp-release-gates-linux-clang-ninja`
- `cxxmcp-release-gates-macos-appleclang-ninja`
- `cxxmcp-release-gates-windows-msvc-ninja-static-runtime`
- `cxxmcp-release-gates-windows-msvc-vs-dynamic-runtime`
- `cxxmcp-doxygen-html`
- `cxxmcp-source`
- `cxxmcp-release-evidence`

## Checksums

Copy checksums from the source artifact `SHA256SUMS.txt`.

```text
<sha256>  cxxmcp-source-<commit>.tar.gz
```

## Canonical SDK Path

State that the canonical SDK entry points are `Peer` / `Service` over the
public `cxxmcp::protocol`, `cxxmcp::transport`, `cxxmcp::handler`,
`cxxmcp::peer`, `cxxmcp::service`, `cxxmcp::client`, `cxxmcp::server`, and
`cxxmcp::sdk` targets.

State that runtime, gateway, CLI, app, adapters, and plugin SDK targets are
optional layers outside the core SDK contract.

## Compatibility Notes

- Source compatibility changes:
- Deprecated APIs and migration notes:
- Public header diff review result:
- ABI note: static-library releases do not claim ABI stability.

## Protocol Notes

- Supported MCP protocol snapshots:
- Unsupported-version behavior:
- Added or removed protocol capabilities:

## Evidence Summary

- `package_smoke` installed-package evidence:
- Public-header compile evidence:
- RMCP interoperability evidence:
- TypeScript SDK interoperability evidence:
- Python SDK interoperability evidence:
- Doxygen generation evidence:
- Release evidence verifier result:

Do not claim fact-standard readiness in release notes unless every required
artifact is present, every advertised matrix leg is green, and the release
candidate checklist is complete.
