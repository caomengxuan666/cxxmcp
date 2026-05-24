// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/protocol.hpp>

int main() {
  auto object = mcp::protocol::Json::object();
  return object.is_object() ? 0 : 1;
}
