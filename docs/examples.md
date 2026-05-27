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

## Downstream Example Repository

`../cxxmcp-examples` is the external examples repository used as downstream
evidence for release and vcpkg maturity. It must stay outside this source tree
and should consume cxxmcp like an application: through public headers, exported
CMake targets, real executables, and CTest probes. Its adjacent-source mode is
useful for SDK development, but release evidence is strongest when the same
suite also builds against an installed package with
`CXXMCP_EXAMPLES_USE_ADJACENT_SDK=OFF` and `find_package(cxxmcp CONFIG
REQUIRED)`.

For release/vcpkg evidence, keep at least these downstream scenarios green:

- SDK loopback smoke for tools, prompts, resources, completion, sampling,
  logging, raw requests, notifications, and task-backed tool calls.
- Minimal stdio server plus process-stdio client probe, including child-process
  launch through public transport APIs.
- Streamable HTTP client/server coverage, including direct HTTP, legacy SSE
  compatibility, and HTTP auth-lite bearer-token propagation.
- Request lifecycle coverage for async requests, timeouts, cancellation,
  cancellation-aware inbound callbacks, pagination helpers, subscriptions, and
  task cancellation.
- Authoring ergonomics coverage for typed tools, handler interfaces,
  server-to-client context calls, rich content blocks, custom role-generic
  transports, and transport adapters.
- Optional runtime/gateway examples when the consumed package exposes those
  targets; installed SDK-only package checks may skip them without weakening
  the core SDK evidence.
