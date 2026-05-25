# RMCP Source Gap Analysis

This document compares the current C++ MCP SDK/runtime code with the local RMCP source under `reference/rmcp`.

The purpose is not to copy RMCP line by line. The goal is to identify what makes RMCP feel like an SDK and what the C++ project should change to provide a similar developer-facing shape while preserving the existing gateway and CLI layers.

## Status

Already covered in the current tree:

- `peer` and `service` public facades for role-aware client/server sessions
- `handler` aggregates and interface-based callback installation
- client-side completion helpers
- client-side elicitation helpers
- task-related client and server APIs, including SDK server task processing
- request handles for async peer calls
- in-memory task management service in `runtime`
- public JSON Schema and tool-definition builders for lower-boilerplate server
  authoring
- lightweight role-generic transport contract under `cxxmcp::transport`

The remaining gaps in this document are the ones that still need attention.

Current review snapshot:

- `cxxmcp` is now roughly `75-80%` of the RMCP SDK shape.
- The current tree is a strong C++ SDK candidate, not yet RMCP-grade.
- Recent work closed several release-blocking and interop-blocking items:
  protocol-version validation, SSE event ids, task/executor guardrails,
  stdio lifecycle hardening, typed server authoring helpers, and package-smoke
  reliability.
- The main remaining gap is depth, not surface area: RMCP still has a more
  unified service runtime, deeper transport/session behavior, broader
  conformance coverage, and stronger authoring ergonomics.

## RMCP Reference Shape

The RMCP core crate is organized around these layers:

- `model`: typed MCP protocol models
- `service`: peer lifecycle, request dispatch, roles, cancellation, request handles
- `handler`: application-facing client/server handler traits
- `transport`: async transport abstraction and transport adapters
- `task_manager`: task lifecycle support for long-running operations
- `rmcp-macros`: tool and handler macros for ergonomic server authoring

The important public SDK shape is:

- `Peer<RoleClient>` / `Peer<RoleServer>`
- `Service<RoleClient>` / `Service<RoleServer>`
- `ClientHandler`
- `ServerHandler`
- `Transport<R>`
- `ServiceExt::serve(...)`

This is different from a concrete `Client` or `Server` class with mutable callback setters.

## Current C++ Shape

The current C++ code already has useful MCP pieces:

- `protocol`: JSON-RPC and MCP model types
- `client`: concrete client with stdio, process stdio, and HTTP transports
- `server`: concrete server with registries, handlers, transports, auth, and rate limiting
- `peer`: role-aware client/server facades
- `service`: synchronous service lifecycle wrappers
- `handler`: aggregate and interface-based callback contracts
- `runtime`, `tools/cli`, and gateway code: orchestration and product behavior

This shape is practical for the current product. The SDK-facing peer/service
facades now give it an RMCP-like entry point, while the remaining work is to
make the lower-level protocol, transport, and task-operation behavior just as
complete.

## Fact-Standard Readiness Compared With RMCP

The current C++ SDK is a serious candidate for the default C++ MCP SDK, but it
is not yet RMCP-level or reference-grade as an SDK. It has the right public
shape and broad feature coverage, but RMCP still has the stronger SDK core:
peer/service lifecycle, async transport, request cancellation, task processing,
protocol modeling, and handler ergonomics are integrated more deeply.

This is a technical SDK-readiness judgment, not an ecosystem adoption judgment.
Even ignoring stars, package downloads, or downstream users, the C++ SDK should
not yet be described as a de facto-standard-grade SDK. A more accurate label is
`RMCP-like strong candidate`.

After the latest local changes, the gap has narrowed. Package-smoke and RMCP
conformance now pass in the reviewed `build-smoke` tree, protocol-version
handling is explicit, and Streamable HTTP now emits SSE event ids. The remaining
RMCP delta is no longer about whether the project has the expected SDK nouns.
It is about whether those nouns are the real runtime kernel and whether every
transport and protocol family is covered by RMCP-grade behavior tests.

| Dimension | RMCP | Current C++ SDK |
|---|---|---|
| SDK center | `Peer<Role>` / `Service<Role>` are the primary implementation model | `Peer` / `Service` are public facades over concrete `Client` / `Server` implementations |
| Lifecycle | `RunningService` has real async run, shutdown, and wait semantics | `serve`, `close`, and `wait` exist, but the service layer is still mostly synchronous and `wait` is not a real driver join |
| Transport | role-generic async transport is the native boundary | role-generic `Transport<Role>` exists, while HTTP and process-stdio still rely heavily on compatibility adapters and request/response-oriented internals |
| Handler ergonomics | handler traits and macros are a first-class authoring model | handler interfaces, aggregates, and C++ builders are useful, but less integrated than RMCP traits and macros |
| Protocol model | model layer is closer to the tracked MCP spec and richer in typed shapes | coverage is broad, but `_meta`, annotations, extension bags, elicitation/schema, and task result details still need tightening |
| Request lifecycle | timeout, cancellation, request handles, and service cancellation are deeply connected | request handles and cooperative cancellation exist, but cancellation and timeout are not yet uniform from transport through service |
| Task support | operation processing and task lifecycle are core SDK concepts | task APIs and server task processing exist, but hard cancellation and richer operation result transport remain gaps |
| Packaging | Cargo makes the SDK easy to consume with consistent build settings | CMake package usage is now tested, but Conan/vcpkg and release artifact discipline still need to be established |

The implication is that the C++ SDK does not need to copy RMCP line by line, but
it should close the abstraction gap before claiming SDK-level standard status.
The highest-value closure points are:

1. make `Peer` / `Service` the real primary implementation path, not just the
   public facade
2. make built-in stdio, process-stdio, and HTTP transports native
   `Transport<Role>` implementations with bidirectional receive loops
3. give `RunningService::wait`, shutdown, and cancellation real lifecycle
   semantics
4. complete the protocol model details where the SDK still stores spec fields as
   raw JSON or omits typed accessors
5. keep package-smoke, RMCP conformance, and cross-SDK interop tests as required
   gates for public releases

This section is intentionally kept in the RMCP gap analysis because it is a
snapshot judgment against RMCP's SDK shape. The durable release and ecosystem
plan lives in [de_facto_standard_roadmap.md](./de_facto_standard_roadmap.md).

## Abstraction Difference Summary

The most important conceptual difference is this:

RMCP abstracts a bidirectional MCP service session.

The current C++ code abstracts concrete callable client/server objects plus runtime and gateway helpers.

In RMCP, the center is the peer session:

```text
model
  -> Transport<R>
  -> Service<R>
  -> Peer<R>
  -> RunningService<R, S>
```

In the current C++ code, the center is the concrete client/server runtime:

```text
protocol
  -> Client
  -> Server
  -> Transport
  -> Registry / Callback
  -> Gateway / CLI
```

| Area | RMCP | Current C++ Code |
|---|---|---|
| Top-level abstraction | `Peer<Role>` + `Service<Role>` | `Peer` facades plus `Client` / `Server` compatibility wrappers |
| Role model | `RoleClient` / `RoleServer` | role-aware peer facades over client/server implementations |
| Application extension point | `ClientHandler` / `ServerHandler` | handler aggregates, interface contracts, and registries |
| Transport | role-generic async send/receive | lightweight role-generic contract plus client/server-specific concrete transports |
| Lifecycle | `serve(...)` returns `RunningService` | `serve(...)` and `start()` / `stop()` wrappers are both present |
| Request dispatch | typed request/response/notification enums | string method dispatch plus manual JSON conversion |
| Bidirectional calls | `Peer` is first-class | partially supported, but not the core abstraction |
| Timeout/cancellation | request handles and cancellation tokens | mostly ad hoc or transport-specific |
| Task support | integrated task manager and handler methods | SDK server task processor now covers generic operations and task-aware `tools/call`; remaining gaps are harder cancellation and deeper result-transport parity |
| Product runtime | outside SDK core | gateway/runtime concepts are prominent in the repository |

This means the current C++ design has both product-friendly compatibility
wrappers and an SDK-first facade. The standardization pressure is now on making
the facade complete enough that users do not need to learn the wrappers first.

The recommended direction is not to remove the current design. Keep tightening
the RMCP-like SDK facade below the gateway and CLI:

```text
protocol
  -> transport
  -> peer
  -> handler
  -> compatibility Client / Server wrappers
  -> gateway / CLI
```

## Major Differences

### 1. SDK Abstraction

RMCP uses a role-based peer abstraction:

- `RoleClient`
- `RoleServer`
- `Peer<R>`
- `Service<R>`
- `RunningService<R, S>`

The C++ SDK now has matching public facades:

- `mcp::RoleClient`
- `mcp::RoleServer`
- `mcp::Peer<RoleClient>`
- `mcp::Peer<RoleServer>`
- `mcp::Service<RoleClient>`
- `mcp::Service<RoleServer>`
- `mcp::RunningService<RoleClient>`
- `mcp::RunningService<RoleServer>`

Covered by current tree:

- shared role-aware peer abstraction
- shared role-generic transport contract
- role markers
- unified service lifecycle layer
- compatibility wrappers that still expose the older concrete client/server entry points

### 2. Handler Model

RMCP application code implements handler traits:

- `ClientHandler`
- `ServerHandler`

Those handlers are converted into `Service<RoleClient>` or `Service<RoleServer>`.

The C++ SDK now also has:

- `ClientHandlerInterface`
- `ServerHandlerInterface`
- aggregate `ClientHandler` / `ServerHandler` structs
- `set_handler(...)` adapters on the public client and server APIs

Covered by current tree:

- explicit handler contracts
- handler aggregates for ergonomic setup
- adapter helpers that install all non-empty callbacks

### 3. Transport Model

RMCP transport is async and role-generic:

- `send(...)` may be concurrent
- `receive(...)` is sequential
- `close(...)` is explicit
- `IntoTransport` adapts streams, workers, async read/write pairs, and concrete transports

The current C++ transport model now has a lightweight role-generic contract,
while the concrete built-in transports remain request/response oriented:

- SDK transport contract exposes `mcp::transport::Transport<Role>` with
  message-level `send`, sequential `receive`, and explicit `close`
- native role-generic stdio stream transport implements direct send/receive
- client/server compatibility adapters can wrap the existing concrete
  transports for outbound request/notification flows
- the existing `mcp::client::Client` can consume a role-generic
  `mcp::transport::ClientTransport` through an adapter that dispatches inbound
  notifications and server-to-client requests while awaiting responses
- the existing `mcp::server::Server` can consume a role-generic
  `mcp::transport::ServerTransport` through an adapter that dispatches inbound
  requests and notifications and supports outbound server messages
- `mcp::ClientPeer` and `mcp::ServerPeer` expose direct role-generic transport
  entry points that wrap those adapters for SDK users
- client transport has `send(request) -> response`
- server transport has `send_request`, `send_notification`, `start`
- HTTP and stdio behavior is embedded in separate client/server transport classes

Gap:

- service lifecycle still runs through the synchronous legacy client/server
  runtime internally, with role-generic transports bridged through adapters
- HTTP and process-stdio transports are not yet fully native role-generic
  receive streams
- legacy client/server callback-loop transports are only partially adapted to
  the role-generic contract
- concurrency guarantees are documented at the interface but not yet stress
  tested against concrete adapters
- cancellation and timeout are exposed at the peer/client request layer, not as
  a role-generic transport contract
- async request behavior exists through request handles, but the transport
  contract itself is still request/response oriented

Action:

- keep `cpp-httplib` internally for now
- deepen HTTP and process-stdio adapters so inbound callback loops become
  receive streams
- make the peer API independent of httplib
- defer Boost.Asio/Beast until load tests prove the need

### 4. Protocol Model Completeness

RMCP's `model` layer is more complete and closer to the MCP spec snapshot it
tracks.

Examples in RMCP:

- `Content` supports text, image, audio, embedded resource, and resource link
- content/resource/prompt/tool types support annotations
- tools support `title`, `output_schema`, `annotations`, `execution`, `icons`, and `_meta`
- resources support `title`, `size`, `icons`, and `_meta`
- prompts support `title`, `icons`, and `_meta`; prompt arguments support
  `title` and `_meta`
- capabilities include `experimental` and `extensions`
- `_meta` and extensions are consistently modeled

The current C++ models are simpler:

- `ContentBlock` supports text, image, audio, embedded resource, and resource
  link variants, with convenience constructors and typed accessors
- `ToolDefinition` has name, description, input schema, output schema,
  streaming flag, icons, execution/task-support metadata, annotations, and
  metadata
- `Resource`, `ResourceTemplate`, and `Prompt` include title, icons,
  annotations, and metadata
- capability storage uses explicit presence flags plus compact booleans for
  optional members, and client, server, and task capability serialization now
  use RMCP-style object presence
- `_meta` exists only as raw optional JSON in some JSON-RPC types

Gap:

- model coverage is incomplete compared with RMCP
- some serialized shapes may not match RMCP/spec exactly
- missing metadata and annotations will limit interop with richer clients

Action:

- finish annotations support where it is still represented as raw JSON
- add `_meta` support at the proper model/request/notification levels
- keep simple helper constructors for common text-only cases

### 5. Capabilities

RMCP capabilities use optional objects, extension maps, and explicit nested capability models.

The current C++ capabilities now use object-presence semantics for client,
server, and task capability families. They also preserve experimental /
extension bags. The capability model can now express present-but-empty roots,
server tool/resource/prompt families, sampling, elicitation modes, and task
families. The remaining work is to audit extension-bag edge cases and each
public configuration API against the pinned RMCP snapshot.

Covered by current tree:

- task capabilities serialize `list`, `cancel`, and task-enabled request
  methods as object members
- task capability parsing accepts the object form and legacy boolean form
- client and server initialize paths share the same task capability serializer
- client initialize serialization omits inactive `roots`, `sampling`,
  `elicitation`, and `tasks` families; supports sampling `tools` / `context`;
  and supports form elicitation `schemaValidation`
- server initialize serialization omits inactive capability families, omits
  false optional members, and serializes `logging` / `completions` as empty
  capability objects
- public capability structs can express present-but-empty roots, server
  tool/resource/prompt families, sampling, elicitation modes, and tasks
- fluent client/server capability builders avoid verbose nested boolean setup
- client/server capability serialization emits only object-shaped experimental
  and extension bags; client parsing rejects non-object bags

Action:
- audit extension bag behavior against future MCP/RMCP snapshots

### 6. Task Support

RMCP has first-class task support:

- `TaskSupport`
- `ToolExecution`
- `OperationProcessor`
- `enqueue_task`
- `list_tasks`
- `get_task_info`
- `get_task_result`
- `cancel_task`
- task timeout handling
- task result transport

The C++ SDK now exposes task-related client and server APIs, task protocol
types, and a server-side task processor for task-aware tool calls.

Covered by current tree:

- task request and result models
- task-capable client methods
- task result handlers on the server side
- tool-level `TaskSupport` / `ToolExecution` wire modeling and server-side
  required/forbidden/optional invocation validation
- SDK server task processor for `tools/call` requests with `task` parameters
- generic SDK server `submit_operation` path for non-tool task operations
- task-aware `tools/call` returns `CreateTaskResult` and executes the tool in
  the background
- built-in server fallback handlers for `tasks/list`, `tasks/get`,
  `tasks/result`, and `tasks/cancel` when no custom task handler is installed
- `tasks/result` returns the original `ToolResult` JSON for completed
  background tool calls
- task-aware tool handlers receive a cooperative cancellation token through
  `ToolContext`
- terminal task records support bounded count retention and optional completed
  task TTL cleanup
- client and gateway `tools/call` paths preserve task request metadata
- task status notifications
- runtime task management service

Still to close:

- hard cancellation of already running C++ handlers; current behavior is a
  cooperative token and late-result suppression
- richer typed operation result transports beyond JSON payload storage

### 7. Elicitation

RMCP models elicitation as a feature-gated surface with typed form and URL flows.

RMCP includes:

- form elicitation params
- URL elicitation params
- client-side default decline behavior
- server peer helpers such as `elicit<T>()`
- URL elicitation completion notification
- capability checks
- schema-driven typed elicitation helpers

The C++ SDK now has elicitation models and helper methods on both the client and peer facades.

Covered by current tree:

- elicitation request and result types
- typed and raw helper APIs
- client and peer entry points

Still to close:

- deeper form-vs-URL structure if required
- stronger schema integration
- broader capability gating

### 8. Completion Convenience

RMCP exposes completion helpers:

- `complete_prompt_argument`
- `complete_prompt_simple`
- `complete_resource_argument`
- `complete_resource_simple`

The C++ SDK now has these helpers on the client and peer facades.

Covered by current tree:

- prompt completion helpers
- resource completion helpers
- raw `complete(...)` overloads for advanced callers

### 9. Request Lifecycle, Timeout, and Cancellation

RMCP has a richer request lifecycle:

- `RequestHandle`
- response awaiting
- request timeout
- cancellation notification on timeout
- `CancellationToken`
- running service cancellation token
- service shutdown waiting

The current C++ API now has request handles, timeout options, and cooperative
cancellation tokens in the peer/client layer. Running services expose explicit
`close()` and `wait()` lifecycle methods plus a shared cooperative cancellation
token. Client and client-peer APIs now expose typed async helpers for common
tool, prompt, resource, resource-template, completion, sampling, elicitation,
and task requests, while the older concrete server API is still mostly
synchronous and direct.
Task-aware tool calls also share the synchronous local validation behavior when
required task parameters are missing.
Typed async helpers preserve `RequestOptions` metadata and expose cancellation
tokens through their returned request handles.

Gap:

- timeout and cancellation behavior still needs stress coverage across typed
  peer request families and concrete transports
- service lifecycle is synchronous and less featureful than RMCP's async
  running-service model

Action:

- keep request handles as the public async request path
- make timeout and cancellation options consistent across peer request families
- send cancellation notifications when timed out
- deepen service lifecycle with real async wait handles

### 10. Macro and Schema Ergonomics

RMCP uses macros and `schemars` to make server authoring concise.

The C++ project cannot copy Rust macros directly, but it can offer equivalent ergonomics:

- typed tool registration
- schema builders
- optional automatic schema generation where possible
- JSON schema validation
- low-boilerplate handler adapters

Covered by current tree:

- JSON Schema object builder and primitive schema helpers
- `SchemaTraits<T>` / `schema_for<T>()` customization for typed helper APIs
- fluent `ToolDefinition` builder for public server tool metadata
- low-boilerplate `App::Builder` adapters for simple C++ callables
- typed `mcp::server::tool<Args, Result>(...)` registration facade
- typed tool handlers can accept decoded args, `ToolContext`, or a cooperative
  cancellation token without dropping to the low-level `ToolHandler`
- prompt shorthand handlers can accept `PromptContext` alongside raw JSON or
  string arguments without dropping to the low-level `PromptHandler`
- resource shorthand handlers can accept read params, requested URI, and
  `ResourceContext` combinations without dropping to the low-level
  `ResourceReadHandler`

Action:

- add optional `json-schema-validator`
- deepen typed helper templates for any remaining non-tool surfaces where
  context injection is useful
- keep registry helpers as convenience wrappers over `ServerHandler`

## Recommended Roadmap

### Phase 1: RMCP-Like Facade Without Breaking Current API

Current tree covers:

- `Peer<RoleClient>` and `Peer<RoleServer>`
- `ClientHandler` and `ServerHandler`
- shared `mcp::transport::Transport<Role>` contract
- existing `Client` and `Server` as wrappers

Still to close:

- adapters from the existing HTTP/stdio transports into the role-generic
  transport abstraction for full bidirectional receive-loop behavior

### Phase 2: Align Core Protocol Models

- keep content variant helpers aligned with new MCP content shapes
- add annotations
- add `_meta` and extensions
- expand tool/resource/prompt models
- align capability serialization

### Phase 3: Complete Request Lifecycle

Current tree covers:

- request handles
- timeout options
- cancellation tokens
- typed async helpers for common client and client-peer request families

Still to close:

- add shutdown/wait lifecycle

### Phase 4: Add Task Manager

Current tree covers:

- SDK server task operation processor
- generic operation submission API
- task-aware `tools/call` creation and background execution
- built-in `tasks/list`, `tasks/get`, `tasks/result`, and `tasks/cancel`
  handling
- cooperative cancellation token for task-aware tool handlers
- bounded and TTL-based cleanup for terminal task records
- task status notifications

Still to close:

- hard cancellation for non-cooperative running handlers
- richer operation result transport parity with RMCP

### Phase 5: Add Elicitation Helpers

- split form and URL elicitation types
- add capability checks
- add typed helper APIs
- add schema validation

### Phase 6: Keep Gateway and CLI Separate

- keep registry, exposure profiles, trust policy, auth, rate limits, and CLI workflows outside the SDK core
- make gateway depend on the SDK, not the other way around

## What Not To Do

Do not immediately replace `cpp-httplib` with Boost.Asio/Beast just to look async.

Do not move gateway concepts into SDK headers.

Do not create a custom protocol dialect.

Do not add a second JSON stack.

Do not make task and elicitation mandatory if the first SDK milestone only needs core MCP parity.

## Summary

The current C++ code is functionally moving toward MCP parity and now has the
right RMCP-like public facade. Remaining parity work is mostly depth: protocol
models, concrete transport adapters, operation processing, and schema
ergonomics.

The most important work is:

1. keep `Peer + Handler + Transport` as the SDK facade
2. keep gateway and CLI above that facade
3. complete the protocol model layer
4. add task and elicitation as optional but well-shaped extensions
5. keep `cpp-httplib` for now behind a replaceable transport adapter

This approach preserves the current product layers while giving the project an SDK surface that feels much closer to RMCP.

## Multi-Agent Fact-Standard Review

Date: 2026-05-25.

This review used four separate sub-agent passes:

- protocol and public API coverage
- transport, interoperability, and conformance
- engineering quality, release, and maintainability
- developer experience, packaging, and ecosystem readiness

The combined conclusion is that `cxxmcp` is a strong candidate for becoming the
default C++ MCP SDK, but it has not yet reached de facto-standard status. The
original aggregate score was about `6.5 / 10`. After the later package-smoke,
protocol-version, task, SSE event-id, and stdio lifecycle fixes captured above,
the current practical RMCP comparison is closer to `7.5-8 / 10`.

| Review area | Score | Summary |
|---|---:|---|
| Protocol and API coverage | 7.5 / 10 | Broad protocol families, protocol-version validation, and peer/service facades exist, but multi-version policy, HTTP auth, and public-contract stability need work. |
| Transport and interop | 7 / 10 | stdio, process stdio, and Streamable HTTP are usable and SSE event ids exist, but conformance coverage is narrow and HTTP session/backpressure/resume behavior is not yet standard-grade. |
| Engineering and release maturity | 7.5 / 10 | CMake packaging, install targets, scripts, release policy, and package-smoke coverage exist, but CI, API/ABI policy enforcement, dependency governance, and package-manager routes are not enough. |
| Developer experience and ecosystem | 7 / 10 | README, examples, CMake targets, typed authoring helpers, CLI, and smoke tests are useful, but package-manager entry points, public docs, governance files, and community signals are missing. |

### Current Baseline

The current tree already has important standard-SDK ingredients:

- SDK-first public story around `protocol`, `transport`, `handler`, `peer`,
  `service`, `client`, and `server`
- stable-looking CMake targets such as `cxxmcp::protocol`,
  `cxxmcp::transport`, `cxxmcp::peer`, `cxxmcp::service`,
  `cxxmcp::client`, `cxxmcp::server`, and `cxxmcp::sdk`
- typed protocol models for tools, prompts, resources, roots, completion,
  logging, sampling, elicitation, tasks, cancellation, and progress
- stdio, process-stdio, and HTTP transport implementations
- RMCP-style peer and service facades above compatibility client/server layers
- package-smoke and RMCP conformance test entries
- release policy, changelog, Doxygen support, formatting, cpplint, and
  clang-tidy scripts

That is enough to call the project a serious SDK candidate. It is not enough to
call it the MCP C++ SDK fact standard yet.

### Verification Results

Local test verification captured by the review:

```powershell
ctest --test-dir build-client-server --output-on-failure
```

Result: `5 / 5` passed.

Covered tests:

- `protocol`
- `client_server`
- `stdio_transport`
- `http_transport`
- `process_stdio_transport`

Broader smoke verification captured by the review:

```powershell
ctest --test-dir build-smoke -C Debug --output-on-failure
```

Latest result: `12 / 12` passed.

Passed:

- `protocol`
- `client_server`
- `stdio_transport`
- `http_transport`
- `rmcp_conformance`
- `sdk`
- `public_targets`
- `process_stdio_transport`
- `transport_contract`
- `transport_stdio_contract`
- `transport_adapters`
- `package_smoke`

The earlier package-smoke blocker has been closed in this local tree. The
durable requirement remains: a de facto-standard SDK must not merely build
in-tree; it must install and link reliably from a clean consumer project on
every supported toolchain and generator.

### Blocking Gaps

1. Release validation must stay continuously enforced.

   Installed-package consumption needs to remain a required release gate. CI
   also needs to run the same package-smoke path on Windows, Linux, and macOS.

2. Protocol version negotiation still needs multi-version policy.

   The SDK now tracks an explicit supported-version list, validates
   `initialize` protocol versions on both client and server paths, rejects
   unsupported versions, and has compatibility tests for the tracked
   `2025-11-25` version. The remaining standard-grade work is the policy and
   test matrix for carrying more than one MCP version at once when future
   protocol snapshots need overlapping support windows.

3. Streamable HTTP is usable but not yet product-grade.

   The implementation has session ids, POST, GET SSE, DELETE, and basic
   server-to-client messaging, but standard-grade behavior needs multi-session
   tests, bounded queues, backpressure, timeout/cancellation semantics,
   reconnect behavior, and `Last-Event-ID` resume coverage. SSE event ids are
   now present; the remaining work is to make them part of a tested resume and
   replay contract.

4. HTTP authorization is not yet an SDK-level contract.

   The client can add bearer and custom headers, and the server has an auth
   hook, but a standard SDK needs explicit request auth context, scopes,
   claims, standard challenge/error behavior, and OAuth/resource-metadata
   integration points.

5. Process stdio is not cross-platform complete.

   Non-Windows process stdio paths still need a POSIX implementation. MCP SDKs
   are commonly used to spawn local MCP servers, so this is part of the core
   SDK expectation.

6. RMCP conformance coverage is still narrow.

   The current conformance test proves useful interop, but it does not yet cover
   a full matrix of tools, prompts, resources, sampling, elicitation, tasks,
   progress, cancellation, error paths, HTTP sessions, and server/client
   combinations across RMCP, TypeScript, Python, and cxxmcp.

7. Ecosystem readiness is not there yet.

   The project needs public CI status, release artifacts, package-manager
   routes such as vcpkg or Conan, a minimal external consumer template,
   published API docs, contribution/security docs, and a clear dependency
   update policy before it can credibly be described as the default C++ MCP SDK.

### Minimal Closure Plan

The shortest path to a defensible fact-standard claim is:

1. Keep `package_smoke` as a required release gate across supported generators
   and runtimes.
2. Add CI for Windows/MSVC, Linux/GCC, Linux/Clang, and macOS/AppleClang.
3. Keep protocol-version negotiation tests as a release gate and extend them
   when the SDK supports more than one MCP protocol version.
4. Complete Streamable HTTP session, backpressure, cancellation, reconnect, and
   resume semantics.
5. Add POSIX process-stdio support.
6. Expand RMCP and cross-SDK interoperability tests into a matrix, not a single
   happy path.
7. Publish install and package-manager paths for normal C++ consumers.

Until those are closed, the right public description is:

```text
cxxmcp is an MCP C++ SDK and a strong candidate for the default C++ SDK,
but it is not yet a de facto-standard-grade SDK.
```

## Latest RMCP Delta After Recent Changes

Date: 2026-05-25.

This section records the current comparison against the pinned local RMCP
source after the latest local additions.

### What Has Moved Closer To RMCP

- The SDK now has the expected public surface: `protocol`, `transport`,
  `handler`, `peer`, `service`, `client`, and `server`.
- `Peer<RoleClient>` / `Peer<RoleServer>` and `Service<RoleClient>` /
  `Service<RoleServer>` give users an RMCP-like entry point.
- Client-side typed async helpers and request handles now cover the common
  request families more broadly.
- Task support is no longer just protocol modeling; SDK server task processing,
  task-aware `tools/call`, fallback task handlers, task status notifications,
  cooperative cancellation, and bounded/TTL cleanup are present.
- Capability bags now use RMCP-style object presence more consistently.
- Protocol-version support is explicit and initialize paths reject unsupported
  versions.
- Streamable HTTP has started to expose the event identity needed for resume
  semantics by emitting SSE event ids.
- Package-smoke and RMCP conformance passed in the reviewed `build-smoke`
  configuration.

### What Still Keeps RMCP Ahead

1. RMCP's service runtime is still deeper.

   RMCP's `Peer`, `Service`, and `RunningService` are the core execution model.
   In `cxxmcp`, the equivalent names exist and are useful, but they still wrap
   concrete `Client` / `Server` paths more than they drive the whole runtime.
   `RunningService::wait()`, shutdown, and cancellation need to become real
   lifecycle joins around the active transport/service loop.

2. RMCP's transport model is more native and async.

   `cxxmcp` has a role-generic transport contract, but built-in HTTP and
   process-stdio still carry compatibility/request-response internals. RMCP is
   closer to having the role-generic transport as the native boundary for all
   service execution.

3. Streamable HTTP still needs session-grade behavior.

   SSE event ids are now present, but RMCP-level Streamable HTTP also needs
   tested `Last-Event-ID` resume behavior, stale-session handling, init
   timeout behavior, concurrent SSE streams, bounded queues, backpressure, and
   inflight response drain behavior.

4. Process stdio must become fully cross-platform.

   Windows lifecycle behavior has improved, but the SDK still needs complete
   POSIX process-stdio support. Spawning local MCP servers is a core SDK use
   case, so this cannot remain platform-partial.

5. Protocol-version support needs an overlap policy.

   Single-version validation is now in place. RMCP-grade behavior needs a
   documented policy and tests for carrying more than one MCP protocol snapshot
   when future versions require overlapping compatibility windows.

6. The model layer still needs richer edge coverage.

   The C++ model layer is broad, but RMCP remains stronger around consistent
   `_meta`, annotations, extension bags, elicitation schema behavior, and
   detailed task result transport. These are the details richer clients and
   servers notice.

7. Authoring ergonomics still favor RMCP.

   C++ builders and typed registration helpers are improving, but RMCP's macro
   routers for tools, prompts, handlers, and task handlers remain more concise.
   The C++ equivalent should keep pushing typed registration, schema traits,
   context injection, and low-boilerplate handler adapters.

8. Conformance is still too narrow.

   Passing `rmcp_conformance` is a good signal, but it is not yet the same as
   RMCP's broader matrix around Streamable HTTP sessions, tasks, progress,
   elicitation, cancellation, concurrent streams, stale sessions, and error
   behavior.

9. Ecosystem proof still trails RMCP.

   Cargo gives RMCP a clear consumption story. `cxxmcp` now has CMake package
   smoke coverage, but still needs public release CI, package-manager routes
   such as vcpkg or Conan, published API docs, governance/security docs, and
   release artifacts before it can credibly act as the default C++ MCP SDK.

### Current Judgment

`cxxmcp` is no longer just an implementation with some SDK APIs. It is a strong
SDK candidate with an RMCP-like public shape and passing smoke/conformance
signals. The remaining work is to make the facade the real runtime, make
Streamable HTTP and process stdio production-grade across platforms, and expand
interop tests from a few happy paths into a release-blocking matrix.

The current practical rating against RMCP is about `75-80%`. The most accurate
public claim is:

```text
cxxmcp is an MCP C++ SDK with RMCP-like architecture and strong standard-SDK
potential, but RMCP remains ahead in service runtime depth, transport/session
semantics, conformance breadth, and ecosystem maturity.
```

Relevant protocol references:

- https://modelcontextprotocol.io/specification
- https://modelcontextprotocol.io/specification/2025-11-25/changelog
- https://blog.modelcontextprotocol.io/tags/protocol/
