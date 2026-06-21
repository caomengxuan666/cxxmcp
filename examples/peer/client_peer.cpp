// Copyright (c) 2025 [caomengxuan666]

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
#include <utility>
#include <variant>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/service.hpp"
#include "cxxmcp/transport.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class LoopbackTransport final : public mcp::transport::ClientTransport {
 public:
  std::string_view name() const noexcept override {
    return "client-peer-loopback";
  }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    if (const auto* request =
            std::get_if<mcp::protocol::JsonRpcRequest>(&message)) {
      std::lock_guard lock(mutex_);
      received_.push_back(make_response(*request));
      cv_.notify_all();
    }
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return stopped_ || !received_.empty(); });
    if (received_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(received_.front());
    received_.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    {
      std::lock_guard lock(mutex_);
      stopped_ = true;
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

  bool stopped() const noexcept {
    std::lock_guard lock(mutex_);
    return stopped_;
  }

 private:
  static mcp::protocol::JsonRpcResponse make_response(
      const mcp::protocol::JsonRpcRequest& request) {
    if (request.method == mcp::protocol::InitializeMethod) {
      mcp::protocol::InitializeResult result;
      result.server_info.name = "client-peer-example";
      result.server_info.version = "1.0.0";
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::initialize_result_to_json(result),
      };
    }
    if (request.method == mcp::protocol::PingMethod) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::Json::object(),
      };
    }
    if (request.method == mcp::protocol::ToolsListMethod) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              mcp::protocol::Json{
                  {"tools",
                   mcp::protocol::Json::array({
                       mcp::protocol::Json{
                           {"name", "echo"},
                           {"description", "Echo the incoming payload"},
                           {"inputSchema",
                            mcp::protocol::Json{{"type", "object"}}},
                           {"streaming", false},
                       },
                   })},
              },
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

  std::deque<RxMessage> received_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stopped_ = false;
};

}  // namespace

int main() {
  try {
    auto transport = std::make_unique<LoopbackTransport>();
    auto* transport_ptr = transport.get();
    mcp::ClientPeer peer(std::move(transport));

    auto running = mcp::serve(std::move(peer));
    require(running.has_value(), "client peer service failed to start");
    require(running->running(), "client peer service should report running");

    const auto initialized = running->peer().initialize();
    require(initialized.has_value(), "client peer initialize failed");

    const auto tools = running->peer().list_tools();
    require(tools.has_value() && tools->size() == 1,
            "client peer list_tools failed");
    require(tools->front().name == "echo", "client peer tool name mismatch");

    require(running->peer().ping().has_value(), "client peer ping failed");
    require(running->stop().has_value(), "client peer stop failed");
    require(!running->running(), "client peer service should report stopped");
    require(transport_ptr->stopped(), "client peer transport was not stopped");

    std::cout << "client peer example passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "client peer example failed: " << ex.what() << '\n';
    return 1;
  }
}
