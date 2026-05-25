// Copyright (c) 2025 [caomengxuan666]

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

#include "cxxmcp/client/transport_adapter.hpp"
#include "cxxmcp/server/transport_adapter.hpp"

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
    return mcp::protocol::JsonRpcResponse{
        .id = request.id,
        .result = Json{{"method", request.method}},
    };
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    notifications.push_back(notification);
    return mcp::core::Unit{};
  }

  void stop() noexcept override { stopped = true; }

  std::vector<mcp::protocol::JsonRpcRequest> requests;
  std::vector<mcp::protocol::JsonRpcNotification> notifications;
  bool stopped = false;
};

class RecordingServerTransport final : public mcp::server::Transport {
 public:
  mcp::core::Result<mcp::core::Unit> start(
      mcp::server::RequestHandler,
      mcp::server::NotificationHandler = {}) override {
    return mcp::core::Unit{};
  }

  mcp::core::Result<mcp::protocol::JsonRpcResponse> send_request(
      const mcp::protocol::JsonRpcRequest& request) override {
    requests.push_back(request);
    return mcp::protocol::JsonRpcResponse{
        .id = request.id,
        .result = Json{{"method", request.method}},
    };
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    notifications.push_back(notification);
    return mcp::core::Unit{};
  }

  void stop() noexcept override { stopped = true; }

  std::string_view name() const noexcept override { return "recording-server"; }

  std::vector<mcp::protocol::JsonRpcRequest> requests;
  std::vector<mcp::protocol::JsonRpcNotification> notifications;
  bool stopped = false;
};

void test_client_transport_adapter() {
  RecordingClientTransport legacy;
  mcp::client::TransportContractAdapter adapter(legacy);

  const auto sent = adapter.send(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(sent.has_value(), "client adapter request send failed");
  require(legacy.requests.size() == 1, "client adapter request count mismatch");

  const auto received = adapter.receive();
  require(received.has_value(), "client adapter receive failed");
  require(received->has_value(), "client adapter response missing");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(response != nullptr, "client adapter response variant mismatch");
  require(response->result->at("method") == "tools/list",
          "client adapter response payload mismatch");

  const auto notification = adapter.send(mcp::protocol::JsonRpcNotification{
      .method = "notifications/initialized",
      .params = Json::object(),
  });
  require(notification.has_value(), "client adapter notification send failed");
  require(legacy.notifications.size() == 1,
          "client adapter notification count mismatch");

  const auto response_send = adapter.send(mcp::protocol::JsonRpcResponse{
      .id = mcp::protocol::RequestId{std::int64_t{2}},
      .result = Json::object(),
  });
  require(!response_send.has_value(), "client adapter should reject responses");

  require(adapter.close().has_value(), "client adapter close failed");
  require(legacy.stopped, "client adapter should stop legacy transport");
}

void test_server_transport_adapter() {
  RecordingServerTransport legacy;
  mcp::server::TransportContractAdapter adapter(legacy);
  require(adapter.name() == "recording-server", "server adapter name mismatch");

  const auto sent = adapter.send(mcp::protocol::JsonRpcRequest{
      .method = "roots/list",
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(sent.has_value(), "server adapter request send failed");
  require(legacy.requests.size() == 1, "server adapter request count mismatch");

  const auto received = adapter.receive();
  require(received.has_value(), "server adapter receive failed");
  require(received->has_value(), "server adapter response missing");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(response != nullptr, "server adapter response variant mismatch");
  require(response->result->at("method") == "roots/list",
          "server adapter response payload mismatch");

  const auto notification = adapter.send(mcp::protocol::JsonRpcNotification{
      .method = "notifications/tools/list_changed",
      .params = Json::object(),
  });
  require(notification.has_value(), "server adapter notification send failed");
  require(legacy.notifications.size() == 1,
          "server adapter notification count mismatch");

  const auto response_send = adapter.send(mcp::protocol::JsonRpcResponse{
      .id = mcp::protocol::RequestId{std::int64_t{2}},
      .result = Json::object(),
  });
  require(!response_send.has_value(), "server adapter should reject responses");

  require(adapter.close().has_value(), "server adapter close failed");
  require(legacy.stopped, "server adapter should stop legacy transport");
}

}  // namespace

int main() {
  try {
    test_client_transport_adapter();
    test_server_transport_adapter();
    std::cout << "transport adapter tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "transport adapter tests failed: " << ex.what() << '\n';
    return 1;
  }
}
