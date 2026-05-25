// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/server/server.hpp"

#include <optional>
#include <utility>

#include "cxxmcp/protocol/logging.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server/http_transport.hpp"
#include "cxxmcp/server/stdio_transport.hpp"

namespace mcp::server {

namespace {

protocol::Json capability_to_json(
    const protocol::ServerCapabilities& capabilities) {
  protocol::Json json = protocol::Json::object();
  json["tools"] = {{"listChanged", capabilities.tools.list_changed}};
  json["resources"] = {{"listChanged", capabilities.resources.list_changed},
                       {"subscribe", capabilities.resources.subscribe}};
  json["prompts"] = {{"listChanged", capabilities.prompts.list_changed}};
  json["logging"] = {{"enabled", capabilities.logging.enabled}};
  json["completions"] = {{"enabled", capabilities.completions.enabled}};
  if (capabilities.tasks.has_value()) {
    protocol::Json tasks =
        protocol::task_capabilities_to_json(*capabilities.tasks);
    if (!tasks.empty()) {
      json["tasks"] = std::move(tasks);
    }
  }
  if (capabilities.experimental.has_value()) {
    json["experimental"] = *capabilities.experimental;
  }
  if (!capabilities.extensions.empty()) {
    json["extensions"] = capabilities.extensions;
  }
  return json;
}

ServerInfo server_info_from_options(const ServerOptions& options) {
  return ServerInfo{
      .name = options.server_name,
      .version = options.server_version,
      .instructions = options.instructions,
  };
}

protocol::Json server_info_to_json(const ServerOptions& options) {
  const auto info = server_info_from_options(options);
  protocol::Json json = protocol::Json::object();
  json["name"] = info.name;
  json["version"] = info.version;
  return json;
}

protocol::Json initialize_result_to_json(const ServerOptions& options) {
  protocol::Json result = protocol::Json::object();
  result["protocolVersion"] = std::string(protocol::McpProtocolVersion);
  result["capabilities"] = capability_to_json(options.capabilities);
  result["serverInfo"] = server_info_to_json(options);
  if (!options.instructions.empty()) {
    result["instructions"] = options.instructions;
  }
  return result;
}

core::Result<protocol::JsonRpcResponse> make_error_response(
    const protocol::JsonRpcRequest& request, int code, std::string message,
    std::string detail = {}) {
  return protocol::make_error_response(
      std::optional<protocol::RequestId>{request.id},
      protocol::make_error(code, std::move(message),
                           detail.empty()
                               ? std::nullopt
                               : std::optional<protocol::Json>{detail}));
}

core::Result<core::Unit> update_resource_subscription(
    std::unordered_map<const Transport*, std::unordered_set<std::string>>&
        subscriptions,
    const SessionContext& context, std::string_view uri, bool subscribed) {
  if (!context.transport) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InternalError),
        "resource subscription requires an active transport",
        {},
    });
  }

  auto& uris = subscriptions[context.transport];
  if (subscribed) {
    uris.insert(std::string(uri));
  } else {
    uris.erase(std::string(uri));
    if (uris.empty()) {
      subscriptions.erase(context.transport);
    }
  }
  return core::Unit{};
}

}  // namespace

Server::Server(ServerOptions options) : options_(std::move(options)) {}

ServerInfo Server::get_info() const {
  return server_info_from_options(options_);
}

const protocol::ServerCapabilities& Server::capabilities() const noexcept {
  return options_.capabilities;
}

ToolRegistry& Server::tools() noexcept { return tools_; }

const ToolRegistry& Server::tools() const noexcept { return tools_; }

std::vector<protocol::ToolDefinition> Server::list_tools() const {
  return tools_.list();
}

core::Result<protocol::ToolDefinition> Server::get_tool(
    std::string_view name) const {
  return tools_.get(name);
}

core::Result<protocol::ToolResult> Server::call_tool(
    std::string_view name, protocol::Json arguments,
    const std::string& session_id) const {
  return tools_.call(name, std::move(arguments), session_id);
}

PromptRegistry& Server::prompts() noexcept { return prompts_; }

const PromptRegistry& Server::prompts() const noexcept { return prompts_; }

std::vector<protocol::Prompt> Server::list_prompts() const {
  return prompts_.list();
}

core::Result<protocol::PromptsGetResult> Server::get_prompt(
    std::string_view name, protocol::Json arguments,
    const std::string& session_id) const {
  return prompts_.get(name, std::move(arguments), session_id);
}

ResourceRegistry& Server::resources() noexcept { return resources_; }

const ResourceRegistry& Server::resources() const noexcept {
  return resources_;
}

std::vector<protocol::Resource> Server::list_resources() const {
  return resources_.list();
}

core::Result<protocol::ResourcesReadResult> Server::read_resource(
    std::string_view uri, protocol::Json params,
    const std::string& session_id) const {
  return resources_.read(uri, std::move(params), session_id);
}

ResourceTemplateRegistry& Server::resource_templates() noexcept {
  return resource_templates_;
}

const ResourceTemplateRegistry& Server::resource_templates() const noexcept {
  return resource_templates_;
}

std::vector<protocol::ResourceTemplate> Server::list_resource_templates()
    const {
  return resource_templates_.list();
}

core::Result<protocol::Json> Server::initialize() {
  return initialize_result_to_json(options_);
}

core::Result<protocol::Json> Server::ping(const SessionContext& context) {
  const auto response = handle_request(
      protocol::JsonRpcRequest{
          .method = std::string(protocol::PingMethod),
          .params = protocol::Json::object(),
          .id = std::int64_t{0},
      },
      context);
  if (!response) {
    return std::unexpected(response.error());
  }
  if (!response->result.has_value()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InternalError),
        "ping response did not contain a result",
        {},
    });
  }
  return *response->result;
}

core::Result<protocol::JsonRpcResponse> Server::handle_request(
    const protocol::JsonRpcRequest& request, const SessionContext& context) {
  if (auth_provider_) {
    AuthRequest auth_request;
    auth_request.remote_address = context.remote_address;
    const auto identity = auth_provider_->authenticate(auth_request);
    if (!identity) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::PermissionDenied),
          "authentication failed", identity.error().message);
    }
  }

  if (rate_limiter_) {
    const auto decision = rate_limiter_->check(RateLimitRequest{
        .subject = context.session_id,
        .method = request.method,
        .request_bytes = request.params.dump().size(),
    });
    if (!decision) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::RateLimited),
          "rate limiting failed", decision.error().message);
    }

    if (!decision->allowed) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::RateLimited),
          "request rate limited");
    }
  }

  if (request.method == protocol::InitializeMethod) {
    return protocol::make_response(request.id,
                                   initialize_result_to_json(options_));
  }

  if (request.method == protocol::PingMethod) {
    return protocol::make_response(request.id, protocol::Json::object());
  }

  if (raw_request_handler_) {
    const auto raw_response = raw_request_handler_(request, context);
    if (raw_response.has_value()) {
      return *raw_response;
    }
  }

  if (request.method == protocol::TasksListMethod) {
    if (!task_list_handler_) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "task list handler is not configured");
    }

    const auto params = protocol::task_list_params_from_json(request.params);
    if (!params) {
      return make_error_response(request, params.error().code,
                                 params.error().message, params.error().detail);
    }

    const auto result = task_list_handler_(*params, context);
    if (!result) {
      return make_error_response(request, result.error().code,
                                 result.error().message, result.error().detail);
    }

    return protocol::make_response(request.id,
                                   protocol::task_list_result_to_json(*result));
  }

  if (request.method == protocol::TasksGetMethod) {
    if (!task_get_handler_) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "task get handler is not configured");
    }

    const auto params = protocol::task_get_params_from_json(request.params);
    if (!params) {
      return make_error_response(request, params.error().code,
                                 params.error().message, params.error().detail);
    }

    const auto result = task_get_handler_(*params, context);
    if (!result) {
      return make_error_response(request, result.error().code,
                                 result.error().message, result.error().detail);
    }

    return protocol::make_response(request.id, protocol::task_to_json(*result));
  }

  if (request.method == protocol::TasksCancelMethod) {
    if (!task_cancel_handler_) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "task cancel handler is not configured");
    }

    const auto params = protocol::task_cancel_params_from_json(request.params);
    if (!params) {
      return make_error_response(request, params.error().code,
                                 params.error().message, params.error().detail);
    }

    const auto result = task_cancel_handler_(*params, context);
    if (!result) {
      return make_error_response(request, result.error().code,
                                 result.error().message, result.error().detail);
    }

    return protocol::make_response(request.id, protocol::task_to_json(*result));
  }

  if (request.method == protocol::TasksResultMethod) {
    if (!task_result_handler_) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "task result handler is not configured");
    }

    const auto params = protocol::task_result_params_from_json(request.params);
    if (!params) {
      return make_error_response(request, params.error().code,
                                 params.error().message, params.error().detail);
    }

    const auto result = task_result_handler_(*params, context);
    if (!result) {
      return make_error_response(request, result.error().code,
                                 result.error().message, result.error().detail);
    }

    return protocol::make_response(request.id, *result);
  }

  if (request.method == "tools/list") {
    protocol::Json result = protocol::Json::object();
    result["tools"] = protocol::Json::array();
    for (const auto& tool : tools_.list()) {
      result["tools"].push_back(protocol::tool_definition_to_json(tool));
    }
    return protocol::make_response(request.id, std::move(result));
  }

  if (request.method == protocol::ToolsGetMethod) {
    if (!request.params.is_object() || !request.params.contains("name") ||
        !request.params.at("name").is_string()) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "tools/get requires a string name");
    }

    const auto tool = tools_.get(request.params.at("name").get<std::string>());
    if (!tool) {
      return protocol::make_error_response(
          std::optional<protocol::RequestId>{request.id},
          protocol::make_error(
              tool.error().code, tool.error().message,
              tool.error().detail.empty()
                  ? std::nullopt
                  : std::optional<protocol::Json>{tool.error().detail}));
    }

    return protocol::make_response(request.id,
                                   protocol::tool_definition_to_json(*tool));
  }

  if (request.method == "tools/call") {
    if (!request.params.is_object()) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "tools/call params must be an object");
    }

    if (!request.params.contains("name") ||
        !request.params.at("name").is_string()) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "tools/call requires a string name");
    }

    protocol::Json arguments = protocol::Json::object();
    if (request.params.contains("arguments")) {
      if (!request.params.at("arguments").is_object()) {
        return make_error_response(
            request, static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "tools/call arguments must be an object");
      }
      arguments = request.params.at("arguments");
    }

    const auto result =
        tools_.call(request.params.at("name").get<std::string>(),
                    std::move(arguments), context);
    if (!result) {
      return protocol::make_error_response(
          std::optional<protocol::RequestId>{request.id},
          protocol::make_error(
              result.error().code, result.error().message,
              result.error().detail.empty()
                  ? std::nullopt
                  : std::optional<protocol::Json>{result.error().detail}));
    }

    return protocol::make_response(request.id,
                                   protocol::tool_result_to_json(*result));
  }

  if (request.method == "prompts/list") {
    return protocol::make_response(
        request.id,
        protocol::prompts_list_result_to_json(protocol::PromptsListResult{
            .prompts = prompts_.list(),
        }));
  }

  if (request.method == "prompts/get") {
    const auto params = protocol::prompts_get_params_from_json(request.params);
    if (!params) {
      return make_error_response(request, params.error().code,
                                 params.error().message, params.error().detail);
    }

    const auto result = prompts_.get(params->name, params->arguments, context);
    if (!result) {
      return protocol::make_error_response(
          std::optional<protocol::RequestId>{request.id},
          protocol::make_error(
              result.error().code, result.error().message,
              result.error().detail.empty()
                  ? std::nullopt
                  : std::optional<protocol::Json>{result.error().detail}));
    }

    return protocol::make_response(
        request.id, protocol::prompts_get_result_to_json(*result));
  }

  if (request.method == "resources/list") {
    return protocol::make_response(
        request.id,
        protocol::resources_list_result_to_json(protocol::ResourcesListResult{
            .resources = resources_.list(),
        }));
  }

  if (request.method == "resources/read") {
    const auto params =
        protocol::resources_read_params_from_json(request.params);
    if (!params) {
      return make_error_response(request, params.error().code,
                                 params.error().message, params.error().detail);
    }

    const auto result = resources_.read(params->uri, request.params, context);
    if (!result) {
      return protocol::make_error_response(
          std::optional<protocol::RequestId>{request.id},
          protocol::make_error(
              result.error().code, result.error().message,
              result.error().detail.empty()
                  ? std::nullopt
                  : std::optional<protocol::Json>{result.error().detail}));
    }

    return protocol::make_response(
        request.id, protocol::resources_read_result_to_json(*result));
  }

  if (request.method == "resources/templates/list") {
    return protocol::make_response(
        request.id, protocol::resource_templates_list_result_to_json(
                        protocol::ResourceTemplatesListResult{
                            .resource_templates = resource_templates_.list(),
                        }));
  }

  if (request.method == "resources/subscribe" ||
      request.method == "resources/unsubscribe") {
    if (!options_.capabilities.resources.subscribe) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "resource subscriptions are not enabled");
    }
    if (!request.params.is_object() || !request.params.contains("uri") ||
        !request.params.at("uri").is_string()) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "resource subscription requires a string uri");
    }
    const auto subscription = set_resource_subscription(
        context, request.params.at("uri").get<std::string>(),
        request.method == protocol::ResourcesSubscribeMethod);
    if (!subscription) {
      return make_error_response(request, subscription.error().code,
                                 subscription.error().message,
                                 subscription.error().detail);
    }
    return protocol::make_response(request.id, protocol::Json::object());
  }

  if (request.method == "completion/complete") {
    if (!completion_handler_) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "completion handler is not configured");
    }
    const auto result = completion_handler_(request.params);
    if (!result) {
      return make_error_response(request, result.error().code,
                                 result.error().message, result.error().detail);
    }
    return protocol::make_response(request.id, *result);
  }

  if (request.method == "sampling/createMessage") {
    if (!sampling_handler_) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "sampling handler is not configured");
    }
    const auto result = sampling_handler_(request.params);
    if (!result) {
      return make_error_response(request, result.error().code,
                                 result.error().message, result.error().detail);
    }
    return protocol::make_response(request.id, *result);
  }

  if (request.method == "logging/setLevel") {
    if (!request.params.is_object() || !request.params.contains("level") ||
        !request.params.at("level").is_string()) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "logging/setLevel requires a string level");
    }
    if (logging_handler_) {
      logging_handler_(request.params.at("level").get<std::string>(),
                       "logging level changed");
    }
    return protocol::make_response(request.id, protocol::Json::object());
  }

  return make_error_response(
      request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
      "method not found", request.method);
}

core::Result<core::Unit> Server::handle_notification(
    const protocol::JsonRpcNotification& notification,
    const SessionContext& context) {
  if (raw_notification_handler_) {
    const auto raw_result = raw_notification_handler_(notification, context);
    if (!raw_result) {
      return std::unexpected(raw_result.error());
    }
  }

  if (notification.method == protocol::RootsListChangedNotificationMethod &&
      roots_list_changed_handler_) {
    const auto result = roots_list_changed_handler_(context);
    if (!result) {
      return std::unexpected(result.error());
    }
  } else if (notification.method == protocol::CancelledNotificationMethod) {
    const auto cancelled =
        protocol::cancelled_notification_params_from_json(notification.params);
    if (!cancelled) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::InvalidParams),
          "cancelled notification requires valid params",
          {},
      });
    }
  } else if (notification.method == protocol::ProgressNotificationMethod &&
             progress_handler_) {
    const auto params =
        protocol::progress_notification_params_from_json(notification.params);
    if (!params) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::InvalidParams),
          "progress notification requires valid params",
          {},
      });
    }
    const auto result = progress_handler_(*params, context);
    if (!result) {
      return std::unexpected(result.error());
    }
  } else if (notification.method ==
                 protocol::ToolsListChangedNotificationMethod &&
             tool_list_changed_handler_) {
    const auto result = tool_list_changed_handler_(context);
    if (!result) {
      return std::unexpected(result.error());
    }
  } else if (notification.method ==
                 protocol::PromptsListChangedNotificationMethod &&
             prompt_list_changed_handler_) {
    const auto result = prompt_list_changed_handler_(context);
    if (!result) {
      return std::unexpected(result.error());
    }
  } else if (notification.method ==
                 protocol::ResourcesListChangedNotificationMethod &&
             resource_list_changed_handler_) {
    const auto result = resource_list_changed_handler_(context);
    if (!result) {
      return std::unexpected(result.error());
    }
  } else if (notification.method ==
                 protocol::ResourcesUpdatedNotificationMethod &&
             resource_updated_handler_) {
    if (!notification.params.is_object() ||
        !notification.params.contains("uri") ||
        !notification.params.at("uri").is_string()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::InvalidParams),
          "resource updated notification requires a string uri",
          {},
      });
    }
    const auto result = resource_updated_handler_(
        notification.params.at("uri").get<std::string>(), context);
    if (!result) {
      return std::unexpected(result.error());
    }
  }

  return core::Unit{};
}

core::Result<core::Unit> Server::broadcast_notification(
    const protocol::JsonRpcNotification& notification) {
  for (auto& transport : transports_) {
    const auto sent = transport->send_notification(notification);
    if (!sent) {
      return std::unexpected(sent.error());
    }
  }
  return core::Unit{};
}

core::Result<core::Unit> Server::notify_roots_list_changed() {
  return broadcast_notification(protocol::JsonRpcNotification{
      .method = std::string(protocol::RootsListChangedNotificationMethod),
      .params = protocol::Json::object(),
  });
}

core::Result<core::Unit> Server::notify_tool_list_changed() {
  return broadcast_notification(protocol::JsonRpcNotification{
      .method = std::string(protocol::ToolsListChangedNotificationMethod),
      .params = protocol::Json::object(),
  });
}

core::Result<core::Unit> Server::notify_prompt_list_changed() {
  return broadcast_notification(protocol::JsonRpcNotification{
      .method = std::string(protocol::PromptsListChangedNotificationMethod),
      .params = protocol::Json::object(),
  });
}

core::Result<core::Unit> Server::notify_resource_list_changed() {
  return broadcast_notification(protocol::JsonRpcNotification{
      .method = std::string(protocol::ResourcesListChangedNotificationMethod),
      .params = protocol::Json::object(),
  });
}

core::Result<core::Unit> Server::notify_resource_updated(std::string_view uri) {
  return notify_resource_subscribers(
      uri,
      protocol::JsonRpcNotification{
          .method = std::string(protocol::ResourcesUpdatedNotificationMethod),
          .params = protocol::Json{{"uri", std::string(uri)}},
      });
}

core::Result<core::Unit> Server::notify_progress(
    const protocol::ProgressNotificationParams& params) {
  return broadcast_notification(protocol::JsonRpcNotification{
      .method = std::string(protocol::ProgressNotificationMethod),
      .params = protocol::progress_notification_params_to_json(params),
  });
}

core::Result<core::Unit> Server::notify_logging_message(
    const protocol::LoggingMessageNotificationParams& params) {
  return broadcast_notification(protocol::JsonRpcNotification{
      .method = std::string(protocol::LoggingMessageNotificationMethod),
      .params = protocol::logging_message_notification_params_to_json(params),
  });
}

core::Result<core::Unit> Server::notify_elicitation_complete(
    std::string elicitation_id) {
  return broadcast_notification(protocol::JsonRpcNotification{
      .method = std::string(protocol::ElicitationCompleteNotificationMethod),
      .params = protocol::elicitation_complete_notification_params_to_json(
          protocol::ElicitationCompleteNotificationParams{
              .elicitation_id = std::move(elicitation_id),
          }),
  });
}

core::Result<core::Unit> Server::notify_task_status(
    const protocol::Task& task) {
  return broadcast_notification(protocol::JsonRpcNotification{
      .method = std::string(protocol::TasksStatusNotificationMethod),
      .params = protocol::task_to_json(task),
  });
}

core::Result<core::Unit> Server::notify_resource_subscribers(
    std::string_view uri, const protocol::JsonRpcNotification& notification) {
  std::vector<Transport*> recipients;
  bool has_any_subscription = false;
  {
    std::lock_guard lock(*subscriptions_mutex_);
    has_any_subscription = !resource_subscriptions_.empty();
    for (auto& transport : transports_) {
      if (!transport) {
        continue;
      }
      const auto subscription_it =
          resource_subscriptions_.find(transport.get());
      if (subscription_it == resource_subscriptions_.end()) {
        continue;
      }
      if (subscription_it->second.find(std::string(uri)) !=
          subscription_it->second.end()) {
        recipients.push_back(transport.get());
      }
    }
  }

  if (!has_any_subscription) {
    return broadcast_notification(notification);
  }
  if (recipients.empty()) {
    return core::Unit{};
  }

  for (auto* transport : recipients) {
    const auto sent = transport->send_notification(notification);
    if (!sent) {
      return std::unexpected(sent.error());
    }
  }
  return core::Unit{};
}

core::Result<core::Unit> Server::set_resource_subscription(
    const SessionContext& context, std::string_view uri, bool subscribed) {
  if (!context.transport) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "resource subscription requires a transport context",
        {},
    });
  }
  {
    std::lock_guard lock(*subscriptions_mutex_);
    auto& uris = resource_subscriptions_[context.transport];
    if (subscribed) {
      uris.insert(std::string(uri));
    } else {
      uris.erase(std::string(uri));
      if (uris.empty()) {
        resource_subscriptions_.erase(context.transport);
      }
    }
  }
  return core::Unit{};
}

core::Result<core::Unit> Server::add_transport(
    std::unique_ptr<Transport> transport) {
  if (!transport) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "transport must not be null",
        {},
    });
  }

  transports_.push_back(std::move(transport));
  return core::Unit{};
}

void Server::set_auth_provider(std::unique_ptr<AuthProvider> auth_provider) {
  auth_provider_ = std::move(auth_provider);
}

void Server::set_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter) {
  rate_limiter_ = std::move(rate_limiter);
}

core::Result<core::Unit> Server::start() {
  for (auto& transport : transports_) {
    const auto started = transport->start(
        [this](const protocol::JsonRpcRequest& request,
               const SessionContext& context) {
          return this->handle_request(request, context);
        },
        [this](const protocol::JsonRpcNotification& notification,
               const SessionContext& context) {
          return this->handle_notification(notification, context);
        });
    if (!started) {
      return std::unexpected(started.error());
    }
  }

  return core::Unit{};
}

void Server::stop() noexcept {
  for (auto& transport : transports_) {
    transport->stop();
  }
}

void Server::set_completion_handler(JsonHandler handler) {
  if (handler) {
    options_.capabilities.completions.enabled = true;
  }
  completion_handler_ = std::move(handler);
}

void Server::set_sampling_handler(JsonHandler handler) {
  sampling_handler_ = std::move(handler);
}

void Server::set_logging_handler(LoggingHandler handler) {
  if (handler) {
    options_.capabilities.logging.enabled = true;
  }
  logging_handler_ = std::move(handler);
}

void Server::set_raw_request_handler(RawRequestHandler handler) {
  raw_request_handler_ = std::move(handler);
}

void Server::set_raw_notification_handler(RawNotificationHandler handler) {
  raw_notification_handler_ = std::move(handler);
}

void Server::set_custom_request_handler(RawRequestHandler handler) {
  set_raw_request_handler(std::move(handler));
}

void Server::set_custom_notification_handler(RawNotificationHandler handler) {
  set_raw_notification_handler(std::move(handler));
}

void Server::set_task_list_handler(TaskListHandler handler) {
  task_list_handler_ = std::move(handler);
}

void Server::set_task_get_handler(TaskGetHandler handler) {
  task_get_handler_ = std::move(handler);
}

void Server::set_task_cancel_handler(TaskCancelHandler handler) {
  task_cancel_handler_ = std::move(handler);
}

void Server::set_task_result_handler(TaskResultHandler handler) {
  task_result_handler_ = std::move(handler);
}

void Server::set_progress_handler(ProgressHandler handler) {
  progress_handler_ = std::move(handler);
}

void Server::set_roots_list_changed_handler(RootsListChangedHandler handler) {
  roots_list_changed_handler_ = std::move(handler);
}

void Server::set_tool_list_changed_handler(ListChangedHandler handler) {
  tool_list_changed_handler_ = std::move(handler);
}

void Server::set_prompt_list_changed_handler(ListChangedHandler handler) {
  prompt_list_changed_handler_ = std::move(handler);
}

void Server::set_resource_list_changed_handler(ListChangedHandler handler) {
  resource_list_changed_handler_ = std::move(handler);
}

void Server::set_resource_updated_handler(ResourceUpdatedHandler handler) {
  resource_updated_handler_ = std::move(handler);
}

ServerBuilder& ServerBuilder::name(std::string value) {
  options_.server_name = std::move(value);
  return *this;
}

ServerBuilder& ServerBuilder::version(std::string value) {
  options_.server_version = std::move(value);
  return *this;
}

ServerBuilder& ServerBuilder::instructions(std::string value) {
  options_.instructions = std::move(value);
  return *this;
}

ServerBuilder& ServerBuilder::with_capabilities(
    protocol::ServerCapabilities capabilities) {
  options_.capabilities = capabilities;
  return *this;
}

ServerBuilder& ServerBuilder::with_transport(
    std::unique_ptr<Transport> transport) {
  transports_.push_back(std::move(transport));
  return *this;
}

ServerBuilder& ServerBuilder::with_auth_provider(
    std::unique_ptr<AuthProvider> auth_provider) {
  auth_provider_ = std::move(auth_provider);
  return *this;
}

ServerBuilder& ServerBuilder::with_rate_limiter(
    std::unique_ptr<RateLimiter> rate_limiter) {
  rate_limiter_ = std::move(rate_limiter);
  return *this;
}

ServerBuilder& ServerBuilder::add_tool(protocol::ToolDefinition definition,
                                       ToolHandler handler) {
  options_.capabilities.tools.list_changed = true;
  registrations_.push_back(
      [definition = std::move(definition),
       handler = std::move(handler)](Server& server) mutable {
        return server.tools().add(std::move(definition), std::move(handler));
      });
  return *this;
}

ServerBuilder& ServerBuilder::add_prompt(protocol::Prompt prompt,
                                         PromptHandler handler) {
  options_.capabilities.prompts.list_changed = true;
  registrations_.push_back(
      [prompt = std::move(prompt),
       handler = std::move(handler)](Server& server) mutable {
        return server.prompts().add(std::move(prompt), std::move(handler));
      });
  return *this;
}

ServerBuilder& ServerBuilder::add_resource(protocol::Resource resource,
                                           ResourceReadHandler handler) {
  options_.capabilities.resources.list_changed = true;
  registrations_.push_back(
      [resource = std::move(resource),
       handler = std::move(handler)](Server& server) mutable {
        return server.resources().add(std::move(resource), std::move(handler));
      });
  return *this;
}

ServerBuilder& ServerBuilder::add_resource_template(
    protocol::ResourceTemplate resource_template) {
  options_.capabilities.resources.list_changed = true;
  registrations_.push_back([resource_template = std::move(resource_template)](
                               Server& server) mutable {
    return server.resource_templates().add(std::move(resource_template));
  });
  return *this;
}

ServerBuilder& ServerBuilder::on_completion(Server::JsonHandler handler) {
  completion_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_sampling(Server::JsonHandler handler) {
  sampling_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_logging(Server::LoggingHandler handler) {
  logging_handler_ = std::move(handler);
  options_.capabilities.logging.enabled = static_cast<bool>(logging_handler_);
  return *this;
}

ServerBuilder& ServerBuilder::on_raw_request(
    Server::RawRequestHandler handler) {
  raw_request_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_raw_notification(
    Server::RawNotificationHandler handler) {
  raw_notification_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_custom_request(
    Server::RawRequestHandler handler) {
  return on_raw_request(std::move(handler));
}

ServerBuilder& ServerBuilder::on_custom_notification(
    Server::RawNotificationHandler handler) {
  return on_raw_notification(std::move(handler));
}

ServerBuilder& ServerBuilder::on_task_list(Server::TaskListHandler handler) {
  task_list_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_task_get(Server::TaskGetHandler handler) {
  task_get_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_task_cancel(
    Server::TaskCancelHandler handler) {
  task_cancel_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_task_result(
    Server::TaskResultHandler handler) {
  task_result_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_progress(Server::ProgressHandler handler) {
  progress_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_roots_list_changed(
    Server::RootsListChangedHandler handler) {
  roots_list_changed_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_tool_list_changed(
    Server::ListChangedHandler handler) {
  tool_list_changed_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_prompt_list_changed(
    Server::ListChangedHandler handler) {
  prompt_list_changed_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_resource_list_changed(
    Server::ListChangedHandler handler) {
  resource_list_changed_handler_ = std::move(handler);
  return *this;
}

ServerBuilder& ServerBuilder::on_resource_updated(
    Server::ResourceUpdatedHandler handler) {
  resource_updated_handler_ = std::move(handler);
  return *this;
}

core::Result<std::unique_ptr<Server>> ServerBuilder::build() {
  auto server = std::make_unique<Server>(options_);
  server->set_auth_provider(std::move(auth_provider_));
  server->set_rate_limiter(std::move(rate_limiter_));
  server->set_completion_handler(std::move(completion_handler_));
  server->set_sampling_handler(std::move(sampling_handler_));
  server->set_logging_handler(std::move(logging_handler_));
  server->set_raw_request_handler(std::move(raw_request_handler_));
  server->set_raw_notification_handler(std::move(raw_notification_handler_));
  server->set_task_list_handler(std::move(task_list_handler_));
  server->set_task_get_handler(std::move(task_get_handler_));
  server->set_task_cancel_handler(std::move(task_cancel_handler_));
  server->set_task_result_handler(std::move(task_result_handler_));
  server->set_progress_handler(std::move(progress_handler_));
  server->set_roots_list_changed_handler(
      std::move(roots_list_changed_handler_));
  server->set_tool_list_changed_handler(std::move(tool_list_changed_handler_));
  server->set_prompt_list_changed_handler(
      std::move(prompt_list_changed_handler_));
  server->set_resource_list_changed_handler(
      std::move(resource_list_changed_handler_));
  server->set_resource_updated_handler(std::move(resource_updated_handler_));

  for (auto& registration : registrations_) {
    const auto registered = registration(*server);
    if (!registered) {
      return std::unexpected(registered.error());
    }
  }

  for (auto& transport : transports_) {
    const auto added = server->add_transport(std::move(transport));
    if (!added) {
      return std::unexpected(added.error());
    }
  }

  return server;
}

App::Builder App::builder() { return Builder{}; }

App::Builder& App::Builder::name(std::string value) {
  builder_.name(std::move(value));
  return *this;
}

App::Builder& App::Builder::version(std::string value) {
  builder_.version(std::move(value));
  return *this;
}

App::Builder& App::Builder::instructions(std::string value) {
  builder_.instructions(std::move(value));
  return *this;
}

App::Builder& App::Builder::stdio() {
  builder_.with_transport(std::make_unique<StdioTransport>());
  return *this;
}

App::Builder& App::Builder::streamable_http(std::string host,
                                            std::uint16_t port,
                                            std::string path) {
  builder_.with_transport(std::make_unique<HttpTransport>(HttpTransportOptions{
      .listen_host = std::move(host),
      .listen_port = static_cast<int>(port),
      .path = std::move(path),
  }));
  return *this;
}

App::Builder& App::Builder::legacy_sse(std::string host, std::uint16_t port,
                                       std::string path) {
  return streamable_http(std::move(host), port, std::move(path));
}

App::Builder& App::Builder::transport(std::unique_ptr<Transport> value) {
  builder_.with_transport(std::move(value));
  return *this;
}

App::Builder& App::Builder::tool(protocol::ToolDefinition definition,
                                 ToolHandler handler) {
  builder_.add_tool(std::move(definition), std::move(handler));
  return *this;
}

App::Builder& App::Builder::prompt(protocol::Prompt prompt,
                                   PromptHandler handler) {
  builder_.add_prompt(std::move(prompt), std::move(handler));
  return *this;
}

App::Builder& App::Builder::resource(protocol::Resource resource,
                                     ResourceReadHandler handler) {
  builder_.add_resource(std::move(resource), std::move(handler));
  return *this;
}

App::Builder& App::Builder::resource_template(
    protocol::ResourceTemplate resource_template) {
  builder_.add_resource_template(std::move(resource_template));
  return *this;
}

core::Result<std::unique_ptr<Server>> App::Builder::build() {
  return builder_.build();
}

int App::Builder::run() {
  const auto server = build();
  if (!server) {
    return 1;
  }
  const auto started = (*server)->start();
  return started ? 0 : 1;
}

}  // namespace mcp::server
