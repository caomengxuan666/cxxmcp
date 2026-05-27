// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/server.hpp>

int main() {
  auto app =
      mcp::server::App::builder()
          .tool(mcp::server::tool<mcp::protocol::Json, mcp::protocol::Json>(
                    "echo")
                    .description("Echo a JSON value.")
                    .handler(
                        [](const mcp::protocol::Json& input) { return input; }))
          .build();

  if (!app) {
    return 1;
  }
  const auto tools = (*app)->list_tools();
  return tools.size() == 1 && tools.front().name == "echo" ? 0 : 1;
}
