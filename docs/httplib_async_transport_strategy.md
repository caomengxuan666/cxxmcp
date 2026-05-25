# Async-Capable Transport Strategy with cpp-httplib

This document describes how to provide an async-capable MCP SDK transport while still using `cpp-httplib` as the HTTP implementation.

## Position

`cpp-httplib` is a good lightweight HTTP stack for this project. It is simple, portable, and already fits the current codebase.

However, it is not a true non-blocking async I/O framework. Its HTTP client calls are blocking, and server concurrency is handled through threads and task queues.

That does not make it unsuitable. It means the SDK should not expose `cpp-httplib` directly as its async model. Instead, the SDK should expose an async-capable transport interface and implement the `cpp-httplib` adapter with bounded worker pools.

The default recommendation is to keep `cpp-httplib`. Do not migrate to Boost.Asio and Boost.Beast unless profiling or load testing proves that the blocking model is a real bottleneck.

## Goal

Keep `cpp-httplib` while making the public SDK API compatible with async usage.

The public SDK should look like this:

```cpp
class Transport {
public:
    virtual Task<core::Result<protocol::JsonRpcResponse>>
    send_request(protocol::JsonRpcRequest request) = 0;

    virtual Task<core::Result<core::Unit>>
    send_notification(protocol::JsonRpcNotification notification) = 0;

    virtual void stop() noexcept = 0;
};
```

The `cpp-httplib` implementation may still use blocking calls internally.

This allows the project to offer an RMCP-like SDK surface without paying the complexity cost of a full async HTTP stack before it is actually needed.

## Key Design Rule

Do not leak `httplib::Client`, `httplib::Server`, or any httplib-specific type into the SDK public peer API.

The SDK should depend on abstract transport contracts. `cpp-httplib` should be only one transport adapter.

## Server-Side Strategy

`cpp-httplib` supports server-side request concurrency through its task queue mechanism. Use that extension point instead of relying on the default queue.

Example:

```cpp
httplib::Server server;

server.new_task_queue = [] {
    return new httplib::ThreadPool(
        8,      // base worker count
        64,     // max worker count
        1024    // max queued requests
    );
};
```

Recommended server behavior:

- configure a bounded task queue
- enforce request body limits
- set read, write, and idle timeouts
- keep JSON-RPC request handling short
- offload long-running tool work to the MCP runtime executor
- do not let long-lived SSE clients starve normal JSON-RPC requests

## Client-Side Strategy

The `cpp-httplib` client is blocking. Wrap it in an executor.

Example shape:

```cpp
class HttplibAsyncClientTransport final : public Transport {
public:
    Task<core::Result<protocol::JsonRpcResponse>>
    send_request(protocol::JsonRpcRequest request) override {
        return executor_.submit([this, request = std::move(request)]() mutable {
            return send_request_blocking(std::move(request));
        });
    }

    Task<core::Result<core::Unit>>
    send_notification(protocol::JsonRpcNotification notification) override {
        return executor_.submit([this, notification = std::move(notification)]() mutable {
            return send_notification_blocking(std::move(notification));
        });
    }

private:
    core::Result<protocol::JsonRpcResponse>
    send_request_blocking(protocol::JsonRpcRequest request);

    core::Result<core::Unit>
    send_notification_blocking(protocol::JsonRpcNotification notification);

    BoundedExecutor executor_;
};
```

Recommended client behavior:

- use a bounded executor, not unbounded `std::async`
- set connection, read, and write timeouts
- keep per-session state synchronized
- limit in-flight requests per upstream server
- translate transport failures into JSON-RPC-compatible errors
- make shutdown cancel queued work and prevent new work

## Executor Requirements

The executor is the important part of this design.

It should provide:

- bounded worker count
- bounded queue length
- backpressure when full
- task cancellation before execution
- graceful shutdown
- forced shutdown for process exit
- observable metrics for queue depth and active workers

The initial implementation can be small. It does not need to be a general-purpose framework.

## SSE and Streamable HTTP

SSE and streamable HTTP need extra care with `cpp-httplib`.

Long-lived connections can occupy worker threads for a long time. That is acceptable only if the gateway has explicit limits.

Recommended behavior:

- use a separate pool or queue for stream/SSE endpoints
- limit the number of concurrent streams per client and per gateway profile
- keep a bounded notification queue per session
- drop or reject streams when the gateway is overloaded
- expose overload state through readiness and diagnostics

## Gateway Runtime Strategy

The gateway should not rely on the HTTP server thread pool for all work.

Use separate execution domains:

- ingress HTTP request handling
- outbound upstream MCP calls
- long-running tool execution
- SSE or stream delivery
- persistence and registry maintenance

This prevents a slow upstream server or long-lived stream from blocking unrelated gateway requests.

Recommended pool split:

```text
http-ingress-pool
  accepts and parses HTTP/JSON-RPC

upstream-call-pool
  runs blocking httplib client calls

tool-runtime-pool
  runs long-running local tools

stream-pool
  owns SSE or streamable HTTP delivery
```

The exact number of pools can be reduced for the first implementation, but the design should not assume one global pool forever.

## When httplib Is Enough

For typical MCP usage, `cpp-httplib` should be enough.

MCP traffic is usually not a raw high-QPS workload. Common usage patterns are:

- CLI calls
- local stdio servers
- IDE or agent tool calls
- small to medium gateway deployments
- long-running tools with moderate request volume

These workloads usually benefit more from timeouts, bounded queues, and clear backpressure than from a full non-blocking HTTP stack.

The practical default should be:

- keep `cpp-httplib`
- bound all queues
- limit in-flight requests per upstream
- set explicit timeouts
- isolate long-lived streams from normal JSON-RPC requests
- expose readiness and saturation diagnostics

## When to Consider Boost.Asio and Boost.Beast

Boost.Asio and Boost.Beast should be treated as a measured upgrade path, not the default design.

Consider a native async transport only when one or more of these conditions is observed:

- many long-lived SSE or streamable HTTP connections
- frequent HTTP worker pool saturation
- frequent executor queue saturation
- high memory usage caused by many blocked threads
- need for true socket-level cancellation
- need for thousands of concurrent connections
- load tests show unacceptable latency under the expected gateway workload

Until those conditions appear, replacing `cpp-httplib` with Boost.Asio and Boost.Beast is likely to increase implementation and maintenance cost without a clear product benefit.

## Public API Recommendation

Expose both sync and async APIs.

The sync API is useful for CLI and simple embedding:

```cpp
core::Result<protocol::ToolResult>
call_tool(std::string_view name, protocol::Json arguments);
```

The async API should be the primary SDK/runtime path:

```cpp
Task<core::Result<protocol::ToolResult>>
call_tool_async(std::string name, protocol::Json arguments);
```

The sync API can be implemented as a blocking wait on the async API.

## Migration Plan

### Phase 1: Add Executor

- add `BoundedExecutor`
- add tests for queue limits, shutdown, and backpressure
- do not change public client/server APIs yet

### Phase 2: Add Async Transport Interface

- introduce async-capable `Transport`
- implement `HttplibAsyncClientTransport`
- keep existing blocking transport as an internal helper

### Phase 3: Add Peer Async Methods

- add async peer methods for core MCP operations
- keep existing sync methods as wrappers
- avoid exposing httplib types

### Phase 4: Split Gateway Pools

- separate inbound HTTP handling from outbound upstream calls
- add stream/SSE limits
- add readiness diagnostics for executor saturation

### Phase 5: Optional Native Async Transport

If the gateway outgrows the `cpp-httplib` adapter according to measured load tests, add a second transport implementation based on a true async stack such as Boost.Asio and Boost.Beast.

The SDK peer API should not need to change.

## Non-Goals

This strategy does not make `cpp-httplib` itself non-blocking.

It also does not require replacing the current HTTP stack immediately.

The goal is to avoid locking the SDK design to blocking calls while keeping the current implementation practical.

## Recommendation

Keep `cpp-httplib` for now.

Add an async-capable transport abstraction above it.

Implement the `cpp-httplib` transport with bounded executors, explicit timeouts, and backpressure.

This gives the project an RMCP-like SDK surface without forcing an immediate migration to a heavier HTTP stack.

Boost.Asio and Boost.Beast should remain a future transport option, not a required dependency for the first RMCP-like SDK design.

## References

- https://github.com/yhirose/cpp-httplib
- https://yhirose.github.io/cpp-httplib/en/
- https://github.com/yhirose/cpp-httplib/issues/875
