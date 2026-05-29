// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/peer.hpp>

int main() {
  auto built =
      mcp::ServerPeer::builder()
          .tool(mcp::server::tool<mcp::protocol::Json, mcp::protocol::Json>(
                    "echo")
                    .handler(
                        [](const mcp::protocol::Json& value) { return value; }))
          .build();
  if (!built) {
    return 1;
  }
  return built->list_tools().size() == 1 ? 0 : 1;
}
