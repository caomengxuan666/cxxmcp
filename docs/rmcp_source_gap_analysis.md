# RMCP Source Gap Analysis

This document compares the current C++ MCP SDK/runtime code with the local RMCP source under `reference/rmcp`.

The purpose is not to copy RMCP line by line. The goal is to identify what makes RMCP feel like an SDK and what the C++ project should change to provide a similar developer-facing shape while preserving the existing gateway and CLI layers.

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
- `app`, `cli`, and gateway code: runtime orchestration and product behavior

This shape is practical for the current product, but it is not yet RMCP-like as a pure SDK.

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
| Top-level abstraction | `Peer<Role>` + `Service<Role>` | `Client` / `Server` |
| Role model | `RoleClient` / `RoleServer` | separate client/server classes |
| Application extension point | `ClientHandler` / `ServerHandler` | callbacks, setters, and registries |
| Transport | role-generic async send/receive | client/server-specific request/response transports |
| Lifecycle | `serve(...)` returns `RunningService` | `start()` / `stop()` on concrete objects |
| Request dispatch | typed request/response/notification enums | string method dispatch plus manual JSON conversion |
| Bidirectional calls | `Peer` is first-class | partially supported, but not the core abstraction |
| Timeout/cancellation | request handles and cancellation tokens | mostly ad hoc or transport-specific |
| Task support | integrated task manager and handler methods | protocol/client pieces exist, server lifecycle is incomplete |
| Product runtime | outside SDK core | gateway/runtime concepts are prominent in the repository |

This means the current C++ design is product-friendly, while RMCP is SDK-first.

The recommended direction is not to remove the current design. Instead, add an RMCP-like SDK facade below the gateway and CLI:

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

The current C++ SDK exposes concrete classes:

- `mcp::client::Client`
- `mcp::server::Server`
- `mcp::client::Transport`
- `mcp::server::Transport`

Gap:

- no shared `Peer` abstraction
- no role abstraction
- no unified service lifecycle
- client/server transports are different interfaces

Action:

- introduce `RoleClient` and `RoleServer` marker types
- introduce `Peer<Role>` as the main SDK facade
- introduce a shared transport abstraction that can support both roles
- keep existing `Client` and `Server` as compatibility wrappers

### 2. Handler Model

RMCP application code implements handler traits:

- `ClientHandler`
- `ServerHandler`

Those handlers are converted into `Service<RoleClient>` or `Service<RoleServer>`.

The current C++ code uses:

- callback setters
- builder callbacks
- registries for tools, prompts, and resources
- raw/custom request handlers

Gap:

- callbacks are spread across the concrete client/server objects
- there is no single handler contract that describes the application behavior
- handler composition and testing are harder than RMCP's trait-based model

Action:

- add `ClientHandler` and `ServerHandler` interfaces or concepts
- route protocol requests through handlers
- keep registries as helper implementations of `ServerHandler`
- reduce public mutable callback setters over time

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
- cancellation and timeout are not part of the core transport contract
- transport is not yet async-capable at the SDK boundary

Action:

- introduce an async-capable transport contract above the existing transports
- keep `cpp-httplib` internally for now
- implement an httplib transport adapter using bounded executors
- make the peer API independent of httplib
- defer Boost.Asio/Beast until load tests prove the need

### 4. Protocol Model Completeness

RMCP's `model` layer is more complete and closer to the latest MCP spec.

Examples in RMCP:

- `Content` supports text, image, audio, embedded resource, and resource link
- content/resource/prompt/tool types support annotations
- tools support `title`, `output_schema`, `annotations`, `execution`, `icons`, and `_meta`
- resources support `title`, `size`, `icons`, and `_meta`
- prompts and prompt arguments support `title`, icons, and `_meta`
- capabilities include `experimental` and `extensions`
- `_meta` and extensions are consistently modeled

The current C++ models are simpler:

- `ContentBlock` is mostly text plus generic `data`
- `ToolDefinition` has name, description, input schema, and streaming flag
- `Resource` and `Prompt` omit title, icons, annotations, and metadata
- capabilities are bool-heavy
- `_meta` exists only as raw optional JSON in some JSON-RPC types

Gap:

- model coverage is incomplete compared with RMCP
- some serialized shapes may not match RMCP/spec exactly
- missing metadata and annotations will limit interop with richer clients

Action:

- expand content models to match RMCP's tagged content variants
- add annotations support
- add `_meta` support at the proper model/request/notification levels
- add tool output schema and tool execution/task support
- add resource/prompt title, icons, and metadata fields
- keep simple helper constructors for common text-only cases

### 5. Capabilities

RMCP capabilities use optional objects, extension maps, and explicit nested capability models.

The current C++ capabilities use booleans in many places.

Example gap:

- RMCP task capabilities use object fields such as `list: {}` and `cancel: {}`
- current C++ code can serialize those as booleans such as `list: true`

Action:

- align capability serialization with RMCP/spec shapes
- represent capability presence with optional empty objects when required
- add `experimental` and `extensions`
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

The current C++ code has task protocol types and client methods, but server support is incomplete.

Current C++ server side has:

- task capability fields
- `notify_task_status`
- task request parameters on some protocol models

Missing pieces:

- task manager / operation processor
- tool-level task support negotiation
- task-based invocation validation
- `enqueue_task`
- server-side `list_tasks`, `get_task`, `task_result`, `cancel_task`
- result retention and timeout policy

Action:

- implement a C++ task manager in the runtime or server layer
- add tool execution metadata with task support mode
- validate task invocation rules before calling tools
- expose task methods through the RMCP-like server handler
- keep task support optional if the first SDK milestone does not need it

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

The current C++ code has elicitation models and handlers, but the surface is flatter.

Gap:

- no typed `elicit<T>()` style helper
- no strong form-vs-url request variants
- limited capability checking
- limited schema integration

Action:

- keep elicitation feature-gated or optional
- split form and URL elicitation models
- add client capability checks
- add typed helper APIs over the raw protocol methods
- integrate JSON schema validation for form elicitation

### 8. Completion Convenience

RMCP exposes completion helpers:

- `complete_prompt_argument`
- `complete_prompt_simple`
- `complete_resource_argument`
- `complete_resource_simple`

The current C++ client has lower-level `complete(...)` overloads but not these convenience helpers.

Action:

- add prompt/resource completion helper methods to the peer facade
- keep raw `complete(...)` for advanced callers

### 9. Request Lifecycle, Timeout, and Cancellation

RMCP has a richer request lifecycle:

- `RequestHandle`
- response awaiting
- request timeout
- cancellation notification on timeout
- `CancellationToken`
- running service cancellation
- service shutdown waiting

The current C++ client/server API is mostly synchronous and direct.

Gap:

- no request handle abstraction
- no unified cancellation token
- no SDK-level timeout/cancellation model
- less explicit service lifecycle

Action:

- add request handles for async peer calls
- add timeout options to peer requests
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

### Phase 1: Add RMCP-Like Facade Without Breaking Current API

- add `Peer<RoleClient>` and `Peer<RoleServer>`
- add `ClientHandler` and `ServerHandler`
- add a shared async-capable transport abstraction
- keep existing `Client` and `Server` as wrappers

### Phase 2: Align Core Protocol Models

- expand content variants
- add annotations
- add `_meta` and extensions
- expand tool/resource/prompt models
- align capability serialization

### Phase 3: Add Request Lifecycle

- add request handles
- add timeout options
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

The current C++ code is functionally moving toward MCP parity, but RMCP-like SDK parity requires a different public shape.

The most important work is:

1. add `Peer + Handler + Transport` as the SDK facade
2. keep gateway and CLI above that facade
3. complete the protocol model layer
4. add task and elicitation as optional but well-shaped extensions
5. keep `cpp-httplib` for now behind a replaceable transport adapter

This approach preserves the current product layers while giving the project an SDK surface that feels much closer to RMCP.
