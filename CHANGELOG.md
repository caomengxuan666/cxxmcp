# Changelog

## 1.2.2

- Prepared release metadata and package references.

## 1.2.1

- Prepared release metadata and package references.

## 1.2.0

- Prepared release metadata and package references.

## 1.1.6

- Fixed `CXXMCP_REFLECT` so reflected DTO specializations compile correctly
  across GCC, MinGW, and CodeQL analysis builds.
- Updated reflection examples and documentation to use namespace-scope/global
  registrations consistently.

## 1.1.5

- Prepared release metadata and package references.

## 1.1.4

- Prepared release metadata and package references.

## 1.1.3

- Prepared release metadata and package references.

## 1.1.2

- Fixed `mcp::core::Result` ABI stability by keeping `tl::expected` as the
  public backend across all supported C++ standards.
- Fixed C++23 HTTP SDK builds by making the server HTTP holder complete before
  `std::unique_ptr` cleanup is instantiated.
- Removed unused optional plugin/adapters extension targets and headers.
- Removed unused vendored `tcb/span.hpp`.
- Added an opt-in benchmark target and Streamable HTTP benchmark driver.
- Cleaned stale root Markdown docs and compressed the root TODO to active
  release evidence gates.

## 1.1.1

- Implemented SEP-1699 SSE retry/reconnection in the SDK client HTTP transport.
- Fixed SSEClient `last_event_id_` tracking for priming events with empty data.
- Updated conformance status: server 109/1, client 447/0 (OpenSSL).

## 1.1.0

- Separated HTTP transport behind the `CXXMCP_ENABLE_HTTP` compile-time flag
  (default OFF) so that the core SDK builds without cpp-httplib.
- Added `CXXMCP_ENABLE_AUTH` flag to the release-blocking CI matrix.
- Fixed CMake export set to include cpp_httplib when HTTP is enabled.
- Fixed CI validation scripts for VERSION-file variable references and
  updated vcpkg overlay port path to `cxxmcp-sdk`.
- Added MinGW provisional compatibility policy documentation.

## 1.0.0

- Added explicit protocol-version coverage for the `2025-11-25` MCP snapshot
  and closed the P1 protocol model evidence gaps for required-field,
  type-constraint, and object-presence capability behavior.
- Added a lightweight default `WWW-Authenticate` parser to the optional
  `cxxmcp::auth` public surface, including support for quoted parameters,
  escaped strings, token68 payloads, case-insensitive parameter lookup, and
  focused auth tests.
- Changed the default in-memory token store to keep entries separated by the
  complete resource/issuer/client token key instead of overwriting unrelated
  credentials.
- Documented the external `cxxmcp-examples` auth-lite coverage and kept the
  examples path aligned with `Peer` / `Service` as the canonical SDK entry.

## 1.0.0 (pre-release)

- Reframed the public SDK around `Peer` / `Service` entry points.
- Kept `client` / `server` as compatibility and convenience wrappers.
- Added HTTP URI support and auth header support for client transport setup.
- Added HTTP auth-lite server behavior: authentication failures are reported as
  auth-category JSON-RPC errors and Streamable HTTP maps them to
  `401 Unauthorized` with configurable `WWW-Authenticate`.
- Added the optional `cxxmcp::auth` scaffold target for OAuth 2.1 / DPoP
  contracts and public header smoke coverage.
- Applied client HTTP bearer-token helpers consistently to Streamable HTTP POST,
  SSE GET, and session DELETE requests.
- Added `ClientPeer` and `ServerPeer` examples.
- Aligned `process_stdio_client` with the peer/service path.
- Routed native `ClientPeer` initialize, synchronous helpers, paginated helpers,
  outbound notifications, and service shutdown through Peer-owned transport
  paths.
- Added `ClientPeer` handler registration helpers so normal client callbacks no
  longer require accessing the underlying `client::Client`.
- Added `ServerPeer` handler registration helpers so normal server callbacks no
  longer require accessing the underlying `server::Server`.
- Marked `ClientPeer::client()` and `ServerPeer::server()` as deprecated
  compatibility escape hatches.
- Tightened process stdio response-id validation with stable transport errors.
- Covered process stdio server-to-client handler-error round trips on both
  concrete and role-generic transports.
- Covered stdio malformed-input and role-generic server close failure paths.
- Reject mismatched JSON response ids on the legacy HTTP client transport.
- Covered native Streamable HTTP client mismatched response ids and duplicate
  in-flight request ids with stable transport errors.
- Covered native process-stdio client mismatched response ids and duplicate
  in-flight request ids with stable transport errors.
- Return HTTP 400 for malformed Streamable HTTP POST bodies and cover the
  direct server transport path.
- Documented why concrete stdio transports do not own duplicate in-flight
  request-id validation.
- Moved native `ClientPeer` inbound request and notification dispatch onto
  Peer-owned roots and handler state.
- Moved native `ServerPeer` notification dispatch for Peer-registered
  notification handlers onto Peer-owned state.
- Routed `ServerPeer` ping request handling through the Peer boundary before
  falling back to the concrete server dispatcher.
- Routed `ServerPeer` initialize request validation and response construction
  through the Peer boundary before falling back to the concrete server
  dispatcher.
- Routed `ServerPeer` raw request override state plus `tools/list` and
  `tools/get` discovery handling through the Peer boundary.
- Routed non-task `tools/call` handling through the `ServerPeer` boundary with
  Peer-owned cancellation token propagation.
- Routed task-aware `tools/call` handling through the `ServerPeer` boundary by
  invoking the configured task manager directly.
- Routed `ServerPeer` prompt/resource discovery and read handling through the
  Peer boundary while preserving session context for handlers.
- Routed resource subscribe/unsubscribe handling through `ServerPeer`, using the
  native transport's compatibility adapter as the subscription identity.
- Routed Peer-registered completion, sampling, and logging request handlers
  through the `ServerPeer` boundary.
- Routed Peer-registered task lifecycle request handlers through the
  `ServerPeer` boundary.
- Added cancellation-aware client inbound handler overloads for roots,
  sampling, elicitation, and custom server-to-client requests on both
  `Client` and `ClientPeer`.
- Added `ServerBuilder::with_handler()` so aggregate and contract-style server
  handlers can be installed during construction instead of through a long
  mutable setter sequence.
- Added a dedicated SDK umbrella test for `cxxmcp/sdk.hpp`.
