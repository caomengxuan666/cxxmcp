# Agent Instructions

This repository is the `cxxmcp` C++ SDK for the Model Context Protocol. Treat it
as an SDK and package project, not as a standalone MCP server, npm package, CLI,
gateway, or hosted runtime.

## Source Of Truth

- Canonical documentation lives in `README.md`, `README_zh.md`, `docs/`, and
  the generated GitHub Pages site. Do not use GitHub Wiki as an authoritative
  documentation source.
- Release gate details are in `docs/release_gates.md`; package consumption rules
  are in `docs/package_consumption.md`; conformance claims are in
  `docs/conformance_evidence.md`.
- Dependency boundaries and update cadence are summarized in
  `DEPENDENCY_POLICY.md`; detailed release-time dependency review rules are in
  `docs/dependency_policy.md`.
- Keep the repository root small. Prefer adding detailed material under `docs/`
  unless a root file is a standard project file.

## Do Not Touch Casually

- Do not edit generated or local-output paths: `out/`, `build*/`,
  `CMakeFiles/`, `Testing/`, `docs/doxygen/`, `.cache/`, or IDE folders.
- Do not change vendored dependencies under `third_party/` unless the task is an
  explicit dependency upgrade. `third_party/httplib` is vendored from upstream;
  preserve its upstream state unless the upgrade is intentional.
- Do not change `.github/dependabot.yml` casually. It is part of the dependency
  update policy, not a generic CI tuning file.
- Do not change `reference/` or RMCP fixture assumptions unless the work is about
  RMCP drift/conformance evidence.
- Do not make SDK public headers expose runtime/gateway/tooling concepts such as
  app/runtime/gateway/CLI/profile/policy/discovery/import/export/trust types, or
  `httplib`. The `sdk_boundary` test enforces this.
- Do not add `FIXME`, `HACK`, or `XXX` markers in first-party source, examples,
  templates, or tests. `scripts/check_source_markers.py` rejects them.

## Public SDK Boundary

- Public SDK headers are under:
  - `sdk/include`
  - `sdk/core/include`
  - `sdk/protocol/include`
  - `sdk/client/include`
  - `sdk/server/include`
  - `sdk/transport/include`
  - `sdk/auth/include` only when auth is enabled
- Public API must remain C++17. Tests and examples may use C++20 where the build
  files already require it.
- If you add or change a public header, update or add a fixture in
  `tests/sdk/public_headers/` so the header compiles independently.
- If you add or rename package targets, update `CMakeLists.txt`,
  `tests/fixtures/package_smoke/`, `tests/CMakeLists.txt`, and package recipes
  together.

## Build Options And Package Contract

- The default project path is SDK-only. Gateway/runtime/CLI tooling belongs in
  external repositories, not in the SDK package surface.
- Keep optional features opt-in:
  - `CXXMCP_ENABLE_HTTP` controls HTTP/SSE transport support.
  - `CXXMCP_ENABLE_AUTH` controls optional OAuth/DPoP auth scaffolding.
  - `CXXMCP_AUTH_CRYPTO=OpenSSL` requires `CXXMCP_ENABLE_AUTH=ON`.
- Default package-manager builds must not pull OpenSSL or full auth into the
  default install path.
- Default source/archive builds use bundled header-only SDK dependencies.
  Registry builds should use `CXXMCP_USE_SYSTEM_DEPS=ON`.
- `cpp-httplib` is a transport implementation detail. Public consumers should
  include cxxmcp HTTP transport headers, not `httplib.h`.

## Protocol And Conformance Work

- Protocol model changes must keep `*_to_json` and `*_from_json` coverage
  symmetric. Run `scripts/check_protocol_model_coverage.py --source .`.
- If a protocol change is based on RMCP sources, update
  `docs/rmcp_source_mapping.json`, `docs/protocol_model_audit.md`, and drift
  evidence together. Run `scripts/check_rmcp_source_drift.py --source .` after
  checking out the pinned RMCP reference if needed.
- Do not update conformance percentages in README/docs unless the underlying
  all-suite conformance evidence has been regenerated.

## Required Local Checks

Run the narrowest check that covers your change. For broad changes, run the full
test preset.

Always run formatting before finishing source changes:

```powershell
pwsh -NoProfile -File scripts\format.ps1 -Check
```

Full SDK test path:

```powershell
cmake --preset tests
cmake --build --preset tests
ctest --preset tests
```

Examples path:

```powershell
cmake --preset examples
cmake --build --preset examples
ctest --preset examples
```

Useful targeted tests:

```powershell
ctest --test-dir out/build/tests -R "^(sdk|client_server|protocol)$" --output-on-failure
ctest --test-dir out/build/tests -R "^(http_transport|stdio_transport|transport_adapters)$" --output-on-failure
ctest --test-dir out/build/tests -R "^(package_smoke|public_targets|sdk_boundary)$" --output-on-failure
```

Source/release consistency checks:

```powershell
python scripts/check_protocol_model_coverage.py --source .
python scripts/check_sdk_header_boundaries.py --source .
python scripts/check_source_markers.py --source .
python scripts/check_package_auth_features.py --source .
python scripts/check_package_recipe_sync.py --source .
python scripts/check_p2_todo_status.py --source .
```

## Change-Specific Expectations

- Public headers: build the matching `mcp_public_header_*` target or run
  `ctest -R "^public_header_"` from the test build.
- CMake/package recipes: run `package_smoke` and the package sync/auth feature
  scripts.
- Transport changes: run the relevant transport tests and `client_server`.
- HTTP changes: configure with `CXXMCP_ENABLE_HTTP=ON` and run
  `http_transport` plus relevant examples.
- Auth changes: configure with `CXXMCP_ENABLE_AUTH=ON`; if OpenSSL code is
  touched, also configure with `CXXMCP_AUTH_CRYPTO=OpenSSL` and run
  `auth_openssl`.
- Process-stdio changes: run `process_stdio_transport` and the interop tests
  when the change can affect external child processes.
- Protocol serialization changes: run `protocol` and consider
  `protocol_serialization_benchmark` if performance claims are affected.
- Release evidence or docs claims: update the evidence docs and run the
  corresponding checker script. Do not change badges or conformance numbers
  speculatively.

## CI And Release Gates

- Every release-blocking test must be registered through
  `cxxmcp_mark_release_blocking()` in `tests/CMakeLists.txt`. The
  `release_gate_manifest` test checks this list.
- Sanitizer CI exists only on Linux/Clang presets (`tests-asan-ubsan`,
  `tests-tsan`) and runs the practical release-blocking subset. Do not assume
  Windows local tests cover sanitizer-only races.
- MinGW jobs are provisional and `continue-on-error`. Do not describe MinGW as a
  release-supported compiler unless the compatibility policy changes.
- The release-supported matrix is defined by `.github/workflows/release-gates.yml`
  and `docs/release_gates.md`, not by a single local build.

## Release Process

- Use `python -B scripts/prepare_release.py <version>` to prepare release
  metadata before tagging. Do not hand-edit version/package references when the
  script can do it. The script computes the deterministic SDK source archive
  SHA256 and writes that hash into package-consumption docs and the xmake
  package recipe.
- `scripts/prepare_release.py` intentionally stops before commit, tag, or push.
  After reviewing and committing its changes, create/push the `v<version>` tag;
  the `release-sdk` workflow publishes the SDK source archive and release
  artifacts from that exact tag.
- The SDK source archive is produced by
  `python -B scripts/create_sdk_source_archive.py --tag v<version> --output ...`.
  Keep `prepare_release.py` and `.github/workflows/release-sdk.yml` on that
  shared archive path; do not reintroduce ad hoc tar commands.
- If a just-published release must be replaced, delete the GitHub release and
  remote tag first, then recreate the tag only after the replacement commit has
  green release evidence.
- Doxygen HTML is generated output. Use `pwsh -NoProfile -File
  scripts/build-doxygen.ps1 -Clean` or the CI Doxygen job to regenerate it; do
  not manually edit `docs/doxygen/`.

## Editing Discipline

- Keep changes scoped. Do not refactor unrelated modules while fixing a local
  bug.
- Preserve user or other-agent changes in the worktree. Never reset or check out
  files to discard changes unless explicitly asked.
- Prefer existing helper APIs and patterns over new abstractions.
- Add tests proportional to the risk. Shared SDK behavior, package targets,
  transport semantics, public headers, and concurrency fixes need regression
  coverage.
- Keep comments sparse and useful. Explain non-obvious concurrency, lifetime, or
  protocol-boundary logic; do not narrate obvious assignments.
