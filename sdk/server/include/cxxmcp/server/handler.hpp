// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Aggregate callback configuration for mcp::server::Server.

#include <memory>
#include <stdexcept>
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
  using ToolsListHandler = Server::ToolsListHandler;
  using PromptsListHandler = Server::PromptsListHandler;
  using ResourcesListHandler = Server::ResourcesListHandler;
  using ResourceTemplatesListHandler = Server::ResourceTemplatesListHandler;
  using TaskListHandler = Server::TaskListHandler;
  using TaskGetHandler = Server::TaskGetHandler;
  using TaskCancelHandler = Server::TaskCancelHandler;
  using TaskResultHandler = Server::TaskResultHandler;
  using RootsListChangedHandler = Server::RootsListChangedHandler;
  using ProgressHandler = Server::ProgressHandler;
  using ListChangedHandler = Server::ListChangedHandler;
  using ResourceUpdatedHandler = Server::ResourceUpdatedHandler;

  virtual ~ServerHandlerInterface() = default;

  virtual std::optional<core::Result<protocol::ToolsListResult>> on_list_tools(
      const SessionContext&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::ToolsListResult>> on_list_tools(
      const protocol::PaginatedRequestParams& params,
      const SessionContext& context) const {
    (void)params;
    return on_list_tools(context);
  }
  virtual std::optional<core::Result<protocol::ToolDefinition>> on_get_tool(
      std::string_view, const SessionContext&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::PromptsListResult>>
  on_list_prompts(const SessionContext&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::PromptsListResult>>
  on_list_prompts(const protocol::PaginatedRequestParams& params,
                  const SessionContext& context) const {
    (void)params;
    return on_list_prompts(context);
  }
  virtual std::optional<core::Result<protocol::ResourcesListResult>>
  on_list_resources(const SessionContext&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::ResourcesListResult>>
  on_list_resources(const protocol::PaginatedRequestParams& params,
                    const SessionContext& context) const {
    (void)params;
    return on_list_resources(context);
  }
  virtual std::optional<core::Result<protocol::ResourceTemplatesListResult>>
  on_list_resource_templates(const SessionContext&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::ResourceTemplatesListResult>>
  on_list_resource_templates(const protocol::PaginatedRequestParams& params,
                             const SessionContext& context) const {
    (void)params;
    return on_list_resource_templates(context);
  }
  virtual std::optional<core::Result<protocol::ToolResult>> on_call_tool(
      const protocol::ToolCall&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::ToolResult>> on_call_tool(
      const protocol::ToolCall& call, const SessionContext& context) const {
    (void)context;
    return on_call_tool(call);
  }
  virtual std::optional<core::Result<protocol::ToolResult>> on_call_tool(
      const protocol::ToolCall& call, const SessionContext& context,
      CancellationToken cancellation) const {
    (void)cancellation;
    return on_call_tool(call, context);
  }
  virtual std::optional<core::Result<protocol::PromptsGetResult>> on_get_prompt(
      const protocol::PromptsGetParams&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::PromptsGetResult>> on_get_prompt(
      const protocol::PromptsGetParams& params,
      const SessionContext& context) const {
    (void)context;
    return on_get_prompt(params);
  }
  virtual std::optional<core::Result<protocol::PromptsGetResult>> on_get_prompt(
      const protocol::PromptsGetParams& params, const SessionContext& context,
      CancellationToken cancellation) const {
    (void)cancellation;
    return on_get_prompt(params, context);
  }
  virtual std::optional<core::Result<protocol::ResourcesReadResult>>
  on_read_resource(const protocol::ResourcesReadParams&) const {
    return std::nullopt;
  }
  virtual std::optional<core::Result<protocol::ResourcesReadResult>>
  on_read_resource(const protocol::ResourcesReadParams& params,
                   const SessionContext& context) const {
    (void)context;
    return on_read_resource(params);
  }
  virtual std::optional<core::Result<protocol::ResourcesReadResult>>
  on_read_resource(const protocol::ResourcesReadParams& params,
                   const SessionContext& context,
                   CancellationToken cancellation) const {
    (void)cancellation;
    return on_read_resource(params, context);
  }

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

inline protocol::JsonRpcResponse server_handler_error_response(
    const protocol::JsonRpcRequest& request, const core::Error& error) {
  return protocol::make_error_response(
      std::optional<protocol::RequestId>{request.id},
      protocol::make_error(error.code, error.message,
                           error.detail.empty()
                               ? std::nullopt
                               : std::optional<protocol::Json>{error.detail}));
}

template <class T, class Serializer>
inline std::optional<protocol::JsonRpcResponse> server_handler_result_response(
    const protocol::JsonRpcRequest& request, const core::Result<T>& result,
    Serializer serializer) {
  if (!result) {
    return server_handler_error_response(request, result.error());
  }
  return protocol::make_response(request.id, serializer(*result));
}

inline std::optional<protocol::JsonRpcResponse>
dispatch_server_handler_discovery_request(
    const ServerHandlerInterface& handler,
    const protocol::JsonRpcRequest& request, const SessionContext& context) {
  if (request.method == protocol::ToolsListMethod) {
    const auto params =
        protocol::paginated_request_params_from_json(request.params);
    if (!params) {
      return server_handler_error_response(
          request, core::Error{
                       static_cast<int>(protocol::ErrorCode::InvalidParams),
                       "tools/list params must be an object with an optional "
                       "string cursor",
                       {},
                   });
    }
    const auto result = handler.on_list_tools(*params, context);
    if (result.has_value()) {
      return server_handler_result_response(
          request, *result, [](const protocol::ToolsListResult& value) {
            return protocol::tools_list_result_to_json(value);
          });
    }
    return std::nullopt;
  }

  if (request.method == protocol::ToolsGetMethod) {
    if (!request.params.is_object() || !request.params.contains("name") ||
        !request.params.at("name").is_string()) {
      return server_handler_error_response(
          request, core::Error{
                       static_cast<int>(protocol::ErrorCode::InvalidParams),
                       "tools/get requires a string name",
                       {},
                   });
    }
    const auto result = handler.on_get_tool(
        request.params.at("name").get<std::string>(), context);
    if (result.has_value()) {
      return server_handler_result_response(
          request, *result, [](const protocol::ToolDefinition& value) {
            return protocol::tool_definition_to_json(value);
          });
    }
    return std::nullopt;
  }

  if (request.method == protocol::PromptsListMethod) {
    const auto params =
        protocol::paginated_request_params_from_json(request.params);
    if (!params) {
      return server_handler_error_response(
          request, core::Error{
                       static_cast<int>(protocol::ErrorCode::InvalidParams),
                       "prompts/list params must be an object with an optional "
                       "string cursor",
                       {},
                   });
    }
    const auto result = handler.on_list_prompts(*params, context);
    if (result.has_value()) {
      return server_handler_result_response(
          request, *result, [](const protocol::PromptsListResult& value) {
            return protocol::prompts_list_result_to_json(value);
          });
    }
    return std::nullopt;
  }

  if (request.method == protocol::ResourcesListMethod) {
    const auto params =
        protocol::paginated_request_params_from_json(request.params);
    if (!params) {
      return server_handler_error_response(
          request, core::Error{
                       static_cast<int>(protocol::ErrorCode::InvalidParams),
                       "resources/list params must be an object with an "
                       "optional string cursor",
                       {},
                   });
    }
    const auto result = handler.on_list_resources(*params, context);
    if (result.has_value()) {
      return server_handler_result_response(
          request, *result, [](const protocol::ResourcesListResult& value) {
            return protocol::resources_list_result_to_json(value);
          });
    }
    return std::nullopt;
  }

  if (request.method == protocol::ResourcesTemplatesListMethod) {
    const auto params =
        protocol::paginated_request_params_from_json(request.params);
    if (!params) {
      return server_handler_error_response(
          request, core::Error{
                       static_cast<int>(protocol::ErrorCode::InvalidParams),
                       "resources/templates/list params must be an object with "
                       "an optional string cursor",
                       {},
                   });
    }
    const auto result = handler.on_list_resource_templates(*params, context);
    if (result.has_value()) {
      return server_handler_result_response(
          request, *result,
          [](const protocol::ResourceTemplatesListResult& value) {
            return protocol::resource_templates_list_result_to_json(value);
          });
    }
  }
  return std::nullopt;
}

inline std::optional<protocol::JsonRpcResponse> dispatch_server_handler_request(
    const ServerHandlerInterface& handler,
    const protocol::JsonRpcRequest& request, const SessionContext& context,
    CancellationToken cancellation) {
  const auto discovery_response =
      dispatch_server_handler_discovery_request(handler, request, context);
  if (discovery_response.has_value()) {
    return discovery_response;
  }

  if (request.method == protocol::ToolsCallMethod) {
    const auto call = protocol::tool_call_from_json(request.params);
    if (!call) {
      return server_handler_error_response(request, call.error());
    }
    if (call->task.has_value()) {
      return std::nullopt;
    }
    const auto result = handler.on_call_tool(*call, context, cancellation);
    if (result.has_value()) {
      return server_handler_result_response(
          request, *result, [](const protocol::ToolResult& value) {
            return protocol::tool_result_to_json(value);
          });
    }
    return std::nullopt;
  }

  if (request.method == protocol::PromptsGetMethod) {
    const auto params = protocol::prompts_get_params_from_json(request.params);
    if (!params) {
      return server_handler_error_response(request, params.error());
    }
    const auto result = handler.on_get_prompt(*params, context, cancellation);
    if (result.has_value()) {
      return server_handler_result_response(
          request, *result, [](const protocol::PromptsGetResult& value) {
            return protocol::prompts_get_result_to_json(value);
          });
    }
    return std::nullopt;
  }

  if (request.method == protocol::ResourcesReadMethod) {
    const auto params =
        protocol::resources_read_params_from_json(request.params);
    if (!params) {
      return server_handler_error_response(request, params.error());
    }
    const auto result =
        handler.on_read_resource(*params, context, cancellation);
    if (result.has_value()) {
      return server_handler_result_response(
          request, *result, [](const protocol::ResourcesReadResult& value) {
            return protocol::resources_read_result_to_json(value);
          });
    }
    return std::nullopt;
  }

  return std::nullopt;
}

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
  using ToolsListHandler = Server::ToolsListHandler;
  using PromptsListHandler = Server::PromptsListHandler;
  using ResourcesListHandler = Server::ResourcesListHandler;
  using ResourceTemplatesListHandler = Server::ResourceTemplatesListHandler;
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
  /// Handles tools/list requests.
  ToolsListHandler on_tools_list;
  /// Handles prompts/list requests.
  PromptsListHandler on_prompts_list;
  /// Handles resources/list requests.
  ResourcesListHandler on_resources_list;
  /// Handles resources/templates/list requests.
  ResourceTemplatesListHandler on_resource_templates_list;
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
    if (on_tools_list) {
      server.set_tools_list_handler(on_tools_list);
    }
    if (on_prompts_list) {
      server.set_prompts_list_handler(on_prompts_list);
    }
    if (on_resources_list) {
      server.set_resources_list_handler(on_resources_list);
    }
    if (on_resource_templates_list) {
      server.set_resource_templates_list_handler(on_resource_templates_list);
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

/// @brief Installs callbacks from an owned contract-style server handler.
inline Server& Server::set_handler(
    std::shared_ptr<const ServerHandlerInterface> handler) {
  if (!handler) {
    throw std::invalid_argument(
        "ServerHandlerInterface shared handler must not be null");
  }

  set_completion_handler(
      [handler](
          const protocol::Json& request, const SessionContext& context,
          CancellationToken cancellation) -> core::Result<protocol::Json> {
        const auto response =
            handler->on_completion(request, context, cancellation);
        if (response.has_value()) {
          return std::move(*response);
        }
        return mcp::core::unexpected(handler_method_not_found(
            "server handler does not handle completion"));
      });
  set_sampling_handler(
      [handler](
          const protocol::Json& request, const SessionContext& context,
          CancellationToken cancellation) -> core::Result<protocol::Json> {
        const auto response =
            handler->on_sampling(request, context, cancellation);
        if (response.has_value()) {
          return std::move(*response);
        }
        return mcp::core::unexpected(handler_method_not_found(
            "server handler does not handle sampling"));
      });
  set_logging_handler(
      [handler](std::string_view level, std::string_view message) {
        handler->on_logging(level, message);
      });
  set_raw_request_handler([handler](const protocol::JsonRpcRequest& request,
                                    const SessionContext& context,
                                    CancellationToken cancellation)
                              -> std::optional<protocol::JsonRpcResponse> {
    const auto handler_response = dispatch_server_handler_request(
        *handler, request, context, cancellation);
    if (handler_response.has_value()) {
      return handler_response;
    }
    return handler->on_custom_request(request, context);
  });
  set_raw_notification_handler(
      [handler](const protocol::JsonRpcNotification& notification,
                const SessionContext& context) -> core::Result<core::Unit> {
        const auto response =
            handler->on_raw_notification(notification, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_custom_notification_handler(
      [handler](const protocol::JsonRpcNotification& notification,
                const SessionContext& context) -> core::Result<core::Unit> {
        const auto response =
            handler->on_custom_notification(notification, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_task_list_handler([handler](const protocol::TaskListParams& params,
                                  const SessionContext& context)
                            -> core::Result<protocol::TaskListResult> {
    const auto response = handler->on_task_list(params, context);
    if (response.has_value()) {
      return std::move(*response);
    }
    return mcp::core::unexpected(
        handler_method_not_found("server handler does not handle task list"));
  });
  set_task_get_handler(
      [handler](const protocol::TaskGetParams& params,
                const SessionContext& context) -> core::Result<protocol::Task> {
        const auto response = handler->on_task_get(params, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return mcp::core::unexpected(handler_method_not_found(
            "server handler does not handle task get"));
      });
  set_task_cancel_handler(
      [handler](const protocol::TaskCancelParams& params,
                const SessionContext& context) -> core::Result<protocol::Task> {
        const auto response = handler->on_task_cancel(params, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return mcp::core::unexpected(handler_method_not_found(
            "server handler does not handle task cancel"));
      });
  set_task_result_handler(
      [handler](const protocol::TaskResultParams& params,
                const SessionContext& context) -> core::Result<protocol::Json> {
        const auto response = handler->on_task_result(params, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return mcp::core::unexpected(handler_method_not_found(
            "server handler does not handle task result"));
      });
  set_progress_handler(
      [handler](const protocol::ProgressNotificationParams& params,
                const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler->on_progress(params, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_roots_list_changed_handler(
      [handler](const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler->on_roots_list_changed(context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_tool_list_changed_handler(
      [handler](const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler->on_tool_list_changed(context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_prompt_list_changed_handler(
      [handler](const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler->on_prompt_list_changed(context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_resource_list_changed_handler(
      [handler](const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler->on_resource_list_changed(context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  set_resource_updated_handler(
      [handler](const std::string& uri,
                const SessionContext& context) -> core::Result<core::Unit> {
        const auto response = handler->on_resource_updated(uri, context);
        if (response.has_value()) {
          return std::move(*response);
        }
        return core::Unit{};
      });
  return *this;
}

/// @brief Installs callbacks from a borrowed contract-style server handler.
inline Server& Server::set_handler(const ServerHandlerInterface& handler) {
  return set_handler(std::shared_ptr<const ServerHandlerInterface>(
      &handler, [](const ServerHandlerInterface*) {}));
}

}  // namespace mcp::server
