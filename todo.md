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
