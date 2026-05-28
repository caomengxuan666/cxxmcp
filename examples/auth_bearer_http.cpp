// Copyright (c) 2025 [caomengxuan666]
//
// First-choice SDK example: HTTP bearer auth wiring for ServerPeer/ClientPeer.

#include <iostream>
#include <memory>
#include <string>

#include "cxxmcp/peer.hpp"

int main() {
  auto auth = std::make_unique<mcp::server::StaticBearerAuthProvider>();
  auth->add_token("demo-token",
                  mcp::server::AuthIdentity{
                      "alice",
                      {{"scope", "tools:call"}, {"tenant", "example"}},
                  });

  auto server =
      mcp::ServerPeer::builder()
          .name("cxxmcp-example-auth-bearer-server")
          .version("1.0.0")
          .auth_provider(std::move(auth))
          .streamable_http("127.0.0.1", 3000, "/mcp")
          .tool(mcp::server::tool<mcp::protocol::Json, mcp::protocol::Json>(
                    "whoami")
                    .description("Return the authenticated subject.")
                    .handler([](const mcp::protocol::Json&,
                                const mcp::server::ToolContext& context) {
                      const auto subject = context.auth_identity.has_value()
                                               ? context.auth_identity->subject
                                               : std::string("anonymous");
                      return mcp::protocol::Json{{"subject", subject}};
                    }))
          .build();
  if (!server) {
    std::cerr << "failed to build bearer auth server: "
              << server.error().message << '\n';
    return 1;
  }

  auto client = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:3000/mcp")
                    .bearer_token("demo-token")
                    .build();
  if (!client) {
    std::cerr << "failed to build bearer auth client: "
              << client.error().message << '\n';
    return 1;
  }

  const auto tools = server->list_tools();
  return tools.size() == 1 && tools.front().name == "whoami" ? 0 : 1;
}
