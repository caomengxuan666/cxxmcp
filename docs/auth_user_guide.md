# Auth User Guide

This guide covers the authentication behavior available today in cxxmcp. The
default SDK path provides server-side auth hooks and client bearer-token
helpers without enabling the optional OAuth target. The optional `cxxmcp::auth`
target adds transport-neutral OAuth metadata, lifecycle, token, registration,
and `WWW-Authenticate` parsing contracts; it does not yet perform full OAuth,
DPoP, JWKS, or JWT verification for applications.

## Build Options

Auth is split into two layers:

- `cxxmcp::server` always includes the lightweight server auth contracts:
  `AuthRequest`, `AuthIdentity`, and `AuthProvider`.
- `cxxmcp::client` and `cxxmcp::transport` include bearer-token helpers and
  refresh-on-401 hooks for HTTP transports.
- `cxxmcp::auth` is optional. Enable it with `CXXMCP_ENABLE_AUTH=ON` or the
  legacy alias `MCP_ENABLE_AUTH=ON` when you need OAuth metadata/lifecycle
  scaffolding or the `WWW-Authenticate` parser.

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
JWT validation by itself.

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

## Current Boundary

The current SDK supports these auth behaviors:

- server-side `AuthProvider` policy injection;
- authenticated identity propagation through `SessionContext`;
- client bearer-token helper injection for HTTP transports;
- explicit `Authorization` header precedence over bearer helpers;
- optional `WWW-Authenticate` parsing and MCP OAuth challenge helpers;
- application-owned refresh-on-401 hook with a one-shot retry;
- an opt-in `cxxmcp::auth` feature gate for transport-neutral OAuth contracts.

The SDK does not currently provide a complete OAuth client flow, browser or
loopback redirect handling, persistent credential storage, DPoP proof signing,
JWT/JWKS verification, or a built-in OAuth authorization server.

## Future Full OAuth

The planned full OAuth/DPoP/JWKS implementation remains opt-in. It must stay
behind `CXXMCP_ENABLE_AUTH=ON` and the `cxxmcp::auth` target, and it must not
add OpenSSL to default SDK builds.

The future implementation is expected to add:

- OAuth 2.1 authorization-code + PKCE orchestration;
- protected-resource and authorization-server discovery integration;
- token exchange and refresh lifecycle wiring into HTTP transports;
- DPoP proof construction and verification;
- JWT access-token and ID-token verification through JWKS-aware code;
- an optional server-side `AuthProvider` implementation for bearer/DPoP token
  validation.

OpenSSL, when required for crypto-backed OAuth/DPoP/JWKS support, must be a
normal package-manager or system dependency of the opt-in auth build. It must
not be vendored into `third_party`, and it must not be pulled by default
`cxxmcp::sdk` consumers.
