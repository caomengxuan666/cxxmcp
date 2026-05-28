// Copyright (c) 2025 [caomengxuan666]
//
// First-choice SDK example: canonical ServerPeer plus Service over stdio.

#include <iostream>
#include <string>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/service.hpp"

int main() {
  auto peer =
      mcp::ServerPeer::builder()
          .name("cxxmcp-example-server-stdio-peer")
          .version("1.0.0")
          .instructions("Canonical ServerPeer stdio example.")
          .stdio()
          .tool(mcp::server::tool<mcp::protocol::Json, mcp::protocol::Json>(
                    "echo")
                    .description("Echo a JSON value.")
                    .handler([](const mcp::protocol::Json& input) {
                      return mcp::protocol::Json{{"echo", input}};
                    }))
          .tool(mcp::server::tool<std::string, std::string>("shout")
                    .description("Append an exclamation mark.")
                    .handler([](std::string text) { return text + "!"; }))
          .build();

  if (!peer) {
    std::cerr << "failed to build server peer: " << peer.error().message
              << '\n';
    return 1;
  }

  auto running = mcp::serve(std::move(*peer));
  if (!running) {
    std::cerr << "failed to serve server peer: " << running.error().message
              << '\n';
    return 1;
  }

  const auto waited = running->wait();
  if (!waited) {
    std::cerr << "server peer stopped with error: " << waited.error().message
              << '\n';
    return 1;
  }
  return 0;
}
