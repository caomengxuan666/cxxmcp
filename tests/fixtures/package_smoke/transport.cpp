// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/transport.hpp>

int main() {
  mcp::client::StdioTransport client_transport;
  auto* transport = static_cast<mcp::client::Transport*>(&client_transport);
  return transport == nullptr ? 1 : 0;
}
