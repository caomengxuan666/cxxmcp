# cxxmcp Release Gates

This document records the release-blocking checks that must describe the same
canonical SDK path as the README, examples, CMake package targets, and
compatibility policy.

## Required CTest Labels

Every release-blocking test must carry the `release-blocking` CTest label. The
static `release_gate_manifest` test verifies that the named gates below stay
registered through `cxxmcp_mark_release_blocking()`.

## SDK And Package Gates

- `sdk_boundary`: public SDK headers cannot expose runtime, gateway, app,
  profile, policy, registry/discovery, import/export, CLI, observability,
  `httplib`, or compatibility transport-adapter internals. The boundary script
  checks both forbidden includes and concrete runtime/tooling API tokens so
  runtime state, managed server registries, discovered capability catalogs,
  exposure profiles, trust policy, import/export services, multi-profile
  hosting, and CLI defaults stay out of the canonical SDK headers.
- `public_header_*`: each canonical public header compiles independently under
  the SDK C++ standard.
- `public_targets`: narrow SDK package targets remain consumable without
  linking external tooling layers.
- `package_smoke`: installed package output is consumed from a clean external
  CMake project with `find_package(cxxmcp CONFIG REQUIRED)`. The external
  consumer configure must use the same release-matrix generator and compiler
  family as the matrix leg producing the evidence. Default package smoke must
  prove optional auth headers are not installed; auth-enabled package smoke
  must prove a clean consumer can explicitly link `cxxmcp::auth`.
- `check_package_auth_features.py`: package-manager metadata keeps auth
  opt-in for vcpkg, Conan, and xmake, and prevents OpenSSL-backed full auth
  from becoming part of the default package path prematurely.
- `package-manager-vcpkg`: installs the repository overlay port through a real
  vcpkg checkout for both the default SDK and `auth` feature, then builds the
  clean CMake `package_smoke` consumer through the vcpkg toolchain. The
  uploaded artifact must include non-empty vcpkg install log plus
  clean-consumer CMake configure/build logs.
- `package-manager-conan`: creates the Conan package for both default and
  `with_auth=True` options, installs a clean downstream dependency graph, and
  builds the same external CMake `package_smoke` consumer through Conan's
  generated toolchain. The uploaded artifact must include non-empty Conan
  create/install logs plus clean-consumer CMake configure/build logs.
- `package-manager-xmake`: consumes the xmake-repo draft from a local xmake
  repository for both default and auth configurations, but rewrites the CI
  recipe to use a generated source archive from the exact workflow checkout,
  including checked-out submodule contents, instead of the last published
  GitHub Release archive. It then compiles a clean downstream C++17 consumer.
  The uploaded artifact must include the temporary local xmake repository,
  generated source archive, and non-empty xmake repo and build logs.

## Source Style Gates

- `source-style`: the release-gates workflow runs `scripts/format.ps1 -Check`
  `scripts/check-cpplint.ps1`, and
  `scripts/check_protocol_model_coverage.py` on Ubuntu before release evidence
  is treated as clean. It also runs
  `scripts/check_source_markers.py`, which rejects unresolved
  `FIXME`/`HACK`/`XXX` markers in first-party SDK, extension, example,
  template, and test source paths, and
  `scripts/selftest_release_artifacts.py`, which constructs synthetic
  release-gates and release-sdk artifact trees and verifies the artifact
  checker end to end. clang-tidy is intentionally tracked separately because
  it depends on a configured compile database and may need a narrower source
  scope.

## Build Configuration Gates

- `build-config-smoke`: the release-gates workflow builds the SDK, client,
  server, and examples in both Debug and Release modes on Linux/Ninja with
  tests and docs disabled. This keeps release-mode
  compile coverage in CI without doubling the full cross-SDK conformance
  matrix.
- `performance-evidence-linux-gcc-ninja`: the release-gates workflow builds
  and runs `protocol_serialization_benchmark` in a Linux GCC Release
  configuration and uploads the JUnit/log output. Treat this as scoped
  serialization evidence, not as a cross-platform performance claim. The
  artifact supports review of the performance debt tracked from
  `docs/technical_audit.md`; it does not by itself justify a fact-standard or
  general SDK performance claim.
- `public-header-compile-evidence-linux-gcc-ninja`: the release-gates workflow
  builds SDK dependencies once, then builds each canonical public-header
  fixture serially and records elapsed time in
  `public-header-compile-evidence.json`. Treat this as Linux GCC evidence for
  `json_fwd` / `extern template` decisions, not as a cross-platform
  compile-time claim. Local public-header timings may guide work, but only the
  exact release-candidate artifact can be cited in release notes.
- `clang-tidy-public-headers`: the release-gates workflow configures an
  auth-enabled compile database, builds the public-header compile fixtures, and
  runs clang-tidy over those fixtures. The scope is intentionally the public SDK
  entry headers first; broader implementation clang-tidy can be added after its
  noise level is managed.
- `public-api-surface.json`: the release evidence job records stable targets,
  optional targets, public include roots, and public header paths through
  `scripts/collect_public_api_surface.py`. Reviewers compare this manifest
  against previous release evidence with
  `scripts/compare_public_api_surface.py` before claiming public SDK surface
  stability.
- `sanitizer-asan-ubsan` and `sanitizer-tsan`: the release-gates workflow runs
  dedicated Linux/Clang Ninja builds with ASan/UBSan and TSan enabled through
  `CMakePresets.json`, after checking out the pinned RMCP reference required by
  RMCP conformance coverage. These sanitizer builds run the practical
  release-blocking subset, including RMCP conformance, and intentionally skip
  `package_smoke` plus external process-stdio interop tests, so sanitizer flags
  do not become part of the default package consumer path. Windows/MSVC and
  normal Linux/macOS release-matrix legs remain unsanitized by default.

## Protocol, Transport, And Interop Gates

- `check_protocol_model_coverage.py`: protocol headers must keep public
  `*_from_json` and `*_to_json` helpers paired, with only documented internal
  helper exceptions. This guards protocol model symmetry as new MCP families or
  fields are added.
- `check_rmcp_source_drift.py`: the pinned RMCP checkout, model source mapping,
  and protocol-model audit table must stay synchronized with the recorded RMCP
  reference commit.
- `protocol`: JSON-RPC and MCP protocol serialization, parsing, version policy,
  and typed model basics.
- `transport_contract` and `transport_stdio_contract`: role-generic transport
  contract behavior.
- `stdio_transport`, `http_transport`, and `transport_adapters`: concrete and
  compatibility transport behavior, including failure-path coverage plus short
  HTTP concurrent-session and many-in-flight request smoke coverage. The
  current HTTP backend decision is recorded in
  `docs/compatibility_policy.md#http-transport-backend-evidence`; another HTTP
  stack requires measured load, lifecycle, sanitizer, or downstream workload
  evidence.
- `process_stdio_transport`: process-stdio diagnostic and regression coverage.
  It is intentionally not release-blocking because it is a broad multi-runtime
  aggregate that duplicates the dedicated process-stdio interop gates and can
  mask the specific child process that timed out.
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
runtime, Windows/ClangCL Ninja with static runtime, and Windows/MSVC Visual
Studio with dynamic runtime. The same workflow also builds the Doxygen HTML
artifact that must be attached to release candidates when API docs are
advertised.

MinGW coverage is intentionally outside the release-supported matrix today.
The scheduled `.github/workflows/compiler-compat.yml` workflow runs
`windows-mingw-ucrt64-gcc` and `windows-mingw-clang64-clang` as provisional,
best-effort compatibility evidence under the `mingw-sdk` job. Because that job
is still `continue-on-error: true`, MinGW UCRT64 GCC and MinGW CLANG64 Clang
must not be presented as release-supported targets in release notes, README
compatibility claims, package-manager claims, or ecosystem maturity evidence.

Each matrix leg uploads a `cxxmcp-release-gates-*` artifact containing the
`CMakeCache.txt`, CTest JUnit XML, and CTest log files. Release candidates must
link or attach those artifacts so package-smoke, public-header, transport,
conformance, and interoperability results are auditable after the workflow run
expires from the Actions UI.
When conformance status is advertised, refresh `docs/conformance_evidence.md`
from current server and client `--suite all` runs. Sub-suite results may be
included as supporting notes, but they must not replace all-suite server/client
comparisons.
`scripts/check_release_artifacts.py` validates more than file presence: it
parses the uploaded JUnit XML and requires the expected release-blocking,
public-header, auth, OpenSSL auth, package-smoke, and interop testcase names to
be present in the corresponding artifacts. It also opens the final SDK source
archive and verifies that SDK sources, release verifier scripts, docs, package
smoke tests, and external consumer templates are present while generated
Doxygen output and external gateway/CLI sources stay out of the SDK source
package. The same verifier opens the final release tarballs produced by
`release-sdk.yml` and checks that release-gates, Doxygen, source, and release
evidence archives contain their expected top-level artifacts.
When the artifacts are from a release candidate, run the same verifier with
`--review-output release-artifact-review.md` plus the exact tag, commit, run
URL, and release URL. The generated markdown is the human review record that
ties the downloaded artifact tree to the release decision; it must not be
generated from local-only or stale workflow artifacts. When verifying an
already assembled release artifact directory without `--review-output`, the
verifier requires `release-artifact-review.md` to be present.

The release evidence manifest records the pinned reference versions used by the
interop matrix:

- RMCP reference commit: `c330fede90e4729c234f8e87fdbc5ea27a1dd10c`
- TypeScript SDK reference: `@modelcontextprotocol/sdk@1.29.0`
- Python SDK reference: `mcp==1.27.1`

The `scripts/check_release_evidence.py` verifier runs before the release
evidence artifact is uploaded. It fails the workflow if the release evidence is
missing required documents, if SDK API docs accidentally include external
tooling headers, if pinned interop versions are absent, or if compatibility
examples are not labelled as non-canonical SDK paths.

`docs/technical_audit.md` travels in the release evidence bundle to preserve
the code-defect audit trail. It is not a substitute for release-gates artifacts:
fixed audit entries describe implementation closure, while package-manager
proof, optional auth/OpenSSL package boundaries, generated docs, public API
surface stability, and performance/compile-time evidence are established only
by the exact-commit artifacts listed here and reviewed through the release
candidate checklist.

The same workflow uploads:

- `cxxmcp-doxygen-html`: generated public API documentation.
- `cxxmcp-auth-release-gate-*`: auth-enabled SDK/package-smoke evidence for
  the optional `cxxmcp::auth` target without making auth part of the default
  SDK package path.
- `cxxmcp-auth-openssl-release-gate-linux-gcc-ninja`: Linux GCC evidence that
  `CXXMCP_AUTH_CRYPTO=OpenSSL` builds, runs `auth_openssl`, and keeps the
  OpenSSL-backed package smoke opt-in. This is not cross-platform OpenSSL
  package evidence unless matching Windows/macOS artifacts are attached.
- `cxxmcp-performance-evidence-linux-gcc-ninja`: Linux GCC Release output from
  `protocol_serialization_benchmark`, used to keep serialization hot-path
  claims attached to a concrete release artifact.
- `cxxmcp-public-header-compile-evidence-linux-gcc-ninja`: Linux GCC Release
  target-level timings for canonical public-header compile fixtures, used to
  evaluate compile-time debt before changing the public JSON or template
  boundary.
- `cxxmcp-package-vcpkg-default` and `cxxmcp-package-vcpkg-http-auth`: real vcpkg
  overlay package-consumption evidence for the default SDK package and optional
  auth feature.
- `cxxmcp-package-conan-default` and `cxxmcp-package-conan-http-auth`: real Conan
  package-consumption evidence for the default SDK package and optional auth
  option.
- `cxxmcp-package-xmake-default` and `cxxmcp-package-xmake-http-auth`: real xmake
  package-consumption evidence for the default SDK package and optional auth
  option. Release-gates artifacts use a temporary local xmake repository whose
  recipe points at a generated source archive from the same workflow checkout,
  including checked-out submodule contents, so xmake verifies the exact
  release-candidate source instead of a stale previously published release
  archive. The release artifact verifier requires both the rewritten temporary
  repository recipe and the generated source archive to be present.
  These package-manager artifacts are Ubuntu Linux evidence unless the release
  notes attach matching Windows or macOS package-manager artifacts for the same
  release commit.
- `cxxmcp-source`: a source archive with recursive submodule contents and a
  `SHA256SUMS.txt` file.
- `cxxmcp-release-evidence`: the README, Chinese README, changelog,
  contribution guide, security policy, code of conduct, auth design and user
  guide, compatibility policy, public API stability policy, dependency policy,
  ecosystem maturity ledger, adoption ledger, protocol model audit, performance
  debt ledger, public API surface manifest, RMCP source mapping, release
  process, graceful shutdown guidance, Peer/Service migration guide, release
  gates, release candidate checklist, release notes template, request lifecycle
  notes, capability lifecycles, technical audit, TODO, the release evidence
  verifier scripts, the external consumer template, and example source files
  used for the release decision.

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
