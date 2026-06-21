// Copyright (c) 2025 [caomengxuan666]

#include <string>

#include "cxxmcp/server/authoring.hpp"

struct Args {
  std::string text;
};

struct Result {
  std::string text;
};

int main() {
  auto registration = mcp::server::tool<Args, Result>("bad").handler(
      [](Args args) { return Result{args.text}; });
  (void)registration;
  return 0;
}
