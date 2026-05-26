# Auth Design

This document records the design decisions for OAuth 2.1 authorization support
in the cxxmcp Streamable HTTP transport layer.

## Feature Gate

OAuth support is an **optional feature**, gated by a CMake option:

```cmake
option(MCP_ENABLE_AUTH "OAuth 2.1 / DPoP authorization support" OFF)
```

- `OFF` (default): zero auth code compiled, zero crypto dependency pulled.
  Suitable for stdio-only consumers and non-authenticated HTTP deployments.
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
transport. The `cxxmcp::auth` target covers:

- PKCE S256 code challenge generation and verification
- DPoP bound token proof construction (asymmetric key pair + signed JWT)
- RFC 9728 protected resource metadata discovery
- RFC 8414 authorization server metadata parsing
- WWW-Authenticate `resource_metadata` and `error=insufficient_scope` parsing
- Dynamic Client Registration (RFC 7591) model
- Client ID Metadata Document model
- Token refresh rotation (OAuth 2.1 requirement)
- Token storage abstraction (`TokenStore` interface, default in-memory impl)

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

The `AuthProvider` is called before request dispatch and is compatible with
both Bearer token validation and any custom auth scheme. OAuth token validation
(DPoP proof verification, audience check, scope check) can be implemented as an
`AuthProvider` subclass by the application or by a future built-in provider.

OAuth-capable HTTP transports must also map authentication failures at the HTTP
layer before JSON-RPC dispatch:

- missing or invalid credentials return `401 Unauthorized` with an appropriate
  `WWW-Authenticate` challenge;
- authenticated credentials with insufficient scope return the MCP/OAuth
  insufficient-scope shape, including a challenge when required by the HTTP
  flow;
- successful authentication stores `AuthIdentity` in the request/session context
  so typed handlers and policy hooks can inspect the subject and claims;
- `AuthRequest` must include normalized HTTP headers, not only the remote
  address, because Bearer and DPoP validation are header-driven.

The current P1 auth-lite implementation exposes this behavior without pulling
crypto dependencies:

- `AuthProvider::authenticate()` failures are encoded as auth-category
  JSON-RPC `PermissionDenied` errors.
- `HttpTransport` maps those auth-category failures to HTTP `401` responses.
- `HttpTransportOptions::auth_challenge` controls the emitted
  `WWW-Authenticate` value and defaults to `Bearer`.
- Successful authentication stores `AuthIdentity` in `SessionContext` before
  typed tool, prompt, and resource handlers run.

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
