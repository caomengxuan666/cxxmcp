// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/request.hpp>

int main() {
  mcp::CancellationSource cancellation;
  mcp::RequestHandle<mcp::protocol::Json> request;
  const auto token = cancellation.token();
  cancellation.cancel();
  return request.timeout().has_value() || !token.cancelled() ? 1 : 0;
}
