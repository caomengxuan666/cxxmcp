// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Adapter from the concrete client transport API to the SDK transport
/// contract.

#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "cxxmcp/client/client.hpp"
#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp::client {

namespace detail {

inline core::Error adapter_error(std::string_view message) {
  return errors::make(protocol::ErrorCode::InvalidRequest, std::string(message),
                      {}, "transport");
}

}  // namespace detail

/// @brief Adapts an existing client::Transport to transport::ClientTransport.
///
/// Request responses are queued for receive(). Inbound server-to-client
/// requests and notifications still belong to the concrete transport's start()
/// callback model and are not synthesized by this adapter. This compatibility
/// adapter is not internally synchronized; callers must serialize send(),
/// receive(), and close() access unless the wrapped transport and caller add
/// their own synchronization.
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
      return std::unexpected(detail::adapter_error("client transport is null"));
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

    return std::unexpected(detail::adapter_error(
        "client transport adapter cannot send responses"));
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
  std::unique_ptr<mcp::client::Transport> owned_;
  mcp::client::Transport* transport_ = nullptr;
  std::deque<RxMessage> received_;
};

/// @brief Adapts a transport::ClientTransport to the existing client::Transport
/// API.
///
/// This lets the established Client compatibility API and ClientPeer run over
/// the role-generic transport contract. Inbound notifications and requests
/// received while waiting for the matching response are dispatched through
/// handlers installed with start(). This compatibility adapter expects send()
/// calls to be serialized by the caller.
class ContractTransportAdapter final : public mcp::client::Transport {
 public:
  explicit ContractTransportAdapter(transport::ClientTransport& transport)
      : transport_(&transport) {}

  explicit ContractTransportAdapter(
      std::unique_ptr<transport::ClientTransport> transport)
      : owned_(std::move(transport)), transport_(owned_.get()) {}

  core::Result<protocol::JsonRpcResponse> send(
      const protocol::JsonRpcRequest& request) override {
    if (transport_ == nullptr) {
      return std::unexpected(
          detail::adapter_error("client contract transport is null"));
    }

    const auto sent = transport_->send(protocol::JsonRpcMessage{request});
    if (!sent) {
      return std::unexpected(sent.error());
    }

    while (true) {
      auto received = transport_->receive();
      if (!received) {
        return std::unexpected(received.error());
      }
      if (!received->has_value()) {
        return std::unexpected(detail::adapter_error(
            "client contract transport closed before response"));
      }

      if (auto* response =
              std::get_if<protocol::JsonRpcResponse>(&received->value())) {
        if (response->id.has_value() && *response->id == request.id) {
          return *response;
        }
        return std::unexpected(detail::adapter_error(
            "client contract transport received unexpected response id"));
      }

      if (auto* notification =
              std::get_if<protocol::JsonRpcNotification>(&received->value())) {
        const auto handled = handle_notification(*notification);
        if (!handled) {
          return std::unexpected(handled.error());
        }
        continue;
      }

      if (auto* inbound_request =
              std::get_if<protocol::JsonRpcRequest>(&received->value())) {
        const auto handled = handle_request(*inbound_request);
        if (!handled) {
          return std::unexpected(handled.error());
        }
        continue;
      }
    }
  }

  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) override {
    if (transport_ == nullptr) {
      return std::unexpected(
          detail::adapter_error("client contract transport is null"));
    }
    return transport_->send(protocol::JsonRpcMessage{notification});
  }

  core::Result<core::Unit> start(
      TransportRequestHandler request_handler,
      TransportNotificationHandler notification_handler = {}) override {
    request_handler_ = std::move(request_handler);
    notification_handler_ = std::move(notification_handler);
    return core::Unit{};
  }

  void stop() noexcept override {
    if (transport_ != nullptr) {
      (void)transport_->close();
    }
  }

 private:
  core::Result<core::Unit> handle_notification(
      const protocol::JsonRpcNotification& notification) {
    if (!notification_handler_) {
      return core::Unit{};
    }
    try {
      return notification_handler_(notification);
    } catch (const std::exception& ex) {
      return std::unexpected(errors::handler_failed(ex.what()));
    } catch (...) {
      return std::unexpected(errors::handler_unknown_exception());
    }
  }

  core::Result<core::Unit> handle_request(
      const protocol::JsonRpcRequest& request) {
    protocol::JsonRpcResponse response;
    if (request_handler_) {
      try {
        auto handled = request_handler_(request);
        if (!handled) {
          response = protocol::make_error_response(
              request.id, error_object_from_core_error(handled.error()));
        } else {
          response = std::move(*handled);
        }
      } catch (const std::exception& ex) {
        response = protocol::make_error_response(
            request.id,
            error_object_from_core_error(errors::handler_failed(ex.what())));
      } catch (...) {
        response = protocol::make_error_response(
            request.id,
            error_object_from_core_error(errors::handler_unknown_exception()));
      }
    } else {
      response = protocol::make_error_response(
          request.id,
          protocol::make_error(protocol::ErrorCode::MethodNotFound,
                               "client request handler is not set"));
    }

    auto sent = transport_->send(protocol::JsonRpcMessage{std::move(response)});
    if (!sent) {
      return std::unexpected(sent.error());
    }
    return core::Unit{};
  }

  static protocol::ErrorObject error_object_from_core_error(
      const core::Error& error) {
    return errors::to_json_rpc_error(error);
  }

  std::unique_ptr<transport::ClientTransport> owned_;
  transport::ClientTransport* transport_ = nullptr;
  TransportRequestHandler request_handler_;
  TransportNotificationHandler notification_handler_;
};

}  // namespace mcp::client
