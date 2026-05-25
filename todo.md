# cxxmcp Fact-Standard TODO

This checklist consolidates the former roadmap, RMCP gap analysis, capability
matrix, SDK guidance, high-level API, and release-policy notes into one
authoritative task list.

Goal: make `cxxmcp` the practical default C++ MCP SDK without forking MCP or
turning the SDK surface into a gateway/runtime product.

## Status Snapshot

- [x] SDK-first public surface exists: `protocol`, `transport`, `handler`,
  `peer`, `service`, `client`, `server`, and aggregate `sdk`.
- [x] CMake package targets and install export exist.
- [x] `package_smoke` verifies installed-package consumption from a clean
  external CMake project.
- [x] RMCP-style `Peer<RoleClient>` / `Peer<RoleServer>` and
  `Service<RoleClient>` / `Service<RoleServer>` entry points exist.
- [x] Core typed protocol families exist for tools, prompts, resources, roots,
  completion, logging, sampling, elicitation, tasks, progress, and
  cancellation.
- [x] Stdio, process stdio, Streamable HTTP, and legacy SSE-compatible paths
  exist.
- [x] `build-smoke` currently passes the local protocol, SDK, transport,
  package-smoke, and RMCP conformance entries.
- [x] P0 request lifecycle has focused regression entries for peer
  cancel/timeout races, HTTP cancellation over SSE, stdio cancellation
  notifications, and process-stdio cancellation while a request is pending.
- [ ] Do not claim fact-standard status yet. Current readiness is a strong
  candidate, roughly `85-90%` against the pinned RMCP SDK shape.

Remaining non-P1/P2 proof gates before claiming fact-standard status:

- Multi-compiler / multi-generator installed-package evidence has to be
  produced by `.github/workflows/release-gates.yml`, not only documented.
- Public generated API docs, source archives, checksums, and release artifacts
  have to be built by the release-gates workflow and attached to an actual
  release candidate.
- README, examples, changelog, compatibility policy, CTest/JUnit evidence
  artifacts, and release artifacts have to be audited together after the next
  successful release-blocking matrix run.

## Current P0 Executable Slices

These are the release-blocking test slices that prove the remaining P0 work
incrementally. Keep them concrete; do not replace them with broad claims.

- Peer/service core:
  - [x] `ServerPeer::add_transport(transport::ServerTransport)` keeps
        role-generic transports on the peer boundary instead of adapting them
        into owned concrete server transports.
  - [x] `serve(ServerPeer)` drives peer-owned role-generic server transports
        through `Peer<RoleServer>::serve_transport`.
  - [x] Peer-owned role-generic server transports remain reachable for
        server-initiated notifications and resource subscription routing.
  - [x] Canonical client Peer/Service docs and examples construct
        `ClientPeer` from `transport::ClientTransport` implementations instead
        of starting from concrete `client::Client` convenience APIs.
  - [x] `ClientPeer` owns role-generic client transports at the peer boundary;
        the concrete client compatibility adapter borrows that transport
        instead of owning it.
  - [x] `ClientPeer::raw_request` and Peer-created raw/typed
        `request_async` handles use a peer-level native transport request loop
        when the peer was constructed from `transport::ClientTransport`.
  - [x] Client peer typed async helpers reuse Peer-level `request_async`
        instead of directly forwarding to concrete `client::Client` helpers.
  - [x] Client peer core synchronous tools/prompts/resources helpers reuse
        Peer-level `raw_request` instead of directly forwarding to concrete
        `client::Client` helpers.
  - [x] Client peer synchronous completion, sampling, elicitation, and task
        helpers reuse Peer-level `raw_request` instead of directly forwarding
        to concrete `client::Client` helpers.
  - [x] Client peer synchronous paginated `list_all_*` helpers collect pages via
        Peer-level `raw_request` instead of concrete `client::Client` helpers.
  - [x] Client peer ping, logging level, resource subscription, and outbound
        notification helpers use Peer-level request/notification paths on
        native role-generic transports.
  - [x] Client peer initialize builds and validates the initialize exchange at
        the Peer boundary for native role-generic transports.
  - [x] Client peer native `raw_request` has direct SDK regression coverage for
        unexpected response ids and closed transport before response.
  - [x] Canonical `cxxmcp/peer.hpp` uses adapter factory declarations instead
        of including compatibility transport-adapter implementation headers.
  - [x] Client service shutdown calls `ClientPeer::stop()` so native
        role-generic transports close through the Peer lifecycle boundary.
  - [x] Canonical client loopback example routes raw requests and inbound
        notification dispatch through `ClientPeer` instead of concrete
        `client::Client`.
  - [x] `ClientPeer` exposes client-side handler registration helpers so
        canonical examples do not need `peer.client()` for normal callbacks.
  - [x] Native `ClientPeer` inbound request and notification dispatch uses
        Peer-owned roots and handler state instead of concrete `client::Client`.
  - [x] `ServerPeer` exposes server-side handler registration helpers so normal
        callbacks do not need `peer.server()`.
  - [x] Native `ServerPeer` notification dispatch uses Peer-owned state for
        Peer-registered notification handlers.
  - [x] `ServerPeer` handles `ping` requests at the Peer boundary before
        falling back to concrete server request dispatch.
  - [x] `ServerPeer` handles `initialize` request validation and result
        construction at the Peer boundary before falling back to concrete server
        request dispatch.
  - [x] `ServerPeer` owns Peer-registered raw request handler state and handles
        `tools/list` / `tools/get` discovery requests at the Peer boundary after
        raw-handler override.
  - [x] `ServerPeer` handles non-task `tools/call` requests at the Peer
        boundary with Peer-owned cancellation token propagation.
  - [x] `ServerPeer` handles task-aware `tools/call` requests at the Peer
        boundary by invoking the configured task manager directly.
  - [x] `ServerPeer` handles prompt and resource discovery/read requests at the
        Peer boundary while preserving full session context for handlers.
  - [x] `ServerPeer` handles resource subscribe/unsubscribe requests at the Peer
        boundary and maps native role-generic transports into the server
        subscription table for update routing.
  - [x] `ServerPeer` owns Peer-registered completion, sampling, and logging
        handlers and dispatches those request families at the Peer boundary.
  - [x] `ServerPeer` owns Peer-registered task lifecycle handlers and dispatches
        `tasks/list`, `tasks/get`, `tasks/cancel`, and `tasks/result` at the
        Peer boundary.
  - [x] `ServerPeer` covers all built-in server request families at the Peer
        boundary; the concrete server dispatcher remains only as a compatibility
        fallback for concrete-only handlers, auth/rate policy, and unknown
        methods.
  - [x] `ClientPeer::client()` and `ServerPeer::server()` are retained as
        deprecated compatibility escape hatches, not canonical SDK entry
        points.
  - [x] Client and server contract transport adapters cover duplicate response
        ids as stable unexpected-response failures.
  - [x] Concrete process stdio client transport rejects mismatched response ids
        with stable transport-category errors.
  - [x] Concrete process stdio client transport rejects duplicate in-flight
        request ids with stable transport-category errors.
  - [x] Changelog and Peer/Service migration docs describe the current Peer
        handler helpers, deprecated concrete accessors, and native peer routing
        direction.
  - [x] README and public Peer/Service headers describe Peer/Service as SDK
        boundaries instead of merely facades.
- Request lifecycle:
  - [x] Client peer timeout emits `notifications/cancelled`.
  - [x] Server-side client peer timeout emits `notifications/cancelled`.
  - [x] Client peer external cancellation token emits
        `notifications/cancelled`.
  - [x] Client peer cancel/timeout race stress keeps a single terminal
        cancellation notification.
  - [x] Server-side client peer cancel/timeout race stress keeps a single
        terminal cancellation notification.
  - [x] HTTP server-to-client request timeout emits cancellation over SSE.
  - [x] HTTP explicit cancellation emits cancellation over SSE.
  - [x] Stdio receives and writes `notifications/cancelled`.
  - [x] Process stdio can send `notifications/cancelled` while another request
        is pending and still unblocks on stop.
  - [x] Normal non-task tool handler cancellation token propagation is covered
        through `ToolContext::cancelled()` and `notifications/cancelled`.
  - [x] RequestHandle timeout, cancellation, missing-task, and worker-exception
        failures use stable structured SDK errors.
  - [x] Native process stdio and Streamable HTTP diagnostics expose active,
        completed, failed, and timed-out request worker counts for timeout
        cleanup tests.
  - [x] Transport-level timeout cleanup is covered by observable pending
        cleanup or deterministic worker/late-response accounting across the
        role-generic stdio, process stdio, and Streamable HTTP paths.
  - [x] Process stdio server-to-client handler errors round-trip through both
        concrete and role-generic transport paths.
  - [x] Stdio malformed-input failures are covered on concrete client reads and
        role-generic server receive/close paths.
  - [x] Legacy HTTP client transport rejects mismatched JSON response ids with
        stable transport errors.
  - [x] Native Streamable HTTP client transport covers mismatched response ids
        and duplicate in-flight request ids.
  - [x] Native process stdio client transport covers mismatched response ids
        and duplicate in-flight request ids.
  - [x] Streamable HTTP server transport rejects malformed POST bodies with
        HTTP 400 and a JSON-RPC parse error body.
  - [x] Concrete stdio transports explicitly document duplicate in-flight
        request-id validation as N/A at that synchronous/message-level layer.
  - [x] Concrete stdio, process stdio, HTTP/Streamable HTTP, and compatibility
        adapters have release-blocking failure-path coverage for the applicable
        unexpected id, duplicate id, closed stream/EOF, handler-error, and
        malformed-message cases.
- Conformance matrix:
  - [x] In-tree Streamable HTTP matrix covers initialize, tools, prompts,
        resources/templates/subscribe, roots/list_changed, completion, logging,
        sampling, elicitation, tasks, progress, cancellation, errors, malformed
        messages, unsupported methods, unsupported protocol versions, and
        session stale/delete behavior.
  - [x] RMCP reference client runs against the cxxmcp Streamable HTTP
        conformance server for `initialize`, `tools_call`, and `sse-retry`.
  - [x] cxxmcp process-stdio client runs against an RMCP-based Rust server
        fixture.
  - [x] cxxmcp process-stdio client runs against a TypeScript SDK server
        fixture.
  - [x] cxxmcp process-stdio client runs against a Python SDK server fixture.
  - [x] RMCP client against cxxmcp process-stdio server is covered by a
        release-blocking CTest fixture.
  - [x] TypeScript SDK client against cxxmcp process-stdio server is covered by
        a release-blocking CTest fixture.
  - [x] Python SDK client against cxxmcp process-stdio server is covered by a
        release-blocking CTest fixture.
  - [x] Stdio interop without process ownership is covered by a role-generic
        client/server stream round-trip.
  - [x] Linux and macOS process-stdio jobs are declared to close the
        cross-platform matrix.

## Definition Of Done

`cxxmcp` can credibly call itself the default C++ MCP SDK when all of these are
true:

- [ ] The SDK-first public surface is stable across releases.
- [ ] `Peer` / `Service` is the real execution core, not only a facade over
  concrete `Client` / `Server` paths.
- [ ] Core MCP capability parity is complete enough for most C++ consumers.
- [ ] Transport behavior is explicit, cross-platform, and stress-tested.
- [ ] Request lifecycle, timeout, cancellation, progress, and shutdown semantics
  are uniform from peer through transport and handler execution.
- [x] Streamable HTTP has production-grade session, resume, stale-session,
  backpressure, and concurrent stream behavior.
- [x] Process stdio is complete on Windows, Linux, and macOS.
- [x] RMCP and cross-SDK interoperability are covered by a release-blocking
  matrix, not a single happy path.
- [ ] Installed-package consumption works on every supported compiler,
  generator, and runtime mode.
- [ ] Public docs, examples, changelog, release artifacts, and compatibility
  policy all describe the same canonical SDK path.
- [x] Gateway, runtime, policy, discovery, and CLI concepts stay outside the
  core SDK contract.

## P0: Keep The Standardization Boundary Clear

- [x] Keep `protocol`, `transport`, `handler`, `peer`, `service`, `client`, and
  `server` as the SDK core.
- [x] Keep `runtime`, `gateway`, `cli`, adapters, plugin tooling, discovery,
  trust policy, exposure profiles, import/export, and profile hosting as
  optional layers above the SDK.
- [x] Add a rule that no gateway/policy/profile/discovery type can enter public
  SDK headers without a design note.
- [x] Keep raw JSON-RPC request and notification escape hatches available for
  vendor-specific or future protocol behavior.
- [x] Keep typed helpers as the default path for normal use.
- [x] Do not create a custom MCP dialect or bespoke wire format.
- [x] Prefer compatibility adapters over protocol extensions when integrating
  unusual transports or runtimes.

## P0: Make Peer And Service The Real SDK Core

- [x] Make `Peer<RoleClient>` and `Peer<RoleServer>` the primary implementation
  path for request dispatch, notification dispatch, and lifecycle.
- [x] Keep concrete `Client` and `Server` classes as compatibility wrappers or
  convenience adapters, not the conceptual center of the SDK.
- [x] Move the real request loop behind the `Peer` / `Service` path.
- [x] Make `Service<Role>` own service lifecycle and serving state directly.
- [x] Make `RunningService<Role>` wait on an active transport/service loop, not
  only a synchronous lifecycle flag.
- [x] Define exact `serve`, `close`, `stop`, `wait`, destructor, and moved-from
  behavior.
- [x] Add a `serve(ServerPeer, transport::ServerTransport)` path that drives the
  server loop through `Peer<RoleServer>::serve_transport`.
- [x] Pass the service cancellation token into native role-generic server
  receive loops and close the native transport on stop.
- [x] Make service cancellation token propagation explicit and consistent.
- [x] Define whether client-side services can actively drive receive loops for
  every built-in transport.
- [x] Add examples that use only `Peer` / `Service` as the first-choice public
  API.
- [x] Keep old concrete client/server examples only as compatibility or
  low-level examples.
- [x] Add migration docs from concrete `Client` / `Server` usage to
  `Peer` / `Service`.

## P0: Stabilize Public API And Package Contract

- [x] Freeze these target names:
  `cxxmcp::protocol`, `cxxmcp::transport`, `cxxmcp::handler`,
  `cxxmcp::peer`, `cxxmcp::service`, `cxxmcp::client`,
  `cxxmcp::server`, and `cxxmcp::sdk`.
- [x] Treat `cxxmcp::sdk` as an aggregate convenience target only.
- [x] Keep narrow targets usable independently by downstream consumers.
- [x] Freeze public include paths under `cxxmcp/...`.
- [x] Define the supported C++ standard and compiler matrix.
- [x] Define source compatibility expectations for public headers.
- [x] Decide whether ABI stability is explicitly out of scope for static builds,
  or define an ABI policy if shared libraries are supported later.
- [x] Add deprecation macros or documented deprecation markers for public API
  migration.
- [x] Require every public rename to follow: add new name, keep old alias,
  document migration, remove only in next major.
- [x] Add a release checklist item for reviewing public header diffs.
- [x] Add tests that compile the canonical headers independently.
- [x] Keep `package_smoke` as a release-blocking test.
- [x] Run package smoke on installed output, not only in-tree targets.
- [x] Label package smoke and canonical SDK tests as release-blocking CTest
  entries.

## P0: Transport Contract And Runtime Behavior

- [x] Make `mcp::transport::Transport<Role>` the native boundary for all
  service execution.
- [x] Keep the role-generic transport contract narrow: send, receive, close,
  name/diagnostics, and documented concurrency guarantees.
- [x] Document whether `send` is concurrent-safe per transport.
- [x] Document that `receive` is sequential and what `std::nullopt` means.
- [x] Define transport close semantics for pending requests and blocked receive.
- [x] Add native role-generic receive-loop behavior for stdio.
- [x] Add native role-generic receive-loop behavior for process stdio.
- [x] Add native role-generic receive-loop behavior for Streamable HTTP.
- [x] Keep compatibility transport adapters out of canonical SDK entry headers.
- [x] Cover compatibility adapter closed-stream and unexpected-response-id
      failures with stable structured errors.
- [x] Keep compatibility adapters for old concrete transports, but make them
  implementation details.
- [x] Stress-test all adapters for bidirectional request/notification flow.
- [x] Test unexpected response ids, duplicate ids, closed streams, handler
  errors, and malformed messages for every concrete transport.
- [x] Define transport-level error categories and map them to JSON-RPC/core
  errors consistently.
- [x] Keep `cpp-httplib` hidden behind transport interfaces so it can be
  replaced later if load testing requires it.
- [x] Do not introduce a second HTTP stack until there is a measured need.

## P0: Streamable HTTP Production Grade

- [x] Define stateful and stateless mode behavior.
- [x] Define initialize-time session creation behavior.
- [x] Define session id issuance, validation, and termination behavior.
- [x] Define stale-session behavior and error mapping.
- [x] Support `Last-Event-ID` resume behavior.
- [x] Persist enough SSE event identity to replay/resume within the documented
  window.
- [x] Add multi-session tests.
- [x] Add concurrent SSE stream tests.
- [x] Add reconnect tests.
- [x] Add stale-session tests.
- [x] Add session DELETE/termination tests.
- [x] Add init-timeout tests.
- [x] Add inflight response drain tests.
- [x] Add bounded outbound queue behavior.
- [x] Add backpressure behavior and tests.
- [x] Add timeout and cancellation propagation tests over HTTP.
- [x] Add progress notification tests over HTTP.
- [x] Add server-to-client request tests over HTTP.
- [x] Add client-to-server notification tests over HTTP.
- [x] Clarify legacy SSE compatibility boundaries.
- [x] Keep Streamable HTTP as the default HTTP transport in docs and examples.
- [x] Mark legacy SSE as compatibility-only.

## P0: Process Stdio Cross-Platform Completion

- [x] Implement POSIX process stdio support for Linux and macOS.
- [x] Support executable path, arguments, working directory, and environment.
- [x] Implement safe argument handling without shell interpolation.
- [x] Implement stdin/stdout pipe setup and cleanup on POSIX.
- [x] Implement child-process termination and timeout behavior on POSIX.
- [x] Avoid leaving orphaned child processes.
- [x] Handle child exit before response.
- [x] Handle malformed child output.
- [x] Handle stderr without blocking stdout protocol flow.
- [x] Add Linux process-stdio tests.
- [x] Add macOS process-stdio tests.
- [x] Keep Windows lifecycle tests.
- [x] Test notification-only flows.
- [x] Test server-to-client requests from a child process.
- [x] Test request timeout and pending-response cleanup.
- [x] Test stop/destructor behavior while a request is pending.

## P0: Request Lifecycle, Timeout, Cancellation, And Progress

- [x] Define `RequestHandle` semantics as a public contract.
- [x] Define whether request handles are single-await or multi-await.
- [x] Define cancellation semantics before send, during send, while waiting, and
  after response.
- [x] Make request id generation thread-safe where a peer/client can be used
  concurrently.
- [x] Keep request options consistent across all typed and raw request helpers.
- [x] Preserve `_meta` / metadata through all typed async request helpers.
- [x] Send `notifications/cancelled` on timeout where the protocol expects it.
- [x] Propagate cancellation token state into handler execution where possible.
- [x] Ensure timeouts unblock or fail pending transport operations
  consistently.
- [x] Ensure timed-out requests clean up pending response state.
- [x] Ensure late responses after timeout are handled deterministically.
- [x] Add stress tests for concurrent async requests.
- [x] Add stress tests for cancellation races.
- [x] Add stress tests for timeout races.
- [x] Add tests for progress token metadata and progress notifications.
- [x] Add tests for cancellation notifications received from peers.
- [x] Add tests for cancellation across client peer, server peer, HTTP, stdio,
  and process stdio.
- [x] Map transport, parse, handler, timeout, and cancellation failures to stable
  error codes/messages.
- [x] Avoid leaking ad hoc exceptions or raw strings through public request
  APIs.

## P0: Protocol Version Policy

- [x] Keep current explicit supported-version validation.
- [x] Define a multi-version overlap policy before supporting the next MCP
  protocol snapshot.
- [x] Decide how long previous protocol snapshots remain supported.
- [x] Decide whether unsupported versions produce protocol errors or negotiation
  fallback.
- [x] Add tests for multiple supported protocol versions once more than one is
  present.
- [x] Add client initialize tests for unsupported versions.
- [x] Add server initialize tests for unsupported versions.
- [x] Add HTTP header/body version mismatch tests.
- [x] Add package/release notes when protocol support changes.
- [x] Keep the SDK aligned with MCP, not with a custom version fork.

## P0: Conformance And Interoperability Matrix

- [x] Expand RMCP conformance beyond the current happy-path tools scenario.
- [x] Add tools/list and tools/call success and failure cases.
- [x] Add prompts/list and prompts/get cases.
- [x] Add resources/list, resources/read, resource templates, subscribe, and
  unsubscribe cases.
- [x] Add roots/list and roots/list_changed cases.
- [x] Add completion/complete cases.
- [x] Add logging/setLevel and notifications/message cases.
- [x] Add sampling/createMessage cases.
- [x] Add elicitation/create form and URL cases.
- [x] Add task lifecycle cases: create, list, get, cancel, result, failure,
  timeout, retention, and status notifications.
- [x] Add progress notification cases.
- [x] Add cancellation notification cases.
- [x] Add structured JSON-RPC error cases.
- [x] Add malformed message cases.
- [x] Add unsupported method cases.
- [x] Add unsupported protocol-version cases.
- [x] Add Streamable HTTP session/resume/stale-session cases.
- [x] Add stdio interop cases.
- [x] Add process-stdio interop cases.
- [x] Test cxxmcp client against RMCP server.
- [x] Test RMCP client against cxxmcp server.
- [x] Test cxxmcp client against TypeScript SDK server.
- [x] Test TypeScript SDK client against cxxmcp server.
- [x] Test cxxmcp client against Python SDK server.
- [x] Test Python SDK client against cxxmcp server.
- [x] Make the interop matrix release-blocking.
- [x] Label the existing RMCP conformance and process-stdio matrix tests as
  release-blocking CTest entries.
- [x] Record pinned SDK/reference versions in test output or docs.

## P1: Protocol Model Completeness

- [ ] Audit all model structs against the pinned MCP/RMCP snapshot.
- [ ] Add `_meta` support where the spec expects it, not only on generic
  JSON-RPC envelopes.
- [ ] Ensure `_meta` round-trips symmetrically.
- [ ] Audit annotations support for tools, prompts, prompt arguments,
  resources, resource templates, and content blocks.
- [ ] Audit icon support for tools, prompts, resources, and templates.
- [ ] Audit title support for tools, prompts, prompt arguments, resources, and
  templates.
- [ ] Audit resource size support.
- [ ] Audit content variants: text, image, audio, embedded resource, resource
  link, and future extension handling.
- [ ] Add extension bags where RMCP/spec provides them.
- [ ] Ensure raw JSON extension data is preserved even when typed helpers do not
  understand it.
- [ ] Make serialization/deserialization exhaustive and symmetric.
- [ ] Add round-trip fixture tests for every protocol family.
- [ ] Add negative parse tests for every required field and type constraint.
- [ ] Keep helper constructors for common text-only flows.
- [ ] Avoid bool-heavy shortcuts when the wire shape expects object presence.

## P1: Capabilities

- [ ] Audit client capabilities against RMCP/spec.
- [ ] Audit server capabilities against RMCP/spec.
- [ ] Audit task capabilities against RMCP/spec.
- [ ] Keep object-presence semantics for active capability families.
- [ ] Preserve present-but-empty capability objects where meaningful.
- [ ] Preserve experimental and extension bags.
- [ ] Reject invalid non-object experimental/extension bags where appropriate.
- [ ] Add tests for every capability serializer and parser.
- [ ] Add tests for capability negotiation affecting helper behavior.
- [ ] Gate roots helpers on roots capability.
- [ ] Gate sampling helpers on sampling capability.
- [ ] Gate elicitation helpers on elicitation capability.
- [ ] Gate task helpers on task capability where applicable.
- [ ] Document which capabilities are core, optional, or experimental.

## P1: Task Lifecycle

- [ ] Keep task models in the SDK protocol layer.
- [ ] Keep runtime task management separate from SDK core task protocol.
- [ ] Complete server-side task lifecycle documentation.
- [ ] Define task negotiation rules.
- [ ] Define task timeout semantics.
- [ ] Define task cancellation semantics.
- [ ] Define terminal task retention and TTL semantics.
- [ ] Add hard-cancellation strategy for non-cooperative running handlers or
  explicitly document why only cooperative cancellation is supported.
- [ ] Add richer typed operation result transport beyond raw JSON payload
  storage where RMCP/spec expects it.
- [ ] Add tests for task-aware `tools/call` success.
- [ ] Add tests for missing/invalid task params.
- [ ] Add tests for task timeout.
- [ ] Add tests for task cancellation before start.
- [ ] Add tests for task cancellation during execution.
- [ ] Add tests for late result suppression after cancellation/timeout.
- [ ] Add tests for task status notifications.
- [ ] Add tests for task result retrieval.
- [ ] Add tests for failed task result retrieval.
- [ ] Add tests for retention count limits.
- [ ] Add tests for completed task TTL cleanup.
- [ ] Add examples for task-aware server tools.

## P1: Elicitation Lifecycle

- [ ] Keep elicitation optional/feature-gated in public docs.
- [ ] Split or clarify form and URL elicitation types if the spec/RMCP shape
  requires deeper structure.
- [ ] Add stronger schema integration for form elicitation.
- [ ] Add schema validation for elicitation content.
- [ ] Add capability checks for form elicitation.
- [ ] Add capability checks for URL elicitation.
- [ ] Add server peer helpers for typed elicitation flows.
- [ ] Add client-side default decline behavior if no handler is installed.
- [ ] Add URL elicitation completion notification tests.
- [ ] Add form elicitation success/decline/cancel tests.
- [ ] Add URL elicitation success/decline/cancel tests.
- [ ] Add raw elicitation escape-hatch tests.
- [ ] Document which elicitation pieces are stable, optional, or experimental.

## P1: Handler And Authoring Ergonomics

- [ ] Keep `ClientHandler` and `ServerHandler` as explicit application
  contracts.
- [ ] Keep aggregate handlers for ergonomic setup.
- [ ] Keep interface-based handlers for durable application code.
- [ ] Route events through explicit handler objects where possible.
- [ ] Reduce public mutable callback setter state over time.
- [ ] Keep registries as convenience wrappers over server handlers.
- [ ] Deepen typed tool registration helpers.
- [ ] Deepen typed prompt helper templates.
- [ ] Deepen typed resource helper templates.
- [ ] Deepen typed completion helper templates.
- [ ] Support context injection consistently for tool, prompt, resource,
  completion, sampling, elicitation, and task handlers.
- [ ] Support cooperative cancellation token injection where meaningful.
- [ ] Keep `SchemaTraits<T>` and `schema_for<T>()` customization stable.
- [ ] Add optional JSON Schema validator integration.
- [ ] Validate tool input schemas where configured.
- [ ] Validate tool output schemas where configured.
- [ ] Validate elicitation schemas where configured.
- [ ] Keep low-boilerplate `App::Builder` helpers as convenience, not the only
  canonical API.
- [ ] Add examples for typed registration, handler interfaces, and raw fallback
  in the same style.

## P1: Error Model

- [ ] Keep typed error model in the protocol/core layer.
- [ ] Map parse errors to JSON-RPC parse errors.
- [ ] Map invalid envelope and validation failures to invalid request or invalid
  params consistently.
- [ ] Map unknown methods to method not found.
- [ ] Map handler exceptions to internal errors with controlled diagnostics.
- [ ] Map permission failures to permission denied.
- [ ] Map rate-limit failures to rate limited.
- [ ] Map missing resources to resource not found.
- [ ] Map missing tools to tool not found.
- [ ] Map elicitation URL requirements to the correct SDK/protocol error.
- [ ] Ensure transport errors do not leak transport-library-specific details
  unless placed in structured debug data.
- [ ] Add tests for every public error family.
- [ ] Add tests that errors serialize as valid JSON-RPC error responses.
- [ ] Add tests that raw handler failures are translated and not thrown through
  public APIs.

## P1: Documentation Bar

- [ ] Keep one canonical "start here" SDK path.
- [ ] Put `Peer` / `Service` examples before concrete `Client` / `Server`
  examples.
- [ ] Document which target to link for each use case.
- [ ] Document which headers to include for each use case.
- [ ] Document stable vs optional vs experimental features.
- [ ] Document transport choices and recommended defaults.
- [ ] Document Streamable HTTP as the default HTTP path.
- [ ] Document legacy SSE as compatibility-only.
- [ ] Document raw JSON-RPC escape hatches.
- [ ] Document migration from old concrete APIs to peer/service APIs.
- [ ] Document timeout, cancellation, progress, and shutdown semantics.
- [ ] Document task lifecycle and retention semantics.
- [ ] Document elicitation lifecycle and capability requirements.
- [ ] Document package consumption with `find_package(cxxmcp CONFIG REQUIRED)`.
- [ ] Publish generated Doxygen docs for releases.
- [ ] Keep README, README_zh, examples, Doxygen, and release notes in sync.
- [ ] Add a minimal external consumer template.

## P1: CI And Release Gates

- [ ] Add public CI for Windows/MSVC.
- [ ] Add public CI for Linux/GCC.
- [ ] Add public CI for Linux/Clang.
- [ ] Add public CI for macOS/AppleClang.
- [ ] Run Debug and Release builds where practical.
- [ ] Run Ninja and Visual Studio generators where practical.
- [ ] Run package-smoke in CI for every supported platform.
- [ ] Run protocol tests in CI.
- [ ] Run client/server tests in CI.
- [ ] Run stdio transport tests in CI.
- [ ] Run process-stdio tests in CI.
- [ ] Run HTTP transport tests in CI.
- [ ] Run transport contract tests in CI.
- [ ] Run transport adapter tests in CI.
- [ ] Run RMCP/cross-SDK conformance tests in CI.
- [ ] Run formatting check in CI.
- [ ] Run cpplint in CI.
- [ ] Run clang-tidy in CI where practical.
- [ ] Build generated docs in CI.
- [ ] Keep all release-blocking tests documented.
- [ ] Add a release checklist that fails if package-smoke or conformance is
  skipped.

## P1: Release And Compatibility Process

- [ ] Use semantic versioning for public releases.
- [ ] Define alpha, beta, rc, and stable gates in release checklist form.
- [ ] For alpha: allow API movement but require clear changelog.
- [ ] For beta: require API mostly settled and breaking changes rare.
- [ ] For rc: only bug fixes and docs.
- [ ] For stable: freeze public contract for the minor line.
- [ ] Include versioned source artifacts for every release.
- [ ] Include generated API documentation for every release.
- [ ] Include changelog entries for every release.
- [ ] Include compatibility notes for every public surface change.
- [ ] Include package metadata for every release.
- [ ] Publish prebuilt or source package artifacts as appropriate.
- [ ] Add a public API diff review step.
- [ ] Add a public dependency update review step.
- [ ] Add a security/advisory process for vulnerabilities.

## P1: Package Manager And Consumer Experience

- [ ] Add vcpkg packaging or a documented vcpkg overlay.
- [ ] Add Conan packaging or a documented Conan recipe.
- [ ] Document FetchContent usage if supported.
- [ ] Document install-from-source usage.
- [ ] Document consuming only `cxxmcp::protocol`.
- [ ] Document consuming only `cxxmcp::client`.
- [ ] Document consuming only `cxxmcp::server`.
- [ ] Document consuming the aggregate `cxxmcp::sdk`.
- [ ] Verify third-party header installation layout.
- [ ] Decide and document dependency vendoring policy.
- [ ] Decide and document whether downstream users can use system versions of
  dependencies.
- [ ] Add package-manager smoke tests when package recipes exist.
- [ ] Add a tiny external consumer repository or template.

## P1: Governance And Project Trust

- [ ] Add `CONTRIBUTING.md`.
- [ ] Add `SECURITY.md`.
- [ ] Add a code of conduct if the project expects outside contributors.
- [ ] Add issue templates for bug reports, feature requests, and protocol
  compatibility reports.
- [ ] Add PR template with testing and public API checklist.
- [ ] Require design notes/RFCs for breaking public API changes.
- [ ] Require every new public surface to state core, optional, or experimental
  status.
- [ ] Require every runtime/gateway concern in SDK headers to be justified.
- [ ] Require every naming change to include migration docs.
- [ ] Track pinned MCP/RMCP reference versions.
- [ ] Track dependency versions and update cadence.

## P2: Developer Experience Polish

- [ ] Keep examples focused on real usable SDK flows.
- [ ] Add minimal stdio server example.
- [ ] Add minimal Streamable HTTP client example.
- [ ] Add process-stdio client example for local MCP servers.
- [ ] Add typed tool server example.
- [ ] Add prompt/resource server example.
- [ ] Add completion example.
- [ ] Add sampling example.
- [ ] Add elicitation example.
- [ ] Add task-aware tool example.
- [ ] Add raw JSON-RPC escape-hatch example.
- [ ] Add handler-interface example.
- [ ] Add graceful shutdown example.
- [ ] Add timeout/cancellation example.
- [ ] Keep examples compiling in CI.
- [ ] Keep example APIs aligned with canonical `Peer` / `Service` docs.

## P2: Runtime, Gateway, CLI Separation

- [ ] Keep gateway docs separate from SDK docs.
- [ ] Keep runtime state out of SDK headers.
- [ ] Keep server registry/discovery out of SDK core.
- [ ] Keep exposure profiles out of SDK core.
- [ ] Keep trust/approval policy out of SDK core.
- [ ] Keep import/export out of SDK core.
- [ ] Keep multi-profile hosting out of SDK core.
- [ ] Make gateway depend on SDK targets, not the other way around.
- [ ] Keep CLI defaults out of public SDK APIs.
- [ ] Add tests that SDK package consumption does not require runtime/gateway
  targets unless explicitly linked.

## P2: Security And Auth

- [ ] Define HTTP auth extension points as SDK-level contracts.
- [ ] Expose request auth context on the server side.
- [ ] Define bearer-token behavior for client helpers.
- [ ] Define custom header behavior.
- [ ] Define challenge/error behavior for unauthorized HTTP requests.
- [ ] Add scope/claims extension points.
- [ ] Add OAuth/resource-metadata integration points if required by the MCP
  snapshot.
- [ ] Add tests for authorized and unauthorized HTTP requests.
- [ ] Add tests for auth context reaching handlers.
- [ ] Document TLS/HTTPS support and build requirements.

## P2: Performance, Load, And Reliability

- [ ] Add load tests for concurrent HTTP sessions.
- [ ] Add load tests for many in-flight requests.
- [ ] Add load tests for high-volume notifications.
- [ ] Add memory/queue bound tests.
- [ ] Add backpressure tests.
- [ ] Add shutdown-under-load tests.
- [ ] Add long-running process-stdio tests.
- [ ] Add malformed input fuzz/smoke tests for JSON-RPC parsing.
- [ ] Add sanitizer builds where practical.
- [ ] Add thread sanitizer builds where practical.
- [ ] Use load-test results before considering a different HTTP backend.

## Non-Goals

- [x] Do not build a custom RPC dialect.
- [x] Do not add a second JSON stack.
- [x] Do not add a second HTTP stack just to look more async.
- [x] Do not make gateway policy part of public SDK headers.
- [x] Do not make task and elicitation mandatory if a milestone only requires
  core MCP parity.
- [x] Do not optimize for clever abstraction at the cost of source
  compatibility.
- [x] Do not let examples present runtime/gateway APIs as the canonical SDK
  path.

## Suggested Milestones

### Milestone 1: Stable SDK Shape

- [x] Freeze target names and include paths.
- [x] Make peer/service examples canonical.
- [x] Keep concrete client/server APIs as compatibility wrappers.
- [x] Add public header diff review.
- [x] Keep package-smoke release-blocking.

### Milestone 2: Native Service And Transport Core

- [x] Make peer/service drive the real request loop.
- [x] Make built-in transports native role-generic transports.
- [x] Deepen service lifecycle into a real active driver.
- [x] Add concurrency and cancellation stress tests.

### Milestone 3: Transport Production Readiness

- [x] Complete Streamable HTTP session/resume/backpressure semantics.
- [x] Complete POSIX process stdio.
- [x] Expand HTTP, stdio, and process-stdio failure-path tests.

### Milestone 4: Protocol And Capability Parity

- [ ] Finish `_meta`, annotations, icons, extension bags, and content variants.
- [ ] Audit capability shapes.
- [ ] Add multi-version protocol policy.
- [ ] Add protocol round-trip and negative tests for every family.

### Milestone 5: Interop Matrix

- [x] Expand RMCP conformance scenarios.
- [x] Add TypeScript SDK interop.
- [x] Add Python SDK interop.
- [x] Make cross-SDK matrix release-blocking.

### Milestone 6: Release And Ecosystem

- [ ] Add public CI matrix.
- [ ] Publish generated docs.
- [ ] Add vcpkg/Conan route.
- [ ] Add contribution/security/governance docs.
- [ ] Publish versioned release artifacts and compatibility notes.
