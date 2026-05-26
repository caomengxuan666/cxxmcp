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
- `ON`: compiles the `cxxmcp::auth` CMake target. Requires OpenSSL (already a
  transitive dependency when `MCP_ENABLE_HTTP=ON` with TLS).

## Dependency Decision: Zero New Libraries

OAuth requires three categories of work. The following table records why no
external OAuth or JWT library is needed.

| Category | Approach | External library |
|---|---|---|
| PKCE (RFC 7636) | ~80 lines: 32-byte random, SHA-256, base64url. | None (OpenSSL for SHA-256) |
| DPoP (RFC 9449) | ~150 lines: header/payload JSON assembly, base64url, ECDSA/EdDSA sign via OpenSSL. | None |
| RFC 9728/8414 metadata | ~120 lines: typed structs + from-json, same pattern as every existing `cxxmcp::protocol` model. | None |
| WWW-Authenticate parsing | ~60 lines: header key-value parser. | None |
| Token model + refresh rotation | ~100 lines: access/refresh token structs, expiry tracking, refresh-on-401 hook. | None |
| JWT / ID token validation | ~80 lines: decode payload, verify signature via OpenSSL. | None (`jwt-cpp` is not required) |

All crypto primitives (SHA-256, ECDSA P-256, Ed25519, Ed448) are provided by
OpenSSL, which is already a transitive dependency of cpp-httplib when TLS is
enabled. **No new runtime dependency is introduced by enabling OAuth.**

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

Transport integration           (I/O via existing transport interfaces)
├── Discovery (GET /.well-known/...) → via transport::Transport<> abstraction
└── Token endpoint (POST)           → via transport::Transport<> abstraction
```

Auth code does not include cpp-httplib headers directly. All HTTP calls go
through the existing `transport::Transport<>` interface, ensuring the auth layer
survives a future HTTP backend replacement.

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

## Delivery

When `MCP_ENABLE_AUTH=ON`, the following ship together as a single deliverable:

- Protocol models: PKCE, DPoP, RFC 9728/8414 metadata, client registration
- HTTP transport natively integrates auth lifecycle:
  discovery → authorize → token exchange → refresh-on-401 → retry
- A default DPoP-aware Bearer token verifier is provided as an `AuthProvider`
  implementation for server-side deployments.
- TokenStore abstraction with in-memory default (applications may provide
  persistent storage by implementing the interface).

No incremental rollout; no manual wiring phase for users.
