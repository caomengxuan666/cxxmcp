// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/sdk.hpp>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

class CanonicalClientTransport final : public mcp::transport::ClientTransport {
 public:
  std::string_view name() const noexcept override { return "canonical-smoke"; }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    const auto* request = std::get_if<mcp::protocol::JsonRpcRequest>(&message);
    if (request == nullptr) {
      return mcp::core::Unit{};
    }

    mcp::protocol::JsonRpcResponse response;
    response.id = request->id;
    if (request->method == std::string(mcp::protocol::ToolsListMethod)) {
      response.result = mcp::protocol::Json{
          {"tools",
           mcp::protocol::Json::array({mcp::protocol::Json{
               {"name", "canonical-tool"},
               {"description", "C++17 smoke tool"},
               {"inputSchema", mcp::protocol::Json{{"type", "object"}}}}})}};
    } else {
      response.result = mcp::protocol::Json::object();
    }
    inbound_.push_back(response);
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    if (inbound_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(inbound_.front());
    inbound_.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    closed_ = true;
    return mcp::core::Unit{};
  }

  bool closed() const noexcept { return closed_; }

 private:
  std::deque<RxMessage> inbound_;
  bool closed_ = false;
};

int main() {
  mcp::ServerPeer server;
  auto service = mcp::make_service(std::move(server));
  if (!service.peer().list_tools().empty()) {
    return 1;
  }

  auto transport = std::make_unique<CanonicalClientTransport>();
  auto* transport_ptr = transport.get();
  mcp::ClientPeer client(std::move(transport));
  const auto tools = client.list_tools();
  if (!tools.has_value() || tools->size() != 1 ||
      tools->front().name != "canonical-tool") {
    return 1;
  }
  client.stop();
  return transport_ptr->closed() ? 0 : 1;
}
