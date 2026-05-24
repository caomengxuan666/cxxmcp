# RMCP-Like SDK Guidance

This document describes how to reshape the current C++ MCP codebase into an SDK that feels like the official Rust `rmcp` library, while keeping the existing C++ style and avoiding a protocol fork.

## Goal

The target is a small, SDK-first surface:

- a protocol layer with typed MCP models and serialization
- a peer layer for client/server interaction
- transport adapters for stdio, child process, and HTTP
- handler interfaces for application code
- optional higher-level runtime tooling kept outside the SDK core

The SDK should feel like a direct MCP library, not like a gateway product with MCP support attached.

## What RMCP-Like Means

The RMCP shape is closer to:

- `Peer` as the main public client/server facade
- `ClientHandler` and `ServerHandler` as application contracts
- transport adapters hidden behind the peer
- typed protocol families for tools, prompts, resources, roots, logging, sampling, completion, elicitation, and tasks
- raw request and notification escape hatches
- feature-gated extensions where the protocol surface is optional

The current code already has much of the protocol modeling work. The remaining work is mostly about public API shape, layering, and naming.

## Current Code Shape

The current repository mixes several concerns:

- `protocol` provides JSON-RPC types and MCP models
- `client` exposes a concrete blocking client
- `server` exposes a concrete blocking server with registries and transport wiring
- `app`, `cli`, and gateway code add runtime orchestration and policy

That is useful for a product runtime, but it is larger and more opinionated than an SDK.

## Recommended Package Split

### 1. `protocol`

Keep this layer purely about data and serialization.

Should contain:

- JSON-RPC request, response, notification, and error types
- MCP model families for tool, prompt, resource, resource template, root, completion, logging, sampling, elicitation, and task
- capability models
- parse and serialize helpers

Should not contain:

- transport logic
- policy logic
- runtime registry logic
- gateway logic

### 2. `transport`

Add a transport abstraction shared by client and server.

It should support:

- stdio
- child-process stdio
- streamable HTTP
- legacy SSE compatibility

The transport API should be narrow:

- start
- send request
- send notification
- stop

The peer layer should own higher-level protocol behavior.

### 3. `peer`

This is the main SDK layer.

Create two peer-style facades:

- `ClientPeer`
- `ServerPeer`

These should expose the public MCP methods directly and hide transport details.

Examples:

- `list_tools`
- `list_prompts`
- `list_resources`
- `read_resource`
- `get_prompt`
- `call_tool`
- `complete`
- `set_level`
- `subscribe`
- `unsubscribe`
- `create_message`
- `create_elicitation`
- `notify_initialized`
- `notify_cancelled`
- `notify_progress`
- `notify_roots_list_changed`

Keep raw JSON-RPC escape hatches:

- `raw_request`
- `raw_notification`

### 4. `handler`

Replace ad hoc callback setters with explicit handler interfaces.

Use application-facing contracts instead of many mutable setters.

Client-side handler responsibilities:

- logging messages
- custom requests and notifications
- roots list requests
- sampling requests
- elicitation requests
- list-change notifications
- progress and cancellation notifications

Server-side handler responsibilities:

- tool calls
- prompt resolution
- resource reads
- completion requests
- sampling callbacks
- logging callbacks
- custom requests and notifications
- progress and roots notifications

### 5. `runtime` or `app`

Keep the following outside the SDK core:

- server registry
- discovery
- trust and approval policy
- rate limiting
- auth providers
- exposure profiles
- gateway composition
- CLI workflows

These are product/runtime concerns, not RMCP-like SDK concerns.

## API Shape Changes

### Client

The current client is concrete and blocking. To feel like RMCP:

- make the peer the main abstraction
- make transport optional and injectable
- keep `connect_*` constructors as convenience helpers
- normalize method names to MCP verbs
- expose typed and raw variants for the same operation

Suggested shape:

- `initialize`
- `ping`
- `list_tools` / `list_all_tools`
- `list_prompts` / `list_all_prompts`
- `list_resources` / `list_all_resources`
- `list_resource_templates` / `list_all_resource_templates`
- `get_prompt`
- `read_resource`
- `call_tool`
- `complete`
- `create_message`
- `create_elicitation`
- `list_tasks` / `list_all_tasks`
- `get_task`
- `cancel_task`
- `task_result`
- `set_level`
- `subscribe`
- `unsubscribe`

The client should also support:

- response dispatch
- request dispatch from the server or peer side
- notification handlers
- root state management

### Server

The current server is a registry-centric concrete object. To feel like RMCP:

- make the peer surface protocol-first
- keep registries as internal helpers, not the main abstraction
- expose explicit handler interfaces for application code
- keep transport setup separate from MCP semantics

Suggested server surface:

- `initialize`
- `ping`
- `get_info`
- `list_tools`
- `call_tool`
- `get_tool`
- `list_resources`
- `read_resource`
- `list_prompts`
- `get_prompt`
- `complete`
- `subscribe`
- `unsubscribe`
- `set_level`
- task-related methods if task support is part of the target parity
- raw request and notification hooks

## Naming Conventions

Prefer names that mirror MCP verbs and RMCP conventions:

- use `list_*` instead of `discover_*` for SDK methods
- use `notify_*` for notifications
- use `get_*` for single-resource fetches
- use `list_all_*` only for pagination helpers
- keep `call_raw` and `raw_request` as escape hatches, not the primary path

Avoid naming that implies product policy or runtime orchestration in the SDK core:

- `gateway`
- `exposure`
- `trust`
- `registry`
- `profile`

Those belong in a separate higher-level package.

## Async vs Sync

RMCP feels like an async SDK because the public API is built around async transport and handler dispatch.

If the C++ SDK stays synchronous, it will not feel RMCP-like, even if the method names match.

Recommended options:

1. Introduce coroutine-friendly APIs in the peer layer.
2. Keep sync wrappers only as convenience helpers.
3. Ensure the transport and handler dispatch can be driven by an event loop or background worker.

The goal is not just non-blocking I/O. The goal is an API that can compose with long-lived sessions, streaming events, and concurrent requests.

## Elicitation and Tasks

Treat these as optional extensions if parity is not complete yet.

Recommendations:

- keep elicitation feature-gated
- keep task models in `protocol`
- do not force task management into the base client/server if the runtime does not need it
- expose task methods only when the transport and session model can support them cleanly

## Error Model

Keep a typed error model in `protocol`, then map transport and handler failures into it.

Use consistent categories:

- parse error
- invalid request
- method not found
- invalid params
- internal error
- permission denied
- rate limited
- resource not found
- tool not found

Make sure raw failures from transport or handler code are translated into JSON-RPC errors, not leaked as ad hoc exceptions or strings.

## Minimal Migration Plan

### Phase 1: Freeze protocol

- keep `protocol` stable
- finish missing typed models
- ensure serialization is exhaustive and symmetric

### Phase 2: Introduce peer layer

- create `ClientPeer` and `ServerPeer`
- move protocol methods out of concrete client/server classes
- keep the old classes as adapters temporarily

### Phase 3: Replace callback setters

- introduce handler interfaces
- route events through explicit handler objects
- remove most public mutable setter state

### Phase 4: Separate runtime concerns

- move gateway, registry, policy, and CLI workflows out of the SDK public surface
- keep them in `app` or a separate package

### Phase 5: Align naming and docs

- rename discover-style methods to MCP-style verbs where appropriate
- document typed and raw forms side by side
- document feature-gated extensions clearly

## Non-Goals

Do not try to turn the SDK into:

- a gateway framework
- a policy engine
- a CLI product layer
- a custom RPC dialect

Those additions make the library larger, but not more RMCP-like.

## Short Version

If the goal is an RMCP-like SDK, the repository should converge on this shape:

- `protocol` for typed MCP data and serialization
- `peer` for the public client/server API
- `handler` for app callbacks
- `transport` for stdio and HTTP adapters
- `runtime` for gateway/policy/persistence features

That keeps the SDK small, predictable, and close to the official Rust library surface without copying Rust-specific implementation details.
