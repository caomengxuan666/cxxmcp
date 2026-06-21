// Copyright (c) 2025 [caomengxuan666]
//
// Optional SDK example: OpenSSL-backed DPoP/JWKS auth-provider wiring.

#include <iostream>
#include <memory>

#include "cxxmcp/auth/openssl/server_auth_provider.hpp"
#include "cxxmcp/peer.hpp"

int main() {
  mcp::auth::JsonWebKeySet trusted_jwks;
  mcp::auth::InMemoryDpopReplayCache replay_cache;

  mcp::auth::DpopAuthProviderOptions auth_options;
  auth_options.require_dpop = true;
  auth_options.issuer = "https://issuer.example";
  auth_options.audience = "https://resource.example/mcp";
  auth_options.required_algorithm = "RS256";

  auto auth =
      std::make_unique<mcp::auth::openssl::StaticJwksDpopBearerAuthProvider>(
          std::move(trusted_jwks), &replay_cache, auth_options);

  auto server =
      mcp::ServerPeer::builder()
          .name("cxxmcp-example-auth-dpop-server")
          .version("1.0.0")
          .auth_provider(std::move(auth))
          .streamable_http(mcp::transport::StreamableHttpServerTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = 3000,
              .path = "/mcp",
          })
          .tool(mcp::server::tool<mcp::protocol::Json, mcp::protocol::Json>(
                    "secure_echo")
                    .description("Echo JSON after DPoP-bound token auth.")
                    .handler(
                        [](const mcp::protocol::Json& input) { return input; }))
          .build();

  if (!server) {
    std::cerr << "failed to build DPoP auth server: " << server.error().message
              << '\n';
    return 1;
  }

  return server->list_tools().size() == 1 ? 0 : 1;
}
