// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/service.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

template <class Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

class LoopbackTransport final : public mcp::server::Transport {
 public:
  mcp::core::Result<mcp::core::Unit> start(
      mcp::server::RequestHandler handler,
      mcp::server::NotificationHandler notification_handler = {}) override {
    request_handler_ = std::move(handler);
    notification_handler_ = std::move(notification_handler);
    started_.store(true);
    return mcp::core::Unit{};
  }

  mcp::core::Result<mcp::protocol::JsonRpcResponse> dispatch_request(
      const mcp::protocol::JsonRpcRequest& request) {
    if (!request_handler_) {
      return std::unexpected(mcp::core::Error{
          static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "transport is not started",
          {},
      });
    }
    return request_handler_(request, mcp::server::SessionContext{
                                         .session_id = "server-peer-example",
                                         .remote_address = "loopback",
                                         .transport = this,
                                     });
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    notifications_.push_back(notification);
    return mcp::core::Unit{};
  }

  void stop() noexcept override {}

  std::string_view name() const noexcept override {
    return "server-peer-loopback";
  }

  bool started() const noexcept { return started_.load(); }

  const std::vector<mcp::protocol::JsonRpcNotification>& notifications()
      const noexcept {
    return notifications_;
  }

 private:
  mcp::server::RequestHandler request_handler_;
  mcp::server::NotificationHandler notification_handler_;
  std::atomic_bool started_{false};
  std::vector<mcp::protocol::JsonRpcNotification> notifications_;
};

}  // namespace

int main() {
  try {
    mcp::server::ServerBuilder builder;
    builder.name("cxxmcp-example-server-peer")
        .version("1.0.0")
        .instructions("Example server peer for the SDK facade.")
        .add_tool(
            mcp::protocol::ToolDefinition{
                .name = "echo",
                .description = "Echo the incoming payload",
                .input_schema = Json{{"type", "object"}},
                .output_schema = Json{{"type", "object"}},
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
    require(server.has_value(), "failed to build example server");

    mcp::ServerPeer peer(std::move(*server));
    const auto tools = peer.list_tools();
    require(tools.size() == 1, "server peer tool count mismatch");
    require(tools.front().name == "echo", "server peer tool name mismatch");

    auto transport = std::make_unique<LoopbackTransport>();
    auto* transport_ptr = transport.get();
    require(peer.add_transport(std::move(transport)).has_value(),
            "server peer add_transport failed");

    auto running = mcp::serve(std::move(peer));
    require(running.has_value(), "server peer service failed to start");
    require(wait_until([&] { return transport_ptr->started(); },
                       std::chrono::milliseconds(1000)),
            "server peer transport did not start");

    const auto listed =
        transport_ptr->dispatch_request(mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ToolsListMethod),
            .params = Json::object(),
            .id = std::int64_t{1},
        });
    require(listed.has_value(), "server peer tools/list request failed");
    require(listed->result.has_value(),
            "server peer tools/list result missing");
    require(listed->result->at("tools").size() == 1,
            "server peer tools/list size mismatch");
    require(listed->result->at("tools").at(0).at("name") == "echo",
            "server peer tools/list name mismatch");

    const auto called = running->peer().call_tool(
        "echo", Json{{"value", "hello"}}, "server-peer-example");
    require(called.has_value(), "server peer call_tool failed");
    require(!called->content.empty(), "server peer call_tool content missing");
    require(called->content.front().text == "{\"value\":\"hello\"}",
            "server peer call_tool result mismatch");

    require(running->peer().notify_roots_list_changed().has_value(),
            "server peer notify_roots_list_changed failed");
    require(transport_ptr->notifications().size() == 1,
            "server peer notification count mismatch");
    require(transport_ptr->notifications().front().method ==
                std::string(mcp::protocol::RootsListChangedNotificationMethod),
            "server peer notification method mismatch");

    require(running->stop().has_value(), "server peer service stop failed");
    std::cout << "server peer example passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "server peer example failed: " << ex.what() << '\n';
    return 1;
  }
}
