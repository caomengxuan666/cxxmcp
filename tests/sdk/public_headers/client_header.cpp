// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/client.hpp>
#include <cxxmcp/client/http_transport.hpp>

int main() {
  mcp::client::HttpTransportOptions options;
  options.stateless = true;
  return options.stateless ? 0 : 1;
}
