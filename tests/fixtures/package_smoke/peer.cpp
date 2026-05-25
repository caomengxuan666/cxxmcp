// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/peer.hpp>

int main() {
  mcp::ServerPeer server;
  return server.list_tools().empty() ? 0 : 1;
}
