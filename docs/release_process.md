# Release Process

This process defines when a cxxmcp release can be published and what evidence
has to travel with it. It is part of the SDK compatibility contract together
with the compatibility policy, release gates, release notes template, and
release candidate checklist.

## Versioning

cxxmcp uses semantic versioning for public SDK releases:

- `MAJOR` changes may remove deprecated public APIs, remove protocol snapshots,
  or change source-level behavior that stable consumers can observe.
- `MINOR` changes may add public APIs, protocol families, transports, package
  manager routes, and compatibility adapters while preserving source
  compatibility for the supported minor line.
- `PATCH` changes are bug fixes, documentation fixes, compatibility fixes, and
  packaging fixes that do not add large public API surfaces.

Pre-release tags use `alpha`, `beta`, or `rc` suffixes. Stable release notes
must not call the project fact-standard-ready unless the full release candidate
checklist is complete for the exact release commit.

## Stage Gates

Each public release has one of these stages:

- Alpha: API movement is allowed, but every public SDK movement must be called
  out in `CHANGELOG.md` and release notes.
- Beta: public SDK APIs should be mostly settled. Breaking changes require a
  design note or issue, migration text, and explicit release notes.
- RC: only bug fixes, release evidence fixes, documentation fixes, and package
  metadata fixes are allowed unless a failed gate forces a targeted code fix.
- Stable: public source compatibility is frozen for the minor line. Breaking
  public API changes move to the next major release unless they fix a security
  vulnerability or a protocol compliance issue that cannot be solved
  additively.

The release candidate checklist is the binding stage review record.

## Required Release Artifacts

Every public release must attach or link artifacts produced from the exact
release commit:

- versioned SDK source archive with recursive submodule contents
- `SHA256SUMS.txt` for published source artifacts
- generated public API documentation
- release evidence bundle
- release notes filled from `docs/release_notes_template.md`
- changelog entry for the release
- package metadata or package recipe references for the routes advertised by
  that release

Source packages are the default SDK artifact. Prebuilt binaries are optional
and may be published only for matrix entries that passed release-blocking gates
for the same commit.

## Public API Review

Before tagging a beta, rc, or stable release, review public header diffs under:

- `sdk/include/cxxmcp`
- `sdk/core/include/cxxmcp`
- `sdk/protocol/include/cxxmcp`
- `sdk/client/include/cxxmcp`
- `sdk/server/include/cxxmcp`
- `sdk/transport/include/cxxmcp`

The review must classify every public change as additive, compatible
deprecation, behavior clarification, or breaking change. Breaking changes
require migration notes and must follow the compatibility policy before they
ship in a stable line.

## Dependency Review

Every release review must record dependency/reference versions from
`docs/dependency_policy.md` and the release evidence manifest. Dependency
updates require:

- the reason for the update
- the package manager route affected, if any
- source-package impact for bundled dependencies
- compatibility impact for `CXXMCP_USE_SYSTEM_DEPS=ON`
- conformance or package-smoke evidence after the update

SDK package submissions should keep runtime/tooling dependencies outside the
core SDK package unless a separate tools package is explicitly created.

## Security And Advisories

Security reports follow `SECURITY.md`. A security release may skip the normal
alpha/beta/rc progression only when delaying the fix would increase user risk.
Even then, the release notes must identify the affected versions, fixed
versions, compatibility impact, and any temporary evidence gaps.

Security fixes that require source-breaking API changes must prefer additive
mitigations first. If a breaking change is unavoidable, it is allowed only with
clear migration notes and a major-version plan unless the vulnerable API cannot
remain available safely.
