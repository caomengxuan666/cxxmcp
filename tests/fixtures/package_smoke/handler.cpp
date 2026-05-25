// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/handler.hpp>

int main() {
  mcp::client::ClientHandler client_handler;
  mcp::server::ServerHandler server_handler;
  return client_handler.on_initialized || server_handler.on_logging ? 1 : 0;
}
