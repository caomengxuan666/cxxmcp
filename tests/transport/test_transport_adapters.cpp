// Copyright (c) 2025 [caomengxuan666]

#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
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

class ScriptedClientContractTransport final
    : public mcp::transport::ClientTransport {
 public:
  std::string_view name() const noexcept override { return "scripted-client"; }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    sent.push_back(std::move(message));
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    if (received.empty()) {
      return std::nullopt;
    }
    auto message = std::move(received.front());
    received.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    closed = true;
    return mcp::core::Unit{};
  }

  void push(RxMessage message) { received.push_back(std::move(message)); }

  std::vector<TxMessage> sent;
  std::deque<RxMessage> received;
  bool closed = false;
};

class ScriptedServerContractTransport final
    : public mcp::transport::ServerTransport {
 public:
  std::string_view name() const noexcept override { return "scripted-server"; }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    sent.push_back(std::move(message));
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    if (received.empty()) {
      return std::nullopt;
    }
    auto message = std::move(received.front());
    received.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    closed = true;
    return mcp::core::Unit{};
  }

  void push(RxMessage message) { received.push_back(std::move(message)); }

  std::vector<TxMessage> sent;
  std::deque<RxMessage> received;
  bool closed = false;
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

void test_client_contract_transport_adapter() {
  ScriptedClientContractTransport contract;
  contract.push(mcp::protocol::JsonRpcNotification{
      .method = "notifications/message",
      .params = Json{{"level", "info"}, {"data", "ready"}},
  });
  contract.push(mcp::protocol::JsonRpcRequest{
      .method = "roots/list",
      .params = Json::object(),
      .id = std::string("server-request"),
  });
  contract.push(mcp::protocol::JsonRpcResponse{
      .id = mcp::protocol::RequestId{std::int64_t{7}},
      .result = Json{{"ok", true}},
  });

  mcp::client::ContractTransportAdapter adapter(contract);
  int notification_count = 0;
  int request_count = 0;
  const auto started = adapter.start(
      [&](const mcp::protocol::JsonRpcRequest& request)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        ++request_count;
        return mcp::protocol::make_response(
            request.id,
            Json{{"roots", Json::array({Json{{"uri", "file:///workspace"}}})}});
      },
      [&](const mcp::protocol::JsonRpcNotification& notification)
          -> mcp::core::Result<mcp::core::Unit> {
        ++notification_count;
        require(notification.method == "notifications/message",
                "contract adapter notification mismatch");
        return mcp::core::Unit{};
      });
  require(started.has_value(), "contract adapter start failed");

  const auto response = adapter.send(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = Json::object(),
      .id = std::int64_t{7},
  });
  require(response.has_value(), "contract adapter request failed");
  require(response->result->at("ok"), "contract adapter response mismatch");
  require(notification_count == 1,
          "contract adapter notification count mismatch");
  require(request_count == 1, "contract adapter request count mismatch");
  require(contract.sent.size() == 2, "contract adapter sent count mismatch");
  require(std::holds_alternative<mcp::protocol::JsonRpcRequest>(
              contract.sent.front()),
          "contract adapter outbound request mismatch");
  require(std::holds_alternative<mcp::protocol::JsonRpcResponse>(
              contract.sent.back()),
          "contract adapter handler response mismatch");

  const auto notification =
      adapter.send_notification(mcp::protocol::JsonRpcNotification{
          .method = "notifications/initialized",
          .params = Json::object(),
      });
  require(notification.has_value(),
          "contract adapter notification send failed");
  require(contract.sent.size() == 3,
          "contract adapter sent notification count mismatch");

  adapter.stop();
  require(contract.closed, "contract adapter should close transport");
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

void test_server_contract_transport_adapter_start() {
  ScriptedServerContractTransport contract;
  contract.push(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params =
          Json{{"capabilities", Json{{"roots", Json{{"listChanged", true}}}}}},
      .id = std::int64_t{3},
  });
  contract.push(mcp::protocol::JsonRpcNotification{
      .method = "notifications/initialized",
      .params = Json::object(),
  });

  mcp::server::ContractTransportAdapter adapter(contract);
  int request_count = 0;
  int notification_count = 0;
  const auto started = adapter.start(
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext& context)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        ++request_count;
        require(request.method == mcp::protocol::InitializeMethod,
                "server contract request mismatch");
        require(context.transport == &adapter,
                "server contract context transport mismatch");
        return mcp::protocol::make_response(request.id,
                                            Json{{"tools", Json::array()}});
      },
      [&](const mcp::protocol::JsonRpcNotification& notification,
          const mcp::server::SessionContext& context)
          -> mcp::core::Result<mcp::core::Unit> {
        ++notification_count;
        require(notification.method == "notifications/initialized",
                "server contract notification mismatch");
        require(context.transport == &adapter,
                "server contract notification context mismatch");
        return mcp::core::Unit{};
      });
  require(started.has_value(), "server contract start failed");
  require(request_count == 1, "server contract request count mismatch");
  require(notification_count == 1,
          "server contract notification count mismatch");
  require(contract.sent.size() == 1,
          "server contract start response count mismatch");
  require(std::holds_alternative<mcp::protocol::JsonRpcResponse>(
              contract.sent.front()),
          "server contract start response mismatch");
  const auto capabilities = adapter.client_capabilities();
  require(capabilities.has_value(),
          "server contract client capabilities missing");
  require(capabilities->roots.enabled,
          "server contract client roots capability missing");
  require(capabilities->roots.list_changed,
          "server contract client roots listChanged mismatch");
}

void test_server_contract_transport_adapter_outbound() {
  ScriptedServerContractTransport contract;
  contract.push(mcp::protocol::JsonRpcNotification{
      .method = "notifications/progress",
      .params = Json{{"progress", 1}},
  });
  contract.push(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = Json::object(),
      .id = std::string("client-request"),
  });
  contract.push(mcp::protocol::JsonRpcResponse{
      .id = mcp::protocol::RequestId{std::int64_t{9}},
      .result = Json{{"roots", Json::array()}},
  });

  mcp::server::ContractTransportAdapter adapter(contract);
  const auto response = adapter.send_request(mcp::protocol::JsonRpcRequest{
      .method = "roots/list",
      .params = Json::object(),
      .id = std::int64_t{9},
  });
  require(response.has_value(), "server contract outbound request failed");
  require(response->result->contains("roots"),
          "server contract outbound response mismatch");
  require(contract.sent.size() == 2,
          "server contract outbound sent count mismatch");
  require(std::holds_alternative<mcp::protocol::JsonRpcRequest>(
              contract.sent.front()),
          "server contract outbound request send mismatch");
  require(std::holds_alternative<mcp::protocol::JsonRpcResponse>(
              contract.sent.back()),
          "server contract inbound response send mismatch");

  const auto notification =
      adapter.send_notification(mcp::protocol::JsonRpcNotification{
          .method = "notifications/tools/list_changed",
          .params = Json::object(),
      });
  require(notification.has_value(), "server contract notification send failed");
  require(contract.sent.size() == 3,
          "server contract notification send count mismatch");
  adapter.stop();
  require(contract.closed, "server contract adapter should close transport");
}

}  // namespace

int main() {
  try {
    test_client_transport_adapter();
    test_client_contract_transport_adapter();
    test_server_transport_adapter();
    test_server_contract_transport_adapter_start();
    test_server_contract_transport_adapter_outbound();
    std::cout << "transport adapter tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "transport adapter tests failed: " << ex.what() << '\n';
    return 1;
  }
}
