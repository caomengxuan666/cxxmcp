// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Adapter from the concrete server transport API to the SDK transport
/// contract.

#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server/transport.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp::server {

namespace detail {

inline core::Error adapter_error(std::string_view message) {
  return errors::make(protocol::ErrorCode::InvalidRequest, std::string(message),
                      {}, "transport");
}

}  // namespace detail

/// @brief Adapts an existing server::Transport to transport::ServerTransport.
///
/// Outbound request responses are queued for receive(). Inbound client
/// messages still belong to the concrete transport's start() callback model and
/// are not synthesized by this adapter. This compatibility adapter is not
/// internally synchronized; callers must serialize send(), receive(), and
/// close() access unless the wrapped transport and caller add their own
/// synchronization.
class TransportContractAdapter final : public transport::ServerTransport {
 public:
  explicit TransportContractAdapter(mcp::server::Transport& transport)
      : transport_(&transport) {}

  explicit TransportContractAdapter(
      std::unique_ptr<mcp::server::Transport> transport)
      : owned_(std::move(transport)), transport_(owned_.get()) {}

  TransportContractAdapter(const TransportContractAdapter&) = delete;
  TransportContractAdapter& operator=(const TransportContractAdapter&) = delete;

  TransportContractAdapter(TransportContractAdapter&& other) noexcept
      : owned_(std::move(other.owned_)),
        transport_(owned_ ? owned_.get() : other.transport_),
        received_(std::move(other.received_)) {
    other.transport_ = nullptr;
  }

  TransportContractAdapter& operator=(
      TransportContractAdapter&& other) noexcept {
    if (this != &other) {
      owned_ = std::move(other.owned_);
      transport_ = owned_ ? owned_.get() : other.transport_;
      received_ = std::move(other.received_);
      other.transport_ = nullptr;
    }
    return *this;
  }

  std::string_view name() const noexcept override {
    return transport_ == nullptr ? "server-transport-adapter"
                                 : transport_->name();
  }

  core::Result<core::Unit> send(TxMessage message) override {
    if (transport_ == nullptr) {
      return mcp::core::unexpected(
          detail::adapter_error("server transport is null"));
    }

    if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      auto response = transport_->send_request(*request);
      if (!response) {
        return mcp::core::unexpected(response.error());
      }
      received_.push_back(std::move(*response));
      return core::Unit{};
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      return transport_->send_notification(*notification);
    }

    return mcp::core::unexpected(detail::adapter_error(
        "server transport adapter cannot send responses"));
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
  std::unique_ptr<mcp::server::Transport> owned_;
  mcp::server::Transport* transport_ = nullptr;
  std::deque<RxMessage> received_;
};

/// @brief Adapts a transport::ServerTransport to the existing server::Transport
/// API.
///
/// start() drains inbound messages from the role-generic transport, dispatches
/// requests and notifications through server handlers, and writes request
/// responses back to the transport. send_request() and send_notification()
/// provide the legacy server-side outbound API over the same message contract.
/// send_request() calls must be serialized by the caller.
class ContractTransportAdapter final : public mcp::server::Transport {
 public:
  explicit ContractTransportAdapter(transport::ServerTransport& transport)
      : transport_(&transport) {}

  explicit ContractTransportAdapter(
      std::unique_ptr<transport::ServerTransport> transport)
      : owned_(std::move(transport)), transport_(owned_.get()) {}

  ContractTransportAdapter(const ContractTransportAdapter&) = delete;
  ContractTransportAdapter& operator=(const ContractTransportAdapter&) = delete;

  ContractTransportAdapter(ContractTransportAdapter&& other) noexcept
      : owned_(std::move(other.owned_)),
        transport_(owned_ ? owned_.get() : other.transport_),
        request_handler_(std::move(other.request_handler_)),
        notification_handler_(std::move(other.notification_handler_)),
        client_capabilities_(std::move(other.client_capabilities_)),
        stopped_(other.stopped_) {
    other.transport_ = nullptr;
    other.stopped_ = true;
  }

  ContractTransportAdapter& operator=(
      ContractTransportAdapter&& other) noexcept {
    if (this != &other) {
      owned_ = std::move(other.owned_);
      transport_ = owned_ ? owned_.get() : other.transport_;
      request_handler_ = std::move(other.request_handler_);
      notification_handler_ = std::move(other.notification_handler_);
      client_capabilities_ = std::move(other.client_capabilities_);
      stopped_ = other.stopped_;
      other.transport_ = nullptr;
      other.stopped_ = true;
    }
    return *this;
  }

  core::Result<core::Unit> start(
      RequestHandler handler,
      NotificationHandler notification_handler = {}) override {
    request_handler_ = std::move(handler);
    notification_handler_ = std::move(notification_handler);
    if (transport_ == nullptr) {
      return mcp::core::unexpected(
          detail::adapter_error("server contract transport is null"));
    }

    stopped_ = false;
    while (!stopped_) {
      auto received = transport_->receive();
      if (!received) {
        return mcp::core::unexpected(received.error());
      }
      if (!received->has_value()) {
        return core::Unit{};
      }

      const auto handled = handle_inbound(received->value());
      if (!handled) {
        return mcp::core::unexpected(handled.error());
      }
    }
    return core::Unit{};
  }

  core::Result<protocol::JsonRpcResponse> send_request(
      const protocol::JsonRpcRequest& request) override {
    if (transport_ == nullptr) {
      return mcp::core::unexpected(
          detail::adapter_error("server contract transport is null"));
    }

    const auto sent = transport_->send(protocol::JsonRpcMessage{request});
    if (!sent) {
      return mcp::core::unexpected(sent.error());
    }

    while (true) {
      auto received = transport_->receive();
      if (!received) {
        return mcp::core::unexpected(received.error());
      }
      if (!received->has_value()) {
        return mcp::core::unexpected(detail::adapter_error(
            "server contract transport closed before response"));
      }

      if (auto* response =
              std::get_if<protocol::JsonRpcResponse>(&received->value())) {
        if (response->id.has_value() && *response->id == request.id) {
          return *response;
        }
        return mcp::core::unexpected(detail::adapter_error(
            "server contract transport received unexpected response id"));
      }

      const auto handled = handle_inbound(received->value());
      if (!handled) {
        return mcp::core::unexpected(handled.error());
      }
    }
  }

  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) override {
    if (transport_ == nullptr) {
      return mcp::core::unexpected(
          detail::adapter_error("server contract transport is null"));
    }
    return transport_->send(protocol::JsonRpcMessage{notification});
  }

  std::optional<protocol::ClientCapabilities> client_capabilities()
      const override {
    return client_capabilities_;
  }

  void stop() noexcept override {
    stopped_ = true;
    if (transport_ != nullptr) {
      (void)transport_->close();
    }
  }

  std::string_view name() const noexcept override {
    return transport_ == nullptr ? "server-contract-transport-adapter"
                                 : transport_->name();
  }

 private:
  core::Result<core::Unit> handle_inbound(
      const protocol::JsonRpcMessage& message) {
    if (auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      return handle_request(*request);
    }
    if (auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      return handle_notification(*notification);
    }
    return core::Unit{};
  }

  core::Result<core::Unit> handle_request(
      const protocol::JsonRpcRequest& request) {
    protocol::JsonRpcResponse response;
    if (request_handler_) {
      try {
        auto handled = request_handler_(request, session_context());
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
                               "server request handler is not set"));
    }

    if (request.method == protocol::InitializeMethod) {
      if (request.params.is_object() &&
          request.params.contains("capabilities")) {
        client_capabilities_ = protocol::client_capabilities_from_json(
            request.params.at("capabilities"));
      } else {
        client_capabilities_.reset();
      }
    }

    auto sent = transport_->send(protocol::JsonRpcMessage{std::move(response)});
    if (!sent) {
      return mcp::core::unexpected(sent.error());
    }
    return core::Unit{};
  }

  core::Result<core::Unit> handle_notification(
      const protocol::JsonRpcNotification& notification) {
    if (!notification_handler_) {
      return core::Unit{};
    }
    try {
      return notification_handler_(notification, session_context());
    } catch (const std::exception& ex) {
      return mcp::core::unexpected(errors::handler_failed(ex.what()));
    } catch (...) {
      return mcp::core::unexpected(errors::handler_unknown_exception());
    }
  }

  SessionContext session_context() noexcept {
    SessionContext context;
    context.remote_address = std::string(name());
    context.transport = this;
    context.transport_lifetime = lifetime_token();
    return context;
  }

  static protocol::ErrorObject error_object_from_core_error(
      const core::Error& error) {
    return errors::to_json_rpc_error(error);
  }

  std::unique_ptr<transport::ServerTransport> owned_;
  transport::ServerTransport* transport_ = nullptr;
  RequestHandler request_handler_;
  NotificationHandler notification_handler_;
  std::optional<protocol::ClientCapabilities> client_capabilities_;
  bool stopped_ = false;
};

}  // namespace mcp::server
