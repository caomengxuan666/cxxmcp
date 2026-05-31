// Copyright (c) 2025 [caomengxuan666]

/// @file
/// @brief Minimal WebSocket MCP server example.
///
/// Builds with:
///   cmake -S . -B build -DCXXMCP_BUILD_EXAMPLES=ON -DCXXMCP_ENABLE_HTTP=ON
///         -DCXXMCP_ENABLE_WEBSOCKET=ON

#include <iostream>
#include <string>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/service.hpp"

int main() {
  using Json = mcp::protocol::Json;

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-ws-example")
                    .version("1.0.0")
                    .websocket(3001)
                    .tool<Json, Json>(
                        "echo",
                        [](const Json& input) -> Json {
                          return Json{{"echo", input}};
                        })
                    .tool<Json, Json>(
                        "greet",
                        [](const Json& input) -> Json {
                          std::string name = "World";
                          if (input.contains("name") &&
                              input["name"].is_string()) {
                            name = input["name"].get<std::string>();
                          }
                          return Json{{"greeting", "Hello, " + name + "!"}};
                        })
                    .build();

  if (!server) {
    std::cerr << "Failed to build server\n";
    return 1;
  }

  auto running = mcp::serve(std::move(*server));
  if (!running) {
    std::cerr << "Failed to start server\n";
    return 1;
  }

  std::cout << "WebSocket MCP server listening on ws://127.0.0.1:3001/mcp\n";
  std::cout << "Press Ctrl+C to stop.\n";

  running->wait();
  return 0;
}
