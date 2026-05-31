// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/peer.hpp>
#include <cxxmcp/transport/websocket_transport.hpp>
#include <memory>
#include <utility>

void configure_peer_builder() {
  auto builder = mcp::ClientPeer::builder();
  builder.websocket("ws://127.0.0.1:3001/mcp");
}

std::unique_ptr<mcp::transport::WebSocketClientTransport> make_client() {
  mcp::transport::WebSocketClientTransportOptions options;
  options.uri = "ws://127.0.0.1:3001/mcp";
  return std::make_unique<mcp::transport::WebSocketClientTransport>(
      std::move(options));
}

std::unique_ptr<mcp::transport::WebSocketServerTransport> make_server() {
  mcp::transport::WebSocketServerTransportOptions options;
  options.listen_port = 3001;
  return std::make_unique<mcp::transport::WebSocketServerTransport>(
      std::move(options));
}

int main() { return 0; }
