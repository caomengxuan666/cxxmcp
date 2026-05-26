// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/error.hpp>

int main() {
  const auto error = mcp::errors::request_cancelled();
  return error.message == "request cancelled" ? 0 : 1;
}
