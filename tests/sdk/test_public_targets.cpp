// Copyright (c) 2025 [caomengxuan666]

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "cxxmcp/handler.hpp"
#include "cxxmcp/service.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void test_handler_and_service_headers_are_linkable() {
  mcp::server::ServerBuilder builder;
  builder.name("public-target-test").version("1.0.0");

  auto server = builder.build();
  require(server.has_value(), "server builder failed");

  auto running = mcp::serve(mcp::ServerPeer(std::move(*server)));
  require(running.has_value(), "service target did not expose serve()");
  require(running->running(), "running service should report running");
  require(running->stop().has_value(), "running service stop failed");
}

}  // namespace

int main() {
  try {
    test_handler_and_service_headers_are_linkable();
    std::cout << "public target compile/link test passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "public target compile/link test failed: " << ex.what()
              << '\n';
    return 1;
  }
}
