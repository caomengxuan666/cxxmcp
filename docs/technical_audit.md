# Technical Audit: cxxmcp C++ MCP SDK

**Original Audit Date:** 2025-05
**Last Updated:** 2026-05-28 (re-verified; release-evidence and auth/OpenSSL
boundary guidance added)
**Scope:** Deep technical review of the SDK source code for production readiness and community standard candidacy.

---

## Fix Status Summary

**Last verified:** 2026-05-28

| Status | Count | Notes |
|--------|-------|-------|
| **FIXED** | 55 | SIGPIPE, data races (3), namespace UB, HTTP hardening, session transport lifetime guard, session limit, move semantics, JSON depth limits, HTTP PImpl ownership, executor exception hook, JSON-RPC version validation, unsupported MCP protocol-version rejection, strict public protocol-version negotiation helper, initialized enforcement (transport), client/server handler synchronization, native out-of-order response correlation, stable batch rejection policy, handler dispatch missing return, unknown content type rejection, apply_to handler overwrite, explicit sampling temperature zero, image/audio base64 validation, protocol finite-number validation, client/server request-cancellation key allocation, RequestHandle timeout-only wait strategy, session transport duplicate lookup, registry list caching, auth constant-time state/key comparison, auth endpoint HTTPS enforcement, auth metadata discovery SSRF guard, auth authorization-state TTL, auth PKCE S256-only public enum, DPoP SecureString key material, DPoP replay/claim validation hardening, handler dispatch ambiguity detection, registry name/URI validation, owned ServerHandlerInterface registration, registry synchronization, server authoring header split, direct JSON-RPC envelope serialization, POSIX process argv mutable buffers, graceful shutdown guidance, rate-limiter params size accounting, stale tooling submodule cleanup, removed plugin/adapters extension surface, server auth HTTP request-target propagation, DPoP-aware server AuthProvider bridge |
| **NOT APPLICABLE** | 3 | ERROR macro conflict (logger deleted), FATAL logger abort semantics (runtime logger deleted), stale `tl::expected` concern (vendored fallback status is rechecked through release dependency review) |
| **ACCEPTED LIMITATION** | 1 | stdio `std::getline` stop-unblock limitation is documented; process-stdio and Streamable HTTP are the recommended production interop paths pending exact release evidence |
| **TRACKED DEBT** | 2 | `extern template` feasibility, public `nlohmann::json` include cost |
| **STILL PRESENT CODE DEFECTS** | 0 | — |

The project has addressed all 3 CRITICAL issues, the known HIGH-severity thread
safety bugs, the session transport lifetime guard, the JSON depth/size security
concern, the minimum DPoP replay/claim validation contract, and the identified
JSON-RPC serialization double-allocation path. Remaining non-defect roadmap
work is tracked in `todo.md`: package maturity, public release evidence,
opt-in auth package proof, and performance measurement debt.

---

## Executive Summary

The audit found broad protocol coverage, a layered architecture aligned with
the official Rust SDK (RMCP), and focused regression evidence for the main
protocol, transport, peer/service, and handler boundaries. cxxmcp is a strong
fact-standard candidate, but fact-standard claims still depend on exact
release-gates evidence, package-manager maturity, and independent ecosystem
validation.

The original deep code audit found **systemic thread safety issues** concentrated in the transport layer, along with several robustness gaps. Those code defects have been closed or explicitly reclassified. The remaining work is now mostly release evidence, compile-time measurement, packaging maturity, and narrowly scoped hot-path debt.

| Category | Status |
|----------|--------|
| Release-blocking code defects from this audit | 0 open |
| Accepted transport limitations | 1 documented |
| Compile-time/performance measurement debt | tracked outside this audit |
| OAuth/DPoP/JWKS implementation | OpenSSL-backed helpers exist; package proof and application-owned integration remain opt-in |

---

## Audit Evidence Map

This map is the release-evidence bridge for the findings below. A `FIXED`
entry means the source-level defect has a named evidence path; it does not by
itself prove package maturity, ecosystem adoption, or fact-standard status.
`ACCEPTED LIMITATION`, `TRANSPORT-SPECIFIC`, and `TRACKED DEBT` entries must
stay visible in release notes, `todo.md`, or `docs/performance_debt.md` until
their policy changes.

| ID | Status | Primary Evidence |
|----|--------|------------------|
| C1 | FIXED | `stdio_transport`, process-stdio interop, release transport matrix |
| C2 | FIXED | `stdio_transport`, ThreadSanitizer/source-style policy |
| C3 | FIXED | `stdio_transport`, notification/response interleaving regressions |
| H1 | ACCEPTED LIMITATION | `docs/request_lifecycle.md#recommended-signal-handling-pattern`, release notes limitation disclosure |
| H2 | FIXED | `sdk`, public authoring compile tests, handler ambiguity diagnostics |
| H3 | FIXED_WITH_TRACKED_DEBT | public-header compile evidence, `docs/performance_debt.md`, `todo.md` compile-time gate |
| H4 | FIXED | `http_transport`, public-header backend isolation checks |
| H5 | FIXED | `stdio_transport`, session capability synchronization coverage |
| M1 | FIXED | `sdk_boundary`, public-header compile tests |
| M2 | FIXED | `http_transport`, malformed/oversized HTTP request coverage |
| M3 | FIXED | `http_transport`, timeout and stale-session coverage |
| M4 | FIXED | `client_server`, guarded `SessionContext::client()` coverage |
| M5 | FIXED | `client_server`, concurrent handler registration coverage |
| M6 | FIXED_POLICY | `protocol`, documented single-message batch rejection policy |
| L1 | FIXED | `sdk`, executor exception-hook coverage |
| L2 | NOT_APPLICABLE | dependency review checklist; time-sensitive upstream status is not a permanent release claim |
| L3 | FIXED | source archive verifier excludes runtime/CLI tooling sources and stale submodules |
| L4 | FIXED | `sdk_boundary`, private protocol serialization dependency checks |
| L5 | FIXED | `protocol`, JSON-RPC version negative tests |
| L6 | FIXED | `process_stdio_transport`, POSIX argv buffer coverage |
| H6 | FIXED | client/peer thread-safety documentation and request-lifecycle tests |
| H7 | FIXED | client/server out-of-order response and unexpected-id transport tests |
| H8 | TRANSPORT_SPECIFIC | reconnect remains transport/application-owned in `docs/request_lifecycle.md` |
| H9 | FIXED | RequestHandle timeout and cancellation tests |
| H10 | FIXED | unsupported MCP protocol-version tests and protocol negotiation helper |
| H11 | FIXED | initialized-boundary transport tests and `docs/request_lifecycle.md` |
| M7 | FIXED | protocol content-block negative tests |
| M8 | FIXED | sampling temperature serialization tests |
| M9 | FIXED | image/audio base64 validation tests |
| M10 | FIXED | finite-number validation tests |
| M11 | FIXED | JSON depth and size limit tests |
| M12 | FIXED_WITH_TRACKED_DEBT | `protocol_serialization_benchmark`, performance evidence artifact, `docs/performance_debt.md` |
| M13 | FIXED | request cancellation key regression coverage |
| M14 | FIXED | HTTP session limit coverage |
| M15 | TRACKED_DEBT | `docs/performance_debt.md`, public-header compile evidence review |
| M16 | TRACKED_DEBT | `docs/performance_debt.md`, public-header compile evidence review |
| L7 | FIXED | rate-limiter request-size tests |
| L8 | FIXED | registry list caching tests |
| L9 | FIXED | session transport duplicate lookup tests |
| L10 | FIXED | transport adapter move-semantics tests |
| Auth-F1 | FIXED | auth constant-time comparison tests |
| Auth-F2 | FIXED | auth endpoint and redirect URI validation tests |
| Auth-F3 | FIXED | PKCE S256-only public enum and lifecycle tests |
| Auth-F4 | FIXED | `SecureString` DPoP key material tests |
| Auth-F5 | FIXED | DPoP replay/claim validation and OpenSSL verifier tests |
| Auth-F6 | FIXED | auth metadata discovery SSRF guard tests |
| Auth-F7 | FIXED | authorization-state TTL tests |
| EXT-H1 | FIXED | handler dispatch return regression coverage |
| EXT-H2 | FIXED_POLICY | `docs/request_lifecycle.md#recommended-signal-handling-pattern` and release evidence docs |
| EXT-H3 | FIXED | synchronized registry reentrancy tests |
| EXT-M1 | FIXED | owned `ServerHandlerInterface` lifetime tests |
| EXT-M2 | FIXED | `ServerHandler::apply_to()` non-empty overwrite tests |
| EXT-M3 | NOT_APPLICABLE | runtime logger deleted; source archive verifier excludes runtime logger |
| EXT-M4 | FIXED | removed plugin/adapters extension surface and package boundary checks |
| EXT-L1 | FIXED | registry name/URI validation tests |
| EXT-L2 | NOT_APPLICABLE | runtime logger deleted; source archive verifier excludes runtime logger |

---

## CRITICAL Issues

### C1. Server Stdio Transport Has No SIGPIPE Protection — **FIXED**

**File:** `sdk/server/src/stdio_transport.cpp`

The client-side `ProcessStdioTransport` has a well-implemented `ScopedSigpipeBlock` (in `sdk/client/src/process_stdio_transport.cpp:236-274`) that blocks SIGPIPE around pipe writes. However, the **server-side** StdioTransport has no SIGPIPE handling at all.

When a server is launched as a child process and the parent closes the read end of stdout, any write to `std::cout` will raise SIGPIPE and **crash the process** on Linux/macOS. This is a deterministic production crash bug.

**Fix:** Add SIGPIPE suppression at transport startup (e.g., `signal(SIGPIPE, SIG_IGN)`) or use the same `ScopedSigpipeBlock` pattern around output writes.

**Resolution:** `ScopedSigpipeBlock` now wraps `write_response()` and `write_notification()` in the server stdio transport (lines 24-63, 79, 100), guarded by `#ifndef _WIN32`.

---

### C2. `StdioTransport::running_` Is a Plain `bool` Accessed from Multiple Threads — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/stdio_transport.hpp:79`

```cpp
bool running_ = false;
```

`running_` is read in the `start()` loop (`stdio_transport.cpp:106`) and written in `stop()` (`stdio_transport.cpp:224`) from different threads. This is a **data race** under the C++ memory model — undefined behavior.

Notably, the client-side `ProcessStdioTransport::Impl` correctly uses `std::atomic_bool running_{true}` (`process_stdio_transport.cpp:860`). The server side was missed.

On x86 this will "work" in practice, but on ARM or with aggressive compiler optimizations, the `start()` loop may never observe the `stop()` write, making `stop()` a no-op.

**Fix:** Change to `std::atomic<bool> running_` with appropriate memory ordering.

**Resolution:** Changed to `std::atomic_bool running_{false}` (line 91). All accesses use explicit `std::memory_order_release` / `std::memory_order_acquire`.

---

### C3. Stdio Main Loop Writes Responses Without Holding `output_mutex_` — **FIXED**

**File:** `sdk/server/src/stdio_transport.cpp:114, 147, 190`

`send_notification()` correctly acquires `output_mutex_` before writing to `*output_`. However, the main loop in `start()` writes responses and errors **without** holding the mutex:

- Line 114: `write_error(*output_, ...)` (parse error path)
- Line 147: `write_error(*output_, ...)` (invalid message path)
- Line 190: `write_response(*output_, *response)` (normal response)

If a handler calls `send_notification()` from another thread while the main loop is writing a response, the two `operator<<` sequences interleave on the same `std::ostream`, producing **corrupted output**.

The mutex is one-sided: `send_notification` locks it, but the main loop does not. Mutual exclusion requires both sides to lock.

**Fix:** Wrap all `*output_` writes in the `start()` loop with `output_mutex_`.

**Resolution:** All `write_error` / `write_response` / `write_notification` calls in the `start()` loop are now wrapped in `std::lock_guard lock(output_mutex_)` blocks (lines 179-183, 226-231, 247-250, 288-291, 316).

---

## HIGH Issues

### H1. Stdio Transport Has No Read Timeout; `stop()` Cannot Interrupt Blocking Reads — **ACCEPTED LIMITATION**

**File:** `sdk/server/src/stdio_transport.cpp:106`

```cpp
while (running_ && std::getline(*input_, line)) {
```

`std::getline` blocks indefinitely. If a client sends a partial line (no newline terminator), the server hangs forever. There is no read timeout mechanism.

`stop()` sets `running_ = false` but cannot interrupt a blocking `std::getline`. The flag is only checked when `getline` returns — which it never does without input. This makes `stop()` **a no-op when the transport is waiting for input**.

**Design limitation:** Using `std::iostream` for transport I/O does not support non-blocking reads, timeouts, or cancellation. A proper fix requires platform-specific I/O (`poll`/`select` on POSIX, `WaitForSingleObject` on Windows) or a dedicated I/O thread with a cancellation pipe.

**Resolution:** This is not treated as a silent production guarantee. The SDK
documents stdio shutdown as best-effort for blocking iostream reads, keeps
process-stdio and Streamable HTTP as the production interop transports, and
tracks any future interruptible stdio redesign as a transport policy decision
rather than a hidden correctness bug.

---

### H2. SFINAE Handler Dispatch Is Order-Dependent with No Ambiguity Detection — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/server.hpp:293-317`

`invoke_tool_handler` tries 7 overload patterns in sequence:

1. `Handler(Args, const ToolContext&)`
2. `Handler(const ToolContext&, Args)`
3. `Handler(Args, CancellationToken)`
4. `Handler(CancellationToken, Args)`
5. `Handler(Args)`
6. `Handler(const ToolContext&)`
7. `Handler()`

If a handler is callable with multiple signatures (e.g., a lambda with default arguments, or a generic `auto` parameter), the **first matching branch wins** with no ambiguity detection. The compiler silently picks the wrong overload.

**Example:** A handler accepting `(int, auto)` could match pattern 1 or 3 depending on the second argument type. No diagnostic is emitted.

**Resolution:** `require_unambiguous_*_handler()` functions now perform
registration-time `static_assert` checks on `*_handler_match_count_v<Handler> <=
1`. They are called from tool, prompt, resource, completion, sampling, and JSON
extension registration paths. Generic/default-argument handlers that match more
than one supported callable shape now produce a compile error with a clear
message directing the user to one explicit callable shape. Prompt/resource
checks prefer exact concrete signatures before falling back to invocability so
ordinary `Json` handlers are not misdiagnosed through implicit conversions.

---

### H3. `server.hpp` Is Still a Large Public Authoring Header — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/server.hpp`

This header contains deeply nested template dispatch logic (`invoke_tool_handler`, `invoke_prompt_handler`, `invoke_resource_handler`, `invoke_completion_handler`) with 20+ `if constexpr` branches each. Every translation unit that includes `cxxmcp/server.hpp` must instantiate all of this.

**Impact:** Significant compilation time increase for consumers. The template dispatch should be in a separate `detail/` header, or handler registration should use type-erased wrappers to avoid per-call-site re-instantiation.

**Resolution:** Handler-shape detection, ambiguity diagnostics, result
adapters, and invoke dispatch templates have been moved to
`cxxmcp/server/detail/handler_dispatch.hpp`. Server invocation contexts now
live in `cxxmcp/server/context.hpp`, public callback aliases live in
`cxxmcp/server/handler_types.hpp`, and typed tool/prompt/resource builders plus
the `App` convenience builder now live in `cxxmcp/server/authoring.hpp`.
`server.hpp` is reduced to the stable `Server` / `ServerBuilder` declaration
surface, no longer includes `detail/handler_dispatch.hpp` or `handler.hpp`, and
is covered by local/in-tree public-header, SDK/client-server, and C++17 SDK
build evidence. Exact release-gates artifact review remains tracked in
`todo.md`.

---

### H4. `HttpServerDeleter` Uses Fragile `void*` Type Erasure — **FIXED**

**File:** `sdk/server/src/http_transport.cpp:361-386`

```cpp
server_.reset(new httplib::Server());
// ...
void HttpServerDeleter::operator()(void* server) const noexcept {
    delete static_cast<httplib::Server*>(server);
}
```

The `unique_ptr<void, HttpServerDeleter>` pattern works but is fragile: if the allocated type changes without updating the deleter, the `static_cast` silently produces UB. The compiler cannot verify type correctness.

**Fix:** Use a forward-declared deleter struct or `std::unique_ptr<httplib::Server>` with the httplib header included only in the `.cpp`.

**Resolution:** Replaced `unique_ptr<void, HttpServerDeleter>` with a private
`HttpServerHolder` PImpl. The public header keeps `httplib` hidden while the
`.cpp` owns and destroys the concrete `httplib::Server` through a checked type.

---

### H5. `client_capabilities_` Has No Synchronization — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/stdio_transport.hpp:80`

```cpp
std::optional<protocol::ClientCapabilities> client_capabilities_;
```

Written in `start()` loop (lines 183-186) without any lock, read via `client_capabilities()` (lines 219-222) also without any lock. Cross-thread access is a data race.

**Fix:** Protect with a mutex or use `std::atomic` with appropriate store/load ordering.

**Resolution:** Added `mutable std::mutex client_capabilities_mutex_` (line 90). Capabilities written under lock (lines 277-285), read under lock (lines 322-324).

---

## MEDIUM Issues

### M1. `std::unexpected` Injected into `namespace std` — Undefined Behavior — **FIXED**

**File:** `sdk/core/include/cxxmcp/core/result.hpp:15-21`

```cpp
namespace std {
template <class E>
constexpr auto unexpected(E&& value) {
    return tl::unexpected<std::decay_t<E>>(std::forward<E>(value));
}
}
```

When `__cpp_lib_expected` is not defined, the code injects a function template into `namespace std`. This is **undefined behavior** per the C++ standard (only explicit specializations of existing templates are permitted).

If a translation unit includes both this header and `<expected>`, the two `std::unexpected` definitions conflict.

**Fix:** Move to a project-specific namespace (e.g., `mcp::compat::unexpected`) or use ADL-friendly wrappers.

**Resolution:** `namespace std` injection removed. `unexpected()` now lives in `mcp::core` namespace (lines 29-35), returning `std::unexpected` or `tl::unexpected` directly.

---

### M2. HTTP Transport Has No Maximum Request Body Size — **FIXED**

**File:** `sdk/server/src/http_transport.cpp`

The POST handler reads `request.body` without checking its size. httplib's default body limit is 0 (unlimited). A malicious client can send an arbitrarily large JSON body to exhaust server memory.

**Fix:** Call `http_server->set_payload_max_length()` with a reasonable limit (e.g., 10 MB).

**Resolution:** `set_payload_max_length()` called at line 536 with default 10 MB (`max_request_body_bytes = 10 * 1024 * 1024`).

---

### M3. HTTP Transport Has No Read/Write Timeouts — **FIXED**

**File:** `sdk/server/src/http_transport.cpp:386`

The httplib server is created with default timeouts (very large or infinite). Slow clients can hold connections open indefinitely, exhausting server resources.

**Fix:** Call `set_read_timeout()` and `set_write_timeout()` with appropriate values.

**Resolution:** Both called at lines 537-538 with default 30s (`read_timeout{30000}`, `write_timeout{30000}`).

---

### M4. `SessionContext::transport` Is a Raw Borrowed Pointer with No Lifetime Tracking — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/transport.hpp:45`

```cpp
Transport* transport = nullptr;
```

This raw pointer propagates into `ToolContext`, `PromptContext`, `ResourceContext`, and `ClientPeer`. If a handler stores the context or creates a long-lived `ClientPeer`, and the transport is destroyed first, subsequent use is **use-after-free**.

The documentation warns about this, but the API provides no enforcement (no `weak_ptr`, no lifetime token, no guard).

**Resolution:** `SessionContext` now carries a weak transport lifetime token
alongside the compatibility raw pointer. Built-in stdio, Streamable HTTP,
transport adapters, peer-service contexts, and registry-derived
tool/prompt/resource contexts propagate that token. `SessionContext::client()`
and `client_peer(context)` create guarded `ClientPeer` instances that check the
token before every transport access and fail closed with `"client peer is not
available"` after the source transport has been destroyed.

---

### M5. `Server` Handler Members Have No Synchronization — **FIXED**

**File:** `sdk/server/src/server.cpp`

`std::function` members (`completion_handler_`, `raw_request_handler_`, etc.) are written by `set_*_handler()` and read by `handle_request()`. No mutex protects these accesses. If a user registers a handler from one thread while the transport thread is dispatching a request, this is a data race.

**Resolution:** Server handler slots are protected by a mutex. Request and notification dispatch snapshot the relevant callbacks under the lock and invoke the copied handlers outside the lock, so concurrent registration is synchronized and reentrant handler replacement cannot deadlock.

---

### M6. Batch Requests Rejected with Wrong Error Code — **FIXED**

**File:** `sdk/protocol/src/serialization.cpp:312-315`

Batch requests are rejected with `ErrorCode::InvalidRequest`, but per JSON-RPC 2.0, a batch is a valid structure. The error code should be a custom server error, and the spec recommends returning an error response for each request in the batch.

**Resolution:** The SDK intentionally keeps `parse_message()` as a single-message API and rejects JSON-RPC batch arrays with a stable `InvalidRequest` error. Protocol tests cover well-formed batch arrays and invalid JSON-RPC version fields, and `todo.md` records this as the public API policy.

---

## LOW Issues

### L1. `BoundedExecutor` Silently Swallows All Exceptions — **FIXED**

**File:** `sdk/core/include/cxxmcp/core/executor.hpp:98-101`

```cpp
try { task(); } catch (...) {}
```

No logging, no error callback. `TaskOperationProcessor` wraps its own tasks in try-catch, but other callers of `BoundedExecutor` will have exceptions silently lost.

**Resolution:** `BoundedExecutor` now accepts an optional exception reporting
hook that receives the original `std::exception_ptr` while preserving the
default no-throw worker behavior.

---

### ~~L2. `tl::expected` Version 1.3.1 Is Very Old (2019)~~ — **NOT APPLICABLE**

**File:** `third_party/tl/expected.hpp`

The original audit treated `1.3.1` as stale. Re-verification against the
upstream `TartanLlama/expected` tag list on 2026-05-28 showed `v1.3.1` is the
latest published upstream tag. Package-manager builds still prefer the
`tl-expected` package through `CXXMCP_USE_SYSTEM_DEPS=ON`; the vendored header
is only the default source-tree fallback. This is a time-sensitive dependency
statement, so each release candidate must re-check it through
`docs/dependency_policy.md` before citing it in release notes.

---

### L3. Third-Party Submodules Track Commits Past Release Tags — **FIXED**

- spdlog: 25 commits past `v1.17.0`
- CLI11: 31 commits past `v2.6.2`

This is risky for reproducible builds and security patching.

**Resolution:** Runtime/tooling dependencies are outside the SDK package
contract. The deleted spdlog and CLI11 source directories are no longer
declared as git submodules, so SDK consumers and package-manager builds do not
inherit stale tooling submodule pins.

---

### L5. No Validation of JSON-RPC `jsonrpc` Field Version — **FIXED**

**File:** `sdk/protocol/src/serialization.cpp`

The parser does not verify that `"jsonrpc"` is `"2.0"`. Messages with `"jsonrpc": "1.0"` or missing the field are accepted.

**Resolution:** JSON-RPC parsing now rejects missing, non-string, and non-`2.0`
`jsonrpc` fields before constructing request, response, or notification values.

---

### L6. `const_cast` on `std::string::c_str()` for `execvp` — **FIXED**

**File:** `sdk/client/src/process_stdio_transport.cpp:318-320`

Standard POSIX pattern but technically UB if the callee modifies the data. `execvp` does not, so this is safe in practice.

**Resolution:** POSIX process stdio startup now builds a `MutableArgv` with owned
mutable `std::string` storage and a parallel `char*` argv vector. The child
passes that mutable buffer to `execvp()` instead of `const_cast`ing
`std::string::c_str()` pointers from `ProcessStdioTransportOptions`.

---

## Historical Thread Safety Findings

These stdio thread-safety defects are closed. This section keeps the original
audit context; the current status is summarized in the fix-status table above.
The original systemic issue was **thread safety in the StdioTransport**, which
had 3 independent data races:

| Race | Location | Impact |
|------|----------|--------|
| `running_` plain bool | `stdio_transport.hpp:79` | `stop()` invisible on ARM/aggressive opts |
| Output writes unprotected | `stdio_transport.cpp:114,147,190` | Corrupted JSON output |
| `client_capabilities_` unprotected | `stdio_transport.hpp:80` | Torn reads |

By contrast, the **client-side** `ProcessStdioTransport` already used
`std::atomic_bool` for its running flag when the original audit was written.
The server side has since been brought into alignment.

The `TaskManager` locking discipline is **sound** — all shared state is protected by a single mutex, and lock/unlock patterns are consistent.

---

## Original Recommendations (Now Closed)

These were the original priority recommendations from the audit. They are now
closed or separately tracked as explicit debt/limitations above.

1. **Fix SIGPIPE** — Closed with scoped POSIX output-write protection.

2. **`running_` -> `std::atomic<bool>`** — Closed.

3. **Lock `output_mutex_` in main loop** — Closed.

4. **HTTP hardening** — Closed.

5. **Move `std::unexpected` out of `namespace std`** — Closed.

6. **Split `server.hpp`** — Header split closed; compile-time evidence review
   remains tracked in `todo.md`.

7. **Replace `void*` type erasure** — Closed.

8. **Add handler ambiguity detection** — Closed.

---

## Positive Findings

Not everything is problematic. The audit also confirmed several areas of high quality:

- **TaskManager locking discipline** is correct — no races found.
- **Service lifecycle** handles self-deadlock (`stop()` from loop thread) correctly.
- **SIGPIPE handling on client side** is well-implemented with scoped blocking.
- **SSE `&request` capture** is tied to the vendored cpp-httplib synchronous
  handler call chain and must be rechecked during every cpp-httplib update.
- **Destructor cleanup** correctly uses `(void)stop()` pattern — acceptable in noexcept context.
- **No `FIXME`/`HACK`/`XXX` annotations** in SDK source — code is clean of known-issue markers, and `scripts/check_source_markers.py` now gates first-party source paths in release source-style evidence.
- **Protocol coverage** is broad with forward-compatible `extensions` fields on
  DTOs; final capability-parity claims still depend on release evidence and
  ecosystem validation.
- **Move semantics** for Server, Peer, RunningService, Client are correctly implemented.
- **Exception safety** is good — exceptions consistently caught at boundaries and converted to `core::Error`.
- **Windows Handle and POSIX FileDescriptor RAII** wrappers are correctly implemented.
- **OAuth lifecycle** structure is sound (PKCE required, HTTPS validated, CSRF state parameter).
- **Sampling/elicitation** validation coverage is strong for the audited DTO
  and parser paths; this does not by itself close the broader core capability
  parity DoD.
- **Cancellation flow** is exception-safe with `shared_ptr<void>` custom deleter cleanup.
- **Initialized enforcement** is now enforced at both transport layers (stdio and HTTP).
- **Handler dispatch** is correct — all list methods properly return results.
- **PImpl pattern** for HTTP server is type-safe with no void* erasure.
- **Handler overwrite protection** — `apply_to()` only installs non-empty callbacks.
- **CSRF constant-time comparison** — `constant_time_string_equal()` XOR-accumulator used in state and credential stores.
- **Temperature semantics** — `std::optional<double>` correctly distinguishes "unset" from "0.0".
- **Handler ambiguity detection** — `static_assert` on registration catches ambiguous callable signatures at compile time.

---

## Client SDK Issues

### H6. Client and Peer Types Are Not Thread-Safe — **FIXED**

**Files:** `sdk/client/include/cxxmcp/client/client.hpp:769-799`, `sdk/include/cxxmcp/peer.hpp:2000-2035`

The `Client` class has no internal synchronization. `transport_started_` (bool), all handler `std::function` members, and `roots_` vector are unprotected. `ensure_transport_started()` is not guarded — concurrent calls race on `transport_started_`.

`Peer<RoleClient>::send_native_request` performs a synchronous request-response loop with no mutex. Two concurrent `call_tool` calls interleave their receive loops.

**Fix:** Document that callers must serialize access, or add internal synchronization.

**Resolution:** `Peer<RoleClient>` native requests are serialized by the
receive-pump/request-id correlation work. Concrete `Client` now protects
handler slots with a handler mutex, snapshots callbacks before dispatch, guards
roots/capability/start state with a state mutex, and returns learned server
capabilities by value instead of exposing a long-lived reference into mutable
state. SDK tests cover reentrant handler replacement for both notification and
request callbacks.

---

### H7. Out-of-Order Responses Cause Hard Errors — **FIXED**

**File:** `sdk/include/cxxmcp/peer.hpp:1980-1983`

```cpp
if (response->id != request.id) {
    return std::unexpected(make_error(..., "unexpected response id"));
}
```

Response correlation matches `response->id == request.id` and errors on mismatch. If a server sends responses out of order (valid JSON-RPC), the client fails instead of buffering the mismatched response.

**Fix:** Buffer mismatched responses and correlate by ID, or document that out-of-order responses are unsupported.

**Resolution:** `serve(ClientPeer)` drives role-generic client transports with
a single receive loop and correlates responses by request id, so
server-to-client requests/notifications and outbound client requests no longer
compete for `receive()`. The direct synchronous native request path now buffers
mismatched responses in the same native response store and returns them when
the matching request id is sent later.

---

### H8. No Reconnection or Retry Logic — **TRANSPORT-SPECIFIC**

No transport or client layer implements reconnection after connection loss. A dropped connection requires constructing an entirely new `Client`. The `Transport` base class has `start()`/`stop()` but no `reconnect()`.

**Resolution:** Streamable HTTP/SSE has scoped recovery behavior:
`Client::send_rpc_request()` detects HTTP session termination errors via
`is_session_terminated_error()`, re-initializes the session using cached
`last_initialize_params_`, and retries the failed request exactly once. SSE
transport also has reconnect timing through `set_reconnect_interval(250)`.
Generic transport-level `reconnect()` remains outside the SDK contract; callers
that need cross-transport reconnect orchestration should own that lifecycle or
propose a separate design note.

---

### H9. Timeout Uses 10ms Polling Loop — **FIXED**

**File:** `sdk/include/cxxmcp/request.hpp:208-231`

`RequestHandle::await_response()` polls with 10ms sleep slices instead of using condition variable notification. This wastes CPU and adds up to 10ms latency to every response.

Background tasks continue running after timeout — cancellation is cooperative only (notification sent to peer, but the task is not forcibly stopped).

**Resolution:** `RequestHandle::await_response()` now uses `AsyncResult<T>` with
CV-based blocking instead of `shared_future` or polling. `CancellationToken` has
been extended with `wait_for_cancel()` (CV-based, zero CPU) backed by a
`CancellationState` containing `atomic_bool` + `mutex` + `condition_variable`.
The cancellation watcher is posted as a `BACKGROUND` task on the executor,
blocks on `token.wait_for_cancel()`, then cancels the `AsyncResult` to wake
waiters. `TaskHandle::wait()` also uses CV-based push notification
(`task_status_cv_`) with an RPC poll fallback for servers that don't send task
status notifications. All `sleep_for` polling has been eliminated.

---

## Protocol Specification Compliance Issues

### H10. Server Silently Negotiates Unsupported Protocol Versions — **FIXED**

**File:** `sdk/server/src/server.cpp:400-412`

`negotiate_protocol_version()` returns the fallback (latest version) for any unknown version string. The MCP spec requires the server to respond with an error for unsupported versions. A client requesting `"not-a-version"` would silently get the latest version.

**Fix:** Reject unknown protocol versions with an error response.

**Resolution:** Direct `Server::handle_request()` and canonical
`ServerPeer::handle_request()` now reject unsupported initialize
`protocolVersion` values with `InvalidParams` while still accepting every
version in `McpSupportedProtocolVersions`. The public
`negotiate_protocol_version()` helper is now strict: it returns the requested
version only when supported and returns `std::nullopt` instead of falling back
to the latest version for unknown inputs.

---

### H11. Server Does Not Enforce `initialized` Notification — **FIXED**

**File:** `sdk/server/src/server.cpp:366-438`

The `handle_request` method processes all MCP methods without checking whether `notifications/initialized` has been received. The MCP spec states clients SHOULD NOT send requests before `initialized`. The server should reject requests from uninitialized sessions.

**Resolution:** Both transport layers now enforce initialized state — stdio transport rejects non-initialize/ping requests and non-initialized notifications before session initialization (stdio_transport.cpp: lines 193-198, 244-257). HTTP transport does the same (http_transport.cpp: lines 640-646, 782-797). `Server::handle_request()` itself still has no check, but all standard transport paths are covered.

---

### M7. Unknown Content Block Types Rejected Instead of Preserved — **FIXED**

**File:** `sdk/protocol/include/cxxmcp/protocol/tool.hpp:509-512`

`content_block_from_json` returns an error for any type not in `{text, image, audio, resource, resource_link}`. This was re-checked against RMCP's `RawContent` model, which is an exhaustive Serde tagged enum over the same content variants. Unknown JSON members on known content block types are still preserved through `extensions`, but unknown wire content block `type` values should be rejected at the protocol boundary.

**Resolution:** The parser now properly rejects unknown content block types with a clear error message (`"content block type is not supported"`). The struct has `Json extensions = Json::object()` for forward-compatible round-tripping of known types. Unknown wire types are correctly rejected at the protocol boundary.

---

### M8. Temperature 0.0 Cannot Be Explicitly Set — **FIXED**

**File:** `sdk/protocol/include/cxxmcp/protocol/sampling.hpp:745-747`

```cpp
if (params.temperature != 0.0) { json["temperature"] = params.temperature; }
```

Temperature 0.0 (a valid value meaning "deterministic") is treated the same as "not set" and omitted. Should use `std::optional<double>`.

**Resolution:** `CreateMessageParams::temperature` is now
`std::optional<double>`, so absent temperature and explicit `0.0` are distinct.
Protocol tests cover explicit zero serialization, parsing, and omission when the
optional is empty.

---

### M9. No Base64 Validation on Image/Audio Data — **FIXED**

**File:** `sdk/protocol/include/cxxmcp/protocol/tool.hpp:472-485`

For `image` and `audio` content blocks, only checks that `data` is a string. Does not validate valid base64 encoding. Invalid base64 passes parsing and fails only at decode time.

**Resolution:** `content_block_from_json()` now validates image/audio `data`
as RFC 4648 base64 with canonical 4-character groups and tail-only padding.
Empty strings are accepted as zero-length payloads and are preserved when
serializing parsed image/audio blocks. Protocol tests cover valid padding,
empty payloads, invalid characters, invalid lengths, and misplaced padding.

---

### M10. No NaN/Infinity Validation on Floating-Point Values — **FIXED**

Throughout the codebase, floating-point values (temperature, number schema min/max) are not validated against NaN or Infinity. nlohmann::json serializes these as `NaN`/`Infinity` which are not valid JSON per RFC 8259.

**Resolution:** Directly parsed protocol floating numbers now require finite
values. Coverage includes sampling `temperature`, model preference priorities,
progress `progress` / `total`, elicitation number schema
`minimum` / `maximum` / `default`, and accepted elicitation number content.

---

### M11. No JSON Depth or Size Limits — **FIXED**

**File:** `sdk/protocol/src/serialization.cpp:37-51`

`parse_json_document` calls `Json::parse` without configuring `max_depth`. All `_from_json` functions have no array/string size limits. A malicious peer could send deeply nested JSON (up to ~1024 levels) or arrays with millions of elements.

**Fix:** Configure `max_depth` explicitly and add size limits to `_from_json` parsers.

**Resolution:** `validate_json_document_limits()` added (lines 44-114) with iterative DFS checking: depth (128), node count (200k), aggregate string bytes (16 MB), collection entries (100k). Called by `parse_json_document` after successful parse (lines 116-141).

---

## Performance and Scalability Issues

### M12. Serialization Double-Allocation Pattern — **FIXED**

**File:** `sdk/protocol/src/serialization.cpp:333-394`

`serialize_message` constructs a full `nlohmann::json` tree, copies fields into it, then calls `json.dump()`. Every outbound message allocates a JSON tree just to serialize it. `method` and `params` are copied (const ref), not moved. `std::string(JsonRpcVersion)` allocates a heap string for the literal `"2.0"` on every call.

**Fix:** Consider direct-to-string serialization for hot paths, or at minimum move `method`/`params`.

**Resolution:** `serialize_message()` now dispatches to internal direct-envelope
serializers for requests, responses, and notifications. The public
`serialize_request()`, `serialize_response()`, and `serialize_notification()`
helpers call the same direct paths instead of first constructing a
`JsonRpcMessage` wrapper. Envelope fields are emitted directly as JSON text
while preserving public API compatibility and `_meta` merge semantics; focused
protocol tests cover direct request/notification helpers and envelope `_meta`
overriding params `_meta`. The `protocol_serialization_benchmark` CTest records
current output bytes, elapsed time, allocation count, and allocated bytes for
small requests, large requests with and without `_meta`, large notifications,
large responses, and large error responses. A Linux GCC Release release-gates
artifact is wired for the benchmark, but exact-commit artifact review is still
required before using it as release evidence.

---

### M13. `request_cancellation_key` Allocates on Every Request — **FIXED**

**File:** `sdk/server/src/server.cpp:112`

Calls `request_id_to_json(request_id).dump()` — creates a temporary `nlohmann::json`, serializes to string, just to use as an `unordered_map` key. For integer IDs (common case), this is wasteful.

**Resolution:** Client and server `request_cancellation_key()` now build typed
keys directly
from the `RequestId` variant (`i:<integer>` / `s:<string>`) instead of
constructing temporary JSON and dumping it for every request.

---

### M14. No Maximum Session Count in HttpTransport — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/http_transport.hpp:192`

`sessions_` is `unordered_map` with no size limit. A malicious client could create unlimited sessions via repeated `initialize` requests.

**Resolution:** `max_sessions = 1024` option added (line 58). Enforcement at lines 755-763 returns 429 when exceeded.

---

### M15. No `extern template` Declarations

Zero `extern template` declarations in the entire SDK. Every TU that includes transport headers re-instantiates `Transport<Role>`, `StdioTransport<Role>`, etc. For multi-TU consumer binaries, this causes duplicate template instantiations.

---

### M16. `nlohmann/json.hpp` Transitively Included by Every Header

**File:** `sdk/protocol/include/cxxmcp/protocol/types.hpp:15`

`types.hpp` defines `using Json = nlohmann::json;` and includes the full `nlohmann/json.hpp`. This is transitively pulled in by virtually every SDK header. No `json_fwd.hpp` usage. The consumer pays the full nlohmann template cost even if they only need protocol type definitions.

---

### L7. Rate Limiter Serializes Params to Measure Byte Count — **FIXED**

**File:** `sdk/server/src/server.cpp:528`

`request.params.dump().size()` serializes the entire params JSON to a temporary string just to measure size, then discards the string. O(n) allocation per request.

**Resolution:** The server now computes the serialized params size by walking
the JSON value and accounting for JSON punctuation, escaped string bytes, and
integer formatting without constructing the complete params string. Floating
point values use bounded numeric formatting for the limiter's approximate byte
fact. Coverage was added for accepted-at-limit and rejected-over-limit
rate-limiter decisions, plus the HTTP transport regression suite was run.

---

### L8. Registry `list()` Sorts on Every Call — **FIXED**

**File:** `sdk/server/src/registry.cpp:256-268`

Every `list()` call copies all entries from `unordered_map` into a vector, then sorts by name. O(n log n) per call. Should maintain sorted order or cache.

**Resolution:** Tool, prompt, resource, and resource-template registries now
cache their sorted list results. `add()` invalidates the cache and `list()`
rebuilds only when dirty, while preserving the public value-returning API.

---

### L9. `add_session_transport` Does Linear Duplicate Check — **FIXED**

**File:** `sdk/server/src/server.cpp:1051-1057`

`std::find` over `session_transports_` vector. O(n) per add.

**Resolution:** `Server` now keeps a side
`std::unordered_set<Transport*>` for duplicate detection while preserving the
existing `session_transports_` vector order for notification fan-out.

---

### L10. Transport Adapters Have Broken Move Semantics — **FIXED**

**File:** `sdk/client/include/cxxmcp/client/transport_adapter.hpp:42-98`

`TransportContractAdapter` stores both a `unique_ptr<Transport> owned_` and a raw `transport_` pointer. After move, `transport_` still points to the old (now-null) `owned_`. Latent use-after-move bug if adapters are moved (they are typically used in place, so this may not trigger in practice).

**Resolution:** Client/server legacy-to-contract and contract-to-legacy
transport adapters now define explicit move constructors and move assignments.
Owned adapters rebind the raw transport pointer to the moved-to `owned_.get()`,
and moved-from adapters become inert. Adapter tests cover owned transport moves
and moved-from error behavior for all four adapter directions.

---

## Updated Recommendations (Priority Order)

| Priority | Fix | Impact | Status |
|----------|-----|--------|--------|
| 1 | ~~SIGPIPE protection on server stdio~~ | ~~Deterministic crash fix~~ | **DONE** |
| 2 | ~~`running_` → `std::atomic<bool>`~~ | ~~Data race elimination~~ | **DONE** |
| 3 | ~~Lock `output_mutex_` in main loop~~ | ~~Output corruption fix~~ | **DONE** |
| 4 | ~~Reject unsupported protocol versions~~ | ~~Spec compliance~~ | **DONE** |
| 5 | ~~HTTP hardening (body limit, timeouts)~~ | ~~Security~~ | **DONE** |
| 6 | ~~JSON depth/size limits~~ | ~~Security (DoS prevention)~~ | **DONE** |
| 7 | ~~Max session count limit~~ | ~~Security (resource exhaustion)~~ | **DONE** |
| 8 | ~~Move `std::unexpected` out of `std`~~ | ~~UB elimination~~ | **DONE** |
| 9 | ~~Enforce `initialized` at transport layer~~ | ~~Spec compliance~~ | **DONE** |
| 10 | ~~Handler dispatch missing return~~ | ~~Silent result loss~~ | **DONE** |
| 11 | ~~Reject unknown content block types~~ | ~~Protocol correctness~~ | **DONE** |
| 12 | ~~`apply_to()` handler overwrite~~ | ~~API correctness~~ | **DONE** |
| 13 | ~~`void*` type erasure~~ | ~~Type safety~~ | **DONE** |
| 14 | ~~CSRF state constant-time comparison~~ | ~~Security (timing attack)~~ | **DONE** |
| 15 | ~~HTTPS enforcement on auth endpoints~~ | ~~Security~~ | **DONE** |
| 16 | ~~Reject PKCE `plain` method~~ | ~~OAuth 2.1 compliance~~ | **DONE** |
| 17 | ~~Fix temperature 0.0 serialization~~ | ~~Correctness~~ | **DONE** |
| 18 | Split `server.hpp` / add `extern template` | Compile time / binary size | Partial: header split done; extern-template/json-fwd compile-time debt remains |
| 19 | ~~Client thread safety documentation~~ | ~~Correctness~~ | **DONE** |
| 20 | ~~Out-of-order response buffering~~ | ~~Correctness~~ | **DONE** |
| 21 | ~~Base64 validation for image/audio~~ | ~~Correctness~~ | **DONE** |
| 22 | ~~NaN/Infinity validation on all protocol floats~~ | ~~Correctness~~ | **DONE** |
| 23 | ~~Direct-to-string serialization~~ | ~~Performance~~ | **DONE** |
| 24 | ~~`request_cancellation_key` allocation~~ | ~~Performance~~ | **DONE** |
| 25 | ~~Registry `list()` caching~~ | ~~Performance~~ | **DONE** |

---

## OAuth / Auth Layer Security Audit

The auth layer is **interface-first**: the stable default contracts keep crypto
and I/O behind injectable interfaces (`DpopSigner`, `DpopVerifier`,
`JwtVerifier`, `OAuthTokenEndpoint`, `PkceGenerator`) and the default SDK path
does not pull OpenSSL. The optional auth/OpenSSL surface now also provides
concrete JOSE/JWK/JWS/JWT/DPoP helpers, `FetchingJwksJwtVerifier`,
`HttpOAuthTokenEndpoint`, `AuthorizationManager`, and server auth-provider
presets behind explicit opt-in package features. Applications still own browser
launching, loopback redirect receivers, persistent secret storage, and remote
HTTP client policy.

### Auth-F1. CSRF State Compared with `==` — Timing Attack — **FIXED**

**File:** `sdk/auth/include/cxxmcp/auth/lifecycle.hpp:202-215`

`InMemoryStateStore::find_entry()` compares `iter->first == state` using `std::string::operator==` — non-constant-time. The `state` parameter is the CSRF token from the OAuth callback. An attacker making repeated callback attempts with different `state` values and measuring timing can potentially guess the stored state incrementally.

Same pattern in `InMemoryCredentialStore::find_entry()` (lines 117-119) and `InMemoryTokenStore` (`token.hpp:98-101`).

No constant-time comparison utility exists anywhere in the codebase.

**Fix:** Implement a constant-time string comparison utility (e.g., byte-by-byte XOR accumulator).

**Resolution:** Added `constant_time_string_equal()` and used it for
`InMemoryStateStore` state lookup plus `InMemoryCredentialStore` /
`InMemoryTokenStore` key matching. Focused tests cover equal strings,
equal-length mismatch, different-length mismatch, and empty-string behavior.

---

### Auth-F2. No HTTPS Enforcement on Auth/Token Endpoints or Redirect URIs — **FIXED**

**File:** `sdk/auth/include/cxxmcp/auth/lifecycle.hpp:672-727`

`build_authorization_url()` validates non-empty values but performs **no scheme validation**. An `http://` authorization_endpoint or redirect_uri would be silently accepted. The only HTTPS enforcement is in `is_valid_client_id_metadata_url()` (`registration.hpp:196`), which only covers URL-based client_id values.

**Fix:** Reject non-HTTPS URLs for authorization_endpoint, token_endpoint, and redirect_uri (with exception for `http://localhost` loopback per RFC 8252).

**Resolution:** `build_authorization_url()` now rejects non-HTTPS
authorization endpoints, rejects non-HTTPS token endpoints when provided, and
allows redirect URIs only when they are HTTPS or loopback HTTP
(`localhost`, `127.0.0.1`, or `[::1]`). Auth tests cover insecure endpoint
rejection and loopback redirect allowance.

---

### Auth-F3. PKCE `plain` Method Accepted — OAuth 2.1 Non-Compliance — **FIXED**

**File:** `sdk/auth/include/cxxmcp/auth/pkce.hpp:15`, `lifecycle.hpp:396-404`

`PkceCodeChallengeMethod::kPlain` is defined and supported. OAuth 2.1 **mandates** S256 and **forbids** `plain`.

**Fix:** Remove `kPlain` or reject it in `start_authorization()`.

**Resolution:** The public PKCE enum now exposes only S256. The older `kPlain`
value has been removed, so SDK users cannot request OAuth 2.1-forbidden plain
PKCE through the typed API.

---

### Auth-F4. DPoP Private Key Stored as Plaintext `std::string` — **FIXED**

**File:** `sdk/auth/include/cxxmcp/auth/dpop.hpp:21-25`

Key material persists in heap memory with no guaranteed zeroization on destruction. No `SecureString` wrapper is provided.

**Fix:** Provide a `SecureString` wrapper or document the risk.

**Resolution:** The public auth API now provides `SecureString`, an owning
string wrapper that zeroizes bytes on reset, move-source cleanup, and
destruction. `DpopKey::private_key_pem` now uses `SecureString` instead of
plain `std::string`.

---

### Auth-F5. DPoP Has No Replay/Clock-Skew/htm+htu Enforcement — **FIXED**

**File:** `sdk/auth/include/cxxmcp/auth/dpop.hpp`

The `DpopVerifier` is pure virtual. The SDK provides no `jti` replay cache, no `clock_skew_tolerance`, and no `htm`/`htu` validation guidance.

**Fix:** Document requirements prominently. Consider providing a reference implementation.

**Resolution:** `dpop.hpp` now provides a `DpopReplayCache` boundary, a
thread-safe `InMemoryDpopReplayCache`, `DpopClaimValidationOptions`, and
`validate_dpop_proof_claims()`. The helper enforces non-empty `jti`, request
method `htm`, request URL `htu`, clock-skew-bounded `iat`, required `ath` for
access-token-bound proofs, optional expected `ath` matching, and replay
rejection through the supplied cache. HTTP server auth requests now also carry
optional method and absolute URL metadata so DPoP providers can validate
`htm`/`htu` against the real request target. The optional auth layer also
provides `DpopBearerAuthProvider`, which adapts injected `JwtVerifier`,
`DpopVerifier`, replay-cache, and access-token hash implementations into a
server `AuthProvider` without decoding JWTs itself. Focused auth and HTTP tests
cover target mismatch, stale proofs, missing/mismatched access-token hash,
duplicate `jti` rejection, HTTP request-target propagation, and the provider
bridge. The opt-in OpenSSL auth surface now also has reusable JOSE base64url
helpers, compact JWS protected-header parsing, public JWK import, RS256/ES256
compact JWS signature verification, trusted JWKS JWT validation for issuer,
audience, expiry, not-before, issued-at, and required claims,
transport-neutral JWKS fetch/cache refresh policy over injected endpoint/cache
contracts, plus ES256/RS256 DPoP proof signing and verification over PEM private
keys and embedded public JWKs. DPoP proof verification validates request binding
and access-token hash, and composes with the existing replay cache for duplicate
`jti` rejection. Separate OpenSSL/server preset providers now own the JWT
verifier, DPoP verifier, access-token hash helper, and DPoP-aware server bridge
without adding server headers to the default auth/OpenSSL include path.

---

### Auth-F6. No SSRF Protection on Metadata Discovery URLs — **FIXED**

**File:** `sdk/auth/include/cxxmcp/auth/lifecycle.hpp:498-545`

`build_protected_resource_metadata_urls()` and `build_authorization_server_metadata_urls()` construct `.well-known` URLs from user-supplied resource URLs without validating scheme or host.

**Fix:** Apply the same validation as `is_valid_client_id_metadata_url()`.

**Resolution:** Metadata discovery candidate builders now reject non-HTTPS
inputs except exact loopback HTTP hosts, reject userinfo, fragments, empty
hosts, literal dot segments, and `%2e`-encoded dot segments before producing
well-known URLs. Invalid `WWW-Authenticate resource_metadata` hints are skipped
instead of being fetched.

---

### Auth-F7. Authorization State Has No TTL Enforcement — **FIXED**

**File:** `sdk/auth/include/cxxmcp/auth/lifecycle.hpp:144-154`

`StoredAuthorizationState` records `created_at` but `exchange_authorization_code()` never checks expiry. OAuth 2.1 recommends authorization codes be valid for max 60 seconds.

**Fix:** Add a configurable TTL and check it in `exchange_authorization_code()`.

**Resolution:** `AuthorizationManager` now defaults authorization state TTL to
60 seconds, exposes `set_authorization_state_ttl()`, consumes one-time state
before the expiry check, and rejects expired state before invoking the token
endpoint.

---

### Auth Positive Findings

- **No token/secret logging** — zero instances of credential material being output
- **Client secret not in URL** — `build_authorization_url()` correctly omits `client_secret` from query parameters
- **One-time state consumption** — `exchange_authorization_code()` removes state from store before use, preventing replay
- **Client metadata URL validation** — enforces HTTPS, rejects userinfo/dot segments/fragments
- **PKCE required** — rejects empty `code_challenge` or `code_verifier`
- **Clean interface boundaries** — all crypto behind pure virtual interfaces

---

## Additional SDK Issues Found in Extended Audit

### HIGH: Missing `return` in Handler Dispatch — Results Silently Dropped — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/handler.hpp:395-417`

In `dispatch_server_handler_discovery_request`, the `ResourcesTemplatesListMethod` branch constructs a response but does not `return` it. Execution falls through to `return std::nullopt`, silently dropping the handler's result.

**Fix:** Add `return` before `server_handler_result_response(...)`.

**Resolution:** The `ResourcesTemplatesListMethod` branch now correctly returns the response when `result.has_value()` is true (lines 395-416), matching the pattern used by all other list methods.

---

### HIGH: No SIGINT/SIGTERM Handling — Unclean Shutdown — **FIXED**

No handler for SIGINT or SIGTERM exists anywhere in the SDK. If the process receives SIGINT (Ctrl+C), the default handler terminates immediately without calling `Server::stop()`, leaving transport threads running, child processes unreaped, and pending request promises unfulfilled.

**Fix:** Provide a signal handler utility or document that applications must install their own graceful shutdown handler.

**Resolution:** `docs/request_lifecycle.md#recommended-signal-handling-pattern` now documents the application-owned
signal model for POSIX `SIGINT` / `SIGTERM` and Windows console control
handling. The guidance uses a minimal atomic flag in the signal/control handler
and calls SDK `stop()` / `close()` APIs from normal application control flow.
`docs/request_lifecycle.md`, `docs/examples.md`, and release evidence checks
link the guidance so long-running services and examples have a documented
shutdown path without the SDK installing process-wide handlers.

---

### HIGH: Mutable Registry References Exposed Without Synchronization — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/server.hpp:694-790`

`Server` exposes mutable references to `tools()`, `prompts()`, `resources()` registries. These have no internal synchronization. Concurrent `add()` + `call()` is undefined behavior.

**Fix:** Document that registry mutation must happen before `serve()` starts, or add synchronization.

**Resolution:** Tool, prompt, resource, and resource-template registries now
synchronize add/get/list/call/read access internally. Dispatch paths copy the
selected handler and metadata under the registry lock, then invoke application
callbacks outside the lock so callbacks can reenter the same registry without
deadlocking. SDK coverage verifies handler reentrancy for tools, prompts, and
resources.

---

### MEDIUM: `ServerHandlerInterface` Lifetime Not Enforced — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/handler.hpp:647-798`

`Server::set_handler(const ServerHandlerInterface&)` creates lambdas capturing `&handler` by reference. If the handler is destroyed before the server, all callbacks become dangling references.

**Resolution:** `Server` and `ServerBuilder` now provide
`std::shared_ptr<const ServerHandlerInterface>` overloads that retain the
contract object for the lifetime of the installed callbacks. The reference
overload remains as an explicitly documented borrowed compatibility path, and
SDK tests prove the owned overload keeps the handler alive until server
destruction and rejects null shared handlers through the builder.

---

### MEDIUM: `ServerHandler::apply_to()` Silently Overwrites Handlers — **FIXED**

**File:** `sdk/server/include/cxxmcp/server/handler.hpp:560-636`

Sets `on_completion`, then `on_completion_with_context`, then `on_completion_with_request_context` — each overwrites the previous. Undocumented.

**Resolution:** `apply_to()` now checks each callback for non-emptiness before applying. Only explicitly set callbacks overwrite existing ones. The doc comment at line 484-486 documents this behavior: "apply_to() and Server::set_handler() install only non-empty members; empty members leave any existing callback on the target Server unchanged."

---

### ~~MEDIUM: Logger `ERROR` Macro Conflicts with `<windows.h>`~~ — **NOT APPLICABLE**

**File:** `runtime/observability/include/cxxmcp/observability/logger.hpp:60-88`

Redefines `ERROR` as a macro. On Windows, `<windows.h>` defines `#define ERROR 0`. Any file including `<windows.h>` before this header will have its `ERROR` macro silently replaced.

**Resolution:** The entire `runtime/` directory has been deleted. The logger no longer exists in the codebase.

---

### MEDIUM: Plugin `ToolExecutionContext` Uses `string_view` — Dangling Risk — **FIXED**

**File:** removed experimental plugin tool context header

`tool_name` was `std::string_view`, borrowing from the caller. If a plugin
stored or queued the context for async processing, the view could dangle.

**Resolution:** The experimental plugin SDK and adapter extension headers were
removed from the SDK package surface. Gateway/plugin integrations now belong in
external repositories until a design note promotes a narrow SDK extension with
release evidence.

---

### LOW: Registry Names Have No Character/Length Validation — **FIXED**

**File:** `sdk/server/src/registry.cpp`

Names are accepted as long as non-empty. No character or length restrictions. Names flow into error messages, JSON responses, and log output.

**Resolution:** Tool, prompt, resource, and resource-template registration now
validate public names and URI keys with explicit byte limits and reject ASCII
control characters while preserving useful punctuation and path-like names.
Focused SDK tests cover accepted punctuation, overlong values, and control
characters.

---

### ~~LOW: `FATAL` Logger Calls `std::abort()` Without Destructors~~ — **NOT APPLICABLE**

**File:** `runtime/observability/include/cxxmcp/observability/logger.hpp:51-56`

Calls `std::abort()` which skips C++ destructors for stack-allocated objects.

**Resolution:** The runtime logger has been removed from the SDK core and the
referenced file no longer exists in the current SDK package surface.

---

### Cancellation System — **CLEAN**

`CancellationToken`/`CancellationSource` use `shared_ptr<atomic_bool>`, are thread-safe, have no leak risk, and handle moved-from states correctly. One-way irreversible cancellation is correct by design.

---

## Original Issue Tally (All Audits Combined, Not Open Defects)

Open code defects are summarized above as 0. This table counts historical
findings by category.

| Category | HIGH | MEDIUM | LOW | INFO |
|----------|------|--------|-----|------|
| Thread Safety (fixed) | 0 | 0 | 0 | — |
| Protocol Compliance | 2 | 2 | 3 | — |
| Security (Auth) | 0 | 3 | 2 | 3 |
| Security (General) | 1 | 1 | 1 | — |
| Transport | 1 | 2 | 0 | — |
| API Design | 1 | 3 | 2 | — |
| Performance | 0 | 0 | 4 | — |
| **Total** | **5** | **11** | **10** | **3** |

**Fixed (55):** SIGPIPE, running_ atomic, output_mutex_, client_capabilities_, session transport lifetime guard, client/server handler synchronization, native out-of-order response correlation, std::unexpected UB, HTTP body/timeout, session limit, move semantics, JSON depth limits, HTTP PImpl ownership, executor exception hook, JSON-RPC version validation, unsupported MCP protocol-version rejection, strict public protocol-version negotiation helper, initialized enforcement (transport), stable batch rejection policy, handler dispatch missing return, unknown content type rejection, apply_to handler overwrite, explicit sampling temperature zero, image/audio base64 validation, protocol finite-number validation, client/server request-cancellation key allocation, RequestHandle timeout-only wait strategy, session transport duplicate lookup, registry list caching, auth constant-time state/key comparison, auth endpoint HTTPS enforcement, auth metadata discovery SSRF guard, auth authorization-state TTL, auth PKCE S256-only public enum, DPoP SecureString key material, DPoP replay/claim validation hardening, handler dispatch ambiguity detection, registry name/URI validation, owned ServerHandlerInterface registration, registry synchronization, server authoring header split, direct JSON-RPC envelope serialization, POSIX process argv mutable buffers, graceful shutdown guidance, rate-limiter params size accounting, stale tooling submodule cleanup, removed plugin/adapters extension surface, server auth HTTP request-target propagation, DPoP-aware server AuthProvider bridge.

**Not applicable (3):** ERROR macro conflict (logger deleted), FATAL logger abort semantics (runtime logger deleted), stale `tl::expected` concern because the vendored fallback is scoped to release dependency review instead of treated as a permanent audit fact.

**Accepted limitation (1):** stdio blocking read stop-unblock behavior is documented and isolated from the recommended process-stdio / Streamable HTTP interop paths.
