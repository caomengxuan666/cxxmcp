// Copyright (c) 2025 [caomengxuan666]
//
// Runtime tooling example: demonstrates the optional gateway layer above the
// SDK. It is not the canonical SDK starting path.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "cxxmcp/client.hpp"
#include "cxxmcp/gateway.hpp"
#include "cxxmcp/protocol/serialization.hpp"

#ifndef MCP_EXAMPLE_CHILD_EXE
#define MCP_EXAMPLE_CHILD_EXE ""
#endif

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

bool wait_for_gateway(int port, std::string_view path) {
  for (int attempt = 0; attempt < 40; ++attempt) {
    mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
        mcp::client::HttpTransportOptions{
            .host = "127.0.0.1",
            .port = port,
            .path = std::string(path),
            .timeout = std::chrono::milliseconds{250},
        }));

    const auto ping =
        client.raw_request(mcp::protocol::make_ping_request(std::int64_t{1}));
    if (ping.has_value()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
  return false;
}

}  // namespace

int main() {
  require(std::string_view(MCP_EXAMPLE_CHILD_EXE).size() != 0,
          "child server executable is not configured");

  auto runtime =
      mcp::gateway::Runtime::builder()
          .profile("profile.example")
          .host("127.0.0.1")
          .port(39981)
          .path("/cxxmcp/example")
          .instruction("Expose reviewed tools only.")
          .trust(true)
          .discover(true)
          .bind_server("server.example")
          .add_stdio_server("server.example", MCP_EXAMPLE_CHILD_EXE, {})
          .build();
  require(runtime.has_value(), "gateway runtime build failed");

  require(runtime->start().has_value(), "gateway runtime start failed");
  require(wait_for_gateway(39981, "/cxxmcp/example"),
          "gateway did not become reachable");

  mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
      mcp::client::HttpTransportOptions{
          .host = "127.0.0.1",
          .port = 39981,
          .path = "/cxxmcp/example",
          .timeout = std::chrono::milliseconds{1000},
      }));

  require(client.initialize().has_value(), "gateway initialize failed");
  require(client.notify_initialized().has_value(),
          "gateway initialized notification failed");

  const auto tools = client.list_tools();
  require(tools.has_value() && !tools->empty(), "gateway tools/list failed");

  const auto prompts = client.list_prompts();
  require(prompts.has_value() && !prompts->empty(),
          "gateway prompts/list failed");

  const auto resources = client.list_resources();
  require(resources.has_value() && !resources->empty(),
          "gateway resources/list failed");

  const auto call = client.call_raw(tools->front().name,
                                    mcp::protocol::Json{{"value", "hello"}});
  require(call.has_value(), "gateway tools/call failed");

  const auto prompt = client.get_prompt(prompts->front().name,
                                        mcp::protocol::Json{{"text", "hello"}});
  require(prompt.has_value() && !prompt->messages.empty(),
          "gateway prompts/get failed");

  const auto resource = client.read_resource(resources->front().uri);
  require(resource.has_value() && !resource->contents.empty(),
          "gateway resources/read failed");

  require(runtime->stop().has_value(), "gateway runtime stop failed");
  std::cout << "gateway runtime example passed\n";
  return 0;
}
