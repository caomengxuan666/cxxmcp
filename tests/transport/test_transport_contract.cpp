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

}  // namespace

int main() {
  try {
    test_role_generic_transport_contract();
    std::cout << "transport contract test passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "transport contract test failed: " << ex.what() << '\n';
    return 1;
  }
}
