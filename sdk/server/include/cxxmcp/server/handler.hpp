// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Aggregate callback configuration for mcp::server::Server.

#include <utility>

#include "cxxmcp/server/server.hpp"

namespace mcp::server {

/// @brief Helper for contract-style handler methods that are not overridden.
inline core::Error handler_method_not_found(std::string_view message) {
  return core::Error{
      static_cast<int>(protocol::ErrorCode::MethodNotFound),
      std::string(message),
      {},
  };
}

/// @brief Contract-style server handler interface.
struct ServerHandlerInterface {
  using JsonHandler = Server::JsonHandler;
  using JsonContextHandler = Server::JsonContextHandler;
  using JsonRequestContextHandler = Server::JsonRequestContextHandler;
  using LoggingHandler = Server::LoggingHandler;
  using RawRequestHandler = Server::RawRequestHandler;
  using RawNotificationHandler = Server::RawNotificationHandler;
  using TaskListHandler = Server::TaskListHandler;
  using TaskGetHandler = Server::TaskGetHandler;
  using TaskCancelHandler = Server::TaskCancelHandler;
  using TaskResultHandler = Server::TaskResultHandler;
  using RootsListChangedHandler = Server::RootsListChangedHandler;
  using ProgressHandler = Server::ProgressHandler;
  using ListChangedHandler = Server::ListChangedHandler;
  using ResourceUpdatedHandler = Server::ResourceUpdatedHandler;

  virtual ~ServerHandlerInterface() = default;

  virtual std::optional<core::Result<protocol::Json>> on_completion(
      const protocol::Json&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::Json>> on_completion(
      const protocol::Json& params, const SessionContext& context) const {
    (void)context;
    return on_completion(params);
  }
  virtual std::optional<core::Result<protocol::Json>> on_completion(
      const protocol::Json& params, const SessionContext& context,
      CancellationToken cancellation) const {
    (void)cancellation;
    return on_completion(params, context);
  }
  virtual std::optional<core::Result<protocol::Json>> on_sampling(
      const protocol::Json&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::Json>> on_sampling(
      const protocol::Json& params, const SessionContext& context) const {
    (void)context;
    return on_sampling(params);
  }
  virtual std::optional<core::Result<protocol::Json>> on_sampling(
      const protocol::Json& params, const SessionContext& context,
      CancellationToken cancellation) const {
    (void)cancellation;
    return on_sampling(params, context);
  }
  virtual void on_logging(std::string_view, std::string_view) const {}
  virtual std::optional<protocol::JsonRpcResponse> on_raw_request(
      const protocol::JsonRpcRequest&, const SessionContext&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<core::Unit>> on_raw_notification(
      const protocol::JsonRpcNotification&, const SessionContext&) const {
    return std::nullopt;
  }
  virtual std::optional<protocol::JsonRpcResponse> on_custom_request(
      const protocol::JsonRpcRequest& request,
      const SessionContext& context) const {
    return on_raw_request(request, context);
  }
  virtual std::optional<core::Result<core::Unit>> on_custom_notification(
      const protocol::JsonRpcNotification& notification,
      const SessionContext& context) const {
    return on_raw_notification(notification, context);
  }
  virtual std::optional<core::Result<protocol::TaskListResult>> on_task_list(
      const protocol::TaskListParams&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::TaskListResult>> on_task_list(
      const protocol::TaskListParams& params,
      const SessionContext& context) const {
    (void)context;
    return on_task_list(params);
  }
  virtual std::optional<core::Result<protocol::Task>> on_task_get(
      const protocol::TaskGetParams&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::Task>> on_task_get(
      const protocol::TaskGetParams& params,
      const SessionContext& context) const {
    (void)context;
    return on_task_get(params);
  }
  virtual std::optional<core::Result<protocol::Task>> on_task_cancel(
      const protocol::TaskCancelParams&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::Task>> on_task_cancel(
      const protocol::TaskCancelParams& params,
      const SessionContext& context) const {
    (void)context;
    return on_task_cancel(params);
  }
  virtual std::optional<core::Result<protocol::Json>> on_task_result(
      const protocol::TaskResultParams&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::Json>> on_task_result(
      const protocol::TaskResultParams& params,
      const SessionContext& context) const {
    (void)context;
    return on_task_result(params);
  }
  virtual std::optional<core::Result<core::Unit>> on_progress(
      const protocol::ProgressNotificationParams&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<core::Unit>> on_progress(
      const protocol::ProgressNotificationParams& params,
      const SessionContext& context) const {
    (void)context;
    return on_progress(params);
  }
  virtual std::optional<core::Result<core::Unit>> on_roots_list_changed()
      const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<core::Unit>> on_roots_list_changed(
      const SessionContext& context) const {
    (void)context;
    return on_roots_list_changed();
  }
  virtual std::optional<core::Result<core::Unit>> on_tool_list_changed() const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<core::Unit>> on_tool_list_changed(
      const SessionContext& context) const {
    (void)context;
    return on_tool_list_changed();
  }
  virtual std::optional<core::Result<core::Unit>> on_prompt_list_changed()
      const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<core::Unit>> on_prompt_list_changed(
      const SessionContext& context) const {
    (void)context;
    return on_prompt_list_changed();
  }
  virtual std::optional<core::Result<core::Unit>> on_resource_list_changed()
      const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<core::Unit>> on_resource_list_changed(
      const SessionContext& context) const {
    (void)context;
    return on_resource_list_changed();
  }
  virtual std::optional<core::Result<core::Unit>> on_resource_updated(
      const std::string&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<core::Unit>> on_resource_updated(
      const std::string& uri, const SessionContext& context) const {
    (void)context;
    return on_resource_updated(uri);
  }
};

/// @brief Optional callback bundle for configuring a Server in one call.
///
/// Each member mirrors a Server::set_*_handler() function. apply_to() and
/// Server::set_handler() install only non-empty members; empty members leave
/// any existing callback on the target Server unchanged.
struct ServerHandler {
  using JsonHandler = Server::JsonHandler;
  using JsonContextHandler = Server::JsonContextHandler;
  using JsonRequestContextHandler = Server::JsonRequestContextHandler;
  using LoggingHandler = Server::LoggingHandler;
  using RawRequestHandler = Server::RawRequestHandler;
  using RawNotificationHandler = Server::RawNotificationHandler;
  using TaskListHandler = Server::TaskListHandler;
  using TaskGetHandler = Server::TaskGetHandler;
  using TaskCancelHandler = Server::TaskCancelHandler;
  using TaskResultHandler = Server::TaskResultHandler;
  using RootsListChangedHandler = Server::RootsListChangedHandler;
  using ProgressHandler = Server::ProgressHandler;
  using ListChangedHandler = Server::ListChangedHandler;
  using ResourceUpdatedHandler = Server::ResourceUpdatedHandler;

  /// Handles completion requests.
  JsonHandler on_completion;
  /// Handles sampling requests.
  JsonHandler on_sampling;
  /// Handles logging notifications.
  LoggingHandler on_logging;
  /// Optionally handles raw requests before built-in dispatch.
  RawRequestHandler on_raw_request;
  /// Handles raw notifications.
  RawNotificationHandler on_raw_notification;
  /// Optionally handles custom requests.
  RawRequestHandler on_custom_request;
  /// Handles custom notifications.
  RawNotificationHandler on_custom_notification;
  /// Handles task list requests.
  TaskListHandler on_task_list;
  /// Handles task get requests.
  TaskGetHandler on_task_get;
  /// Handles task cancel requests.
  TaskCancelHandler on_task_cancel;
  /// Handles task result requests.
  TaskResultHandler on_task_result;
  /// Handles progress notifications from clients.
  ProgressHandler on_progress;
  /// Handles roots-list-changed notifications from clients.
  RootsListChangedHandler on_roots_list_changed;
  /// Handles tool-list-changed notifications from clients.
  ListChangedHandler on_tool_list_changed;
  /// Handles prompt-list-changed notifications from clients.
  ListChangedHandler on_prompt_list_changed;
  /// Handles resource-list-changed notifications from clients.
  ListChangedHandler on_resource_list_changed;
  /// Handles resource-updated notifications from clients.
  ResourceUpdatedHandler on_resource_updated;
  /// Handles completion requests with session context.
  JsonContextHandler on_completion_with_context;
  /// Handles sampling requests with session context.
  JsonContextHandler on_sampling_with_context;
  /// Handles completion requests with session context and cancellation.
  JsonRequestContextHandler on_completion_with_request_context;
  /// Handles sampling requests with session context and cancellation.
  JsonRequestContextHandler on_sampling_with_request_context;

  /// @brief Applies all non-empty callbacks to a server.
  /// @param server Server to configure.
  void apply_to(Server& server) const {
    if (on_completion) {
      server.set_completion_handler(on_completion);
    }
    if (on_sampling) {
      server.set_sampling_handler(on_sampling);
    }
    if (on_logging) {
      server.set_logging_handler(on_logging);
    }
    if (on_raw_request) {
      server.set_raw_request_handler(on_raw_request);
    }
    if (on_raw_notification) {
      server.set_raw_notification_handler(on_raw_notification);
    }
    if (on_custom_request) {
      server.set_custom_request_handler(on_custom_request);
    }
    if (on_custom_notification) {
      server.set_custom_notification_handler(on_custom_notification);
    }
    if (on_task_list) {
      server.set_task_list_handler(on_task_list);
    }
    if (on_task_get) {
      server.set_task_get_handler(on_task_get);
    }
    if (on_task_cancel) {
      server.set_task_cancel_handler(on_task_cancel);
    }
    if (on_task_result) {
      server.set_task_result_handler(on_task_result);
    }
    if (on_progress) {
      server.set_progress_handler(on_progress);
    }
    if (on_roots_list_changed) {
      server.set_roots_list_changed_handler(on_roots_list_changed);
    }
    if (on_tool_list_changed) {
      server.set_tool_list_changed_handler(on_tool_list_changed);
    }
    if (on_prompt_list_changed) {
      server.set_prompt_list_changed_handler(on_prompt_list_changed);
    }
    if (on_resource_list_changed) {
      server.set_resource_list_changed_handler(on_resource_list_changed);
    }
    if (on_resource_updated) {
      server.set_resource_updated_handler(on_resource_updated);
    }
    if (on_completion_with_context) {
      server.set_completion_handler(on_completion_with_context);
    }
    if (on_sampling_with_context) {
      server.set_sampling_handler(on_sampling_with_context);
    }
    if (on_completion_with_request_context) {
      server.set_completion_handler(on_completion_with_request_context);
    }
    if (on_sampling_with_request_context) {
      server.set_sampling_handler(on_sampling_with_request_context);
    }
  }
};

/// @brief Installs all non-empty callbacks from a ServerHandler.
/// @return Reference to this server for chaining.
inline Server& Server::set_handler(const ServerHandler& handler) {
  handler.apply_to(*this);
  return *this;
}

/// @brief Installs callbacks from a contract-style server handler.
inline Server& Server::set_handler(const ServerHandlerInterface& handler) {
  set_completion_handler(
      [&handler](
          const protocol::Json& request, const SessionContext& context,
          CancellationToken cancellation) -> core::Result<protocol::Json> {
        const auto response =
            handler.on_completion(request, context, cancellation);
        if (response.has_value()) {
          return std::move(*response);
        }
        return std::unexpected(handler_method_not_found(
            "server handler does not handle completion"));
      });
  set_sampling_handler(
      [&handler](
          const protocol::Json& request, const SessionContext& context,
          CancellationToken cancellation) -> core::Result<protocol::Json> {
        const auto response =
            handler.on_sampling(request, context, cancellation);
        if (response.has_value()) {
          return std::move(*response);
        }
        return std::unexpected(handler_method_not_found(
            "server handler does not handle sampling"));
      });
  set_logging_handler(
      [&handler](std::string_view level, std::string_view message) {
        handler.on_logging(level, message);
      });
  set_raw_request_handler([&handler](const protocol::JsonRpcRequest& request,
                                     const SessionContext& context)
                              -> std::optional<protocol::JsonRpcResponse> {
    return handler.on_raw_request(request, context);
  });
  set_raw_notification_handler(
      [&handler](const protocol::JsonRpcNotification& notification,
                 const SessionContext& context) -> core::Result<core::Unit> {
        const auto response =
            handler.on_raw_notification(notification, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_custom_request_handler([&handler](const protocol::JsonRpcRequest& request,
                                        const SessionContext& context)
                                 -> std::optional<protocol::JsonRpcResponse> {
    return handler.on_custom_request(request, context);
  });
  set_custom_notification_handler(
      [&handler](const protocol::JsonRpcNotification& notification,
                 const SessionContext& context) -> core::Result<core::Unit> {
        const auto response =
            handler.on_custom_notification(notification, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_task_list_handler([&handler](const protocol::TaskListParams& params,
                                   const SessionContext& context)
                            -> core::Result<protocol::TaskListResult> {
    const auto response = handler.on_task_list(params, context);
    if (response.has_value()) {
      return std::move(*response);
    }
    return std::unexpected(
        handler_method_not_found("server handler does not handle task list"));
  });
  set_task_get_handler([&handler](const protocol::TaskGetParams& params,
                                  const SessionContext& context)
                           -> core::Result<protocol::Task> {
    const auto response = handler.on_task_get(params, context);
    if (response.has_value()) {
      return std::move(*response);
    }
    return std::unexpected(
        handler_method_not_found("server handler does not handle task get"));
  });
  set_task_cancel_handler([&handler](const protocol::TaskCancelParams& params,
                                     const SessionContext& context)
                              -> core::Result<protocol::Task> {
    const auto response = handler.on_task_cancel(params, context);
    if (response.has_value()) {
      return std::move(*response);
    }
    return std::unexpected(
        handler_method_not_found("server handler does not handle task cancel"));
  });
  set_task_result_handler([&handler](const protocol::TaskResultParams& params,
                                     const SessionContext& context)
                              -> core::Result<protocol::Json> {
    const auto response = handler.on_task_result(params, context);
    if (response.has_value()) {
      return std::move(*response);
    }
    return std::unexpected(
        handler_method_not_found("server handler does not handle task result"));
  });
  set_progress_handler(
      [&handler](const protocol::ProgressNotificationParams& params,
                 const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler.on_progress(params, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_roots_list_changed_handler(
      [&handler](const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler.on_roots_list_changed(context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_tool_list_changed_handler(
      [&handler](const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler.on_tool_list_changed(context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_prompt_list_changed_handler(
      [&handler](const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler.on_prompt_list_changed(context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_resource_list_changed_handler(
      [&handler](const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler.on_resource_list_changed(context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_resource_updated_handler(
      [&handler](const std::string& uri,
                 const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler.on_resource_updated(uri, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  return *this;
}

}  // namespace mcp::server
