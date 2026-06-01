// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/server.hpp>

int main() {
  mcp::server::SessionContext context;
  mcp::server::SessionClient session_client = context.client();
  mcp::server::ClientHandle client_handle = context.client();
  return session_client.available() || client_handle.available() ? 1 : 0;
}
