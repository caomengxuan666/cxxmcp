# Auth Design

This document records the design decisions for OAuth 2.1 authorization support
in the cxxmcp Streamable HTTP transport layer.

## Feature Gate

OAuth protocol scaffolding is an **optional feature**, gated by CMake options:

```cmake
option(CXXMCP_ENABLE_AUTH "OAuth 2.1 / DPoP authorization scaffolding" OFF)
# Legacy alias accepted by the build:
option(MCP_ENABLE_AUTH "OAuth 2.1 / DPoP authorization scaffolding" OFF)
```

- `OFF` (default): no optional `cxxmcp::auth` OAuth target is exported and no
  crypto dependency is pulled. The lightweight server auth contracts in
  `cxxmcp::server` remain available because HTTP auth policy has to integrate
  with normal server dispatch.
- `ON`: compiles the `cxxmcp::auth` CMake target. The current scaffold exposes
  transport-neutral auth contracts without requiring OpenSSL or MiniOAuth2.
  The full OAuth implementation will add MiniOAuth2 and OpenSSL only behind
  this opt-in feature gate.

## Dependency Decision: Planned Vendored MiniOAuth2

The full OAuth implementation should vendor MiniOAuth2 under
`third_party/MiniOAuth2`. MiniOAuth2 is a header-only C++17 OAuth helper focused
on the Authorization Code Flow with PKCE. This matches the SDK's public C++17
baseline and avoids raising `CXXMCP_SDK_CXX_STANDARD` for auth-enabled builds.

MiniOAuth2 is an implementation detail of `cxxmcp::auth`. Public cxxmcp headers
must not expose MiniOAuth2 types, namespaces, macros, or include paths. This
keeps the public SDK ABI/API stable if the OAuth helper is replaced later.

When MiniOAuth2 is added, the vendored copy is used in library mode only:

- do not build MiniOAuth2 examples, tests, Crow integration, or Google auth
  sample targets from the cxxmcp build;
- do not let MiniOAuth2 fetch Crow, cpp-httplib, GoogleTest, or another
  nlohmann-json copy;
- use cxxmcp's existing JSON dependency and protocol model style for MCP-facing
  metadata, token, and challenge structures;
- keep MiniOAuth2 usage limited to PKCE, URL/form helper utilities, and
  authorization-code/token request construction where it provides value.

OAuth still requires cxxmcp-owned protocol and security code around that helper:

| Category | Approach | External library |
|---|---|---|
| PKCE (RFC 7636) | Use MiniOAuth2 for verifier/challenge and request helper utilities, wrapped by cxxmcp API. | MiniOAuth2 |
| Authorization code + token request helpers | Use MiniOAuth2 where it avoids duplicating URL/form encoding and OAuth parameter assembly. | MiniOAuth2 |
| RFC 9728/8414 metadata | Typed structs + from-json, same pattern as existing `cxxmcp::protocol` models. | cxxmcp |
| WWW-Authenticate parsing | Header key-value parser owned by cxxmcp so MCP-specific challenge behavior is explicit. | cxxmcp |
| Token model + refresh rotation | Access/refresh token structs, expiry tracking, refresh-on-401 hook. | cxxmcp |
| DPoP (RFC 9449) | Header/payload JSON assembly, base64url, ECDSA/EdDSA signing and verification via OpenSSL. | cxxmcp + OpenSSL |
| JWT / ID token validation | Decode and verify signatures/claims via OpenSSL/JWKS-aware code. MiniOAuth2 JWT parsing is not sufficient because it does not validate signatures or claims. | cxxmcp + OpenSSL |

OpenSSL is the only binary/system dependency that the full implementation should
introduce when crypto-backed auth is enabled. The initial scaffold does not call
`find_package(OpenSSL)`. For vcpkg and Conan builds, OpenSSL must come from the
same package-manager resolution as the rest of the dependency graph. For plain
CMake builds, CMake uses the user's installed OpenSSL and may be guided with
standard hints such as `OPENSSL_ROOT_DIR` when the platform does not provide a
default search path.

Full auth-enabled package builds must keep OpenSSL as a normal package
dependency, not as vendored source. MiniOAuth2 is vendored because it is
header-only, small, C++17-compatible, and directly tied to the SDK auth
implementation.

## Build And Packaging Contract

Auth support must preserve the existing SDK packaging behavior:

- public SDK headers and package targets remain C++17-compatible;
- `MCP_ENABLE_AUTH=OFF` builds do not include `third_party/MiniOAuth2`, do not
  call `find_package(OpenSSL)`, and do not export auth targets;
- `MCP_ENABLE_AUTH=ON` builds add `cxxmcp::auth` and install/export the auth
  contract headers with the rest of the SDK package;
- vcpkg builds resolve OpenSSL through the active vcpkg toolchain and port
  dependency metadata;
- Conan builds resolve OpenSSL through the Conan dependency graph and generated
  CMake toolchain files;
- the full crypto-backed implementation uses `find_package(OpenSSL REQUIRED)`
  against the active package manager or user's local OpenSSL installation, with
  standard CMake hints such as `OPENSSL_ROOT_DIR` available for non-default
  layouts;
- OpenSSL must not be copied into `third_party`;
- MiniOAuth2 must be copied into `third_party/MiniOAuth2` and treated like other
  vendored header-only source dependencies.

The implementation should prefer target-local include directories and compile
definitions. MiniOAuth2 include paths and feature macros should be private to
`mcp_auth` or private implementation files in `mcp_client` / `mcp_server`.
Consumers should see only `cxxmcp/auth/*.hpp` and the `cxxmcp::auth` target.

## TLS / HTTPS Build Requirements

Streamable HTTP auth is designed for HTTPS in real deployments. The current
default SDK build does not force a TLS backend because many consumers use
stdio, local loopback HTTP, or a TLS-terminating reverse proxy. HTTPS endpoint
URIs are accepted by the public HTTP transport options, but cpp-httplib can
only open HTTPS connections when it is compiled with a TLS backend such as
OpenSSL (`CPPHTTPLIB_OPENSSL_SUPPORT` / `CPPHTTPLIB_SSL_ENABLED`) and linked to
the matching crypto libraries.

The auth scaffold therefore keeps these requirements explicit:

- `CXXMCP_ENABLE_AUTH=OFF` does not call `find_package(OpenSSL)` and does not
  compile crypto-backed auth code.
- `CXXMCP_ENABLE_AUTH=ON` currently exposes transport-neutral auth contracts
  only; it still does not require OpenSSL until the full OAuth/DPoP
  implementation lands.
- OAuth/DPoP and first-party HTTPS support must add OpenSSL through normal
  package-manager resolution, not by vendoring OpenSSL into `third_party`.
- Applications may place cxxmcp behind a reverse proxy that terminates TLS; in
  that mode the SDK still receives and forwards normal HTTP headers, including
  `Authorization`, `DPoP`, and `WWW-Authenticate`.

## Architecture: Network-Library-Agnostic

The auth layer is split into a pure-protocol core and transport I/O:

```
include/cxxmcp/auth/          (pure protocol, no network I/O)
├── pkce.hpp                  PKCE code_verifier / code_challenge
├── dpop.hpp                  DPoP proof construction and verification
├── metadata.hpp              RFC 9728 + RFC 8414 models
├── www_auth.hpp              WWW-Authenticate header parser
├── token.hpp                 Token model and refresh rotation logic
└── registration.hpp          DCR + Client ID Metadata Document models

third_party/MiniOAuth2/       Planned vendored C++17 OAuth helper; private include

Transport integration           (I/O via existing transport interfaces)
├── Discovery (GET /.well-known/...) → via transport::Transport<> abstraction
└── Token endpoint (POST)           → via transport::Transport<> abstraction
```

Auth code does not include cpp-httplib headers directly. All HTTP calls go
through the existing `transport::Transport<>` interface, ensuring the auth layer
survives a future HTTP backend replacement. MiniOAuth2 is not used as an HTTP
client abstraction; it supplies OAuth request/PKCE helper logic only.

## MCP OAuth Scope

The MCP 2025-11-25 spec mandates OAuth 2.1 with PKCE for Streamable HTTP
transport. The current `cxxmcp::auth` scaffold defines public contracts for:

- PKCE S256 code challenge generation and verification boundaries
- DPoP bound token proof construction and verification boundaries
- RFC 9728 protected resource metadata discovery models
- RFC 8414 authorization server metadata models
- WWW-Authenticate `resource_metadata` and `error=insufficient_scope`
  challenge helpers
- Dynamic Client Registration (RFC 7591) model
- Client ID Metadata Document model
- Token refresh rotation boundaries (OAuth 2.1 requirement)
- Token storage abstraction (`TokenStore` interface, default in-memory impl)

Default parser, PKCE, DPoP, discovery, token exchange, refresh-on-401, and
OpenSSL-backed verifier implementations are still planned work. Public headers
must keep these interfaces C++17-compatible while those implementations land.

The current lightweight resource-metadata integration point is deliberately
small and header-only: `cxxmcp/auth/www_auth.hpp` exposes stable parameter
constants plus helpers for extracting `resource_metadata` and
`insufficient_scope` from parsed `WWW-Authenticate` challenges, while
`cxxmcp/auth/metadata.hpp` owns the RFC 9728 / RFC 8414 value models. Actual
HTTP discovery and token exchange remain a later `cxxmcp::auth`
implementation detail routed through SDK transport boundaries.

### Intentional Non-Goals (belong in application code)

- Opening a browser or starting a localhost loopback redirect receiver
- Persistent token storage (keychain, TPM, encrypted file)
- OAuth authorization server implementation
- Consent screen / user interaction UI
- DCR registration caching policy

These concerns are platform-specific or UX-domain and belong outside the SDK
contract.

## Server-Side Auth

Server-side authentication extension points already exist in
`sdk/server/include/cxxmcp/server/auth.hpp`:

- `AuthRequest` — transport-neutral auth input (headers + remote address)
- `AuthIdentity` — authenticated principal and claims
- `AuthProvider` — abstract interface, applications implement `authenticate()`

The concrete `server::Server` dispatcher and canonical `ServerPeer` native
dispatch call `AuthProvider` before request dispatch and are compatible with
both Bearer token validation and any custom auth scheme. OAuth token validation
(DPoP proof verification, audience check, scope check) can be implemented as
an `AuthProvider` subclass by the application or by a future built-in provider.

OAuth-capable HTTP transports must also map authentication failures at the HTTP
layer before JSON-RPC dispatch:

- missing or invalid credentials return `401 Unauthorized` with an appropriate
  `WWW-Authenticate` challenge;
- authenticated credentials with insufficient scope return the MCP/OAuth
  insufficient-scope shape, including a challenge when required by the HTTP
  flow;
- successful authentication stores `AuthIdentity` in the request/session context
  so typed handlers and policy hooks can inspect the subject and claims;
- `AuthRequest` must include HTTP headers, not only the remote address, because
  Bearer and DPoP validation are header-driven. Header normalization and
  duplicate-header policy are still planned hardening work.

The current P1 auth-lite implementation exposes this behavior without pulling
crypto dependencies:

- `AuthProvider::authenticate()` failures are encoded as auth-category
  JSON-RPC `PermissionDenied` errors.
- `HttpTransport` maps those auth-category failures to HTTP `401` responses.
- `HttpTransportOptions::auth_challenge` controls the emitted
  `WWW-Authenticate` value and defaults to `Bearer`.
- Successful authentication stores `AuthIdentity` in `SessionContext` before
  typed tool, prompt, and resource handlers run.
- `Server::authenticate_context()` is shared by concrete `Server` dispatch and
  native `ServerPeer` request dispatch, so peer-boundary handlers receive the
  same authenticated context as concrete server handlers.
- Client HTTP transports apply `auth_header` as `Authorization: Bearer <token>`
  on POST, SSE GET, and session DELETE requests unless an explicit
  `Authorization` entry already exists in the custom header map.

## Client-Side Bearer Token Helper

`mcp::client::HttpTransportOptions::auth_header` and
`mcp::transport::StreamableHttpClientTransportOptions::auth_header` are bearer
token helpers, not raw header values. When set to a non-empty token, the client
transport sends `Authorization: Bearer <token>` on every outbound Streamable
HTTP request:

- JSON-RPC POST requests and notifications;
- SSE GET requests used for server-to-client messages;
- POSTed responses to server-to-client requests;
- session DELETE during transport shutdown.

If `headers` already contains an explicit `Authorization` value, the explicit
header wins. This keeps custom schemes and preformatted DPoP/Bearer experiments
possible without changing the helper contract.

## Delivery

The full crypto-backed auth implementation should ship the following together
as a single deliverable:

- Vendored MiniOAuth2 integration under `third_party/MiniOAuth2`, private to
  the auth implementation
- Protocol models: PKCE wrappers, DPoP, RFC 9728/8414 metadata, client
  registration
- HTTP transport natively integrates auth lifecycle:
  discovery → authorize → token exchange → refresh-on-401 → retry
- A default DPoP-aware Bearer token verifier is provided as an `AuthProvider`
  implementation for server-side deployments.
- TokenStore abstraction with in-memory default (applications may provide
  persistent storage by implementing the interface).

No incremental rollout; no manual wiring phase for users.
