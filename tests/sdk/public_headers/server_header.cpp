// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/server.hpp>
#include <cxxmcp/server/http_transport.hpp>
#include <cxxmcp/server/peer.hpp>

int main() {
  mcp::server::HttpTransportOptions options;
  options.stateless = true;

  mcp::server::SessionContext context;
  mcp::server::ClientPeer client = context.client();

  return options.stateless && !client.available() ? 0 : 1;
}
