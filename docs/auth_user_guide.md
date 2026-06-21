# Auth User Guide

This guide covers the authentication behavior available today in cxxmcp. The
default SDK path provides server-side auth hooks and client bearer-token
helpers without enabling the optional OAuth target. The optional `cxxmcp::auth`
target adds transport-neutral OAuth metadata, lifecycle, token, registration,
injected HTTP metadata/token endpoint helpers, and `WWW-Authenticate` parsing
contracts. Crypto-backed JOSE/JWK/JWS/JWT and DPoP helpers are available only
when the auth target is built with `CXXMCP_AUTH_CRYPTO=OpenSSL`; default SDK
consumers still do not pull OpenSSL or optional auth headers.

## Build Options

Auth is split into two layers:

- `cxxmcp::server` always includes the lightweight server auth contracts:
  `AuthRequest`, `AuthIdentity`, and `AuthProvider`.
- `cxxmcp::client` and `cxxmcp::transport` include bearer-token helpers and
  refresh-on-401 hooks for HTTP transports.
- `cxxmcp::auth` is optional. Enable it with `CXXMCP_ENABLE_AUTH=ON` or the
  legacy alias `MCP_ENABLE_AUTH=ON` when you need OAuth metadata/lifecycle
  scaffolding, injected HTTP token/metadata endpoint helpers, or the
  `WWW-Authenticate` parser.

The default build keeps `CXXMCP_ENABLE_AUTH=OFF`. In that mode no
`cxxmcp::auth` target is exported, optional auth headers are not installed, and
OpenSSL is not required.

Enable the optional auth target in CMake:

```cmake
cmake -S . -B build-auth -DCXXMCP_ENABLE_AUTH=ON
cmake --build build-auth
```

Installed-package consumers link the optional target explicitly:

```cmake
find_package(cxxmcp CONFIG REQUIRED)
target_link_libraries(app PRIVATE cxxmcp::sdk cxxmcp::auth)
```

## Server Authentication

Servers authenticate requests by installing an `mcp::server::AuthProvider`.
The provider receives an `AuthRequest` containing transport headers and a
best-effort remote address, and returns an `AuthIdentity` on success.

For small embedded deployments or tests, use the built-in static bearer
provider:

```cpp
auto provider = std::make_unique<mcp::server::StaticBearerAuthProvider>();
provider->add_token(
    "expected-token",
    mcp::server::AuthIdentity{
        .subject = "service-account",
        .claims = {{"scope", "tools:read"}},
    });
server.set_auth_provider(std::move(provider));
```

For OAuth, DPoP-bound tokens, remote introspection, or tenant-specific policy,
install a custom provider:

```cpp
#include <cxxmcp/server/auth.hpp>
#include <cxxmcp/server/server.hpp>

class HeaderAuthProvider final : public mcp::server::AuthProvider {
 public:
  mcp::core::Result<mcp::server::AuthIdentity> authenticate(
      const mcp::server::AuthRequest& request) override {
    const auto authorization = request.headers.find("Authorization");
    if (authorization == request.headers.end() ||
        authorization->second != "Bearer expected-token") {
      return std::unexpected(
          mcp::server::make_auth_error("missing or invalid bearer token"));
    }

    return mcp::server::AuthIdentity{
        .subject = "service-account",
        .claims = {{"scope", "tools:read"}},
    };
  }
};

mcp::server::Server server;
server.set_auth_provider(std::make_unique<HeaderAuthProvider>());
```

When `cxxmcp::auth` is enabled, server deployments can also include
`<cxxmcp/auth/server_auth_provider.hpp>` and use
`mcp::auth::DpopBearerAuthProvider` as a bridge from injected token/proof
verifiers to `mcp::server::AuthProvider`. The bridge still does not decode JWTs
or verify signatures itself; it requires application-owned or future
OpenSSL-backed `JwtVerifier`, `DpopVerifier`, replay-cache, and access-token
hash implementations.

Authentication happens before request dispatch. If authentication succeeds, the
returned identity is stored in `SessionContext::auth_identity`; typed handlers,
contract-style handlers, direct server handlers, and native `ServerPeer`
dispatch can inspect it.

```cpp
server.tools().add(
    mcp::protocol::ToolDefinition{
        .name = "whoami",
        .input_schema = mcp::protocol::Json::object(),
    },
    [](const mcp::server::ToolContext& context)
        -> mcp::core::Result<mcp::protocol::ToolResult> {
      if (!context.auth_identity.has_value()) {
        return std::unexpected(
            mcp::server::make_auth_error("authentication required"));
      }

      return mcp::protocol::ToolResult::text(context.auth_identity->subject);
    });
```

For Streamable HTTP, auth failures produced with
`mcp::server::make_auth_error()` are mapped to `401 Unauthorized`.
`HttpTransportOptions::auth_challenge` controls the emitted
`WWW-Authenticate` value and defaults to `Bearer`. Set it to an empty string
only when another layer is responsible for challenges.

## Client Bearer Helper

HTTP client transports expose `auth_header` as a raw bearer token helper. Set
the token only, not the full `Authorization` header value:

```cpp
mcp::client::HttpTransportOptions options;
options.uri = "https://mcp.example.com/mcp";
options.auth_header = "access-token";
```

The transport sends `Authorization: Bearer <token>` on Streamable HTTP POST
requests, SSE GET requests, POSTed responses to server-to-client requests, and
session DELETE requests. Empty tokens are ignored.

The same behavior is available on the role-generic transport through
`mcp::transport::StreamableHttpClientTransportOptions::auth_header`.

## Authorization Header Precedence

An explicit `Authorization` entry in the custom header map wins over
`auth_header`. Use this when you need a preformatted value, a custom scheme, or
an experimental DPoP-style header layout:

```cpp
mcp::client::HttpTransportOptions options;
options.uri = "https://mcp.example.com/mcp";
options.auth_header = "ignored-token";
options.headers["Authorization"] = "Bearer externally-managed-token";
```

The transport does not duplicate the header or overwrite the explicit value.

## Refresh On 401

Client HTTP transports can call an application hook once after receiving
`401 Unauthorized`. The hook receives the status code, request method, response
headers, and the first `WWW-Authenticate` value when present. Returning a new
raw bearer token updates `auth_header` and retries the failed POST once.
Returning `std::nullopt` leaves the original failure intact.

```cpp
mcp::client::HttpTransportOptions options;
options.uri = "https://mcp.example.com/mcp";
options.auth_header = "expired-token";
options.auth_refresh_handler =
    [](const mcp::client::HttpAuthChallenge& challenge)
        -> std::optional<std::string> {
      if (challenge.status_code != 401) {
        return std::nullopt;
      }

      // Call application-owned token refresh code here.
      return "fresh-token";
    };
```

This hook is deliberately a transport boundary. The HTTP transport does not
perform OAuth discovery, browser interaction, token exchange, DPoP signing, or
JWT validation by itself. Applications that enable `cxxmcp::auth` can use
`AuthorizationManager` plus `HttpOAuthMetadataEndpoint` and
`HttpOAuthTokenEndpoint` behind this hook, while still owning the concrete HTTP
client and user interaction.

The same refresh callback is also exposed on
`client::Client::StreamableHttpEndpoint::auth_refresh_handler` and
`Peer::builder().auth_refresh_handler(...)`.

For the common one-shot retry case, the auth layer provides a transport-neutral
bridge:

```cpp
mcp::auth::HttpResponseMetadata response;
response.status_code = challenge.status_code;
response.headers = challenge.headers;

auto retry = manager.refresh_after_unauthorized_response(response);
if (retry.has_value() && retry->should_retry) {
  return retry->bearer_token;
}
return std::nullopt;
```

The helper only decides and refreshes; the HTTP transport still owns the actual
retry and keeps it to one attempt.

## DPoP Request Headers

`cxxmcp::auth` also provides transport-neutral DPoP header builders over the
`DpopSigner` interface:

```cpp
mcp::auth::DpopAuthorizationRequest request;
request.target.method = "POST";
request.target.url = "https://mcp.example.com/mcp";
request.access_token = "access-token";
request.key = key;

auto headers = mcp::auth::build_dpop_authorization_headers(signer, request);
```

The signer owns proof construction and cryptographic signing. The helper only
validates the method/URL target and packages the returned proof into `DPoP` and
`Authorization: DPoP ...` headers.

## JWKS Boundary

`cxxmcp::auth` exposes JWKS value models and selection helpers for future
JWT/JWKS verification:

```cpp
auto jwks = mcp::auth::parse_json_web_key_set(json);
mcp::auth::JwkSelectionCriteria criteria;
criteria.key_id = "kid-from-jwt-header";
criteria.algorithm = "ES256";
auto key = mcp::auth::select_json_web_key(*jwks, criteria);
```

This is only key discovery and selection. Signature verification, key trust,
issuer/audience checks, and token expiry validation remain the responsibility
of `JwtVerifier` implementations.

## WWW-Authenticate Parsing

When `cxxmcp::auth` is enabled, applications can parse authentication
challenges with the SDK-owned parser:

```cpp
#include <cxxmcp/auth/www_auth.hpp>

auto parsed = mcp::auth::parse_www_authenticate(
    R"(Bearer resource_metadata="https://mcp.example.com/.well-known/oauth-protected-resource", scope="tools:read")");
if (parsed.has_value()) {
  auto metadata_url = mcp::auth::first_resource_metadata_url(*parsed);
}
```

The default parser handles comma-separated challenges, auth-param key/value
pairs, quoted strings with backslash escapes, token68 payloads, and
case-insensitive parameter lookup. It includes helpers for MCP OAuth
`resource_metadata` and `insufficient_scope` challenges.

## OAuth Client Flow Builder

For browser authorization-code clients, prefer
`mcp::auth::oauth_client_flow()` from
`<cxxmcp/auth/client_orchestrator.hpp>`. The builder owns the common metadata
and token endpoint adapters while keeping browser presentation, redirect
receiving, concrete HTTP, PKCE, registration, and persistence injectable.

```cpp
class AppCallback : public mcp::auth::OAuthClientCallback {
 public:
  mcp::core::Result<mcp::core::Unit> present_authorization_url(
      const std::string& url) override;

  mcp::core::Result<std::pair<std::string, std::string>> wait_for_callback(
      std::chrono::seconds timeout) override;
};

AppCallback callback;
auto pkce = std::make_shared<mcp::auth::openssl::OpenSslPkceGenerator>();

auto flow = mcp::auth::oauth_client_flow("https://resource.example/mcp")
                .client_name("my-mcp-client")
                .scopes({"mcp:tools"})
                .callback(callback)
                .http_endpoints(app_http_get, app_http_post)
                .pkce_generator(std::move(pkce))
                .build();
if (!flow.has_value()) {
  return flow.error();
}

auto tokens = (*flow)->authorize();
```

Use `.client_id(...)` when a client id is already provisioned. Add
`.registration_endpoint(...)` when the authorization server supports dynamic
client registration and the application wants the SDK flow to configure the
client id during authorization. OpenSSL PKCE remains opt-in through
`cxxmcp::auth_openssl`; applications may supply any `PkceGenerator`
implementation instead.

## Current Boundary

The current SDK supports these auth behaviors:

- server-side `AuthProvider` policy injection;
- built-in static bearer-token `AuthProvider` for small embedded deployments
  and tests;
- optional DPoP-aware server `AuthProvider` bridge over injected JWT/DPoP
  verifier interfaces;
- optional DPoP request-header builders over an injected `DpopSigner`;
- optional JWKS value models, parser, selector, fetch boundary, and cache
  contract, with OpenSSL-backed JWT verification available through
  `cxxmcp::auth_openssl`;
- authenticated identity propagation through `SessionContext`;
- client bearer-token helper injection for HTTP transports;
- explicit `Authorization` header precedence over bearer helpers;
- optional `WWW-Authenticate` parsing and MCP OAuth challenge helpers;
- optional HTTP metadata and token endpoint helpers over injected GET/POST
  callbacks;
- application-owned refresh-on-401 hook with a one-shot retry;
- a transport-neutral `AuthorizationManager` helper for converting a 401
  challenge into a refreshed bearer token for that one-shot retry;
- a high-level `OAuthClientFlowBuilder` for assembling the common browser +
  PKCE authorization-code flow from injected callbacks and endpoints;
- an opt-in `cxxmcp::auth` feature gate for transport-neutral OAuth contracts.

The SDK does not currently provide browser or loopback redirect handling,
persistent credential storage, a built-in HTTP client for OAuth/JWKS retrieval,
or a built-in OAuth authorization server. `CXXMCP_AUTH_CRYPTO=OpenSSL` currently
adds `cxxmcp::auth_openssl` with SHA-256/base64url helpers, JOSE compact JWS
parsing primitives, public JWK import, RS256/ES256 compact JWS signature
verification, trusted in-memory JWKS JWT validation, and DPoP access-token hash
construction for `ath`. `StaticJwksJwtVerifier` validates the signature,
selected public key, issuer, audience, expiry, not-before, issued-at, and
required claims against a caller-supplied JWKS. `OpenSslDpopSigner` and
`OpenSslDpopVerifier` sign and verify ES256/RS256 DPoP proof JWTs over PEM
private keys and embedded public JWKs, including `htm`/`htu` request binding,
`ath`, `nonce`, and replay-cache-compatible `jti` extraction. Remote JWKS
fetching remains transport-neutral through the injected `JwksEndpoint` and
`JwksCache`; `FetchingJwksJwtVerifier` owns cache hit/miss behavior and refreshes
once on key or signature failures to handle key rotation. Server deployments can
include `cxxmcp/auth/openssl/server_auth_provider.hpp` to use presets that own
the OpenSSL JWT verifier, DPoP verifier, access-token hash helper, and the
DPoP-aware `AuthProvider` bridge.

## Remaining Auth Integration

The OAuth/DPoP/JWKS implementation is opt-in. It stays behind
`CXXMCP_ENABLE_AUTH=ON` and `CXXMCP_AUTH_CRYPTO=OpenSSL` where crypto is
required, and it does not add OpenSSL to default SDK builds.

Remaining integration work is intentionally outside the core SDK contract:

- optional first-party HTTP client helpers for OAuth/JWKS retrieval, if the SDK
  decides to ship a concrete HTTP dependency in the auth package;
- application UX around browser opening, loopback redirects, credential
  persistence, and consent.

OpenSSL, when required for crypto-backed OAuth/DPoP/JWKS support, must be a
normal package-manager or system dependency of the opt-in auth build. It must
not be vendored into `third_party`, and it must not be pulled by default
`cxxmcp::sdk` consumers.
