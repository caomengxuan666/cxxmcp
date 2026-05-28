// Copyright (c) 2025 [caomengxuan666]
//
// First-choice SDK example: HTTP bearer auth wiring for ServerPeer/ClientPeer.

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/service.hpp"

int main() {
  try {
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
            .streamable_http("127.0.0.1", 3001, "/mcp")
            .tool(mcp::server::tool<mcp::protocol::Json, mcp::protocol::Json>(
                      "whoami")
                      .description("Return the authenticated subject.")
                      .handler([](const mcp::protocol::Json&,
                                  const mcp::server::ToolContext& context) {
                        const auto subject =
                            context.auth_identity.has_value()
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

    auto running = mcp::serve(std::move(*server));
    if (!running) {
      std::cerr << "bearer auth server failed to start\n";
      return 1;
    }

    // Give the server time to bind.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto client = mcp::ClientPeer::builder()
                      .streamable_http("http://127.0.0.1:3001/mcp")
                      .bearer_token("demo-token")
                      .build();
    if (!client) {
      std::cerr << "failed to build bearer auth client: "
                << client.error().message << '\n';
      return 1;
    }

    auto client_running = mcp::serve(std::move(*client));
    if (!client_running) {
      std::cerr << "bearer auth client service failed to start\n";
      return 1;
    }

    // Give the client service time to start its receive loop.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto inited = client_running->peer().initialize();
    if (!inited) {
      std::cerr << "bearer auth initialize failed: " << inited.error().message
                << " (code=" << inited.error().code << ")\n";
      return 1;
    }
    const auto notify = client_running->peer().notify_initialized();
    if (!notify) {
      std::cerr << "bearer auth notify_initialized failed: "
                << notify.error().message << " (code=" << notify.error().code
                << ")\n";
      return 1;
    }

    const auto tools = client_running->peer().list_tools();
    if (!tools || tools->size() != 1 || tools->front().name != "whoami") {
      std::cerr << "bearer auth tools/list failed";
      if (!tools) {
        std::cerr << ": " << tools.error().message
                  << " (code=" << tools.error().code << ")";
      }
      std::cerr << '\n';
      return 1;
    }

    std::cout << "auth bearer HTTP example passed\n";

    (void)client_running->stop();
    (void)running->stop();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "auth bearer HTTP example failed: " << ex.what() << '\n';
    return 1;
  }
}
