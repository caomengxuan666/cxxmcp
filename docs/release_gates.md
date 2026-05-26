# cxxmcp Release Gates

This document records the release-blocking checks that must describe the same
canonical SDK path as the README, examples, CMake package targets, and
compatibility policy.

## Required CTest Labels

Every release-blocking test must carry the `release-blocking` CTest label. The
static `release_gate_manifest` test verifies that the named gates below stay
registered through `cxxmcp_mark_release_blocking()`.

## SDK And Package Gates

- `sdk_boundary`: public SDK headers cannot expose runtime, gateway, policy,
  profile, discovery, `httplib`, or compatibility transport-adapter internals.
- `public_header_*`: each canonical public header compiles independently under
  the SDK C++ standard.
- `public_targets`: narrow SDK package targets remain consumable without
  linking runtime or gateway layers.
- `package_smoke`: installed package output is consumed from a clean external
  CMake project with `find_package(cxxmcp CONFIG REQUIRED)`. The external
  consumer configure must use the same release-matrix generator and compiler
  family as the matrix leg producing the evidence.

## Source Style Gates

- `source-style`: the release-gates workflow runs `scripts/format.ps1 -Check`
  and `scripts/check-cpplint.ps1` on Ubuntu before release evidence is treated
  as clean. clang-tidy is intentionally tracked separately because it depends on
  a configured compile database and may need a narrower source scope.

## Build Configuration Gates

- `build-config-smoke`: the release-gates workflow builds the SDK, client,
  server, and examples in both Debug and Release modes on Linux/Ninja with
  runtime, gateway, CLI, tests, and docs disabled. This keeps release-mode
  compile coverage in CI without doubling the full cross-SDK conformance
  matrix.

## Protocol, Transport, And Interop Gates

- `protocol`: JSON-RPC and MCP protocol serialization, parsing, version policy,
  and typed model basics.
- `transport_contract` and `transport_stdio_contract`: role-generic transport
  contract behavior.
- `stdio_transport`, `process_stdio_transport`, `http_transport`, and
  `transport_adapters`: concrete and compatibility transport behavior,
  including failure-path coverage.
- `client_server`, `sdk`: canonical Peer/Service, request lifecycle,
  cancellation, progress, and public SDK ergonomics.
- `rmcp_conformance`, `interop_typescript_client_process_stdio`,
  `interop_python_client_process_stdio`, and
  `interop_rmcp_client_process_stdio`: cross-SDK process-stdio and Streamable
  HTTP interoperability.

## Release Matrix

A release may claim support only for compiler, generator, runtime-library, and
platform combinations where the release-blocking set above passed. The intended
matrix is:

- Windows MSVC with Ninja and Visual Studio generators
- Linux GCC with Ninja
- Linux Clang with Ninja
- macOS AppleClang with Ninja
- MSVC static runtime and dynamic runtime modes when Windows artifacts are
  advertised

The `.github/workflows/release-gates.yml` workflow is the canonical public
evidence producer for this matrix. It runs the `release-blocking` CTest label
set on Linux/GCC, Linux/Clang, macOS/AppleClang, Windows/MSVC Ninja with static
runtime, and Windows/MSVC Visual Studio with dynamic runtime. The same workflow
also builds the Doxygen HTML artifact that must be attached to release
candidates when API docs are advertised.

Each matrix leg uploads a `cxxmcp-release-gates-*` artifact containing the
`CMakeCache.txt`, CTest JUnit XML, and CTest log files. Release candidates must
link or attach those artifacts so package-smoke, public-header, transport,
conformance, and interoperability results are auditable after the workflow run
expires from the Actions UI.

The release evidence manifest records the pinned reference versions used by the
interop matrix:

- RMCP reference commit: `c330fede90e4729c234f8e87fdbc5ea27a1dd10c`
- TypeScript SDK reference: `@modelcontextprotocol/sdk@1.29.0`
- Python SDK reference: `mcp==1.27.1`

The `scripts/check_release_evidence.py` verifier runs before the release
evidence artifact is uploaded. It fails the workflow if the release evidence is
missing required documents, if SDK API docs accidentally include runtime or
tooling headers, if pinned interop versions are absent, or if compatibility and
runtime examples are not labelled as non-canonical SDK paths.

The same workflow uploads:

- `cxxmcp-doxygen-html`: generated public API documentation.
- `cxxmcp-source`: a source archive with recursive submodule contents and a
  `SHA256SUMS.txt` file.
- `cxxmcp-release-evidence`: the README, Chinese README, changelog,
  contribution guide, security policy, code of conduct, compatibility policy,
  dependency policy, release process, Peer/Service migration guide, release
  gates, release candidate checklist, release notes template, request lifecycle
  notes, TODO, the external consumer template, and example source files used
  for the release decision.

Release candidates must attach the source, documentation, and release evidence
artifacts, or replace them with equivalent versioned artifacts built from the
same commit and with matching checksums recorded in the release notes.

Use [Release candidate checklist](release_candidate_checklist.md) to bind the
workflow artifacts, public API review, compatibility policy, examples, README,
and changelog into one auditable release decision.
Use [Release notes template](release_notes_template.md) for the release notes
that publish that decision.
Use [Release process](release_process.md) for semantic versioning, stage gates,
artifact, API diff, dependency update, and security/advisory rules.

## Public API Review

Before a release, review public header diffs under:

- `sdk/include/cxxmcp`
- `sdk/core/include/cxxmcp`
- `sdk/protocol/include/cxxmcp`
- `sdk/client/include/cxxmcp`
- `sdk/server/include/cxxmcp`
- `sdk/transport/include/cxxmcp`

Public renames must add the new API first, keep the old name with
`CXXMCP_DEPRECATED("message")`, document the migration, and remove the old name
only in the next major release.

Do not replace a stable public source API only to make the implementation
abstraction cleaner. Prefer additive Peer/Service helpers, compatibility
adapters, and documented deprecation over source-breaking abstraction churn.
