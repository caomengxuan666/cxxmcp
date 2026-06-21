// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/sdk.hpp>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

}  // namespace

int main() {
  try {
    mcp::ServerPeer server;
    require(server.list_tools().empty(), "default server tools mismatch");

    auto service = mcp::make_service(std::move(server));
    require(service.peer().list_prompts().empty(),
            "default service prompts mismatch");

    std::cout << "cxxmcp C++17 consumer example passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "cxxmcp C++17 consumer example failed: " << ex.what() << '\n';
    return 1;
  }
}
