// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Adapter from the concrete server transport API to the SDK transport
/// contract.

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "cxxmcp/server/transport.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp::server {

/// @brief Adapts an existing server::Transport to transport::ServerTransport.
///
/// Outbound request responses are queued for receive(). Inbound client
/// messages still belong to the concrete transport's start() callback model and
/// are not synthesized by this adapter.
class TransportContractAdapter final : public transport::ServerTransport {
 public:
  explicit TransportContractAdapter(mcp::server::Transport& transport)
      : transport_(&transport) {}

  explicit TransportContractAdapter(
      std::unique_ptr<mcp::server::Transport> transport)
      : owned_(std::move(transport)), transport_(owned_.get()) {}

  std::string_view name() const noexcept override {
    return transport_ == nullptr ? "server-transport-adapter"
                                 : transport_->name();
  }

  core::Result<core::Unit> send(TxMessage message) override {
    if (transport_ == nullptr) {
      return std::unexpected(adapter_error("server transport is null"));
    }

    if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      auto response = transport_->send_request(*request);
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
        adapter_error("server transport adapter cannot send responses"));
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

  std::unique_ptr<mcp::server::Transport> owned_;
  mcp::server::Transport* transport_ = nullptr;
  std::deque<RxMessage> received_;
};

}  // namespace mcp::server
