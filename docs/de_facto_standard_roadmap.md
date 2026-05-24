# 成为事实标准的路线

This document describes how `cxxmcp` can become the default C++ MCP SDK in practice:

- for new projects that need a C++ MCP library
- for existing codebases that want a stable embedded SDK
- for contributors who need a clear target for API shape, capability parity, and release policy

This is not a protocol fork plan. It is a standardization plan for the current C++ SDK.

## Status

Completed in the current tree:

- SDK-first public surface around `protocol`, `client`, `server`, `peer`, `service`, `handler`, and `transport`
- release policy and changelog discipline in `docs/release_policy.md` and `CHANGELOG.md`
- interop coverage against local RMCP source through the stdio fixture tests and RMCP conformance client
- client/server, stdio, and HTTP regression coverage in the C++ test suite

## 1. What "Fact Standard" Means

In this repository, "fact standard" means four things at once:

1. **Default choice**: when someone asks for a C++ MCP library, `cxxmcp` should be the obvious answer.
2. **Stable public shape**: the public headers, target names, and core concepts should stay predictable across releases.
3. **Broad capability coverage**: the SDK should cover the MCP families that RMCP already exposes, or provide explicit compatibility wrappers when it cannot.
4. **Trustworthy evolution**: changes should be versioned, documented, and test-backed so downstream users can upgrade with confidence.

If any one of those is missing, the project can be useful, but not yet a fact standard.

## 2. Current Baseline

The current codebase already has the right direction:

- `protocol` for typed MCP data and JSON-RPC serialization
- `client` and `server` as public SDK entry points
- `peer`, `service`, `handler`, and `transport` as the intended SDK shape
- `runtime`, `gateway`, and `cli` as higher-level product layers

The repo is already moving away from a pure concrete `Client` / `Server` model toward a peer/service model. That is the right direction for standardization.

The remaining issue is not whether the project is useful. The issue is whether the public surface is stable, complete, and simple enough that other C++ users can treat it as the reference implementation.

## 3. Standardization Principles

The following rules should hold for the SDK core:

### 3.1 SDK first

The public package should read like a library first, not a runtime product.

- `protocol`, `client`, `server`, `peer`, `service`, `handler`, `transport` are the core
- `runtime`, `gateway`, and `cli` remain optional layers
- gateway policy, trust policy, discovery, and profile management stay out of core headers

### 3.2 No protocol fork

The SDK should track MCP and RMCP-compatible behavior, not invent a separate dialect.

- keep JSON-RPC and MCP model shapes aligned with the spec
- keep escape hatches for raw requests and notifications
- prefer compatibility adapters over bespoke wire formats

### 3.3 Compatibility over novelty

Public API stability matters more than a perfectly minimal design.

- keep old entry points until a documented replacement exists
- add aliases before renaming
- deprecate in phases, not in one sweep

### 3.4 Typed first, raw always available

The SDK should expose typed helpers for normal use and raw methods for edge cases.

- typed tools/prompts/resources/task helpers for common flows
- raw JSON-RPC methods for uncommon or vendor-specific flows

### 3.5 Runtime leakage stays out of SDK headers

The SDK should not require downstream users to understand repository-local runtime state.

- no gateway policy types in core API
- no CLI defaults in public SDK headers
- no hidden repository path assumptions

## 4. Capability Targets

Use the existing capability matrix as the baseline.

### Tier 1: core parity

These should be stable and boring:

- initialize / ping
- tools list / call
- prompts list / get
- resources list / read
- resource templates
- roots
- completion
- logging
- notifications and progress
- raw JSON-RPC escape hatches

### Tier 2: extended parity

These should be first-class even if some remain optional:

- sampling
- subscriptions and change notifications
- elicitation
- task lifecycle
- cancellation propagation
- stdio, Streamable HTTP, and legacy SSE compatibility

### Tier 3: runtime and gateway

These are useful, but they are not what makes the SDK a standard:

- upstream discovery
- exposure profiles
- trust and approval policy
- import/export
- multi-profile hosting
- gateway orchestration

The SDK can coexist with these, but it should not depend on them.

## 5. The Closure Points

This is the shortest honest list of what blocked fact-standard status. The current tree now covers each item below.

### 5.1 Public abstraction is not fully settled

RMCP centers the API on role-aware peer/service objects. The C++ SDK is moving in that direction, but the compatibility wrappers are still visible.

Covered by current tree:

- keep `Peer<RoleClient>` / `Peer<RoleServer>` as the main story
- keep `Service<Role>` as the lifecycle story
- make `Client` and `Server` look like compatibility layers, not the core abstraction

### 5.2 Transport is not yet a first-class role-generic contract

RMCP's transport model is broader and more abstract.

Covered by current tree:

- a narrow shared transport contract
- explicit request / notification flow
- async-capable behavior at the SDK boundary
- support for stdio, HTTP, and compatibility transports without leaking implementation detail

### 5.3 Request lifecycle is still too light

The SDK has started to add request handles and timeout options, but this needs to become a stable public promise.

Covered by current tree:

- request handle semantics
- cancellation on timeout
- explicit request options
- consistent error mapping for transport and handler failures

### 5.4 Protocol modeling still needs more depth

RMCP's model layer is richer.

Covered by current tree:

- `_meta` support where the spec expects it
- annotations, icons, and extension bags where they matter
- fuller content variants
- capability serialization that matches spec shapes, not just bool-heavy shortcuts

### 5.5 Task and elicitation need complete lifecycle stories

These are already present in the codebase, but not yet standard enough.

Covered by current tree:

- clear negotiation rules
- clean typed helper APIs
- server-side lifecycle support
- documentation that says what is core, optional, or experimental

### 5.6 Streamable HTTP needs a richer story

The HTTP transport needs to feel like a product-grade MCP transport, not just a wrapper around a request library.

Covered by current tree:

- session behavior
- reconnect behavior
- stateful vs stateless mode where relevant
- cancellation and progress propagation
- clear compatibility boundaries for legacy SSE

### 5.7 The package contract must freeze

This is the point that turns "good library" into "fact standard."

Needed:

- stable target names
- stable include paths
- documented compatibility rules
- deprecation windows
- release notes for every breaking change

## 6. API Contract to Freeze

The following should become the durable public contract:

- `cxxmcp::protocol`
- `cxxmcp::client`
- `cxxmcp::server`
- `cxxmcp::peer`
- `cxxmcp::service`
- `cxxmcp::handler`
- `cxxmcp::transport`
- `cxxmcp::sdk` as the aggregate convenience target only

The aggregate target should stay optional. Library users should still prefer the narrower targets they actually need.

### 6.1 Peer as the primary SDK surface

`Peer` should be the canonical way users think about an MCP session.

- `Peer<RoleClient>` is the client-side public facade
- `Peer<RoleServer>` is the server-side public facade
- `Client` and `Server` remain as compatibility wrappers and implementation adapters
- `Service<Role>` owns lifecycle and serving state

This matters because the standard should read like a session-based SDK, not like two unrelated concrete classes.

## 7. Compatibility Policy

The project needs an explicit compatibility policy.

### 7.1 Versioning

Use semantic versioning for public releases.

- patch: bug fixes and doc fixes
- minor: additive API and capability growth
- major: breaking public API or behavior

### 7.2 Deprecation

Any public rename or removal should have an explicit deprecation period.

- add new name first
- keep old name as alias
- document the migration
- remove only in the next major version

### 7.3 Source compatibility

For a C++ SDK, source compatibility matters more than internal layout purity.

- avoid unnecessary signature churn
- keep overload sets stable
- prefer additive changes over replacements

## 8. Quality Bar

The SDK now meets the boring reliability bar through the test suite.

Minimum quality bar:

- round-trip tests for protocol models
- client/server integration tests
- stdio, HTTP, and process transport tests
- interop tests against RMCP reference behavior
- failure-path tests for timeout, cancellation, and malformed messages
- regression tests for every public rename or behavior change

If a capability exists in the docs but not in tests, it is not ready to be standard.

## 9. Documentation Bar

The docs must answer the questions users actually ask:

- what do I link against?
- which headers do I include?
- which target do I depend on?
- which features are stable?
- which features are optional?
- what is the recommended public API?
- how do I migrate from old to new forms?

The repository should keep one clear recommendation, not a spread of equivalent-looking entry points.

## 10. Release Bar

Release discipline is part of standardization, and it is now documented in the tree.

### 10.1 Release stages

- **alpha**: shape can still move
- **beta**: API mostly settled, major changes should be rare
- **rc**: only bug fixes and documentation changes
- **stable**: public contract is frozen for the minor line

### 10.2 Release artifacts

Every release should provide:

- versioned source and package metadata
- installable CMake targets
- generated documentation
- changelog entries
- a clear compatibility note

## 11. Governance

The project needs a small amount of process so the API does not drift back into a product-shaped surface.

Suggested governance:

- every breaking API change goes through an RFC or design note
- every new public surface area must state whether it is core, optional, or experimental
- every runtime/gateway concern must be justified if it appears in SDK headers
- every naming change must come with a migration path

## 12. Definition of Done

`cxxmcp` can reasonably call itself the C++ MCP fact standard when all of the following are true:

1. The SDK-first public surface is stable.
2. The core MCP capability set is complete enough for most C++ users.
3. The RMCP-style peer/service model is the recommended entry point.
4. The transport and request lifecycle are explicit and well-tested.
5. Runtime and gateway behavior are clearly separated from the SDK core.
6. The docs, tests, and release process all tell the same story.
7. Downstream users can upgrade without reading source code to guess intent.

## 13. Practical Roadmap

### Phase 1: Freeze the core shape

- stabilize `protocol`, `peer`, `service`, `handler`, `transport`
- keep compatibility wrappers
- stop introducing new public runtime concepts into SDK headers

### Phase 2: Close capability gaps

- finish model parity work
- finish request lifecycle work
- finish task and elicitation behavior
- align transport behavior with the RMCP reference model

### Phase 3: Tighten compatibility

- document deprecations
- reduce public setter-heavy patterns
- turn wrappers into thin adapters

### Phase 4: Declare release discipline

- define alpha/beta/rc/stable gates
- publish changelog discipline
- add interop and regression coverage for every release

### Phase 5: Treat the SDK as the default answer

- publish the recommended headers and targets first
- keep runtime/gateway docs separate
- make examples use the canonical public path

## 14. Non-goals

Do not optimize for these if the goal is fact-standard SDK status:

- a custom RPC dialect
- a second JSON stack
- a second HTTP stack
- gateway policy in public SDK headers
- clever abstraction that makes upgrading harder

## 15. Related Docs

- [Capability Matrix](./capability_matrix.md)
- [RMCP-Like SDK Guidance](./rmcp_like_sdk_guidance.md)
- [RMCP Source Gap Analysis](./rmcp_source_gap_analysis.md)
