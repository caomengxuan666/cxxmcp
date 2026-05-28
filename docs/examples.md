# Examples

The in-tree examples are intentionally compact SDK flows. They are not a
sample application suite and should not introduce package or external tooling
requirements into the default SDK path.

## First-Choice SDK Examples

- `cxx17_consumer.cpp`: minimal C++17 installed-SDK-shaped consumer path.
- `auth_bearer_http.cpp`: HTTP bearer-token auth wiring for
  `ServerPeer` / `ClientPeer`.
- `server_stdio_peer.cpp`: copyable server-side `Peer` / `Service` over stdio.
- `server_peer.cpp`: server-side `Peer` / `Service` loopback coverage.
- `client_peer.cpp`: client-side `Peer` / `Service` requests.
- `process_stdio_client.cpp`: launching and talking to a local MCP server.
- `timeout_cancellation.cpp`: request timeout and cooperative cancellation.
- `elicitation_client.cpp`: client-side elicitation handling.
- `stdio_server.cpp`: compact stdio server using `ServerPeer::builder()` with
  typed tool, prompt, resource, completion, sampling, and logging registration.
- `typed_stdio_server.cpp`: typed tool registration with reflected structs via
  `ServerPeer::builder()` and `mcp::server::tool<>()`.

## Focused Capability Examples

- `handler_contracts.cpp`: durable handler interfaces.
- `auth_dpop_openssl.cpp`: opt-in OpenSSL DPoP/JWKS auth-provider wiring.
- `task_async_client_server.cpp`: task-aware tool lifecycle.
- `streamable_http_client.cpp`: Streamable HTTP client construction.

## Compatibility Or Low-Level Examples

- `client_loopback.cpp`: local loopback coverage using `ServerPeer::builder()`
  with the deprecated `server()` accessor for client transport plumbing.

## Runtime Tooling Example

Runtime/gateway examples have moved to the external gateway repository. This SDK
repository does not ship `gateway_runtime.cpp` or present gateway tooling as a
canonical SDK example path.

`ctest --preset examples` runs self-contained smoke examples. Long-running
server samples and externally hosted HTTP samples are build-checked but are not
registered as standalone CTest cases.

`cxx17_consumer.cpp` is compiled as `cxx_std_17` in-tree. The richer examples
may use newer C++ syntax for readability, but they are not allowed to raise the
public SDK or installed-package baseline.

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
  task cancellation. Application-level signal handling should follow
  [Graceful Shutdown](graceful_shutdown.md#recommended-pattern) so examples and downstream services
  call `stop()` / `close()` from normal control flow instead of from a signal
  handler.
- Authoring ergonomics coverage for typed tools, handler interfaces,
  server-to-client context calls, rich content blocks, custom role-generic
  transports, and transport adapters. See
  [Transport Adapter Ergonomics](transport_adapters.md) for the stable helper
  layer used by custom source/sink and queue integrations.
