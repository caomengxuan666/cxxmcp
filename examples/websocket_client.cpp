// Copyright (c) 2025 [caomengxuan666]

/// @file
/// @brief Minimal WebSocket MCP client example.
///
/// Connects to a WebSocket MCP server, lists tools, and calls the "echo" tool.
///
/// Builds with:
///   cmake -S . -B build -DCXXMCP_BUILD_EXAMPLES=ON -DCXXMCP_ENABLE_HTTP=ON
///         -DCXXMCP_ENABLE_WEBSOCKET=ON

#include <iostream>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/service.hpp"

int main() {
  using Json = mcp::protocol::Json;

  auto peer = mcp::ClientPeer::builder()
                  .websocket("ws://127.0.0.1:3001/mcp")
                  .build();

  if (!peer) {
    std::cerr << "Failed to build client peer\n";
    return 1;
  }

  auto svc = mcp::serve(std::move(*peer));
  if (!svc) {
    std::cerr << "Failed to start client service\n";
    return 1;
  }

  // Initialize the MCP session
  auto init_result = svc->peer().initialize("ws-client", "1.0.0");
  if (!init_result) {
    std::cerr << "Initialize failed: " << init_result.error().message << "\n";
    return 1;
  }
  std::cout << "Connected to: " << init_result->server_info.name << " "
            << init_result->server_info.version << "\n";

  // List available tools
  auto tools = svc->peer().list_all_tools();
  if (tools) {
    std::cout << "Available tools (" << tools->size() << "):\n";
    for (const auto& tool : *tools) {
      std::cout << "  - " << tool.name;
      if (!tool.description.empty()) {
        std::cout << ": " << tool.description;
      }
      std::cout << "\n";
    }
  }

  // Call the echo tool
  Json echo_input = {{"value", "hello from websocket client"}};
  auto echo_result = svc->peer().call_tool("echo", echo_input);
  if (echo_result) {
    std::cout << "echo result: " << echo_result->dump(2) << "\n";
  } else {
    std::cerr << "echo call failed: " << echo_result.error().message << "\n";
  }

  // Call the greet tool
  Json greet_input = {{"name", "cxxmcp"}};
  auto greet_result = svc->peer().call_tool("greet", greet_input);
  if (greet_result) {
    std::cout << "greet result: " << greet_result->dump(2) << "\n";
  } else {
    std::cerr << "greet call failed: " << greet_result.error().message << "\n";
  }

  svc->stop();
  return 0;
}
