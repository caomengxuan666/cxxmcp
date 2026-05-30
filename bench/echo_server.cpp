// Copyright (c) 2025 [caomengxuan666]

// Minimal Streamable HTTP echo server for benchmarking.
// Starts on 127.0.0.1:3000/mcp and waits until interrupted.

#include <chrono>
#include <iostream>
#include <optional>
#include <thread>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/service.hpp"
#include "cxxmcp/transport/http_transport.hpp"

int main() {
  // Benchmark-tuned transport options.
  mcp::transport::StreamableHttpServerTransportOptions transport_opts;
  transport_opts.listen_host = "127.0.0.1";
  transport_opts.listen_port = 3000;
  transport_opts.path = "/mcp";
  transport_opts.stateless = false;
  transport_opts.read_timeout = std::chrono::milliseconds(2000);
  transport_opts.write_timeout = std::chrono::milliseconds(2000);
  transport_opts.request_timeout = std::chrono::milliseconds(5000);
  transport_opts.max_sessions = 0;  // Unlimited.

  auto server = mcp::ServerPeer::builder()
                    .name("cxxmcp-bench-echo")
                    .version("1.0.0")
                    .streamable_http(std::move(transport_opts))
                    .tool<std::string, std::string>(
                        "echo", [](std::string text) { return text; })
                    .build();

  if (!server.has_value()) {
    std::cerr << "build failed: " << server.error().message << "\n";
    return 1;
  }

  auto running = mcp::serve(std::move(*server));
  if (!running.has_value()) {
    std::cerr << "serve failed: " << running.error().message << "\n";
    return 1;
  }

  running->wait_until_ready();
  std::cout << "echo server listening on http://127.0.0.1:3000/mcp\n";

  // Block until killed.
  while (true) {
    std::this_thread::sleep_for(std::chrono::hours(24));
  }
  return 0;
}
