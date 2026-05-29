# cxxmcp Project Assessment

> This document records a comprehensive technical audit of the cxxmcp SDK, covering three dimensions: ecosystem maturity ("de facto standard" readiness), technical capability, and API usability. The audit was conducted on 2026-05-28.

---

## 1. De Facto Standard Readiness

### Verdict

cxxmcp is a **very strong candidate** that is not yet a de facto standard. The implementation quality, architecture, test coverage, interoperability matrix, and release infrastructure are all at a level that exceeds what most open-source C++ libraries achieve before their first stable release. The primary barriers are **ecosystem signals, not technical gaps**.

### What Is Already In Place

- **Protocol fidelity**: Typed models for every MCP capability family (tools, prompts, resources, sampling, elicitation, tasks, completion, progress, cancellation). Tracks published MCP protocol snapshots, no custom extensions.
- **Cross-SDK interoperability as release-blocking gate**: Tests run cxxmcp against RMCP (Rust), TypeScript SDK (`@modelcontextprotocol/sdk@1.29.0`), and Python SDK (`mcp==1.27.1`) in both directions over process stdio and Streamable HTTP. Current all-suite conformance evidence is tracked in `docs/conformance_evidence.md`.
- **6-platform release matrix**: Linux GCC, Linux Clang, macOS AppleClang, Windows MSVC (static), Windows ClangCL (static), Windows MSVC (dynamic). Each leg runs sanitizer builds, performance baselines, and public-header compile evidence.
- **Release discipline**: 19-artifact evidence bundle, JUnit XML content verification (not just pass/fail), binding release candidate checklist.
- **Package manager readiness**: vcpkg overlay port, Conan 2 recipe, xmake recipe, FetchContent/CPM documentation.
- **Documentation depth**: 30+ policy/process/audit documents covering compatibility, public API stability, dependency policy, ecosystem maturity, request/task/elicitation lifecycles, auth design.

### What Is Still Missing

| Gap | Impact |
|-----|--------|
| No published release tags | The `release-sdk` workflow exists but no actual tagged releases with attached artifacts |
| Zero independent public downstream adoption | Adoption ledger explicitly records "none" |
| vcpkg curated registry rejection | `microsoft/vcpkg#51972` closed 2026-05-27 for insufficient maturity |
| No stable release line declared | Compatibility policy says "once a stable release line is declared" |
| Single maintainer | Sustained maintenance by a team is typically expected for a de facto standard |

### Path to De Facto Standard

1. Publish 2-3 tagged releases with green release-gates artifacts.
2. Accumulate even modest independent adoption (2-3 independent projects).
3. Accept `jsonrpcpp` port in vcpkg curated registry (`microsoft/vcpkg#52045`).
4. Resubmit to vcpkg with the evidence documented in `ecosystem_maturity_evidence.md`.

The technical foundation is in place; what remains is time and ecosystem proof.

---

## 2. Technical Capability Assessment

### 2.1 Code Volume

| Layer | Key Files | Scale |
|-------|-----------|-------|
| Peer (core dispatch) | `peer.hpp` | 4212 lines / 155KB |
| Server | `server.hpp` 31KB, `peer.hpp` 35KB, `handler.hpp` 34KB | Three large headers |
| Client | `client.hpp` | 36KB |
| Auth (OAuth 2.1 + DPoP) | `lifecycle.hpp` 47KB + 17 headers | Complete OAuth 2.1 stack |
| HTTP Transport | `http_transport.hpp` + `.cpp` | Production-grade Streamable HTTP |
| Protocol models | 18 headers | Full MCP capability family modeling |
| Reflection system | `reflect.hpp` | Compile-time DTO serialization framework |
| Tests | 26+ release-blocking tests + interop | Cross-language (Rust/TS/Python) conformance |
| Scripts | 20+ Python/PowerShell | Release verification, API surface governance |

Estimated total: **150K-200K lines** including headers, implementation, tests, scripts, and documentation.

### 2.2 Architecture Highlights

#### Role-Tagged Generic Architecture

```cpp
template <class Role> class Peer;
template <> class Peer<RoleClient> { /* ~2300 lines */ };
template <> class Peer<RoleServer> { /* ~1900 lines */ };
```

Empty tag structs (`RoleClient`/`RoleServer`) enable compile-time role separation with zero runtime overhead. `Service<Role>` (pre-run) and `RunningService<Role>` (active) enforce lifecycle state machine at the type level.

#### Two-Layer Transport Abstraction

- **Protocol layer** `Transport<Role>`: minimal interface (send/receive/close), stateless, role-generic.
- **Server layer** `server::Transport`: session-aware, lifetime token, auth identity.

Bridged by `StreamableHttpServerTransport` adapter. Clean separation of concerns.

#### Compile-Time Reflection Serialization (`reflect.hpp`)

```cpp
// Compile-time field descriptor
FieldDescriptor<Struct, Field> -- constexpr construction, zero overhead

// std::apply + fold expression serialization
std::apply([&](const auto&... fds) {
    (serialize_one(json, obj, fds), ...);
}, Reflect<T>::fields());

// SFINAE type traits
is_optional, is_nullable_variant, has_extensions_member
// JsonFieldTraits<T> partial specializations: scalars, string, optional,
// vector, variant, enum, nested DTOs
```

Achieves near-C++20 reflection effects in C++17, zero-overhead.

#### Cooperative Cancellation System

`CancellationToken`/`CancellationTokenSource` separation:
- `cancelled()` uses `atomic_bool` + `memory_order_acquire` for fast path.
- `wait_for_cancel()` uses CV-based waiting (zero CPU polling).
- RAII cleanup via `shared_ptr<void>` custom deleters.

#### CV-Based Async Result (Replaces `std::promise`)

`AsyncResult<T>` with `then()` monadic chaining. If result is already set when `then()` is called, callback fires immediately -- avoids "missed notification" race.

### 2.3 HTTP Transport Quality (Production-Grade)

#### Session Management

- Multi-session support (max 1024, returns 429 on overflow).
- Session lifecycle tracking: `initialized` flag, `client_capabilities`, pending notifications, replay buffer.
- DELETE handling: aborts all pending requests, clears queues, reassigns `default_session_id_`.

#### SSE Implementation

- Dual back-pressure: event count (1024) + byte count (4MB).
- `Last-Event-ID` reconnection replay (sliding window, max 256 events).
- Single-stream enforcement: second GET on same session returns 409.
- Heartbeat: `condition_variable::wait_for(100ms)` + connection liveness check.

#### Security Validation Chain

1. Host header validation (DNS rebinding protection).
2. Origin header validation (configurable allowlist).
3. Content-Type / Accept validation.
4. `MCP-Protocol-Version` header cross-validation with body.
5. `Mcp-Session-Id` validation.
6. `Mcp-Method` / `Mcp-Name` header cross-validation with JSON-RPC body.

#### Pending Request Correlation

Each `PendingRequest` uses its own `mutex` + `condition_variable` (avoids priority inversion). Timeout cleanup + batch abort on stop (swap map out first, then lock-and-notify per entry -- correct to avoid holding transport lock during notification).

IPv6 bracket parsing, case-insensitive host comparison, SSRF protection -- production-grade network code.

### 2.4 Protocol Model Completeness

| MCP Capability | Status | Notes |
|----------------|--------|-------|
| Tools | Complete | Builder pattern + `schema_for<T>()` auto-generates JSON Schema |
| Prompts | Complete | |
| Resources | Complete | |
| Sampling | Complete + SEP-1577 | Tool calling, `ToolChoice`, content role validation |
| Elicitation | Complete | Form + URL dual mode, 5 primitive schema types, Builder API, content validation |
| Tasks | Complete | Background execution + cancellation + timeout + observer hooks |
| Completion | Complete | |
| Progress / Cancellation | Complete | |
| Capabilities | Complete | |

Every protocol struct has bidirectional `to_json`/`from_json`, structured validation in `from_json` (returns `Result<T>`, no exceptions), `extensions` field + `collect_json_extensions()` for forward compatibility, and reflection serialization support.

### 2.5 Auth Implementation (OAuth 2.1 + DPoP)

Completeness exceeds most open-source MCP SDKs:

- PKCE S256 (OpenSSL `RAND_bytes` 32 bytes + SHA-256).
- Authorization code flow + token refresh + refresh token rotation detection.
- Dynamic client registration (RFC 7591).
- Client ID Metadata Document.
- Metadata discovery (RFC 9728 Protected Resource Metadata + AS Metadata).
- DPoP proof construction + validation + replay cache + `ath` binding.
- Scope upgrade (`insufficient_scope` challenge handling).
- 401/403 response analysis + auto-refresh retry.

Security-conscious design:
- `SecureString`: volatile zeroize on destruction.
- `constant_time_string_equal()` for JWKS cache lookups and state comparison.
- SSRF protection in metadata URL construction.
- HTTPS-only endpoint enforcement + loopback-only HTTP redirect URIs.

Architecture split:
- `mcp_auth` (INTERFACE, no crypto dependency): types, abstract interfaces, parsing logic.
- `mcp_auth_openssl` (INTERFACE, links OpenSSL): concrete implementations.

Adding a libsodium/BoringSSL backend requires only a new implementation directory.

### 2.6 Test Infrastructure

| Category | Description |
|----------|-------------|
| Protocol serialization | JSON fixture round-trip + malformed input validation |
| Transport contracts | `QueueTransport` mock validates interface semantics |
| Cross-language interop | Rust rmcp, TypeScript SDK, Python SDK bidirectional conformance; all-suite conformance evidence recorded separately in `docs/conformance_evidence.md` |
| Public header compile | 11 headers compiled independently to verify self-containment |
| SDK boundary | Static scan forbids internal types leaking into public API |
| Package smoke | Separate CMake project consumes installed targets |
| Auth unit | 15 tests: constant-time comparison, DPoP claims, JWKS parsing, token store |
| Auth crypto integration | Real EC P-256 / RSA 2048 key generation + JWS/JWT verification |
| Release gate manifest | 26+ tests must be green across 6 platforms |

Zero test framework dependency -- hand-rolled `require()` + function pointer lists. Minimal build dependency tree at the cost of test isolation.

Recording Mock pattern: all auth abstract boundaries have `Recording*` test doubles, validating composition correctness rather than just logic.

### 2.7 Engineering Discipline

- 13 CMake targets, layered dependency graph, namespace aliases.
- Dual-mode deps: vendored (default) / system `find_package()`.
- C++17/20/23/26 configurable standard.
- Static/dynamic runtime selection (MSVC).
- `check_release_artifacts.py` parses JUnit XML to verify test name presence (not just file existence).
- `sdk_boundary.cmake` + `collect_public_api_surface.py` + `compare_public_api_surface.py` for API governance.

### 2.8 Technical Debt (Honest)

| Issue | Severity | Description |
|-------|----------|-------------|
| `peer.hpp` 4212 lines | Medium | Should split into independent translation units |
| `RunningService` duplication | Medium | Two specializations with near-identical code; CRTP can eliminate |
| `AsyncResult<T>` vs `AsyncResult<void>` duplication | Medium | Same as above |
| `handle_request` long if-chain | Low | Should use dispatch table |
| ~~`TaskHandle::wait()` polling~~ | ~~Low~~ | **Fixed.** Now uses CV-based push notification (`task_status_cv_`) with RPC poll fallback. Zero CPU polling. |
| `host_failure_status()` string matching | Low | Should use error code |
| Singleton executor not injectable | Low | Partially addressed: transports now use per-instance `Executor` (2 IO_BOUND threads each). Global singleton in `request.hpp` remains for `RequestHandle::spawn()`. |

---

## 3. API Usability Assessment

### 3.1 Overall Verdict

The basic path (hello-world server) is easy and competitive with Python/TypeScript SDKs. The advanced path (typed tools, handler contracts, custom schemas) has significant friction that will slow adoption by teams with non-trivial use cases.

### 3.2 What Works Well

**Builder chain is fluent and readable:**

```cpp
return mcp::server::App::builder()
    .name("my-server").version("1.0.0").stdio()
    .tool<std::string, std::string>("echo", [](std::string s) { return s; })
    .run();
```

Five lines, self-documenting, comparable to Python/TypeScript MCP SDK hello-world line counts.

**Error handling is consistent:** `Result<T>` throughout, no exceptions, `error().message` directly readable.

**Lifecycle safety is strong:** `Service<Role>` -> `RunningService<Role>` enforces state machine at type level, `&&`-qualified `serve()` prevents use-after-move.

### 3.3 Pain Points (Ranked by Impact)

#### Pain Point 1: Quadruple Redundancy for Typed Tool Registration

Registering a tool with custom types requires:

1. Define struct `SearchArgs`.
2. Write `from_json` ADL function.
3. Write `to_json` ADL function.
4. Specialize `SchemaTraits<SearchArgs>` (repeats field names and types).
5. Finally register the tool.

**~65 lines of boilerplate before one line of business logic.** Field renames require changes in 4 places. This is the single biggest adoption barrier.

Developer's likely complaint: *"The struct already has all the information -- why do I need hand-written JSON serialization AND schema AND traits specialization?"*

**Improvement direction:** Leverage the existing `reflect.hpp` system to auto-generate `SchemaTraits` from a single `REFLECT_FIELDS(...)` declaration. Simple structs should need only one declaration site.

#### Pain Point 2: Two Competing API Paths — **RESOLVED**

| | App::builder() | ServerPeer::builder() |
|---|---|---|
| Official status | **Deprecated** (`CXXMCP_DEPRECATED`) | Canonical path |
| Typed tool | `.tool<Args, Result>(...)` | `.tool<Args, Result>(...)` (same syntax) |
| prompt/resource | `.prompt(...)`, `.resource(...)` | `.prompt(...)`, `.resource(...)` (same syntax) |
| Usability | Deprecated | **Same or better** |

`ServerPeer::Builder` now accepts direct-handler `.tool<Args, Result>(name, handler)`,
`.prompt(name, handler)`, `.resource(name, handler)`, `.resource_template(name, handler)`,
`.completion(handler)`, `.sampling(handler)`, `.logging(handler)`, and
`.raw_request(handler)` — the same ergonomics as the old `App::Builder`. `App` is formally
deprecated with `CXXMCP_DEPRECATED`.

#### Pain Point 3: ServerHandlerInterface Too Wide

Every handler method has 3 overloads (no context / with context / with context + cancellation). 18 methods x 3 = **54 virtual functions**.

```cpp
virtual optional<Result<T>> on_call_tool(const ToolCall&) const;
virtual optional<Result<T>> on_call_tool(const ToolCall&, const SessionContext&) const;
virtual optional<Result<T>> on_call_tool(const ToolCall&, const SessionContext&, CancellationToken) const;
```

If a developer overrides only the 2-parameter version, cancellation support is **silently lost**.

**Improvement direction:** Use CRTP or template mixin to replace triple overloads. Keep only the fullest signature, pass context/cancellation via default parameters or tag dispatch.

#### Pain Point 4: Monolithic 4212-line `peer.hpp`

`Peer<RoleClient>` + `Peer<RoleServer>` + two Builders all in one file. 30+ includes. Cannot include just client or just server.

**Improvement direction:** Split into `peer_client.hpp`, `peer_server.hpp`, `peer_builder.hpp`. Keep `peer.hpp` as an aggregate include for backward compatibility.

#### Pain Point 5: Triple Handler Storage

Each handler callback is stored three times:
- Peer instance private members.
- Builder private members.
- Underlying Client/Server object.

Adding a new handler requires changes in 6+ locations. State can drift out of sync.

**Improvement direction:** Builder stores handlers, injects them into Peer/Client/Server at `build()` time. Runtime keeps only one copy.

#### Pain Point 6: `std::optional<core::Result<T>>` Three-State Return

ServerHandlerInterface methods return `optional<Result<T>>`:
- `nullopt` = "I don't handle this method"
- `Result` with value = "Handled, here's the result"
- `Result` with error = "Handled, but failed"

Three states are cognitively expensive. `nullopt` and error are easy to confuse.

**Improvement direction:** Use an explicit enum to distinguish `Unhandled` / `Handled(Result<T>)`, or use nested `std::expected`.

#### Pain Point 7: Process-Wide Singleton Executor (Partially Addressed)

```cpp
inline RequestExecutorState& request_executor_state() {
    static RequestExecutorState state;
    return state;
}
```

The global singleton in `request.hpp` remains for `RequestHandle::spawn()`.
However, transports now use per-instance `Executor` objects (2 IO_BOUND threads
each) instead of spawning unbounded `std::thread` per request. The `Executor`
class itself is a proper injectable component with priority queues and timer
wheel.

**Remaining improvement:** Bind the request executor to `Service` lifecycle
instead of the process global.

### 3.4 Other Medium/Low Priority Issues

| Issue | Impact |
|-------|--------|
| `core::Unit` leaks into call sites (`Result<Unit>` instead of void) | API noise |
| `ClientHandler` 30+ fields with compat aliases | Poor discoverability |
| Naming inconsistency (`on_tools_list` vs `on_list_roots_request`) | Muscle memory friction |
| Error code is raw `int`, category is raw `string` | Weak type safety |
| Internal flags (`output_schema_present`, `required_present`) exposed on public structs | API pollution |
| Two similar transport type names (`client::Transport` vs `transport::ClientTransport`) | Confusion |
| Examples are test-style (`require()`), not tutorial-style | High onboarding barrier |
| Template error messages unreadable (`detail::require_unambiguous_tool_handler`) | Debugging pain |

### 3.5 Recommended Improvement Priority

If conducting a "usability sprint," tackle in this order:

1. **Eliminate typed tool quadruple redundancy** -- leverage `reflect.hpp` for one-declaration schema + serialization. This is the biggest adoption friction.
2. **Unify API paths** -- port App's generic registration to ServerPeer, deprecate App.
3. **Split `peer.hpp`** -- client/server separation, reduce compile time and cognitive load.
4. **Simplify ServerHandlerInterface** -- remove triple overloads, use single signature + default parameters.
5. **Eliminate triple handler storage** -- Builder injects at build time.
6. **Bind executor to Service lifecycle** -- eliminate global singleton.

All of these are **pure engineering refactors** with no protocol changes. They can be done incrementally without breaking MCP compatibility.

---

## Appendix: Key Files Referenced

| File | Role |
|------|------|
| `docs/ecosystem_maturity_evidence.md` | Ecosystem maturity self-assessment ledger |
| `docs/public_api_stability.md` | API stability policy |
| `docs/release_gates.md` | Release-blocking test matrix definition |
| `docs/release_candidate_checklist.md` | Binding release audit checklist |
| `docs/compatibility_policy.md` | Source/ABI compatibility policy |
| `docs/dependency_policy.md` | Dependency management policy |
| `sdk/include/cxxmcp/peer.hpp` | Core Peer template (4212 lines) |
| `sdk/include/cxxmcp/service.hpp` | Service lifecycle layer |
| `sdk/include/cxxmcp/request.hpp` | Request lifecycle helpers |
| `sdk/include/cxxmcp/cancellation.hpp` | Cancellation primitives |
| `sdk/include/cxxmcp/error.hpp` | Error factory helpers |
| `sdk/protocol/include/cxxmcp/protocol/reflect.hpp` | Compile-time reflection system |
| `sdk/protocol/include/cxxmcp/protocol/tool.hpp` | Protocol tool definitions |
| `sdk/protocol/include/cxxmcp/protocol/sampling.hpp` | Sampling protocol model |
| `sdk/protocol/include/cxxmcp/protocol/elicitation.hpp` | Elicitation protocol model |
| `sdk/server/include/cxxmcp/server/http_transport.hpp` | HTTP transport implementation |
| `sdk/server/include/cxxmcp/server/authoring.hpp` | App builder and typed registration |
| `sdk/server/include/cxxmcp/server/handler.hpp` | ServerHandlerInterface |
| `sdk/server/include/cxxmcp/server/task_manager.hpp` | Task manager |
| `sdk/core/include/cxxmcp/core/executor.hpp` | Thread pool executor |
| `sdk/core/include/cxxmcp/core/async_result.hpp` | CV-based async result |
| `sdk/core/include/cxxmcp/core/result.hpp` | Core Result type |
