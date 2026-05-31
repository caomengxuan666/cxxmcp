# Compatibility Policy

This policy defines what cxxmcp treats as the stable SDK contract. It is part
of the release decision together with the README, examples, changelog,
[public API stability](public_api_stability.md), release gates, and release
candidate checklist.

## Canonical SDK Surface

The canonical SDK path is `Peer` / `Service` over the public CMake targets and
headers under `cxxmcp/...`. `Peer` / `Service` are the first-choice application
entry points for new code.

The project and package name is `cxxmcp`, and public CMake targets use the
`cxxmcp::` prefix. The stable C++ namespace remains `mcp` by design. This keeps
source examples concise while the package and include paths avoid collisions.
Renaming the C++ namespace is a breaking API change, not a cosmetic cleanup, and
must follow the public rename policy below if it is ever proposed.

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

Gateway/runtime/CLI/plugin tooling lives outside this SDK repository. External
tooling and optional extension types must not enter public SDK headers without
a design note and release review.

## Source Compatibility And API Classes

Public headers compile as C++17 by default. The
`CXXMCP_SDK_CXX_STANDARD` CMake cache value may be raised by downstream builds,
but stable public headers must not require a standard newer than the configured
SDK standard.

`mcp::core::Result<T>` uses `tl::expected<T, mcp::core::Error>` for every
supported C++ standard in this major release. It must not switch to
`std::expected` based on the consumer's C++ language mode because `Result`
appears in exported SDK signatures and would otherwise produce incompatible
symbols between a C++17-built static library and a C++23 consumer.

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

MinGW is tracked separately as scheduled compiler compatibility evidence, not
as a release-supported target. The
`.github/workflows/compiler-compat.yml` workflow runs the
`windows-mingw-ucrt64-gcc` and `windows-mingw-clang64-clang` jobs as
provisional, best-effort checks while the `mingw-sdk` job remains
`continue-on-error: true`. Release notes and README compatibility claims must
describe MinGW UCRT64 GCC and MinGW CLANG64 Clang as provisional best-effort
compatibility evidence, not release-supported targets, until those jobs become
release-blocking.

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

## HTTP Transport Backend Evidence

This section records the current evidence for keeping `cpp-httplib` as the
Streamable HTTP implementation backend. It is not a benchmark report. It is the
minimum release-gate evidence required before considering a second HTTP stack.

### Current Decision

Keep `cpp-httplib` hidden behind `cxxmcp::transport` and the public HTTP
transport option types. Do not add another HTTP backend unless release-blocking
load, lifecycle, or interoperability tests show a concrete failure that cannot
be fixed behind the existing transport boundary.

The SDK contract remains backend-neutral:

- public headers expose `cxxmcp` transport types, not `httplib` types;
- auth metadata fetches use `OAuthMetadataEndpoint`, not an HTTP client type;
- package consumers do not link or include `cpp-httplib` directly;
- a future backend replacement must preserve the same Streamable HTTP behavior
  and package targets.

### Evidence Gates

The release-blocking `http_transport` CTest entry currently covers the backend
behaviors that would justify replacing or retaining the implementation:

- concurrent Streamable HTTP sessions;
- many in-flight client requests;
- high-volume server-to-client notifications;
- shutdown while an SSE stream is active;
- outbound SSE event-count backpressure;
- outbound SSE byte-queue bounds;
- malformed POST handling;
- stale session, resume, DELETE, and reconnect behavior;
- timeout and cancellation propagation over SSE.

Focused local verification used for this decision:

```powershell
cmake --build build-auth-on-ninja --target mcp_http_transport_tests --parallel
ctest --test-dir build-auth-on-ninja -R "^http_transport$" --output-on-failure --timeout 180
```

The latest local run passed. That proves the current backend satisfies the
short deterministic release-gate load/lifecycle requirements. It does not prove
high-throughput production capacity, so future larger benchmarks should be
added before making performance claims.

### Replacement Trigger

Considering another HTTP backend requires at least one of these:

- a release-blocking load or lifecycle test fails because of a backend limit;
- a required MCP Streamable HTTP behavior cannot be implemented safely on the
  current backend;
- sanitizer or thread-sanitizer gates expose backend-caused memory or
  concurrency defects that cannot be isolated;
- downstream users provide reproducible workload evidence that exceeds the
  current backend's practical envelope.

Without that evidence, adding a second HTTP stack would increase API, package,
and CI surface without improving the SDK contract.
