// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/sdk.hpp>

int main() {
  mcp::ServerPeer server;
  return server.list_prompts().empty() ? 0 : 1;
}
