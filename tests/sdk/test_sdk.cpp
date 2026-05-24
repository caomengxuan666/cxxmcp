// Copyright (c) 2025 [caomengxuan666]

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/sdk.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class RecordingClientTransport final : public mcp::client::Transport {
 public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse> send(
      const mcp::protocol::JsonRpcRequest& request) override {
    requests.push_back(request);
    if (request.method == "initialize") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{
                  {"protocolVersion",
                   std::string(mcp::protocol::McpProtocolVersion)},
                  {"capabilities", Json::object()},
                  {"serverInfo",
                   Json{{"name", "sdk-test-server"}, {"version", "1"}}},
              },
      };
    }
    if (request.method == "ping") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = Json::object(),
      };
    }
    if (request.method == "tools/list") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{
                  {"tools", Json::array({
                                Json{
                                    {"name", "echo"},
                                    {"description", "Echo payload"},
                                    {"inputSchema", Json{{"type", "object"}}},
                                    {"streaming", false},
                                },
                            })},
              },
      };
    }
    if (request.method == "tools/call") {
      const auto arguments = request.params.value("arguments", Json::object());
      mcp::protocol::ToolResult result;
      result.content.push_back(mcp::protocol::ContentBlock{
          .type = "text",
          .text = arguments.dump(),
          .data = Json::object(),
      });
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::tool_result_to_json(result),
      };
    }
    return mcp::protocol::JsonRpcResponse{
        .id = request.id,
        .error =
            mcp::protocol::ErrorObject{
                .code =
                    static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
                .message = "unexpected method",
            },
    };
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification&) override {
    return mcp::core::Unit{};
  }

  void stop() noexcept override { stopped = true; }

  std::vector<mcp::protocol::JsonRpcRequest> requests;
  bool stopped = false;
};

void test_sdk_peer_and_service_surface() {
  auto transport = std::make_unique<RecordingClientTransport>();
  auto* transport_ptr = transport.get();
  mcp::ClientPeer client_peer(std::move(transport));

  auto running_client = mcp::serve(std::move(client_peer));
  require(running_client.has_value(), "client service should start");
  require(running_client->running(), "client service should report running");

  require(running_client->peer().initialize().has_value(),
          "client initialize failed");

  const auto tools = running_client->peer().list_tools();
  require(tools.has_value() && tools->size() == 1, "client tools list failed");
  require(tools->front().name == "echo", "client tool name mismatch");

  const auto call =
      running_client->peer().call_tool("echo", Json{{"value", "client"}});
  require(call.has_value(), "client tool call failed");
  require(!call->content.empty(), "client tool call content missing");
  require(call->content.front().text == "{\"value\":\"client\"}",
          "client tool call result mismatch");

  require(running_client->stop().has_value(), "client service stop failed");
  require(transport_ptr->stopped, "client transport should stop");

  mcp::server::ServerBuilder builder;
  builder.name("sdk-test-server")
      .version("1.0.0")
      .add_tool(
          mcp::protocol::ToolDefinition{
              .name = "echo",
              .description = "Echo payload",
              .input_schema = Json{{"type", "object"}},
          },
          [](const mcp::server::ToolContext& context)
              -> mcp::core::Result<mcp::protocol::ToolResult> {
            mcp::protocol::ToolResult result;
            result.structured_content = context.arguments;
            result.content.push_back(mcp::protocol::ContentBlock{
                .type = "text",
                .text = context.arguments.dump(),
                .data = Json::object(),
            });
            return result;
          });

  auto server = builder.build();
  require(server.has_value(), "server builder failed");

  mcp::ServerPeer server_peer(std::move(*server));
  const auto server_tools = server_peer.list_tools();
  require(server_tools.size() == 1, "server tools list mismatch");
  require(server_tools.front().name == "echo", "server tool name mismatch");

  const auto server_call =
      server_peer.call_tool("echo", Json{{"value", "server"}});
  require(server_call.has_value(), "server tool call failed");
  require(!server_call->content.empty(), "server tool content missing");
  require(server_call->content.front().text == "{\"value\":\"server\"}",
          "server tool call result mismatch");

  auto running_server = mcp::serve(std::move(server_peer));
  require(running_server.has_value(), "server service should start");
  require(running_server->running(), "server service should report running");
  require(running_server->stop().has_value(), "server service stop failed");
}

}  // namespace

int main() {
  try {
    test_sdk_peer_and_service_surface();
    std::cout << "sdk peer/service test passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "sdk peer/service test failed: " << ex.what() << '\n';
    return 1;
  }
}
