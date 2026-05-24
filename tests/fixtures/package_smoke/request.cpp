// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/request.hpp>

int main() {
  mcp::RequestHandle<mcp::protocol::Json> request;
  return request.timeout().has_value() ? 1 : 0;
}
