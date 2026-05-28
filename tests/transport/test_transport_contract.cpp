// Copyright (c) 2025 [caomengxuan666]

#include <deque>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "cxxmcp/transport.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class QueueTransport final : public mcp::transport::ClientTransport {
 public:
  std::string_view name() const noexcept override { return "queue"; }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    sent_.push_back(std::move(message));
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    if (received_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(received_.front());
    received_.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    closed_ = true;
    return mcp::core::Unit{};
  }

  void push_inbound(RxMessage message) {
    received_.push_back(std::move(message));
  }

  const std::deque<TxMessage>& sent() const noexcept { return sent_; }

  bool closed() const noexcept { return closed_; }

 private:
  std::deque<TxMessage> sent_;
  std::deque<RxMessage> received_;
  bool closed_ = false;
};

void test_role_generic_transport_contract() {
  static_assert(
      std::is_same_v<mcp::transport::MessageTraits<mcp::RoleClient>::TxMessage,
                     mcp::protocol::JsonRpcMessage>);
  static_assert(std::is_same_v<mcp::transport::ClientTransport,
                               mcp::transport::Transport<mcp::RoleClient>>);

  QueueTransport transport;
  require(transport.name() == "queue", "transport name mismatch");
  require(transport.diagnostics().is_object(),
          "default diagnostics should be an object");

  const auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{1},
  });
  require(sent.has_value(), "send should succeed");
  require(transport.sent().size() == 1, "sent message count mismatch");
  require(std::holds_alternative<mcp::protocol::JsonRpcRequest>(
              transport.sent().front()),
          "sent message variant mismatch");

  transport.push_inbound(mcp::protocol::JsonRpcNotification{
      .method = "notifications/initialized",
      .params = mcp::protocol::Json::object(),
  });
  const auto received = transport.receive();
  require(received.has_value(), "receive result should succeed");
  require(received->has_value(), "receive should return a message");
  require(std::holds_alternative<mcp::protocol::JsonRpcNotification>(
              received->value()),
          "received message variant mismatch");

  const auto eof = transport.receive();
  require(eof.has_value(), "empty receive result should succeed");
  require(!eof->has_value(), "empty receive should report closed stream");

  const auto closed = transport.close();
  require(closed.has_value(), "close should succeed");
  require(transport.closed(), "transport close state mismatch");
}

void test_function_transport_adapter() {
  std::deque<mcp::protocol::JsonRpcMessage> inbound;
  std::deque<mcp::protocol::JsonRpcMessage> outbound;
  bool closed = false;

  mcp::transport::ClientFunctionTransport transport(
      mcp::transport::FunctionTransportOptions<mcp::RoleClient>{
          .name = "function-test",
          .send = [&](mcp::protocol::JsonRpcMessage message)
              -> mcp::core::Result<mcp::core::Unit> {
            outbound.push_back(std::move(message));
            return mcp::core::Unit{};
          },
          .receive = [&]() -> mcp::core::Result<
                               std::optional<mcp::protocol::JsonRpcMessage>> {
            if (inbound.empty()) {
              return std::nullopt;
            }
            auto message = std::move(inbound.front());
            inbound.pop_front();
            return message;
          },
          .close = [&]() -> mcp::core::Result<mcp::core::Unit> {
            closed = true;
            return mcp::core::Unit{};
          },
          .diagnostics = [] { return mcp::protocol::Json{{"custom", true}}; },
      });

  require(transport.name() == "function-test",
          "function transport name mismatch");
  require(transport.diagnostics().at("custom") == true,
          "function transport diagnostics mismatch");

  const auto sent = transport.send(mcp::protocol::JsonRpcNotification{
      .method = "notifications/initialized",
      .params = mcp::protocol::Json::object(),
  });
  require(sent.has_value(), "function transport send failed");
  require(outbound.size() == 1, "function transport outbound mismatch");

  inbound.push_back(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{3},
  });
  const auto received = transport.receive();
  require(received.has_value() && received->has_value(),
          "function transport receive failed");
  require(
      std::holds_alternative<mcp::protocol::JsonRpcRequest>(received->value()),
      "function transport received variant mismatch");
  require(transport.close().has_value(), "function transport close failed");
  require(closed, "function transport close callback mismatch");
}

void test_json_line_transport_adapter() {
  std::deque<std::string> inbound;
  std::deque<std::string> outbound;
  bool closed = false;

  const auto serialized =
      mcp::protocol::serialize_message(mcp::protocol::JsonRpcNotification{
          .method = "notifications/initialized",
          .params = mcp::protocol::Json::object(),
      });
  require(serialized.has_value(), "json-line test serialization failed");
  inbound.push_back(*serialized);

  mcp::transport::ClientJsonLineTransport transport(
      mcp::transport::JsonLineTransportOptions<mcp::RoleClient>{
          .name = "json-line-test",
          .write_line =
              [&](std::string line) -> mcp::core::Result<mcp::core::Unit> {
            outbound.push_back(std::move(line));
            return mcp::core::Unit{};
          },
          .read_line = [&]() -> mcp::transport::JsonLineTransportOptions<
                                 mcp::RoleClient>::RxLine {
            if (inbound.empty()) {
              return std::nullopt;
            }
            auto line = std::move(inbound.front());
            inbound.pop_front();
            return line;
          },
          .close = [&]() -> mcp::core::Result<mcp::core::Unit> {
            closed = true;
            return mcp::core::Unit{};
          },
      });

  const auto received = transport.receive();
  require(received.has_value() && received->has_value(),
          "json-line transport receive failed");
  require(std::holds_alternative<mcp::protocol::JsonRpcNotification>(
              received->value()),
          "json-line transport received variant mismatch");

  const auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{4},
  });
  require(sent.has_value(), "json-line transport send failed");
  require(outbound.size() == 1, "json-line transport outbound mismatch");
  require(outbound.front().find("\"method\":\"ping\"") != std::string::npos,
          "json-line transport serialized request mismatch");
  require(transport.close().has_value(), "json-line transport close failed");
  require(closed, "json-line close callback mismatch");
}

void test_queue_transport_adapter() {
  mcp::transport::ClientQueueTransport transport(
      mcp::transport::QueueTransportOptions<mcp::RoleClient>{
          .name = "queue-test",
          .max_inbound = 1,
          .max_outbound = 1,
      });

  const auto pushed = transport.push_inbound(mcp::protocol::JsonRpcResponse{
      .id = std::int64_t{5},
      .result = mcp::protocol::Json::object(),
  });
  require(pushed.has_value(), "queue transport push failed");
  const auto overflow = transport.push_inbound(
      mcp::protocol::JsonRpcNotification{.method = "too-many"});
  require(!overflow.has_value(), "queue inbound overflow should fail");

  const auto received = transport.receive();
  require(received.has_value() && received->has_value(),
          "queue transport receive failed");
  require(
      std::holds_alternative<mcp::protocol::JsonRpcResponse>(received->value()),
      "queue transport received variant mismatch");

  require(transport
              .send(mcp::protocol::JsonRpcNotification{
                  .method = "notifications/initialized"})
              .has_value(),
          "queue transport send failed");
  const auto outbound = transport.pop_outbound();
  require(outbound.has_value(), "queue transport outbound missing");
  require(std::holds_alternative<mcp::protocol::JsonRpcNotification>(*outbound),
          "queue transport outbound variant mismatch");
  require(transport.close().has_value(), "queue transport close failed");
}

}  // namespace

int main() {
  try {
    test_role_generic_transport_contract();
    test_function_transport_adapter();
    test_json_line_transport_adapter();
    test_queue_transport_adapter();
    std::cout << "transport contract test passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "transport contract test failed: " << ex.what() << '\n';
    return 1;
  }
}
