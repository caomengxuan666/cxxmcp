// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/peer.hpp>

int main() {
  auto peer =
      mcp::ServerPeer::builder()
          .name("cxxmcp-external-consumer")
          .version("1.0.0")
          .tool(mcp::server::tool<mcp::protocol::Json, mcp::protocol::Json>(
                    "echo")
                    .description("Echo a JSON value.")
                    .handler(
                        [](const mcp::protocol::Json& input) { return input; }))
          .build();

  if (!peer) {
    return 1;
  }
  const auto tools = peer->list_tools();
  return tools.size() == 1 && tools.front().name == "echo" ? 0 : 1;
}
