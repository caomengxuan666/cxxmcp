# HTTP Transport Backend Evidence

This note records the current evidence for keeping `cpp-httplib` as the
Streamable HTTP implementation backend. It is not a benchmark report. It is the
minimum release-gate evidence required before considering a second HTTP stack.

## Current Decision

Keep `cpp-httplib` hidden behind `cxxmcp::transport` and the public HTTP
transport option types. Do not add another HTTP backend unless release-blocking
load, lifecycle, or interoperability tests show a concrete failure that cannot
be fixed behind the existing transport boundary.

The SDK contract remains backend-neutral:

- public headers expose `cxxmcp` transport types, not `httplib` types;
- auth metadata fetches use `OAuthMetadataEndpoint`, not an HTTP client type;
- package consumers do not link or include `cpp-httplib` directly;
- a future backend replacement must preserve the same Streamable HTTP behavior
  and package targets.

## Evidence Gates

The release-blocking `http_transport` CTest entry currently covers the backend
behaviors that would justify replacing or retaining the implementation:

- concurrent Streamable HTTP sessions;
- many in-flight client requests;
- high-volume server-to-client notifications;
- shutdown while an SSE stream is active;
- outbound SSE event-count backpressure;
- outbound SSE byte-queue bounds;
- malformed POST handling;
- stale session, resume, DELETE, and reconnect behavior;
- timeout and cancellation propagation over SSE.

Focused local verification used for this decision:

```powershell
cmake --build build-auth-on-ninja --target mcp_http_transport_tests --parallel
ctest --test-dir build-auth-on-ninja -R "^http_transport$" --output-on-failure --timeout 180
```

The latest local run passed. That proves the current backend satisfies the
short deterministic release-gate load/lifecycle requirements. It does not prove
high-throughput production capacity, so future larger benchmarks should be
added before making performance claims.

## Replacement Trigger

Considering another HTTP backend requires at least one of these:

- a release-blocking load or lifecycle test fails because of a backend limit;
- a required MCP Streamable HTTP behavior cannot be implemented safely on the
  current backend;
- sanitizer or thread-sanitizer gates expose backend-caused memory or
  concurrency defects that cannot be isolated;
- downstream users provide reproducible workload evidence that exceeds the
  current backend's practical envelope.

Without that evidence, adding a second HTTP stack would increase API, package,
and CI surface without improving the SDK contract.
