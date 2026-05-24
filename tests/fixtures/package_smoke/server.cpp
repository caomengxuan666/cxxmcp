// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/server.hpp>

int main() {
  mcp::server::Server server(mcp::server::ServerOptions{});
  return server.list_tools().empty() ? 0 : 1;
}
