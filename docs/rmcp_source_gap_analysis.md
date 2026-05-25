# RMCP Source Gap Analysis

This document compares the current C++ MCP SDK/runtime code with the local RMCP source under `reference/rmcp`.

The purpose is not to copy RMCP line by line. The goal is to identify what makes RMCP feel like an SDK and what the C++ project should change to provide a similar developer-facing shape while preserving the existing gateway and CLI layers.

## Status

Already covered in the current tree:

- `peer` and `service` public facades for role-aware client/server sessions
- `handler` aggregates and interface-based callback installation
- client-side completion helpers
- client-side elicitation helpers
- task-related client and server APIs
- request handles for async peer calls
- in-memory task management service in `runtime`

The remaining gaps in this document are the ones that still need attention.

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
| Transport | role-generic async send/receive | client/server-specific request/response transports |
| Lifecycle | `serve(...)` returns `RunningService` | `serve(...)` and `start()` / `stop()` wrappers are both present |
| Request dispatch | typed request/response/notification enums | string method dispatch plus manual JSON conversion |
| Bidirectional calls | `Peer` is first-class | partially supported, but not the core abstraction |
| Timeout/cancellation | request handles and cancellation tokens | mostly ad hoc or transport-specific |
| Task support | integrated task manager and handler methods | protocol/client pieces and runtime task management exist; the remaining gap is deeper RMCP operation processor parity |
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

The current C++ transport model is more request/response oriented:

- client transport has `send(request) -> response`
- server transport has `send_request`, `send_notification`, `start`
- HTTP and stdio behavior is embedded in separate client/server transport classes

Gap:

- transport is not role-generic
- transport does not model concurrent send plus sequential receive
- cancellation and timeout are exposed at the peer/client request layer, not as
  a role-generic transport contract
- async request behavior exists through request handles, but the transport
  contract itself is still request/response oriented

Action:

- introduce an async-capable transport contract above the existing transports
- keep `cpp-httplib` internally for now
- implement an httplib transport adapter using bounded executors
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
- prompts and prompt arguments support `title`, icons, and `_meta`
- capabilities include `experimental` and `extensions`
- `_meta` and extensions are consistently modeled

The current C++ models are simpler:

- `ContentBlock` supports text, image, audio, embedded resource, and resource
  link variants
- `ToolDefinition` has name, description, input schema, and streaming flag
- `Resource` and `Prompt` omit title, icons, annotations, and metadata
- capability storage uses explicit presence flags plus compact booleans for
  optional members, and client, server, and task capability serialization now
  use RMCP-style object presence
- `_meta` exists only as raw optional JSON in some JSON-RPC types

Gap:

- model coverage is incomplete compared with RMCP
- some serialized shapes may not match RMCP/spec exactly
- missing metadata and annotations will limit interop with richer clients

Action:

- add remaining content convenience builders and typed accessors outside the
  core `ContentBlock` helpers where higher-level APIs need them
- finish annotations support where it is still represented as raw JSON
- add `_meta` support at the proper model/request/notification levels
- add tool output schema and tool execution/task support
- add resource/prompt title, icons, and metadata fields
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

Action:

- tighten extension bag typing and empty-map semantics
- add builder helpers to avoid verbose capability setup

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

The C++ SDK now exposes task-related client and server APIs, plus task protocol types.

Covered by current tree:

- task request and result models
- task-capable client methods
- task result handlers on the server side
- task status notifications
- runtime task management service

Still to close:

- richer task lifecycle orchestration
- retention and timeout policy
- deeper parity with RMCP's operation processor model

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
- running service cancellation
- service shutdown waiting

The current C++ API now has request handles and timeout options in the
peer/client layer, while the older concrete client/server API is still mostly
synchronous and direct.

Gap:

- no unified cancellation token
- timeout and cancellation behavior still needs a consistently role-generic
  shape
- service lifecycle is less explicit than RMCP's running-service model

Action:

- keep request handles as the public async request path
- make timeout options consistent across peer request families
- send cancellation notifications when timed out
- add service start/stop/wait lifecycle objects

### 10. Macro and Schema Ergonomics

RMCP uses macros and `schemars` to make server authoring concise.

The C++ project cannot copy Rust macros directly, but it can offer equivalent ergonomics:

- typed tool registration
- schema builders
- optional automatic schema generation where possible
- JSON schema validation
- low-boilerplate handler adapters

Action:

- add JSON schema builder utilities
- add optional `json-schema-validator`
- add typed helper templates for tool registration
- keep registry helpers as convenience wrappers over `ServerHandler`

## Recommended Roadmap

### Phase 1: RMCP-Like Facade Without Breaking Current API

Current tree covers:

- `Peer<RoleClient>` and `Peer<RoleServer>`
- `ClientHandler` and `ServerHandler`
- existing `Client` and `Server` as wrappers

Still to close:

- shared role-generic transport abstraction

### Phase 2: Align Core Protocol Models

- expand content variants
- add annotations
- add `_meta` and extensions
- expand tool/resource/prompt models
- align capability serialization

### Phase 3: Complete Request Lifecycle

Current tree covers:

- request handles
- timeout options

Still to close:

- add cancellation tokens
- add shutdown/wait lifecycle

### Phase 4: Add Task Manager

- add task operation processor
- add task result retention
- add task cancellation
- add task status notifications
- add tool-level task support validation

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
models, role-generic transport, operation processing, and schema ergonomics.

The most important work is:

1. keep `Peer + Handler + Transport` as the SDK facade
2. keep gateway and CLI above that facade
3. complete the protocol model layer
4. add task and elicitation as optional but well-shaped extensions
5. keep `cpp-httplib` for now behind a replaceable transport adapter

This approach preserves the current product layers while giving the project an SDK surface that feels much closer to RMCP.
