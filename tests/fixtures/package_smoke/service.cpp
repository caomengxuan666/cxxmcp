// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/service.hpp>

int main() {
  auto service = mcp::make_service(mcp::ServerPeer{});
  return service.peer().list_tools().empty() ? 0 : 1;
}
