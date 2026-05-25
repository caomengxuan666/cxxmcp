// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/transport.hpp>
#include <optional>
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
  const auto sent = transport.send(mcp::protocol::JsonRpcNotification{
      .method = "notifications/initialized",
  });
  const auto closed = transport.close();
  return sent.has_value() && closed.has_value() && transport.closed() ? 0 : 1;
}
