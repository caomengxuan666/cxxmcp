// Copyright (c) 2025 [caomengxuan666]

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/service.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class LoopbackTransport final : public mcp::client::Transport {
 public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse> send(
      const mcp::protocol::JsonRpcRequest& request) override {
    if (request.method == "initialize") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              mcp::protocol::Json{
                  {"protocolVersion",
                   std::string(mcp::protocol::McpProtocolVersion)},
                  {"capabilities", mcp::protocol::Json::object()},
                  {"serverInfo",
                   mcp::protocol::Json{{"name", "client-peer-example"},
                                       {"version", "1.0.0"}}},
              },
      };
    }
    if (request.method == "ping") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::Json::object(),
      };
    }
    if (request.method == "tools/list") {
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

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification&) override {
    return mcp::core::Unit{};
  }

  void stop() noexcept override { stopped_ = true; }

  bool stopped() const noexcept { return stopped_; }

 private:
  bool stopped_ = false;
};

}  // namespace

int main() {
  try {
    auto transport = std::make_unique<LoopbackTransport>();
    auto* transport_ptr = transport.get();
    mcp::ClientPeer peer(mcp::client::Client(std::move(transport)));

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
