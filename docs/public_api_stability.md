# Public API Stability

This document defines how cxxmcp classifies public C++17 SDK APIs and what
must be reviewed before a release can claim source compatibility. It is part of
the release contract together with the compatibility policy, release process,
release gates, and release candidate checklist.

## Public API Scope

The public SDK surface is the installed C++17 API that downstream consumers use
through:

- public headers under `sdk/**/include/cxxmcp/...`
- exported CMake targets documented as SDK targets
- documented package features and package-manager consumption paths
- generated Doxygen pages for the public SDK headers and release policy docs

The canonical stable SDK path is `Peer` / `Service` over the public
`cxxmcp::protocol`, `cxxmcp::transport`, `cxxmcp::handler`, `cxxmcp::peer`,
`cxxmcp::service`, `cxxmcp::client`, `cxxmcp::server`, and `cxxmcp::sdk`
targets. The aggregate `cxxmcp::sdk` target is a convenience target; narrow
targets remain supported for consumers that want smaller dependency surfaces.

Runtime, gateway, CLI, app, adapter, plugin, policy, discovery, and profile
types are outside the core SDK contract unless they are explicitly promoted by
a design note, compatibility review, release notes, and package evidence.

## Stability Classes

Every new public surface must be classified before it appears in release notes
or first-choice documentation.

### Stable

Stable APIs are supported for source compatibility within a stable major
version.
For a stable API, cxxmcp may:

- add overloads, helpers, enum values, fields with documented default behavior,
  and new CMake targets in minor releases;
- clarify behavior where existing valid callers keep compiling and keep their
  documented semantics;
- fix bugs, protocol compliance issues, and security issues in patch releases.

For a stable API, cxxmcp must not:

- remove names, change required include paths, or rename exported targets before
  the next major release;
- require a C++ standard newer than the configured public SDK standard;
- expose private implementation dependencies such as `jsonrpcpp` as public SDK
  requirements;
- replace a working source API only to make implementation structure cleaner.

Stable public renames must be additive first: add the new name, keep the old
name as an alias or forwarding wrapper, mark the old name with
`CXXMCP_DEPRECATED("message")` where possible, document the migration, and
remove the old spelling only in the next major release.

### Experimental

Experimental APIs are public enough for early adopters, but they are not part
of the stable source-compatibility promise until promoted. Experimental status
must be explicit in the header documentation, Doxygen/release notes, or the
surrounding feature document.

Experimental APIs may change or be removed in a minor release when release
notes include the impact and migration path. They must not be presented as the
first-choice SDK path, and stable public APIs must not require downstream users
to consume experimental types.

An experimental API can be promoted to stable only after:

- release-blocking tests cover its documented behavior;
- package-smoke or public-header compile coverage proves it is consumable from
  installed output when it is part of the package surface;
- the release notes state that it is promoted;
- the release candidate checklist records the API diff classification.

### Deprecated

Deprecated APIs are still public, but callers should migrate away from them.
Deprecation requires:

- `CXXMCP_DEPRECATED("message")` where the language form allows it;
- release notes or migration docs naming the replacement;
- tests or package-smoke coverage while the deprecated spelling remains
  installed;
- removal no earlier than the next major release, except for a security or
  protocol compliance issue that cannot be mitigated additively.

Compatibility wrappers such as concrete `client::Client` / `server::Server`
escape hatches may remain available for migration and low-boilerplate examples,
but they must not displace `Peer` / `Service` as the canonical SDK path.

## API Diff Review

Before every beta, rc, and stable release, reviewers must compare public header
diffs under:

- `sdk/include/cxxmcp`
- `sdk/core/include/cxxmcp`
- `sdk/protocol/include/cxxmcp`
- `sdk/client/include/cxxmcp`
- `sdk/server/include/cxxmcp`
- `sdk/transport/include/cxxmcp`
- `sdk/auth/include/cxxmcp` when the advertised package includes auth

Classify each public change as one of:

- additive stable API
- experimental API
- compatible deprecation
- behavior clarification
- bug, security, or protocol compliance fix
- breaking change

Breaking changes cannot ship in an rc or stable release line unless the release
owner records why an additive mitigation is impossible. The release notes must
name the affected API, the reason, the migration path, and the evidence gap if
normal release gates could not run before a security release.

## Evidence Requirements

A release may claim public API stability only when the exact release commit has:

- green release-blocking CTest labels for every advertised matrix leg;
- installed-output `package_smoke` evidence;
- public-header compile evidence for the canonical headers;
- generated Doxygen API documentation from the same commit;
- release notes containing public API diff classifications;
- a completed release candidate checklist linking the artifacts and any design
  notes for new stable or experimental surfaces.

Static-library releases do not claim ABI stability. If shared libraries become
stable release artifacts later, the project must add a separate ABI policy
before advertising ABI compatibility.
