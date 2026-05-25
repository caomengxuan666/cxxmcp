# Release Candidate Checklist

Use this checklist for every public release candidate before calling cxxmcp a
fact-standard-ready C++ MCP SDK.

## Identity

- Release tag:
- Commit SHA:
- MCP protocol snapshot:
- RMCP reference commit:
- TypeScript SDK reference version:
- Python SDK reference version:

## Required Evidence Artifacts

Attach or link all artifacts from `.github/workflows/release-gates.yml`:

- `cxxmcp-release-gates-linux-gcc-ninja`
- `cxxmcp-release-gates-linux-clang-ninja`
- `cxxmcp-release-gates-macos-appleclang-ninja`
- `cxxmcp-release-gates-windows-msvc-ninja-static-runtime`
- `cxxmcp-release-gates-windows-msvc-vs-dynamic-runtime`
- `cxxmcp-doxygen-html`
- `cxxmcp-source`
- `cxxmcp-release-evidence`

Each release-gate artifact must contain `CMakeCache.txt`, CTest JUnit XML, and
CTest logs. The source artifact must contain `SHA256SUMS.txt`. The release
evidence artifact must contain the README, README_zh, changelog,
compatibility policy, Peer/Service migration guide, release gates, release
candidate checklist, TODO, and example source files used for the canonical SDK
path review.

## Gate Review

- [ ] All release-blocking CTest labels passed on every advertised matrix leg.
- [ ] `package_smoke` passed from installed output on every advertised matrix
      leg, using the same generator and compiler family as that matrix leg.
- [ ] Public header compile tests passed on every advertised matrix leg.
- [ ] RMCP, TypeScript SDK, and Python SDK interoperability gates passed where
      those runtimes are advertised for the release.
- [ ] Doxygen HTML was generated from the same commit as the source artifact.
- [ ] Source archive checksum was recorded in release notes.
- [ ] `scripts/check_release_evidence.py` passed for the source tree and the
      uploaded `cxxmcp-release-evidence` artifact.

## Public Surface Review

- [ ] Public headers under `sdk/**/include/cxxmcp` were reviewed for accidental
      runtime, gateway, policy, discovery, profile, or transport-backend leaks.
- [ ] Public target list still matches README and CMake package exports.
- [ ] Deprecated APIs have migration text and use `CXXMCP_DEPRECATED`.
- [ ] Public renames keep old aliases until the next major release.
- [ ] ABI stability is not claimed for static-library releases.

## Canonical SDK Path Review

- [ ] README and README_zh present `Peer` / `Service` before concrete
      `Client` / `Server` APIs.
- [ ] Examples listed as first-choice SDK examples use `Peer` / `Service`.
- [ ] Examples listed as compatibility, low-level, or runtime tooling examples
      are labeled that way in their source comments or surrounding docs.
- [ ] Changelog entries describe the same canonical Peer/Service path.
- [ ] Compatibility policy, release gates, release evidence artifact, and
      package targets agree on the supported compiler/generator/runtime matrix.
- [ ] Generated API docs and release evidence present `Peer` / `Service` as the
      canonical SDK path.
- [ ] Optional runtime, gateway, CLI, app, adapters, and plugin SDK remain
      outside the core SDK contract.

## Release Notes

- [ ] Include the supported compiler/generator/runtime matrix.
- [ ] Include protocol snapshot support and unsupported-version behavior.
- [ ] Include source compatibility notes for public API changes.
- [ ] Include dependency/reference versions used by conformance tests.
- [ ] Include checksums for published source artifacts.
- [ ] Link generated API documentation.

Do not publish a stable release or claim fact-standard status while any required
evidence artifact is missing or any release-blocking matrix leg is red.
