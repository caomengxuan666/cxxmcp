// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/transport.hpp>
#include <deque>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

class SmokeTransport final : public mcp::transport::ClientTransport {
 public:
  std::string_view name() const noexcept override { return "smoke"; }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    message_ = std::move(message);
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    return std::nullopt;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    closed_ = true;
    return mcp::core::Unit{};
  }

  bool closed() const noexcept { return closed_; }

 private:
  std::optional<TxMessage> message_;
  bool closed_ = false;
};

int main() {
  SmokeTransport transport;
  mcp::protocol::JsonRpcNotification notification;
  notification.method = "notifications/initialized";
  const auto sent = transport.send(notification);
  const auto closed = transport.close();
  std::istringstream input;
  std::ostringstream output;
  mcp::transport::ClientStdioTransport stdio_transport(input, output);
  mcp::protocol::JsonRpcNotification stdio_notification;
  stdio_notification.method = "notifications/initialized";
  const auto stdio_sent = stdio_transport.send(stdio_notification);
  std::deque<mcp::protocol::JsonRpcMessage> outbound;
  mcp::transport::FunctionTransportOptions<mcp::RoleClient> options;
  options.send = [&](mcp::protocol::JsonRpcMessage message)
      -> mcp::core::Result<mcp::core::Unit> {
    outbound.push_back(std::move(message));
    return mcp::core::Unit{};
  };
  options.receive =
      []() -> mcp::core::Result<std::optional<mcp::protocol::JsonRpcMessage>> {
    return std::nullopt;
  };
  mcp::transport::ClientFunctionTransport function_transport(
      std::move(options));
  mcp::protocol::JsonRpcNotification function_notification;
  function_notification.method = "notifications/initialized";
  const auto function_sent = function_transport.send(function_notification);
  return sent.has_value() && closed.has_value() && transport.closed() &&
                 stdio_sent.has_value() && function_sent.has_value() &&
                 outbound.size() == 1
             ? 0
             : 1;
}
