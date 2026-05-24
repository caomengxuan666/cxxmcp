// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Aggregate callback configuration for mcp::client::Client.

#include <utility>

#include "cxxmcp/client/client.hpp"

namespace mcp::client {

inline core::Error handler_method_not_found(std::string_view message) {
  return core::Error{
      static_cast<int>(protocol::ErrorCode::MethodNotFound),
      std::string(message),
      {},
  };
}

/// @brief Contract-style client handler interface.
///
/// Implementations may override only the callbacks they need. Request hooks
/// return std::optional so callers can distinguish "not handled" from a
/// protocol failure.
struct ClientHandlerInterface {
  using InitializedHandler = Client::InitializedHandler;
  using CancelledHandler = Client::CancelledHandler;
  using LoggingMessageHandler = Client::LoggingMessageHandler;
  using ChangedHandler = Client::ListChangedHandler;
  using ResourceUpdatedHandler = Client::ResourceUpdatedHandler;
  using ProgressHandler = Client::ProgressHandler;
  using ElicitationCompleteHandler = Client::ElicitationCompleteHandler;
  using TaskStatusHandler = Client::TaskStatusHandler;
  using RootsListRequestHandler = Client::RootsListRequestHandler;
  using SamplingRequestHandler = Client::SamplingRequestHandler;
  using ElicitationRequestHandler = Client::ElicitationRequestHandler;
  using CustomRequestHandler = Client::CustomRequestHandler;
  using RawNotificationHandler = Client::RawNotificationHandler;

  virtual ~ClientHandlerInterface() = default;

  virtual void on_initialized() const {}
  virtual void on_cancelled(const protocol::RequestId&,
                            std::string_view) const {}
  virtual void on_logging_message(std::string_view, std::string_view) const {}
  virtual void on_tool_list_changed() const {}
  virtual void on_prompt_list_changed() const {}
  virtual void on_resource_list_changed() const {}
  virtual void on_resource_updated(const std::string&) const {}
  virtual void on_progress(const protocol::ProgressNotificationParams&) const {}
  virtual void on_elicitation_complete(std::string_view) const {}
  virtual void on_task_status(const protocol::Task&) const {}
  virtual void on_roots_list_changed() const {}
  virtual std::optional<core::Result<protocol::RootsListResult>>
  on_list_roots_request() const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::CreateMessageResult>>
  on_create_message_request(const protocol::CreateMessageParams&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::CreateElicitationResult>>
  on_create_elicitation_request(
      const protocol::CreateElicitationRequestParam&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::Json>> on_custom_request(
      const protocol::JsonRpcRequest&) const {
    return std::nullopt;
  }
  virtual void on_raw_notification(const protocol::JsonRpcNotification&) const {
  }
};

/// @brief Optional callback bundle for configuring a Client in one call.
///
/// Each member mirrors a Client::on_* registration function. apply_to() and
/// Client::set_handler() install only non-empty members; empty members leave
/// any existing callback on the target Client unchanged.
struct ClientHandler {
  using InitializedHandler = Client::InitializedHandler;
  using CancelledHandler = Client::CancelledHandler;
  using LoggingMessageHandler = Client::LoggingMessageHandler;
  using ChangedHandler = Client::ListChangedHandler;
  using ResourceUpdatedHandler = Client::ResourceUpdatedHandler;
  using ProgressHandler = Client::ProgressHandler;
  using ElicitationCompleteHandler = Client::ElicitationCompleteHandler;
  using TaskStatusHandler = Client::TaskStatusHandler;
  using RootsListRequestHandler = Client::RootsListRequestHandler;
  using SamplingRequestHandler = Client::SamplingRequestHandler;
  using ElicitationRequestHandler = Client::ElicitationRequestHandler;
  using CustomRequestHandler = Client::CustomRequestHandler;
  using RawNotificationHandler = Client::RawNotificationHandler;

  /// Called when the server sends an initialized notification.
  InitializedHandler on_initialized;
  /// Called when the server cancels a request.
  CancelledHandler on_cancelled;
  /// Called for server logging message notifications.
  LoggingMessageHandler on_logging_message;
  /// Called when the server's tool list changes.
  ChangedHandler on_tool_list_changed;
  /// Called when the server's prompt list changes.
  ChangedHandler on_prompt_list_changed;
  /// Called when the server's resource list changes.
  ChangedHandler on_resource_list_changed;
  /// Called when a subscribed resource URI is updated.
  ResourceUpdatedHandler on_resource_updated;
  /// Called for progress notifications.
  ProgressHandler on_progress;
  /// Called when an elicitation flow completes.
  ElicitationCompleteHandler on_elicitation_complete;
  /// Called when a task status notification is received.
  TaskStatusHandler on_task_status;
  /// Called when the client's roots list changes.
  ChangedHandler on_roots_list_changed;
  /// Handles server list-roots requests.
  RootsListRequestHandler on_list_roots_request;
  /// Handles server sampling createMessage requests.
  SamplingRequestHandler on_create_message_request;
  /// Handles server elicitation requests.
  ElicitationRequestHandler on_create_elicitation_request;
  /// Handles custom server requests.
  CustomRequestHandler on_custom_request;
  /// Compatibility alias for on_list_roots_request.
  RootsListRequestHandler on_roots_list_request;
  /// Compatibility alias for on_create_message_request.
  SamplingRequestHandler on_sampling_request;
  /// Compatibility alias for on_create_elicitation_request.
  ElicitationRequestHandler on_elicitation_request;
  /// Observes raw inbound notifications after built-in dispatch.
  RawNotificationHandler on_raw_notification;
  /// Compatibility alias for on_raw_notification.
  RawNotificationHandler on_custom_notification;

  /// @brief Applies all non-empty callbacks to a client.
  /// @param client Client to configure.
  void apply_to(Client& client) const {
    if (on_initialized) {
      client.on_initialized(on_initialized);
    }
    if (on_cancelled) {
      client.on_cancelled(on_cancelled);
    }
    if (on_logging_message) {
      client.on_logging_message(on_logging_message);
    }
    if (on_tool_list_changed) {
      client.on_tool_list_changed(on_tool_list_changed);
    }
    if (on_prompt_list_changed) {
      client.on_prompt_list_changed(on_prompt_list_changed);
    }
    if (on_resource_list_changed) {
      client.on_resource_list_changed(on_resource_list_changed);
    }
    if (on_resource_updated) {
      client.on_resource_updated(on_resource_updated);
    }
    if (on_progress) {
      client.on_progress(on_progress);
    }
    if (on_elicitation_complete) {
      client.on_elicitation_complete(on_elicitation_complete);
    }
    if (on_task_status) {
      client.on_task_status(on_task_status);
    }
    if (on_roots_list_changed) {
      client.on_roots_list_changed(on_roots_list_changed);
    }
    if (on_list_roots_request) {
      client.on_list_roots_request(on_list_roots_request);
    }
    if (on_create_message_request) {
      client.on_create_message_request(on_create_message_request);
    }
    if (on_create_elicitation_request) {
      client.on_create_elicitation_request(on_create_elicitation_request);
    }
    if (on_custom_request) {
      client.on_custom_request(on_custom_request);
    }
    if (on_roots_list_request) {
      client.on_roots_list_request(on_roots_list_request);
    }
    if (on_sampling_request) {
      client.on_sampling_request(on_sampling_request);
    }
    if (on_elicitation_request) {
      client.on_elicitation_request(on_elicitation_request);
    }
    if (on_raw_notification) {
      client.on_raw_notification(on_raw_notification);
    }
    if (on_custom_notification) {
      client.on_custom_notification(on_custom_notification);
    }
  }
};

/// @brief Installs all non-empty callbacks from a ClientHandler.
/// @param handler Callback aggregate to apply.
/// @return Reference to this client for chaining.
inline Client& Client::set_handler(const ClientHandler& handler) {
  handler.apply_to(*this);
  return *this;
}

/// @brief Installs callbacks from a contract-style client handler.
inline Client& Client::set_handler(const ClientHandlerInterface& handler) {
  on_initialized([&handler]() { handler.on_initialized(); });
  on_cancelled([&handler](const protocol::RequestId& request_id,
                          std::string_view reason) {
    handler.on_cancelled(request_id, reason);
  });
  on_logging_message(
      [&handler](std::string_view level, std::string_view message) {
        handler.on_logging_message(level, message);
      });
  on_tool_list_changed([&handler]() { handler.on_tool_list_changed(); });
  on_prompt_list_changed([&handler]() { handler.on_prompt_list_changed(); });
  on_resource_list_changed(
      [&handler]() { handler.on_resource_list_changed(); });
  on_resource_updated(
      [&handler](const std::string& uri) { handler.on_resource_updated(uri); });
  on_progress([&handler](const protocol::ProgressNotificationParams& params) {
    handler.on_progress(params);
  });
  on_elicitation_complete([&handler](std::string_view elicitation_id) {
    handler.on_elicitation_complete(elicitation_id);
  });
  on_task_status(
      [&handler](const protocol::Task& task) { handler.on_task_status(task); });
  on_roots_list_changed([&handler]() { handler.on_roots_list_changed(); });
  on_list_roots_request(
      [&handler]() -> core::Result<protocol::RootsListResult> {
        const auto response = handler.on_list_roots_request();
        if (response.has_value()) {
          return std::move(*response);
        }
        return std::unexpected(handler_method_not_found(
            "client handler does not handle list_roots"));
      });
  on_create_message_request(
      [&handler](const protocol::CreateMessageParams& params)
          -> core::Result<protocol::CreateMessageResult> {
        const auto response = handler.on_create_message_request(params);
        if (response.has_value()) {
          return std::move(*response);
        }
        return std::unexpected(handler_method_not_found(
            "client handler does not handle create_message"));
      });
  on_create_elicitation_request(
      [&handler](const protocol::CreateElicitationRequestParam& params)
          -> core::Result<protocol::CreateElicitationResult> {
        const auto response = handler.on_create_elicitation_request(params);
        if (response.has_value()) {
          return std::move(*response);
        }
        return std::unexpected(handler_method_not_found(
            "client handler does not handle elicitation"));
      });
  on_custom_request([&handler](const protocol::JsonRpcRequest& request)
                        -> core::Result<protocol::Json> {
    const auto response = handler.on_custom_request(request);
    if (response.has_value()) {
      return std::move(*response);
    }
    return std::unexpected(handler_method_not_found(
        "client handler does not handle custom request"));
  });
  on_raw_notification(
      [&handler](const protocol::JsonRpcNotification& notification) {
        handler.on_raw_notification(notification);
      });
  return *this;
}

}  // namespace mcp::client
