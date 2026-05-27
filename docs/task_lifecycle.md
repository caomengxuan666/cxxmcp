# Task Lifecycle

This document describes the task behavior that is part of the SDK contract
today. It is intentionally scoped to the SDK and does not define runtime,
gateway, policy, profile, or CLI behavior.

## Layering

Task wire models live in `cxxmcp/protocol/task.hpp`. They are part of the
`cxxmcp::protocol` target and can be used without linking the server runtime.

Server-side task execution is implemented by
`mcp::server::TaskOperationProcessor` in the `cxxmcp::server` target. The
processor is a convenience execution component above the protocol model. It is
not a gateway scheduler and does not depend on runtime profiles, trust policy,
discovery, import/export, or CLI state.

## Negotiation

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

## Server Processor Lifecycle

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

## Timeout

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

## Cancellation

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

## Retention

Terminal task records are retained in memory by the processor. Retention is
bounded by:

- `completed_task_ttl`, which removes terminal records after the configured
  age, when set;
- `max_completed_tasks`, which trims the oldest terminal records when the
  retained terminal count exceeds the configured limit.

Active `working` tasks are not removed by terminal retention trimming.

The processor is in-memory only. It does not persist tasks across process
restart and does not define a distributed task store.

## Result Payload

`tasks/result` returns the retained raw JSON payload for a completed task. For
task-aware tool calls, that payload is the serialized `ToolResult` JSON.

Richer typed operation-result wrappers may be added later if the pinned MCP or
RMCP reference shape requires them. Until then, protocol task models remain
typed, while operation-specific result payloads stay raw JSON.
