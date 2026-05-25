// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Adapter from the concrete client transport API to the SDK transport
/// contract.

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "cxxmcp/client/client.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp::client {

/// @brief Adapts an existing client::Transport to transport::ClientTransport.
///
/// Request responses are queued for receive(). Inbound server-to-client
/// requests and notifications still belong to the concrete transport's start()
/// callback model and are not synthesized by this adapter.
class TransportContractAdapter final : public transport::ClientTransport {
 public:
  explicit TransportContractAdapter(mcp::client::Transport& transport)
      : transport_(&transport) {}

  explicit TransportContractAdapter(
      std::unique_ptr<mcp::client::Transport> transport)
      : owned_(std::move(transport)), transport_(owned_.get()) {}

  std::string_view name() const noexcept override {
    return "client-transport-adapter";
  }

  core::Result<core::Unit> send(TxMessage message) override {
    if (transport_ == nullptr) {
      return std::unexpected(adapter_error("client transport is null"));
    }

    if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      auto response = transport_->send(*request);
      if (!response) {
        return std::unexpected(response.error());
      }
      received_.push_back(std::move(*response));
      return core::Unit{};
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      return transport_->send_notification(*notification);
    }

    return std::unexpected(
        adapter_error("client transport adapter cannot send responses"));
  }

  core::Result<std::optional<RxMessage>> receive() override {
    if (received_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(received_.front());
    received_.pop_front();
    return message;
  }

  core::Result<core::Unit> close() override {
    if (transport_ != nullptr) {
      transport_->stop();
    }
    return core::Unit{};
  }

 private:
  static core::Error adapter_error(std::string_view message) {
    return core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        std::string(message),
        {},
    };
  }

  std::unique_ptr<mcp::client::Transport> owned_;
  mcp::client::Transport* transport_ = nullptr;
  std::deque<RxMessage> received_;
};

}  // namespace mcp::client
