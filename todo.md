# cxxmcp Release Evidence TODO

This file tracks only unresolved release and ecosystem evidence gates. Completed
implementation history, old roadmap slices, and closed P0/P1/P2 checklists have
been removed from the root TODO so this file stays useful during release review.

Detailed policy and evidence records live in:

- `docs/release_candidate_checklist.md`
- `docs/release_gates.md`
- `docs/ecosystem_maturity_evidence.md`
- `docs/adoption_ledger.md`
- `docs/examples.md`
- `docs/runtime_gateway.md`
- `docs/performance_debt.md`

## Status Notes

Status notes that must stay true until exact release evidence says otherwise:

- Do not claim fact-standard status yet. The current SDK surface is strong, but
  final status depends on release evidence, maturity evidence, and public
  downstream adoption.
- Current open checkboxes are intentionally external-evidence gates. Do not
  close them from local source edits alone: they require exact-commit GitHub
  artifacts, tagged release artifacts, repeated release history, or downstream
  adoption evidence.

## Open Gates

- [ ] The SDK-first public surface is stable across releases.
- [ ] Core MCP capability parity is complete enough for most C++ consumers.
- [ ] Installed-package consumption works on every supported compiler,
  generator, and runtime mode.
- [ ] Public docs, examples, changelog, release artifacts, and compatibility
  policy all describe the same canonical SDK path.

## Planned SDK Ergonomics Work

These are local SDK improvement items, not external release-evidence gates.
Handle them on feature branches before closing the broader open gates above.

- First, converge public examples and docs on the recommended
  `ServerPeer::builder()` / `ClientPeer::builder()` plus `Service` / `run()`
  entry path. Keep `Client`, `Server`, and transport-level APIs as stable
  lower-level compatibility surfaces.
- Add a clearer alias for `mcp::server::ClientPeer` such as
  `mcp::server::SessionClient` or `mcp::server::ClientHandle`, then migrate new
  examples to the clearer name while preserving the existing public type.
- Add a high-level OAuth client flow builder that assembles the common
  metadata endpoint, token endpoint, PKCE, browser presentation, and loopback
  callback pieces without hiding the lower-level injectable auth interfaces.
- Record the optional C++20 coroutine adapter as future work only. The public
  SDK baseline remains C++17, so coroutine support should live in an opt-in
  experimental header if it is implemented later.
- Continue layering examples by capability and integration level. Any example
  path or grouping change must update `examples/CMakeLists.txt`, release
  evidence collection, release artifact checks, and example documentation
  together.
- Strengthen auth public-header coverage so each auth public header that is
  exposed when auth is enabled has an independent C++17 compile fixture, not
  only umbrella-header coverage.
- Completed locally: `RequestHandle` cancellation internals keep
  `RequestHandle::cancel()` and `RequestOptions::cancellation_token`, while
  token callbacks now wake request handles without long-lived cancellation
  watchers occupying shared request executor workers.

## Ecosystem And Registry

- [ ] Accumulate maturity evidence before resubmitting to the vcpkg curated
  registry: stable release history, green release-gates over time, downstream
  examples, package-manager smoke evidence, changelog discipline, and public
  user adoption signals.
- [ ] Maintain an adoption ledger with real downstream repositories,
  integration reports, issue links, or release-note references that can be
  cited in future fact-standard and vcpkg curated-registry claims.
- [ ] Resubmit to the vcpkg curated registry only after the maturity evidence is
  strong enough to address `microsoft/vcpkg#51972` without relying on policy
  exceptions.

## Release Artifacts

- [ ] Publish generated docs from an exact tagged release candidate run.
- [ ] Publish and review versioned release artifacts plus compatibility notes
  for the exact release candidate commit.
