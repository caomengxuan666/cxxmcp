// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/transport.hpp>

int main() {
  mcp::transport::StreamableHttpClientTransportOptions client_options;
  client_options.stateless = true;

  mcp::transport::StreamableHttpServerTransportOptions server_options;
  server_options.stateless = true;

  return client_options.stateless && server_options.stateless ? 0 : 1;
}
