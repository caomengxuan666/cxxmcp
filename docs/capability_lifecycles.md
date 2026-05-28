# Capability Lifecycles

This document describes the SDK-contract behavior for MCP capability families
that have non-trivial lifecycle semantics. It covers tasks and elicitation.
Other capability families (tools, prompts, resources, sampling, completion) are
covered by their protocol models in `cxxmcp/protocol/` and the request
lifecycle in `docs/request_lifecycle.md`.

---

## Tasks

### Layering

Task wire models live in `cxxmcp/protocol/task.hpp`. They are part of the
`cxxmcp::protocol` target and can be used without linking the server runtime.

Server-side task execution is implemented by
`mcp::server::TaskOperationProcessor` in the `cxxmcp::server` target. The
processor is a convenience execution component above the protocol model. It is
not a gateway scheduler and does not depend on runtime profiles, trust policy,
discovery, import/export, or CLI state.

### Negotiation

Task support is negotiated through the `tasks` capability object:

- `tasks.list` gates `tasks/list`.
- `tasks.cancel` gates `tasks/cancel`.
- `tasks.requests.tools.call` gates task-aware `tools/call`.
- `tasks.requests.sampling.createMessage` gates task-aware
  `sampling/createMessage` where that request path is used.
- `tasks.requests.elicitation.create` gates task-aware `elicitation/create`
  where that request path is used.

Typed client helpers check negotiated server capabilities after `initialize`.
Server-side `ClientPeer` helpers check the connected client's negotiated
capabilities. If a typed helper depends on a missing task capability, it fails
locally with a protocol error instead of sending the unsupported method.

Raw JSON-RPC remains available for conformance probes, vendor extensions, and
future protocol behavior, but raw calls bypass typed helper gating.

### Server Processor Lifecycle

`TaskOperationProcessor::submit_operation()` creates a task in `working` state,
assigns a generated `task-N` identifier, records `createdAt` and
`lastUpdatedAt`, and queues the operation on a bounded executor.

When the operation returns successfully, the task becomes `completed` and the
returned JSON payload is retained for `tasks/result`.

When the operation returns an error or throws, the task becomes `failed`; the
controlled error is retained and returned by `tasks/result`.

`submit_tool_call()` is a convenience wrapper that runs a registered tool call
as a background task and stores the serialized `ToolResult` JSON as the task
result payload.

### Notifications

`TaskOperationProcessorOptions::task_status_hook` is called after task snapshots
move through lifecycle states. Applications that expose an MCP server should
connect this hook to `Server::notify_task_status()` when they want automatic
`notifications/tasks/status` delivery instead of mirroring every state change
manually.

`TaskOperationProcessorOptions::task_progress_hook` is called when a task state
change can be mapped to `notifications/progress`. `submit_tool_call()` reads
`_meta.progressToken` from the original `tools/call` request and stores it on
the task record. When a progress token exists and
`emit_progress_for_task_state_changes` remains enabled, the processor emits
state-derived progress notifications with `total = 1.0`, `progress = 0.0` for
non-terminal states, and `progress = 1.0` for terminal states. The progress
message is the task `statusMessage` when present, otherwise the task status
string.

Hooks are invoked after the processor releases its internal mutex. Hook
implementations may call server notification APIs, record telemetry, or enqueue
application work without risking task-processor lock reentrancy.

### Timeout

The processor uses `TaskOperationProcessorOptions::default_timeout` when the
request omits `task.ttl`. If the request supplies a non-negative `task.ttl`,
that value becomes the operation timeout and is also reflected in the task
snapshot.

Timeout is observed when task state is refreshed by task-management methods.
When a working task exceeds its timeout, the processor:

- cancels the task cancellation token,
- marks the task `failed`,
- sets `statusMessage` to `Operation timed out`,
- clears any stored result,
- stores a controlled timeout failure for `tasks/result`.

Negative `task.ttl` values are rejected before a task is created.

### Cancellation

`tasks/cancel` marks a non-terminal task `cancelled`, updates
`lastUpdatedAt`, stores a controlled cancellation failure, clears any stored
result, and signals the task cancellation token.

Cancellation is cooperative. Handlers receive a `CancellationToken` and should
check it during long-running work. The SDK does not forcibly kill user code or
interrupt arbitrary blocking operations. This avoids unsafe thread termination
and keeps behavior portable across the supported C++17 compiler/platform
matrix. Non-cooperative handlers may continue running, but late completion is
ignored if the task has already left `working`.

Calling `tasks/cancel` for an already terminal task returns the retained
terminal snapshot without changing it.

### Retention

Terminal task records are retained in memory by the processor. Retention is
bounded by:

- `completed_task_ttl`, which removes terminal records after the configured
  age, when set;
- `max_completed_tasks`, which trims the oldest terminal records when the
  retained terminal count exceeds the configured limit.

Active `working` tasks are not removed by terminal retention trimming.

The processor is in-memory only. It does not persist tasks across process
restart and does not define a distributed task store.

### Result Payload

`tasks/result` returns the retained raw JSON payload for a completed task. For
task-aware tool calls, that payload is the serialized `ToolResult` JSON.

Richer typed operation-result wrappers may be added later if the pinned MCP or
RMCP reference shape requires them. Until then, protocol task models remain
typed, while operation-specific result payloads stay raw JSON.

---

## Elicitation

### Layering

Elicitation wire models live in `cxxmcp/protocol/elicitation.hpp` and belong to
the `cxxmcp::protocol` target. Client/server helper APIs live above those
models and send normal `elicitation/create` requests or
`notifications/elicitation/complete` notifications.

Runtime, gateway, policy, approval UI, profile, and CLI behavior is outside the
SDK elicitation contract.

### Modes

The SDK models the two current elicitation modes explicitly:

- Form mode sends a user-facing `message` and a constrained
  `requestedSchema`. The schema builder supports string, number, integer,
  boolean, email, and string enum fields.
- URL mode sends a `message`, `elicitationId`, and `url`. Completion is
  correlated later with `notifications/elicitation/complete`.

Unknown JSON members and `_meta` are preserved for forward-compatible
round-trips.

### Capability Gating

Server-side `ClientPeer::create_elicitation()` checks the connected client's
negotiated capabilities before sending:

- form requests require `elicitation.form`;
- URL requests require `elicitation.url`;
- URL requests rejected by capability gating use the protocol-level
  URL-elicitation-required error code.

For common SDK authoring, `ClientPeer::elicit<T>()` and
`ClientPeer::elicit_async<T>()` build form requests from `SchemaTraits<T>`.
Primitive schemas are wrapped as a required `value` field; object schemas with
`properties` are sent directly and decoded from the accepted content object.
The explicit-schema overload lets applications supply an
`ElicitationSchema` while still receiving typed content. `decline` and `cancel`
results are mapped to stable `elicitation` errors. `ClientPeer::elicit_url()`
and `elicit_url_async()` provide the same capability gating, timeout, and
cancellation behavior for URL-mode requests.

Raw JSON-RPC remains available for conformance probes, vendor extensions, and
future protocol behavior, but raw calls bypass typed helper gating.

### Client Request Handling

Clients may install an `on_create_elicitation_request` handler. If no handler is
installed, the SDK validates the incoming request parameters and returns a typed
`decline` result. Invalid request parameters still return a JSON-RPC error
instead of being silently declined.

Handlers can return `accept`, `decline`, or `cancel`. Accepted form results may
include a content object. URL-mode requests normally return immediately and use
the completion notification to signal that the external flow has finished.

### Schema Validation

The protocol layer validates the shape of elicitation schemas and primitive
property schemas when parsing.

Applications can call `validate_elicitation_content(schema, content)` to check
returned form content against the SDK's constrained schema model. The validator
checks required fields, primitive JSON types, numeric bounds, and enum values.
`validate_elicitation_result_content(schema, result)` applies the same rules to
accepted elicitation results and treats `decline` / `cancel` results as not
requiring content.

This is intentionally narrower than a complete JSON Schema implementation.
Unknown content members are allowed because the SDK schema model does not expose
`additionalProperties`.

### Stability

The typed models, serializers, parsers, client request handler hook,
server-side `create_elicitation()` / `elicit<T>()` / `elicit_url()` helpers,
and completion notification are stable SDK API. Content validation and richer
application-level approval UX are optional follow-up work.
