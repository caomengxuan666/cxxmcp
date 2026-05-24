// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/client.hpp>

int main() {
  mcp::client::Client::StdioEndpoint endpoint;
  return endpoint.command.empty() ? 0 : 1;
}
