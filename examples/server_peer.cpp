// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/service.hpp"
#include "cxxmcp/transport.hpp"

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

class LoopbackTransport final : public mcp::transport::ServerTransport {
 public:
  std::string_view name() const noexcept override {
    return "server-peer-loopback";
  }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    {
      std::lock_guard lock(mutex_);
      sent_.push_back(std::move(message));
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    std::unique_lock lock(mutex_);
    receiving_.store(true);
    cv_.notify_all();
    cv_.wait(lock, [&] { return closed_ || !incoming_.empty(); });
    if (incoming_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(incoming_.front());
    incoming_.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    {
      std::lock_guard lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

  void push_request(mcp::protocol::JsonRpcRequest request) {
    {
      std::lock_guard lock(mutex_);
      incoming_.push_back(mcp::protocol::JsonRpcMessage{std::move(request)});
    }
    cv_.notify_all();
  }

  bool receiving() const noexcept { return receiving_.load(); }

  std::optional<mcp::protocol::JsonRpcResponse> take_response(
      const mcp::protocol::RequestId& id, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock lock(mutex_);
    while (std::chrono::steady_clock::now() < deadline) {
      for (auto it = sent_.begin(); it != sent_.end(); ++it) {
        auto* response = std::get_if<mcp::protocol::JsonRpcResponse>(&*it);
        if (response != nullptr && response->id.has_value() &&
            *response->id == id) {
          auto value = std::move(*response);
          sent_.erase(it);
          return value;
        }
      }
      cv_.wait_until(lock, deadline);
    }
    return std::nullopt;
  }

  std::size_t notification_count(std::string_view method) const {
    std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto& message : sent_) {
      const auto* notification =
          std::get_if<mcp::protocol::JsonRpcNotification>(&message);
      if (notification != nullptr && notification->method == method) {
        ++count;
      }
    }
    return count;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<RxMessage> incoming_;
  std::deque<TxMessage> sent_;
  std::atomic_bool receiving_{false};
  bool closed_ = false;
};

}  // namespace

int main() {
  try {
    mcp::server::ServerBuilder builder;
    builder.name("cxxmcp-example-server-peer")
        .version("1.0.0")
        .instructions("Example server peer for the canonical SDK path.")
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
    require(wait_until([&] { return transport_ptr->receiving(); },
                       std::chrono::milliseconds(1000)),
            "server peer transport did not enter receive loop");

    transport_ptr->push_request(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::ToolsListMethod),
        .params = Json::object(),
        .id = std::int64_t{1},
    });
    const auto listed = transport_ptr->take_response(
        std::int64_t{1}, std::chrono::milliseconds(1000));
    require(listed.has_value(), "server peer tools/list response missing");
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
    require(transport_ptr->notification_count(
                mcp::protocol::RootsListChangedNotificationMethod) == 1,
            "server peer notification count mismatch");

    require(running->stop().has_value(), "server peer service stop failed");
    std::cout << "server peer example passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "server peer example failed: " << ex.what() << '\n';
    return 1;
  }
}
