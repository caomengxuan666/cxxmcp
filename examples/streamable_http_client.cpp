// Copyright (c) 2025 [caomengxuan666]
//
// Self-contained example: starts an in-process HTTP server, then connects
// a client peer over Streamable HTTP and exercises initialize + tools/list.

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/service.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

}  // namespace

int main() {
  try {
    // Start an in-process HTTP server on port 3000.
    auto server =
        mcp::ServerPeer::builder()
            .name("cxxmcp-example-http-server")
            .version("1.0.0")
            .streamable_http("127.0.0.1", 3000, "/mcp")
            .tool(mcp::server::tool<Json, Json>("echo")
                      .description("Echo JSON payload.")
                      .handler([](const Json& input) { return input; }))
            .build();
    require(server.has_value(), "HTTP server peer build failed");

    auto running = mcp::serve(std::move(*server));
    require(running.has_value(), "HTTP server failed to start");

    // Wait until the HTTP server socket is bound.
    running->wait_until_ready();

    // Connect a client peer to the server.
    auto peer = mcp::ClientPeer::builder()
                    .streamable_http("http://127.0.0.1:3000/mcp")
                    .build();
    require(peer.has_value(), "streamable HTTP client peer build failed");

    auto client_running = mcp::serve(std::move(*peer));
    require(client_running.has_value(),
            "streamable HTTP client service failed to start");

    require(client_running->peer().initialize().has_value(),
            "streamable HTTP initialize failed");
    require(client_running->peer().notify_initialized().has_value(),
            "streamable HTTP notify_initialized failed");

    const auto tools = client_running->peer().list_tools();
    require(tools.has_value(), "streamable HTTP tools/list failed");

    std::cout << "streamable HTTP client connected; tools=" << tools->size()
              << '\n';

    require(client_running->stop().has_value(),
            "streamable HTTP client service failed to stop");
    require(running->stop().has_value(),
            "streamable HTTP server failed to stop");
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "streamable HTTP client example failed: " << ex.what() << '\n';
    return 1;
  }
}
