# Auth Design

This document records the design decisions for OAuth 2.1 authorization support
in the cxxmcp Streamable HTTP transport layer.

For application-facing usage, see `docs/auth_user_guide.md`. This design note
focuses on ownership boundaries, dependency policy, and the next-stage OAuth
delivery plan.

## Feature Gate

OAuth protocol scaffolding is an **optional feature**, gated by CMake options:

```cmake
option(CXXMCP_ENABLE_AUTH "OAuth 2.1 / DPoP authorization scaffolding" OFF)
# Legacy alias accepted by the build:
option(MCP_ENABLE_AUTH "OAuth 2.1 / DPoP authorization scaffolding" OFF)
option(CXXMCP_ENABLE_OPENSSL "OpenSSL-backed optional integrations" OFF)
set(CXXMCP_AUTH_CRYPTO "NONE" CACHE STRING "NONE or OpenSSL")
```

- `OFF` (default): no optional `cxxmcp::auth` OAuth target is exported and no
  crypto dependency is pulled. The lightweight server auth contracts in
  `cxxmcp::server` remain available because HTTP auth policy has to integrate
  with normal server dispatch. Client HTTP bearer helpers and refresh-on-401
  hooks also remain available through `cxxmcp::client` / `cxxmcp::transport`.
- `ON`: compiles the `cxxmcp::auth` CMake target. This exposes
  transport-neutral auth contracts, metadata/token endpoint helpers, DPoP
  request-header builders, and JWKS value/cache boundaries without requiring
  OpenSSL or MiniOAuth2.
- `CXXMCP_ENABLE_OPENSSL=ON`: enables OpenSSL-backed HTTP/WebSocket TLS support
  without enabling auth by itself.
- `CXXMCP_AUTH_CRYPTO=OpenSSL`: requires `CXXMCP_ENABLE_AUTH=ON`, resolves
  OpenSSL through CMake/package-manager discovery, and exports
  `cxxmcp::auth_openssl` for crypto-backed helpers.

The feature gate is a packaging contract, not a runtime policy switch. Code
that needs OAuth metadata, lifecycle, token, registration, or
`WWW-Authenticate` parser contracts must link `cxxmcp::auth` explicitly.
Default `cxxmcp::sdk` consumers must not receive OpenSSL, optional auth
headers, or the `cxxmcp::auth` target by accident.

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
| DPoP (RFC 9449) | Header/payload JSON assembly, base64url, ECDSA/EdDSA signing and verification via OpenSSL-backed implementations behind `cxxmcp::auth`. | cxxmcp + OpenSSL |
| JWT / ID token validation | Verify signatures and claims via OpenSSL/JWKS-aware code. Decode-only helpers are intentionally not part of the public SDK. | cxxmcp + OpenSSL |

OpenSSL is the only binary/system dependency that the full implementation should
introduce when crypto-backed auth or transport TLS is enabled. The default auth
scaffold does not call `find_package(OpenSSL)`. Package-manager builds that
expose OpenSSL must resolve it through the same package-manager graph as the
rest of the dependencies. For plain CMake builds, CMake uses the user's
installed OpenSSL and may be guided with standard hints such as
`OPENSSL_ROOT_DIR` when the platform does not provide a default search path.

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
- installed-package smoke checks default builds do not install optional auth
  headers, while auth-enabled builds can consume `cxxmcp::auth` from a clean
  external CMake project;
- vcpkg builds resolve OpenSSL through the active vcpkg toolchain and port
  dependency metadata;
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
  only; it still does not require OpenSSL.
- `CXXMCP_ENABLE_OPENSSL=ON` explicitly enables OpenSSL-backed HTTP/WebSocket
  TLS support for bundled builds; package-manager builds resolve the matching
  `cpp-httplib` TLS feature through the package manager.
- `CXXMCP_AUTH_CRYPTO=OpenSSL` explicitly enables OpenSSL-backed auth helper
  surfaces when `CXXMCP_ENABLE_AUTH=ON`.
- OAuth/DPoP and first-party HTTPS/WSS support must add OpenSSL through normal
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
├── jwks.hpp                  JWKS parser, selector, cache, and fetch contracts
├── www_auth.hpp              WWW-Authenticate header parser
├── token.hpp                 Token model and refresh rotation logic
├── lifecycle.hpp             OAuth lifecycle stores, state machine, and
│                              token endpoint boundaries
├── registration.hpp          DCR + Client ID Metadata Document models and
│                              client_id selection lifecycle boundaries
└── http_metadata_endpoint.hpp
                               default metadata JSON parser over injected HTTP GET
include/cxxmcp/auth/openssl/  (enabled by cxxmcp::auth_openssl)
└── sha256.hpp                 OpenSSL-backed SHA-256/base64url helpers

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
- JWT verifier boundary for access tokens, ID tokens, client assertions, and
  DPoP proofs; public auth APIs expose verification contracts, not decode-only
  parsing helpers
- RFC 9728 protected resource metadata discovery models
- RFC 8414 authorization server metadata models
- WWW-Authenticate `resource_metadata` and `error=insufficient_scope`
  challenge helpers
- Dynamic Client Registration (RFC 7591) model, endpoint boundary, request
  defaults, and response-to-client-config normalization
- Client ID Metadata Document model, HTTPS URL validation, support-flag
  detection, and RMCP-style client_id selection fallback to DCR
- Credential storage abstraction (`CredentialStore`) and authorization state
  storage abstraction (`StateStore`) with in-memory defaults
- Transport-neutral metadata fetch boundary (`OAuthMetadataEndpoint`) plus
  protected-resource and authorization-server metadata URL planning helpers
- Default `HttpOAuthMetadataEndpoint` implementation that parses RFC 9728 /
  RFC 8414 JSON over an injected HTTP GET callback without exposing a concrete
  HTTP library
- Default `HttpOAuthTokenEndpoint` implementation that constructs
  `application/x-www-form-urlencoded` authorization-code and refresh-token
  requests, parses JSON token responses, and leaves concrete HTTP/TLS transport
  ownership to the application or package-integrated adapter
- Authorization URL construction from caller-supplied PKCE values
- Authorization-code exchange and refresh-token endpoint interfaces
- `AuthorizationManager` lifecycle state for
  unauthorized -> authorization pending -> authorized transitions
- RMCP-style scope selection order, scope upgrade policy, and
  `WWW-Authenticate` response analysis for `insufficient_scope`
- Token refresh rotation boundaries (OAuth 2.1 requirement)
- Token storage abstraction (`TokenStore` interface, default in-memory impl)

Browser/loopback UX, credential persistence, and concrete HTTP JWKS retrieval
remain application-owned or future package-integration work. Public headers must
keep these interfaces C++17-compatible while those integrations evolve.

The current lightweight resource-metadata integration point is deliberately
small and header-only: `cxxmcp/auth/www_auth.hpp` exposes stable parameter
constants, a default `WWW-Authenticate` parser, and helpers for extracting
`resource_metadata` and `insufficient_scope` from parsed challenges, while
`cxxmcp/auth/metadata.hpp` owns the RFC 9728 / RFC 8414 value models.
`cxxmcp/auth/http_metadata_endpoint.hpp` provides the default SDK metadata
endpoint parser over an injected HTTP GET function, and
`cxxmcp/auth/http_token_endpoint.hpp` provides the default token endpoint form
encoder / JSON parser over an injected HTTP POST function, so applications can
reuse cxxmcp's discovery and token lifecycle parsing without exposing
cpp-httplib or another HTTP stack in public auth APIs.
`cxxmcp/auth/token.hpp` exposes the token model plus an in-memory token store
that separates entries by the full
resource/issuer/client key. `cxxmcp/auth/lifecycle.hpp` exposes the public
OAuth lifecycle scaffold: applications can plug in credential/state stores and
a token endpoint or metadata endpoint implementation, while cxxmcp owns
metadata URL planning, protected-resource and authorization-server discovery
execution over that endpoint boundary, RMCP-style scope selection,
authorization URL assembly, one-time state consumption, credential persistence,
refresh-token rotation bookkeeping, access-token refresh decisions, and
scope-upgrade URL generation. `cxxmcp/auth/registration.hpp` owns the DCR and
Client ID Metadata Document lifecycle boundary, including request defaults,
empty-secret normalization, URL client_id validation, and fallback from CIMD to
DCR when the authorization server does not advertise support. The scaffold does
not include fake crypto, decode-only JWT helpers, or browser/loopback behavior.
Concrete JWT/DPoP verification must remain behind `cxxmcp::auth` and be backed
by OpenSSL/JWKS-aware implementation code before it is shipped as a first-party
provider. Deeper refresh-on-401 OAuth orchestration remains separate
implementation work.

## Current Supported Runtime Behavior

The SDK currently supports an auth-lite runtime surface without full OAuth
execution:

- Server applications install `mcp::server::AuthProvider` to authenticate
  transport headers and remote metadata.
- `mcp::server::StaticBearerAuthProvider` covers static bearer-token validation
  for small embedded deployments and tests without enabling the optional
  `cxxmcp::auth` target.
- Successful authentication writes `mcp::server::AuthIdentity` into
  `SessionContext::auth_identity` before handlers run.
- Auth failures built with `mcp::server::make_auth_error()` use the auth error
  category so HTTP transports can map them to `401 Unauthorized`.
- `HttpTransportOptions::auth_challenge` controls the emitted
  `WWW-Authenticate` value and defaults to `Bearer`.
- Client HTTP transports treat `auth_header` as a raw bearer token and emit
  `Authorization: Bearer <token>`.
- An explicit `Authorization` entry in the custom header map has priority over
  the bearer helper.
- Client HTTP transports expose an application-owned refresh hook for a single
  retry after `401 Unauthorized`; the hook receives status, headers, method,
  and the first `WWW-Authenticate` value.
- With the optional auth target enabled, `DefaultWwwAuthenticateParser` parses
  challenges and exposes MCP OAuth helpers for `resource_metadata` and
  `insufficient_scope`.
- With the optional auth target enabled, `HttpOAuthMetadataEndpoint` and
  `HttpOAuthTokenEndpoint` provide SDK-owned metadata/token parsing over
  injected HTTP callbacks without making auth depend on a specific HTTP stack.
- With the optional auth target enabled,
  `AuthorizationManager::refresh_after_unauthorized_response()` converts a
  `401 WWW-Authenticate` response into a refreshed bearer token that an HTTP
  transport hook can use for its one-shot retry.
- With the optional auth target enabled,
  `<cxxmcp/auth/server_auth_provider.hpp>` provides
  `DpopBearerAuthProvider`, a server-side bridge over injected JWT/DPoP
  verifiers, replay cache, and access-token hash function.
- With the optional auth target enabled,
  `<cxxmcp/auth/client_orchestrator.hpp>` provides
  `OAuthClientFlowBuilder`, an owning assembly helper for the common browser +
  PKCE authorization-code client flow over injected callback, metadata, token,
  PKCE, and optional registration endpoints.
- With the optional auth target enabled, DPoP request-header builders produce
  `DPoP` and `Authorization: DPoP ...` headers over an injected `DpopSigner`
  and transport-neutral HTTP method/URL target.
- With the optional auth target enabled, JWKS models, parsing, selection,
  fetch, and cache contracts are available without treating parsed keys as
  verified tokens. JWT verification is supplied only by an explicit crypto
  backend such as `cxxmcp::auth_openssl`.
- With `CXXMCP_AUTH_CRYPTO=OpenSSL`, `cxxmcp::auth_openssl` exposes
  OpenSSL-backed SHA-256/base64url helpers, JOSE compact JWS parsing
  primitives, public JWK import, RS256/ES256 compact JWS signature
  verification, trusted in-memory JWKS JWT validation, and DPoP access-token
  hash construction for `ath`. `StaticJwksJwtVerifier` selects keys by
  `kid`/`alg`, enforces `use=sig` and `key_ops=verify` when present, and
  validates issuer, audience, expiry, not-before, issued-at, and required
  claims. `OpenSslDpopSigner` and `OpenSslDpopVerifier` sign and verify
  ES256/RS256 DPoP proof JWTs over PEM private keys and embedded public JWKs,
  including `htm`/`htu`, `ath`, `nonce`, and replay-cache-compatible `jti`
  extraction. `FetchingJwksJwtVerifier` adds transport-neutral JWKS fetch/cache
  policy over injected endpoint/cache contracts, including one refresh on key
  or signature failures for key rotation. `StaticJwksDpopBearerAuthProvider`
  and `FetchingJwksDpopBearerAuthProvider` provide server-side presets in a
  separate header so applications opt into the server dependency explicitly.

These behaviors are stable enough for user documentation and package smoke
coverage. They do not imply that cxxmcp owns token issuance, browser UX,
persistent storage, concrete remote JWKS HTTP transport, OAuth discovery policy,
or a fully automatic HTTP-client-owned OAuth runtime today.

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
an `AuthProvider` subclass by the application or by the optional
`DpopBearerAuthProvider` bridge when concrete verifier implementations are
supplied.

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
- Client HTTP transports expose a transport-level auth refresh hook for
  `401 Unauthorized`: the hook receives status, headers, method, and
  `WWW-Authenticate`, may return a replacement bearer token, and the transport
  retries the failed POST once. A final `401` or `403` is surfaced as an
  auth-category `PermissionDenied` error carrying the `WWW-Authenticate`
  detail when present.

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

The full crypto-backed auth implementation should ship behind the opt-in auth
feature as a coherent deliverable:

- Vendored MiniOAuth2 integration under `third_party/MiniOAuth2`, private to
  the auth implementation.
- Protocol models and implementations for PKCE, DPoP, RFC 9728/8414 metadata,
  client registration, token refresh, JWKS discovery/cache, and JWKS-aware
  verification boundaries.
- HTTP transport natively integrates auth lifecycle:
  discovery -> authorize -> token exchange -> refresh-on-401 -> retry.
- OpenSSL/JWKS-backed verifier implementations are provided for the optional
  DPoP-aware server `AuthProvider` bridge, building on the existing injected
  verifier shape.
- TokenStore abstraction with in-memory default (applications may provide
  persistent storage by implementing the interface).
- OpenSSL-backed DPoP/JWT/JWKS code is compiled only when the auth feature is
  enabled, resolves OpenSSL through the active package manager or system
  install, and never vendors OpenSSL into `third_party`.
- Default SDK/package installs continue to prove that optional auth headers are
  absent and OpenSSL is not pulled unless the consumer opts in.

No incremental rollout of fake-security pieces: decode-only JWT helpers,
non-verifying DPoP paths, and placeholder JWKS checks must not be shipped as
security features.
