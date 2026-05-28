// Copyright (c) 2025 [caomengxuan666]

#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
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
  require(response_send.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "client adapter response error code mismatch");
  require(response_send.error().message ==
              "client transport adapter cannot send responses",
          "client adapter response error message mismatch");
  require(response_send.error().category == "transport",
          "client adapter response error category mismatch");

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
  require(response_send.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "server adapter response error code mismatch");
  require(response_send.error().message ==
              "server transport adapter cannot send responses",
          "server adapter response error message mismatch");
  require(response_send.error().category == "transport",
          "server adapter response error category mismatch");

  require(adapter.close().has_value(), "server adapter close failed");
  require(legacy.stopped, "server adapter should stop legacy transport");
}

void test_server_contract_transport_adapter_start() {
  ScriptedServerContractTransport contract;
  contract.push(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
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

void test_contract_transport_adapters_bidirectional_stress() {
  constexpr int kRounds = 64;

  {
    ScriptedClientContractTransport contract;
    mcp::client::ContractTransportAdapter adapter(contract);
    int notifications_seen = 0;
    int requests_seen = 0;
    const auto started = adapter.start(
        [&](const mcp::protocol::JsonRpcRequest& request)
            -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
          ++requests_seen;
          return mcp::protocol::make_response(request.id,
                                              Json{{"method", request.method}});
        },
        [&](const mcp::protocol::JsonRpcNotification&)
            -> mcp::core::Result<mcp::core::Unit> {
          ++notifications_seen;
          return mcp::core::Unit{};
        });
    require(started.has_value(), "client stress adapter should start");

    for (int i = 0; i < kRounds; ++i) {
      contract.push(mcp::protocol::JsonRpcNotification{
          .method = "notifications/message",
          .params = Json{{"round", i}},
      });
      contract.push(mcp::protocol::JsonRpcRequest{
          .method = "roots/list",
          .params = Json::object(),
          .id = std::string("server-") + std::to_string(i),
      });
      contract.push(mcp::protocol::JsonRpcResponse{
          .id = mcp::protocol::RequestId{std::int64_t{i}},
          .result = Json{{"round", i}},
      });

      const auto response = adapter.send(mcp::protocol::JsonRpcRequest{
          .method = "tools/list",
          .params = Json::object(),
          .id = std::int64_t{i},
      });
      require(response.has_value(), "client stress request should succeed");
      require(response->result->at("round") == i,
              "client stress response mismatch");

      const auto notified =
          adapter.send_notification(mcp::protocol::JsonRpcNotification{
              .method = "notifications/initialized",
              .params = Json{{"round", i}},
          });
      require(notified.has_value(),
              "client stress notification should succeed");
    }

    require(notifications_seen == kRounds,
            "client stress notification count mismatch");
    require(requests_seen == kRounds, "client stress request count mismatch");
    require(contract.sent.size() == static_cast<std::size_t>(kRounds * 3),
            "client stress sent message count mismatch");
  }

  {
    ScriptedServerContractTransport contract;
    mcp::server::ContractTransportAdapter adapter(contract);

    for (int i = 0; i < kRounds; ++i) {
      contract.push(mcp::protocol::JsonRpcNotification{
          .method = "notifications/progress",
          .params = Json{{"round", i}},
      });
      contract.push(mcp::protocol::JsonRpcRequest{
          .method = "ping",
          .params = Json::object(),
          .id = std::string("client-") + std::to_string(i),
      });
      contract.push(mcp::protocol::JsonRpcResponse{
          .id = mcp::protocol::RequestId{std::int64_t{i}},
          .result = Json{{"round", i}},
      });

      const auto response = adapter.send_request(mcp::protocol::JsonRpcRequest{
          .method = "roots/list",
          .params = Json::object(),
          .id = std::int64_t{i},
      });
      require(response.has_value(), "server stress request should succeed");
      require(response->result->at("round") == i,
              "server stress response mismatch");

      const auto notified =
          adapter.send_notification(mcp::protocol::JsonRpcNotification{
              .method = "notifications/tools/list_changed",
              .params = Json{{"round", i}},
          });
      require(notified.has_value(),
              "server stress notification should succeed");
    }

    require(contract.sent.size() == static_cast<std::size_t>(kRounds * 3),
            "server stress sent message count mismatch");
  }
}

void test_client_contract_transport_adapter_failure_paths() {
  {
    ScriptedClientContractTransport contract;
    contract.push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{21}},
        .result = Json{{"ok", true}},
    });
    contract.push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{21}},
        .result = Json{{"duplicate", true}},
    });

    mcp::client::ContractTransportAdapter adapter(contract);
    const auto first = adapter.send(mcp::protocol::JsonRpcRequest{
        .method = "tools/list",
        .params = Json::object(),
        .id = std::int64_t{21},
    });
    require(first.has_value(),
            "client contract duplicate-id first response should succeed");

    const auto second = adapter.send(mcp::protocol::JsonRpcRequest{
        .method = "tools/list",
        .params = Json::object(),
        .id = std::int64_t{22},
    });
    require(!second.has_value(),
            "client contract duplicate response id should fail next request");
    require(second.error().message ==
                "client contract transport received unexpected response id",
            "client contract duplicate response id message mismatch");
  }

  {
    ScriptedClientContractTransport contract;
    contract.push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{8}},
        .result = Json{{"ok", true}},
    });

    mcp::client::ContractTransportAdapter adapter(contract);
    const auto response = adapter.send(mcp::protocol::JsonRpcRequest{
        .method = "tools/list",
        .params = Json::object(),
        .id = std::int64_t{7},
    });
    require(!response.has_value(),
            "client contract adapter should reject unexpected response ids");
    require(response.error().code ==
                static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
            "client unexpected response id code mismatch");
    require(response.error().message ==
                "client contract transport received unexpected response id",
            "client unexpected response id message mismatch");
    require(response.error().category == "transport",
            "client unexpected response id category mismatch");
  }

  {
    ScriptedClientContractTransport contract;
    mcp::client::ContractTransportAdapter adapter(contract);
    const auto response = adapter.send(mcp::protocol::JsonRpcRequest{
        .method = "tools/list",
        .params = Json::object(),
        .id = std::int64_t{9},
    });
    require(!response.has_value(),
            "client contract adapter should fail on closed stream");
    require(response.error().code ==
                static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
            "client closed stream code mismatch");
    require(response.error().message ==
                "client contract transport closed before response",
            "client closed stream message mismatch");
    require(response.error().category == "transport",
            "client closed stream category mismatch");
  }

  {
    ScriptedClientContractTransport contract;
    contract.push(mcp::protocol::JsonRpcRequest{
        .method = "roots/list",
        .params = Json::object(),
        .id = std::string("server-request"),
    });
    contract.push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{12}},
        .result = Json{{"ok", true}},
    });

    mcp::client::ContractTransportAdapter adapter(contract);
    const auto started =
        adapter.start([](const mcp::protocol::JsonRpcRequest&)
                          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
          return mcp::core::unexpected(mcp::core::Error{
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
              "client handler boom",
              {},
              "handler"});
        });
    require(started.has_value(),
            "client contract handler-error adapter should start");

    const auto response = adapter.send(mcp::protocol::JsonRpcRequest{
        .method = "tools/list",
        .params = Json::object(),
        .id = std::int64_t{12},
    });
    require(response.has_value(),
            "client contract handler-error flow should still receive response");
    require(contract.sent.size() == 2,
            "client contract handler-error sent count mismatch");
    const auto* handler_error =
        std::get_if<mcp::protocol::JsonRpcResponse>(&contract.sent.back());
    require(handler_error != nullptr,
            "client contract should send handler error response");
    require(handler_error->error.has_value(),
            "client contract handler error response missing error");
    require(handler_error->error->message == "client handler boom",
            "client contract handler error message mismatch");
  }

  {
    ScriptedClientContractTransport contract;
    contract.push(mcp::protocol::JsonRpcRequest{
        .method = "roots/list",
        .params = Json::object(),
        .id = std::string("server-throw"),
    });
    contract.push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{14}},
        .result = Json{{"ok", true}},
    });

    mcp::client::ContractTransportAdapter adapter(contract);
    const auto started =
        adapter.start([](const mcp::protocol::JsonRpcRequest&)
                          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
          throw std::runtime_error("client handler threw");
        });
    require(started.has_value(),
            "client contract throwing-handler adapter should start");

    const auto response = adapter.send(mcp::protocol::JsonRpcRequest{
        .method = "tools/list",
        .params = Json::object(),
        .id = std::int64_t{14},
    });
    require(response.has_value(),
            "client contract throwing-handler flow should still respond");
    const auto* handler_error =
        std::get_if<mcp::protocol::JsonRpcResponse>(&contract.sent.back());
    require(handler_error != nullptr,
            "client contract throwing handler should send response");
    require(handler_error->error.has_value(),
            "client contract throwing handler error missing");
    require(handler_error->error->message == "handler failed",
            "client contract throwing handler message mismatch");
    require(handler_error->error->data.has_value() &&
                *handler_error->error->data == "client handler threw",
            "client contract throwing handler detail mismatch");
  }
}

void test_server_contract_transport_adapter_failure_paths() {
  {
    ScriptedServerContractTransport contract;
    contract.push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{23}},
        .result = Json{{"ok", true}},
    });
    contract.push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{23}},
        .result = Json{{"duplicate", true}},
    });

    mcp::server::ContractTransportAdapter adapter(contract);
    const auto first = adapter.send_request(mcp::protocol::JsonRpcRequest{
        .method = "roots/list",
        .params = Json::object(),
        .id = std::int64_t{23},
    });
    require(first.has_value(),
            "server contract duplicate-id first response should succeed");

    const auto second = adapter.send_request(mcp::protocol::JsonRpcRequest{
        .method = "roots/list",
        .params = Json::object(),
        .id = std::int64_t{24},
    });
    require(!second.has_value(),
            "server contract duplicate response id should fail next request");
    require(second.error().message ==
                "server contract transport received unexpected response id",
            "server contract duplicate response id message mismatch");
  }

  {
    ScriptedServerContractTransport contract;
    contract.push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{10}},
        .result = Json{{"ok", true}},
    });

    mcp::server::ContractTransportAdapter adapter(contract);
    const auto response = adapter.send_request(mcp::protocol::JsonRpcRequest{
        .method = "roots/list",
        .params = Json::object(),
        .id = std::int64_t{9},
    });
    require(!response.has_value(),
            "server contract adapter should reject unexpected response ids");
    require(response.error().code ==
                static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
            "server unexpected response id code mismatch");
    require(response.error().message ==
                "server contract transport received unexpected response id",
            "server unexpected response id message mismatch");
    require(response.error().category == "transport",
            "server unexpected response id category mismatch");
  }

  {
    ScriptedServerContractTransport contract;
    mcp::server::ContractTransportAdapter adapter(contract);
    const auto response = adapter.send_request(mcp::protocol::JsonRpcRequest{
        .method = "roots/list",
        .params = Json::object(),
        .id = std::int64_t{11},
    });
    require(!response.has_value(),
            "server contract adapter should fail on closed stream");
    require(response.error().code ==
                static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
            "server closed stream code mismatch");
    require(response.error().message ==
                "server contract transport closed before response",
            "server closed stream message mismatch");
    require(response.error().category == "transport",
            "server closed stream category mismatch");
  }

  {
    ScriptedServerContractTransport contract;
    contract.push(mcp::protocol::JsonRpcRequest{
        .method = mcp::protocol::InitializeMethod,
        .params = Json{{"capabilities", Json::object()}},
        .id = std::int64_t{13},
    });

    mcp::server::ContractTransportAdapter adapter(contract);
    const auto started =
        adapter.start([](const mcp::protocol::JsonRpcRequest&,
                         const mcp::server::SessionContext&)
                          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
          return mcp::core::unexpected(mcp::core::Error{
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
              "server handler boom",
              {},
              "handler"});
        });
    require(started.has_value(),
            "server contract handler-error start should drain stream");
    require(contract.sent.size() == 1,
            "server contract handler-error sent count mismatch");
    const auto* handler_error =
        std::get_if<mcp::protocol::JsonRpcResponse>(&contract.sent.front());
    require(handler_error != nullptr,
            "server contract should send handler error response");
    require(handler_error->error.has_value(),
            "server contract handler error response missing error");
    require(handler_error->error->message == "server handler boom",
            "server contract handler error message mismatch");
  }

  {
    ScriptedServerContractTransport contract;
    contract.push(mcp::protocol::JsonRpcRequest{
        .method = mcp::protocol::InitializeMethod,
        .params = Json{{"capabilities", Json::object()}},
        .id = std::int64_t{15},
    });

    mcp::server::ContractTransportAdapter adapter(contract);
    const auto started =
        adapter.start([](const mcp::protocol::JsonRpcRequest&,
                         const mcp::server::SessionContext&)
                          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
          throw std::runtime_error("server handler threw");
        });
    require(started.has_value(),
            "server contract throwing-handler start should drain stream");
    const auto* handler_error =
        std::get_if<mcp::protocol::JsonRpcResponse>(&contract.sent.back());
    require(handler_error != nullptr,
            "server contract throwing handler should send response");
    require(handler_error->error.has_value(),
            "server contract throwing handler error missing");
    require(handler_error->error->message == "handler failed",
            "server contract throwing handler message mismatch");
    require(handler_error->error->data.has_value() &&
                *handler_error->error->data == "server handler threw",
            "server contract throwing handler detail mismatch");
  }
}

void test_transport_adapters_move_owned_transports() {
  {
    auto legacy = std::make_unique<RecordingClientTransport>();
    auto* legacy_ptr = legacy.get();
    mcp::client::TransportContractAdapter adapter(std::move(legacy));
    mcp::client::TransportContractAdapter moved(std::move(adapter));

    const auto sent = moved.send(mcp::protocol::JsonRpcRequest{
        .method = "tools/list",
        .params = Json::object(),
        .id = std::int64_t{31},
    });
    require(sent.has_value(), "moved client transport adapter send failed");
    require(legacy_ptr->requests.size() == 1,
            "moved client transport adapter did not use owned transport");
    require(!adapter
                 .send(mcp::protocol::JsonRpcNotification{
                     .method = "notifications/initialized",
                     .params = Json::object(),
                 })
                 .has_value(),
            "moved-from client transport adapter should be inert");
  }

  {
    auto contract = std::make_unique<ScriptedClientContractTransport>();
    auto* contract_ptr = contract.get();
    contract_ptr->push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{32}},
        .result = Json{{"ok", true}},
    });
    mcp::client::ContractTransportAdapter adapter(std::move(contract));
    mcp::client::ContractTransportAdapter moved(std::move(adapter));

    const auto response = moved.send(mcp::protocol::JsonRpcRequest{
        .method = "tools/list",
        .params = Json::object(),
        .id = std::int64_t{32},
    });
    require(response.has_value(),
            "moved client contract adapter request failed");
    require(contract_ptr->sent.size() == 1,
            "moved client contract adapter did not use owned transport");
    require(!adapter
                 .send_notification(mcp::protocol::JsonRpcNotification{
                     .method = "notifications/initialized",
                     .params = Json::object(),
                 })
                 .has_value(),
            "moved-from client contract adapter should be inert");
  }

  {
    auto legacy = std::make_unique<RecordingServerTransport>();
    auto* legacy_ptr = legacy.get();
    mcp::server::TransportContractAdapter adapter(std::move(legacy));
    mcp::server::TransportContractAdapter moved(std::move(adapter));

    const auto sent = moved.send(mcp::protocol::JsonRpcRequest{
        .method = "roots/list",
        .params = Json::object(),
        .id = std::int64_t{33},
    });
    require(sent.has_value(), "moved server transport adapter send failed");
    require(legacy_ptr->requests.size() == 1,
            "moved server transport adapter did not use owned transport");
    require(!adapter
                 .send(mcp::protocol::JsonRpcNotification{
                     .method = "notifications/tools/list_changed",
                     .params = Json::object(),
                 })
                 .has_value(),
            "moved-from server transport adapter should be inert");
  }

  {
    auto contract = std::make_unique<ScriptedServerContractTransport>();
    auto* contract_ptr = contract.get();
    contract_ptr->push(mcp::protocol::JsonRpcResponse{
        .id = mcp::protocol::RequestId{std::int64_t{34}},
        .result = Json{{"ok", true}},
    });
    mcp::server::ContractTransportAdapter adapter(std::move(contract));
    mcp::server::ContractTransportAdapter moved(std::move(adapter));

    const auto response = moved.send_request(mcp::protocol::JsonRpcRequest{
        .method = "roots/list",
        .params = Json::object(),
        .id = std::int64_t{34},
    });
    require(response.has_value(),
            "moved server contract adapter request failed");
    require(contract_ptr->sent.size() == 1,
            "moved server contract adapter did not use owned transport");
    require(!adapter
                 .send_notification(mcp::protocol::JsonRpcNotification{
                     .method = "notifications/tools/list_changed",
                     .params = Json::object(),
                 })
                 .has_value(),
            "moved-from server contract adapter should be inert");
  }
}

}  // namespace

int main() {
  try {
    test_client_transport_adapter();
    test_client_contract_transport_adapter();
    test_server_transport_adapter();
    test_server_contract_transport_adapter_start();
    test_server_contract_transport_adapter_outbound();
    test_contract_transport_adapters_bidirectional_stress();
    test_client_contract_transport_adapter_failure_paths();
    test_server_contract_transport_adapter_failure_paths();
    test_transport_adapters_move_owned_transports();
    std::cout << "transport adapter tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "transport adapter tests failed: " << ex.what() << '\n';
    return 1;
  }
}
