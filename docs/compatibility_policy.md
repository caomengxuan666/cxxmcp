# Compatibility Policy

This policy defines what cxxmcp treats as the stable SDK contract. It is part
of the release decision together with the README, examples, changelog,
[public API stability](public_api_stability.md), release gates, and release
candidate checklist.

## Canonical SDK Surface

The canonical SDK path is `Peer` / `Service` over the public CMake targets and
headers under `cxxmcp/...`. `Peer` / `Service` are the first-choice application
entry points for new code.

Concrete `client::Client`, `server::Server`, and `server::App` surfaces are
compatibility or convenience APIs. They may remain available for migration,
tests, and low-boilerplate examples, but public docs and release artifacts must
not present them as the first-choice SDK architecture.

Stable public SDK targets are:

- `cxxmcp::protocol`
- `cxxmcp::transport`
- `cxxmcp::handler`
- `cxxmcp::peer`
- `cxxmcp::service`
- `cxxmcp::client`
- `cxxmcp::server`
- `cxxmcp::sdk`

The aggregate `cxxmcp::sdk` target is only a convenience target. Consumers that
need a narrow dependency should link the narrow target directly.

Runtime, gateway, CLI, app, adapter, and plugin SDK targets are optional tools
above the SDK. They are not allowed to define the core SDK contract, and their
types must not enter public SDK headers without a design note and release
review.

## Source Compatibility And API Classes

Public headers compile as C++17 by default. The
`CXXMCP_SDK_CXX_STANDARD` CMake cache value may be raised by downstream builds,
but stable public headers must not require a standard newer than the configured
SDK standard.

Public APIs are classified as stable, experimental, or deprecated by
[Public API Stability](public_api_stability.md). Stable APIs are source-frozen
within a stable major version. Experimental APIs must be explicitly labeled and
cannot be required by stable entry points. Deprecated APIs remain public until
their documented removal window.

Public include paths stay under `cxxmcp/...`. Public API renames must follow
this sequence:

1. Add the new API.
2. Keep the old spelling as an alias or forwarding wrapper.
3. Mark the old spelling with `CXXMCP_DEPRECATED("message")` where possible.
4. Document the migration in the changelog or migration guide.
5. Remove the old spelling only in the next major release.

Source compatibility follows semantic versioning once a stable release line is
declared. Until then, pre-release builds may move APIs, but every public move
must be visible in the changelog and release notes.

## ABI Policy

cxxmcp builds and releases static libraries by default. ABI stability is not
claimed for static-library releases.

If shared libraries become stable release artifacts later, a separate ABI
policy must be written before ABI compatibility is advertised.

## Protocol Compatibility

cxxmcp follows published MCP protocol snapshots. It does not mint custom MCP
protocol versions or alternate wire formats.

When support for a new MCP snapshot is added, the previous supported snapshot
must remain available for at least one minor release unless the release notes
explicitly call out a breaking compatibility event.

Unsupported protocol versions fail fast with protocol or transport validation
errors. The SDK must not silently downgrade to an unadvertised dialect.

## Release Support Matrix

A release may claim support only for compiler, generator, runtime-library, and
platform combinations that passed the release-blocking gates for that exact
release commit.

The intended public matrix is:

- Windows MSVC with Ninja and Visual Studio generators
- Linux GCC with Ninja
- Linux Clang with Ninja
- macOS AppleClang with Ninja
- MSVC static runtime and dynamic runtime modes when Windows artifacts are
  advertised

The `.github/workflows/release-gates.yml` workflow is the canonical evidence
producer for this matrix. Release notes must not claim unsupported matrix
entries just because the source is expected to work there.

## Release Evidence

Before a release can claim fact-standard readiness, these artifacts must be
available from the release-gates workflow or an equivalent release pipeline for
the same commit:

- release-blocking CTest/JUnit/log evidence for every advertised matrix leg
- installed-package `package_smoke` evidence
- independent public-header compile evidence
- RMCP, TypeScript SDK, and Python SDK interoperability evidence where those
  runtimes are advertised
- generated public API documentation
- source archive with checksums
- release evidence documents containing README, examples, changelog, release
  gates, release candidate checklist, public API stability policy, and this
  compatibility policy

The release candidate checklist is the binding audit record. Do not publish a
stable release or claim fact-standard status while any required evidence
artifact is missing or any advertised matrix leg is red.
