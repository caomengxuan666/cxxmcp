# Examples

The in-tree examples are intentionally compact SDK flows. They are not a
sample application suite and should not introduce package, runtime, or gateway
requirements into the default SDK path.

## First-Choice SDK Examples

- `server_peer.cpp`: server-side `Peer` / `Service` authoring.
- `client_peer.cpp`: client-side `Peer` / `Service` requests.
- `process_stdio_client.cpp`: launching and talking to a local MCP server.
- `timeout_cancellation.cpp`: request timeout and cooperative cancellation.
- `elicitation_client.cpp`: client-side elicitation handling.

## Focused Capability Examples

- `handler_contracts.cpp`: durable handler interfaces.
- `task_async_client_server.cpp`: task-aware tool lifecycle.
- `typed_stdio_server.cpp`: typed tool registration.
- `streamable_http_client.cpp`: Streamable HTTP client construction.

## Compatibility Or Low-Level Examples

- `stdio_server.cpp`: minimal stdio server process used by other examples.
- `client_loopback.cpp`: local loopback compatibility coverage.

## Runtime Tooling Example

- `gateway_runtime.cpp`: optional runtime/gateway composition. This is useful
  for tooling development but is not the canonical SDK entry path.

`ctest --preset examples` runs self-contained smoke examples. Long-running
server samples and externally hosted HTTP samples are build-checked but are not
registered as standalone CTest cases.
