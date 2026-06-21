// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/server.hpp>
#include <cxxmcp/server/http_transport.hpp>

int main() {
  mcp::server::HttpTransportOptions options;
  options.stateless = true;
  return options.stateless ? 0 : 1;
}
