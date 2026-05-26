// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/client/client.hpp"

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cxxmcp/client/http_transport.hpp"
#include "cxxmcp/client/process_stdio_transport.hpp"
#include "cxxmcp/client/session.hpp"
#include "cxxmcp/client/stdio_transport.hpp"
#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/completion.hpp"
#include "cxxmcp/protocol/elicitation.hpp"
#include "cxxmcp/protocol/logging.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/sampling.hpp"
#include "cxxmcp/protocol/serialization.hpp"

namespace mcp::client {

namespace {

core::Error make_client_error(int code, std::string message,
                              std::string detail = {}) {
  return core::Error{code, std::move(message), std::move(detail), "protocol"};
}

core::Result<protocol::Json> require_result_payload(
    const protocol::JsonRpcResponse& response) {
  if (response.error.has_value()) {
    return std::unexpected(core::Error{
        response.error->code,
        response.error->message,
        response.error->data.has_value() ? response.error->data->dump()
                                         : std::string{},
        "protocol",
    });
  }

  if (!response.result.has_value()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                          "response did not contain a result"));
  }

  return *response.result;
}

core::Result<protocol::Json> require_initialize_payload(
    const protocol::JsonRpcResponse& response) {
  const auto payload = require_result_payload(response);
  if (!payload) {
    return std::unexpected(payload.error());
  }
  if (!payload->is_object()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                          "initialize response must be an object"));
  }
  if (!payload->contains("protocolVersion") ||
      !payload->at("protocolVersion").is_string()) {
    return std::unexpected(make_client_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "initialize response requires a string protocolVersion"));
  }

  return *payload;
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

protocol::Json cursor_params(const std::optional<std::string>& cursor) {
  protocol::Json params = protocol::Json::object();
  if (cursor.has_value()) {
    params["cursor"] = *cursor;
  }
  return params;
}

protocol::ClientCapabilities default_client_capabilities(
    const std::optional<protocol::ClientCapabilities>& capabilities) {
  if (capabilities.has_value()) {
    return *capabilities;
  }

  protocol::ClientCapabilities defaults;
  defaults.roots.enabled = true;
  defaults.roots.list_changed = true;
  defaults.sampling.enabled = true;
  defaults.elicitation.form = true;
  defaults.elicitation.url = true;
  return defaults;
}

protocol::Json initialize_params(
    std::string client_name, std::string client_version,
    const std::optional<protocol::ClientCapabilities>& capabilities) {
  protocol::Json params = protocol::Json::object();
  params["protocolVersion"] = std::string(protocol::McpProtocolVersion);
  params["capabilities"] = protocol::client_capabilities_to_json(
      default_client_capabilities(capabilities));
  params["clientInfo"] = protocol::Json{
      {"name", std::move(client_name)},
      {"version", std::move(client_version)},
  };
  return params;
}

bool is_session_terminated_error(const core::Error& error) {
  return error.code == static_cast<int>(protocol::ErrorCode::InvalidRequest) &&
         error.message == "http transport session was terminated";
}

}  // namespace

core::Result<core::Unit> Transport::send_notification(
    const protocol::JsonRpcNotification&) {
  return std::unexpected(
      make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                        "client transport does not support notifications"));
}

Client::Client(std::unique_ptr<Transport> transport)
    : transport_(std::move(transport)) {}

Client::Client(Client&& other) noexcept
    : transport_(std::move(other.transport_)),
      next_request_id_(other.next_request_id_.load(std::memory_order_relaxed)),
      transport_started_(other.transport_started_),
      roots_(std::move(other.roots_)),
      last_initialize_params_(std::move(other.last_initialize_params_)),
      capabilities_(std::move(other.capabilities_)),
      server_capabilities_(std::move(other.server_capabilities_)),
      initialized_handler_(std::move(other.initialized_handler_)),
      cancelled_handler_(std::move(other.cancelled_handler_)),
      logging_message_handler_(std::move(other.logging_message_handler_)),
      tool_list_changed_handler_(std::move(other.tool_list_changed_handler_)),
      prompt_list_changed_handler_(
          std::move(other.prompt_list_changed_handler_)),
      resource_list_changed_handler_(
          std::move(other.resource_list_changed_handler_)),
      resource_updated_handler_(std::move(other.resource_updated_handler_)),
      progress_handler_(std::move(other.progress_handler_)),
      elicitation_complete_handler_(
          std::move(other.elicitation_complete_handler_)),
      task_status_handler_(std::move(other.task_status_handler_)),
      roots_list_changed_handler_(std::move(other.roots_list_changed_handler_)),
      roots_list_request_handler_(std::move(other.roots_list_request_handler_)),
      sampling_request_handler_(std::move(other.sampling_request_handler_)),
      elicitation_request_handler_(
          std::move(other.elicitation_request_handler_)),
      custom_request_handler_(std::move(other.custom_request_handler_)),
      raw_notification_handler_(std::move(other.raw_notification_handler_)) {
  other.transport_started_ = false;
}

Client& Client::operator=(Client&& other) noexcept {
  if (this != &other) {
    transport_ = std::move(other.transport_);
    next_request_id_.store(
        other.next_request_id_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    transport_started_ = other.transport_started_;
    roots_ = std::move(other.roots_);
    last_initialize_params_ = std::move(other.last_initialize_params_);
    capabilities_ = std::move(other.capabilities_);
    server_capabilities_ = std::move(other.server_capabilities_);
    initialized_handler_ = std::move(other.initialized_handler_);
    cancelled_handler_ = std::move(other.cancelled_handler_);
    logging_message_handler_ = std::move(other.logging_message_handler_);
    tool_list_changed_handler_ = std::move(other.tool_list_changed_handler_);
    prompt_list_changed_handler_ =
        std::move(other.prompt_list_changed_handler_);
    resource_list_changed_handler_ =
        std::move(other.resource_list_changed_handler_);
    resource_updated_handler_ = std::move(other.resource_updated_handler_);
    progress_handler_ = std::move(other.progress_handler_);
    elicitation_complete_handler_ =
        std::move(other.elicitation_complete_handler_);
    task_status_handler_ = std::move(other.task_status_handler_);
    roots_list_changed_handler_ = std::move(other.roots_list_changed_handler_);
    roots_list_request_handler_ = std::move(other.roots_list_request_handler_);
    sampling_request_handler_ = std::move(other.sampling_request_handler_);
    elicitation_request_handler_ =
        std::move(other.elicitation_request_handler_);
    custom_request_handler_ = std::move(other.custom_request_handler_);
    raw_notification_handler_ = std::move(other.raw_notification_handler_);
    other.transport_started_ = false;
  }
  return *this;
}

core::Result<core::Unit> Client::ensure_transport_started() {
  if (transport_started_) {
    return core::Unit{};
  }
  if (!transport_) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                          "client transport is not configured"));
  }

  const auto started = transport_->start(
      [this](const protocol::JsonRpcRequest& request) {
        return this->handle_request(request);
      },
      [this](const protocol::JsonRpcNotification& notification) {
        return this->handle_notification(notification);
      });
  if (!started) {
    return std::unexpected(started.error());
  }

  transport_started_ = true;
  return core::Unit{};
}

const std::optional<protocol::ServerCapabilities>& Client::server_capabilities()
    const noexcept {
  return server_capabilities_;
}

core::Result<core::Unit> Client::record_server_capabilities(
    const protocol::Json& initialize_payload) {
  if (!initialize_payload.is_object()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                          "initialize response must be an object"));
  }

  if (!initialize_payload.contains("capabilities")) {
    server_capabilities_ = protocol::ServerCapabilities{};
    return core::Unit{};
  }

  const auto parsed = protocol::server_capabilities_from_json(
      initialize_payload.at("capabilities"));
  if (!parsed.has_value()) {
    return std::unexpected(make_client_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "initialize response contains invalid server capabilities"));
  }

  server_capabilities_ = *parsed;
  return core::Unit{};
}

bool Client::server_capabilities_known() const noexcept {
  return server_capabilities_.has_value();
}

bool Client::supports_server_completion() const noexcept {
  return !server_capabilities_known() ||
         server_capabilities_->completions.enabled;
}

bool Client::supports_server_logging() const noexcept {
  return !server_capabilities_known() || server_capabilities_->logging.enabled;
}

bool Client::supports_server_resource_subscribe() const noexcept {
  return !server_capabilities_known() ||
         server_capabilities_->resources.subscribe;
}

bool Client::supports_server_task_list() const noexcept {
  return !server_capabilities_known() ||
         (server_capabilities_->tasks.has_value() &&
          server_capabilities_->tasks->list);
}

bool Client::supports_server_task_cancel() const noexcept {
  return !server_capabilities_known() ||
         (server_capabilities_->tasks.has_value() &&
          server_capabilities_->tasks->cancel);
}

bool Client::supports_server_tasks() const noexcept {
  return !server_capabilities_known() ||
         server_capabilities_->tasks.has_value();
}

bool Client::supports_server_task_tool_call() const noexcept {
  return !server_capabilities_known() ||
         (server_capabilities_->tasks.has_value() &&
          server_capabilities_->tasks->tools_call);
}

Client Client::connect_streamable_http(StreamableHttpEndpoint endpoint) {
  HttpTransportOptions options;
  options.uri = std::move(endpoint.uri);
  options.host = std::move(endpoint.host);
  options.port = endpoint.port;
  options.path = std::move(endpoint.path);
  options.headers = std::move(endpoint.headers);
  options.auth_header = std::move(endpoint.auth_header);
  options.timeout = endpoint.timeout;
  return Client(std::make_unique<HttpTransport>(std::move(options)));
}

Client Client::connect_streamable_http(std::string uri) {
  StreamableHttpEndpoint endpoint;
  endpoint.uri = std::move(uri);
  return connect_streamable_http(std::move(endpoint));
}

Client Client::connect_legacy_sse(StreamableHttpEndpoint endpoint) {
  endpoint.headers.emplace("Accept", "application/json, text/event-stream");
  return connect_streamable_http(std::move(endpoint));
}

Client Client::connect_legacy_sse(std::string uri) {
  StreamableHttpEndpoint endpoint;
  endpoint.uri = std::move(uri);
  return connect_legacy_sse(std::move(endpoint));
}

Client Client::connect_stdio(StdioEndpoint endpoint) {
  if (endpoint.command.empty()) {
    return Client(std::make_unique<StdioTransport>());
  }
  ProcessStdioTransportOptions options;
  options.command = std::move(endpoint.command);
  options.args = std::move(endpoint.args);
  options.cwd = std::move(endpoint.cwd);
  options.env = std::move(endpoint.env);
  return Client(std::make_unique<ProcessStdioTransport>(std::move(options)));
}

core::Result<protocol::Json> Client::send_request(std::string method,
                                                  protocol::Json params) {
  const auto response = send_rpc_request(protocol::make_request(
      std::move(method), next_request_id(), std::move(params)));
  if (!response) {
    return std::unexpected(response.error());
  }

  return require_result_payload(*response);
}

core::Result<protocol::JsonRpcResponse> Client::send_rpc_request(
    protocol::JsonRpcRequest request) {
  const auto started = ensure_transport_started();
  if (!started) {
    return std::unexpected(started.error());
  }

  if (request.method != std::string(protocol::InitializeMethod) &&
      !last_initialize_params_.has_value() &&
      dynamic_cast<HttpTransport*>(transport_.get()) != nullptr) {
    last_initialize_params_ = initialize_params("cxxmcp", "0", capabilities_);
    const auto initialize_response = transport_->send(
        protocol::make_request(std::string(protocol::InitializeMethod),
                               next_request_id(), *last_initialize_params_));
    if (!initialize_response) {
      return std::unexpected(initialize_response.error());
    }

    const auto initialized_payload =
        require_initialize_payload(*initialize_response);
    if (!initialized_payload) {
      return std::unexpected(initialized_payload.error());
    }
    const auto recorded = record_server_capabilities(*initialized_payload);
    if (!recorded) {
      return std::unexpected(recorded.error());
    }
  }

  bool retried_after_session_reset = false;
  while (true) {
    auto response = transport_->send(request);
    if (!response) {
      if (!retried_after_session_reset &&
          is_session_terminated_error(response.error())) {
        retried_after_session_reset = true;
        if (request.method != std::string(protocol::InitializeMethod)) {
          if (!last_initialize_params_.has_value()) {
            return std::unexpected(response.error());
          }

          const auto initialize_response =
              transport_->send(protocol::make_request(
                  std::string(protocol::InitializeMethod), next_request_id(),
                  *last_initialize_params_));
          if (!initialize_response) {
            return std::unexpected(initialize_response.error());
          }

          const auto initialized_payload =
              require_initialize_payload(*initialize_response);
          if (!initialized_payload) {
            return std::unexpected(initialized_payload.error());
          }
          const auto recorded =
              record_server_capabilities(*initialized_payload);
          if (!recorded) {
            return std::unexpected(recorded.error());
          }
        }
        continue;
      }

      return std::unexpected(response.error());
    }

    return *response;
  }
}

core::Result<protocol::Json> Client::initialize(std::string client_name,
                                                std::string client_version) {
  auto params = initialize_params(std::move(client_name),
                                  std::move(client_version), capabilities_);
  last_initialize_params_ = params;
  const auto response = send_rpc_request(
      protocol::make_request(std::string(protocol::InitializeMethod),
                             next_request_id(), std::move(params)));
  if (!response) {
    return std::unexpected(response.error());
  }
  const auto payload = require_initialize_payload(*response);
  if (!payload) {
    return std::unexpected(payload.error());
  }
  const auto recorded = record_server_capabilities(*payload);
  if (!recorded) {
    return std::unexpected(recorded.error());
  }
  return *payload;
}

core::Result<core::Unit> Client::notify_initialized() {
  return raw_notification(protocol::make_notification(
      std::string(protocol::InitializedMethod), protocol::Json::object()));
}

Client& Client::on_progress(ProgressHandler handler) {
  progress_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_elicitation_complete(ElicitationCompleteHandler handler) {
  elicitation_complete_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_task_status(TaskStatusHandler handler) {
  task_status_handler_ = std::move(handler);
  return *this;
}

core::Result<core::Unit> Client::notify_cancelled(
    protocol::RequestId request_id, std::string reason) {
  protocol::CancelledNotificationParams params;
  params.request_id = std::move(request_id);
  params.reason = std::move(reason);
  return raw_notification(protocol::make_notification(
      "notifications/cancelled",
      protocol::cancelled_notification_params_to_json(params)));
}

core::Result<core::Unit> Client::notify_progress(
    protocol::ProgressToken progress_token, double progress,
    std::optional<double> total, std::string message) {
  protocol::ProgressNotificationParams params;
  params.progress_token = std::move(progress_token);
  params.progress = progress;
  params.total = total;
  params.message = std::move(message);
  return raw_notification(protocol::make_notification(
      "notifications/progress",
      protocol::progress_notification_params_to_json(params)));
}

core::Result<core::Unit> Client::notify_roots_list_changed() {
  return raw_notification(protocol::make_notification(
      "notifications/roots/list_changed", protocol::Json::object()));
}

core::Result<core::Unit> Client::ping() {
  const auto payload =
      send_request(std::string(protocol::PingMethod), protocol::Json::object());
  if (!payload) {
    return std::unexpected(payload.error());
  }
  return core::Unit{};
}

core::Result<std::vector<protocol::Prompt>> Client::list_prompts() {
  const auto payload = send_request(std::string(protocol::PromptsListMethod),
                                    protocol::Json::object());
  if (!payload) {
    return std::unexpected(payload.error());
  }
  const auto prompts = protocol::prompts_list_result_from_json(*payload);
  if (!prompts) {
    return std::unexpected(prompts.error());
  }

  return prompts->prompts;
}

core::Result<std::vector<protocol::Prompt>> Client::list_all_prompts() {
  std::vector<protocol::Prompt> all;
  std::optional<std::string> cursor;
  do {
    const auto payload = send_request(std::string(protocol::PromptsListMethod),
                                      cursor_params(cursor));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto page = protocol::prompts_list_result_from_json(*payload);
    if (!page) {
      return std::unexpected(page.error());
    }
    all.insert(all.end(), page->prompts.begin(), page->prompts.end());
    cursor = page->next_cursor;
  } while (cursor.has_value() && !cursor->empty());
  return all;
}

core::Result<protocol::PromptsGetResult> Client::get_prompt(
    const protocol::PromptsGetParams& params) {
  const auto response = send_rpc_request(protocol::make_request(
      std::string(protocol::PromptsGetMethod), next_request_id(),
      protocol::prompts_get_params_to_json(params)));
  if (!response) {
    return std::unexpected(response.error());
  }

  const auto payload = require_result_payload(*response);
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto prompt = protocol::prompts_get_result_from_json(*payload);
  if (!prompt) {
    return std::unexpected(prompt.error());
  }

  return *prompt;
}

core::Result<protocol::PromptsGetResult> Client::get_prompt(
    std::string_view name, const protocol::Json& arguments) {
  protocol::PromptsGetParams params;
  params.name = std::string(name);
  params.arguments = arguments;
  return get_prompt(params);
}

core::Result<std::vector<protocol::Resource>> Client::list_resources() {
  const auto response = send_rpc_request(
      protocol::make_request(std::string(protocol::ResourcesListMethod),
                             next_request_id(), protocol::Json::object()));
  if (!response) {
    return std::unexpected(response.error());
  }

  const auto payload = require_result_payload(*response);
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto resources = protocol::resources_list_result_from_json(*payload);
  if (!resources) {
    return std::unexpected(resources.error());
  }

  return resources->resources;
}

core::Result<std::vector<protocol::Resource>> Client::list_all_resources() {
  std::vector<protocol::Resource> all;
  std::optional<std::string> cursor;
  do {
    const auto payload = send_request(
        std::string(protocol::ResourcesListMethod), cursor_params(cursor));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto page = protocol::resources_list_result_from_json(*payload);
    if (!page) {
      return std::unexpected(page.error());
    }
    all.insert(all.end(), page->resources.begin(), page->resources.end());
    cursor = page->next_cursor;
  } while (cursor.has_value() && !cursor->empty());
  return all;
}

core::Result<protocol::ResourcesReadResult> Client::read_resource(
    const protocol::ResourcesReadParams& params) {
  const auto response = send_rpc_request(protocol::make_request(
      std::string(protocol::ResourcesReadMethod), next_request_id(),
      protocol::resources_read_params_to_json(params)));
  if (!response) {
    return std::unexpected(response.error());
  }

  const auto payload = require_result_payload(*response);
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto resource = protocol::resources_read_result_from_json(*payload);
  if (!resource) {
    return std::unexpected(resource.error());
  }

  return *resource;
}

core::Result<protocol::ResourcesReadResult> Client::read_resource(
    std::string_view uri) {
  return read_resource(protocol::ResourcesReadParams{std::string(uri)});
}

core::Result<std::vector<protocol::ResourceTemplate>>
Client::list_resource_templates() {
  const auto payload =
      send_request("resources/templates/list", protocol::Json::object());
  if (!payload) {
    return std::unexpected(payload.error());
  }
  const auto templates =
      protocol::resource_templates_list_result_from_json(*payload);
  if (!templates) {
    return std::unexpected(templates.error());
  }
  return templates->resource_templates;
}

core::Result<std::vector<protocol::ResourceTemplate>>
Client::list_all_resource_templates() {
  std::vector<protocol::ResourceTemplate> all;
  std::optional<std::string> cursor;
  do {
    const auto payload =
        send_request("resources/templates/list", cursor_params(cursor));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto page =
        protocol::resource_templates_list_result_from_json(*payload);
    if (!page) {
      return std::unexpected(page.error());
    }
    all.insert(all.end(), page->resource_templates.begin(),
               page->resource_templates.end());
    cursor = page->next_cursor;
  } while (cursor.has_value() && !cursor->empty());
  return all;
}

core::Result<std::vector<protocol::ToolDefinition>> Client::list_tools() {
  const auto response = send_rpc_request(protocol::make_request(
      "tools/list", next_request_id(), protocol::Json::object()));
  if (!response) {
    return std::unexpected(response.error());
  }

  const auto payload = require_result_payload(*response);
  if (!payload) {
    return std::unexpected(payload.error());
  }

  if (!payload->is_object() || !payload->contains("tools") ||
      !payload->at("tools").is_array()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                          "tools/list response must contain a tools array"));
  }

  std::vector<protocol::ToolDefinition> tools;
  tools.reserve(payload->at("tools").size());
  for (const auto& item : payload->at("tools")) {
    const auto definition = protocol::tool_definition_from_json(item);
    if (!definition) {
      return std::unexpected(definition.error());
    }
    tools.push_back(*definition);
  }

  return tools;
}

core::Result<std::vector<protocol::ToolDefinition>> Client::list_all_tools() {
  std::vector<protocol::ToolDefinition> all;
  std::optional<std::string> cursor;
  do {
    const auto payload = send_request("tools/list", cursor_params(cursor));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    if (!payload->is_object() || !payload->contains("tools") ||
        !payload->at("tools").is_array()) {
      return std::unexpected(make_client_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "tools/list response must contain a tools array"));
    }
    for (const auto& item : payload->at("tools")) {
      const auto definition = protocol::tool_definition_from_json(item);
      if (!definition) {
        return std::unexpected(definition.error());
      }
      all.push_back(*definition);
    }
    cursor = std::nullopt;
    if (payload->contains("nextCursor")) {
      if (!payload->at("nextCursor").is_string()) {
        return std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "tools/list nextCursor must be a string"));
      }
      cursor = payload->at("nextCursor").get<std::string>();
    }
  } while (cursor.has_value() && !cursor->empty());
  return all;
}

core::Result<protocol::ToolResult> Client::call_tool(
    const protocol::ToolCall& call) {
  const auto response = send_rpc_request(protocol::make_request(
      std::string(protocol::ToolsCallMethod), next_request_id(),
      protocol::tool_call_to_json(call)));
  if (!response) {
    return std::unexpected(response.error());
  }

  const auto payload = require_result_payload(*response);
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto tool_result = protocol::tool_result_from_json(*payload);
  if (!tool_result) {
    return std::unexpected(tool_result.error());
  }

  return *tool_result;
}

core::Result<protocol::CreateTaskResult> Client::call_tool_task(
    const protocol::ToolCall& call) {
  if (!call.task.has_value()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                          "task-aware tool call requires task parameters"));
  }
  if (!supports_server_task_tool_call()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support task-aware tool calls"));
  }

  const auto response = send_rpc_request(protocol::make_request(
      std::string(protocol::ToolsCallMethod), next_request_id(),
      protocol::tool_call_to_json(call)));
  if (!response) {
    return std::unexpected(response.error());
  }

  const auto payload = require_result_payload(*response);
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto task = protocol::create_task_result_from_json(*payload);
  if (!task) {
    return std::unexpected(task.error());
  }
  return *task;
}

core::Result<protocol::ToolResult> Client::call_raw(
    std::string_view name, const protocol::Json& arguments) {
  protocol::ToolCall call;
  call.name = std::string(name);
  call.arguments = arguments;
  return call_tool(call);
}

core::Result<protocol::CompleteResult> Client::complete(
    const protocol::CompleteParams& request) {
  if (!supports_server_completion()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support completion"));
  }
  const auto payload =
      send_request(std::string(protocol::CompletionCompleteMethod),
                   protocol::complete_params_to_json(request));
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto result = protocol::complete_result_from_json(*payload);
  if (!result) {
    return std::unexpected(result.error());
  }
  return *result;
}

core::Result<protocol::Json> Client::complete(const protocol::Json& request) {
  if (!supports_server_completion()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support completion"));
  }
  return send_request("completion/complete", request);
}

core::Result<protocol::CompletionResult> Client::complete_prompt_argument(
    std::string_view prompt_name, std::string_view argument_name,
    std::string current_value, protocol::Json context) {
  protocol::CompletionArgument argument;
  argument.name = std::string(argument_name);
  argument.value = std::move(current_value);
  protocol::CompleteParams params;
  params.ref = protocol::prompt_completion_reference(std::string(prompt_name));
  params.argument = std::move(argument);
  params.context = std::move(context);
  const auto result = complete(params);
  if (!result) {
    return std::unexpected(result.error());
  }
  return result->completion;
}

core::Result<protocol::CompletionResult> Client::complete_resource_argument(
    std::string_view uri_template, std::string_view argument_name,
    std::string current_value, protocol::Json context) {
  protocol::CompletionArgument argument;
  argument.name = std::string(argument_name);
  argument.value = std::move(current_value);
  protocol::CompleteParams params;
  params.ref =
      protocol::resource_completion_reference(std::string(uri_template));
  params.argument = std::move(argument);
  params.context = std::move(context);
  const auto result = complete(params);
  if (!result) {
    return std::unexpected(result.error());
  }
  return result->completion;
}

core::Result<std::vector<std::string>> Client::complete_prompt_simple(
    std::string_view prompt_name, std::string_view argument_name,
    std::string current_value, protocol::Json context) {
  const auto completion = complete_prompt_argument(
      prompt_name, argument_name, std::move(current_value), std::move(context));
  if (!completion) {
    return std::unexpected(completion.error());
  }
  return completion->values;
}

core::Result<std::vector<std::string>> Client::complete_resource_simple(
    std::string_view uri_template, std::string_view argument_name,
    std::string current_value, protocol::Json context) {
  const auto completion =
      complete_resource_argument(uri_template, argument_name,
                                 std::move(current_value), std::move(context));
  if (!completion) {
    return std::unexpected(completion.error());
  }
  return completion->values;
}

core::Result<protocol::CreateMessageResult> Client::create_message(
    const protocol::CreateMessageParams& request) {
  const auto payload =
      send_request(std::string(protocol::SamplingCreateMessageMethod),
                   protocol::create_message_params_to_json(request));
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto result = protocol::create_message_result_from_json(*payload);
  if (!result) {
    return std::unexpected(result.error());
  }
  return *result;
}

core::Result<protocol::Json> Client::create_message(
    const protocol::Json& request) {
  return send_request("sampling/createMessage", request);
}

core::Result<protocol::CreateElicitationResult> Client::create_elicitation(
    const protocol::CreateElicitationRequestParam& request) {
  const auto payload =
      send_request(std::string(protocol::ElicitationCreateMethod),
                   protocol::create_elicitation_request_param_to_json(request));
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto result = protocol::create_elicitation_result_from_json(*payload);
  if (!result) {
    return std::unexpected(result.error());
  }
  return *result;
}

core::Result<protocol::Json> Client::create_elicitation(
    const protocol::Json& request) {
  return send_request(std::string(protocol::ElicitationCreateMethod), request);
}

core::Result<std::vector<protocol::Task>> Client::list_tasks() {
  if (!supports_server_task_list()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support task listing"));
  }
  const auto payload = send_request(std::string(protocol::TasksListMethod),
                                    protocol::Json::object());
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto tasks = protocol::task_list_result_from_json(*payload);
  if (!tasks) {
    return std::unexpected(tasks.error());
  }

  return tasks->tasks;
}

core::Result<std::vector<protocol::Task>> Client::list_all_tasks() {
  if (!supports_server_task_list()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support task listing"));
  }
  std::vector<protocol::Task> all;
  std::optional<std::string> cursor;
  do {
    const auto payload = send_request(std::string(protocol::TasksListMethod),
                                      cursor_params(cursor));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto page = protocol::task_list_result_from_json(*payload);
    if (!page) {
      return std::unexpected(page.error());
    }
    all.insert(all.end(), page->tasks.begin(), page->tasks.end());
    cursor = page->next_cursor;
  } while (cursor.has_value() && !cursor->empty());
  return all;
}

core::Result<protocol::Task> Client::get_task(
    const protocol::TaskGetParams& request) {
  if (!supports_server_tasks()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support tasks"));
  }
  const auto payload = send_request(std::string(protocol::TasksGetMethod),
                                    protocol::task_get_params_to_json(request));
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto task = protocol::task_from_json(*payload);
  if (!task) {
    return std::unexpected(task.error());
  }

  return *task;
}

core::Result<protocol::Task> Client::get_task(std::string_view task_id) {
  protocol::TaskGetParams params;
  params.task_id = std::string(task_id);
  return get_task(params);
}

core::Result<protocol::Task> Client::cancel_task(
    const protocol::TaskCancelParams& request) {
  if (!supports_server_task_cancel()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support task cancellation"));
  }
  const auto payload =
      send_request(std::string(protocol::TasksCancelMethod),
                   protocol::task_cancel_params_to_json(request));
  if (!payload) {
    return std::unexpected(payload.error());
  }

  const auto task = protocol::task_from_json(*payload);
  if (!task) {
    return std::unexpected(task.error());
  }

  return *task;
}

core::Result<protocol::Task> Client::cancel_task(std::string_view task_id) {
  protocol::TaskCancelParams params;
  params.task_id = std::string(task_id);
  return cancel_task(params);
}

core::Result<protocol::Json> Client::task_result(
    const protocol::TaskResultParams& request) {
  if (!supports_server_tasks()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support tasks"));
  }
  return send_request(std::string(protocol::TasksResultMethod),
                      protocol::task_result_params_to_json(request));
}

core::Result<protocol::Json> Client::task_result(std::string_view task_id) {
  protocol::TaskResultParams params;
  params.task_id = std::string(task_id);
  return task_result(params);
}

core::Result<core::Unit> Client::set_level(
    const protocol::LoggingSetLevelParams& params) {
  if (!supports_server_logging()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support logging"));
  }
  const auto result =
      send_request(std::string(protocol::LoggingSetLevelMethod),
                   protocol::logging_set_level_params_to_json(params));
  if (!result) {
    return std::unexpected(result.error());
  }
  return core::Unit{};
}

core::Result<core::Unit> Client::set_level(std::string_view level) {
  const auto parsed = protocol::logging_level_from_string(std::string(level));
  if (!parsed.has_value()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                          "logging/setLevel level is invalid"));
  }
  protocol::LoggingSetLevelParams params;
  params.level = *parsed;
  return set_level(params);
}

core::Result<core::Unit> Client::subscribe(std::string_view uri) {
  if (!supports_server_resource_subscribe()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support resource subscriptions"));
  }
  const auto result = send_request("resources/subscribe",
                                   protocol::Json{{"uri", std::string(uri)}});
  if (!result) {
    return std::unexpected(result.error());
  }
  return core::Unit{};
}

core::Result<core::Unit> Client::unsubscribe(std::string_view uri) {
  if (!supports_server_resource_subscribe()) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "server does not support resource subscriptions"));
  }
  const auto result = send_request("resources/unsubscribe",
                                   protocol::Json{{"uri", std::string(uri)}});
  if (!result) {
    return std::unexpected(result.error());
  }
  return core::Unit{};
}

core::Result<protocol::Json> Client::request(
    const protocol::JsonRpcRequest& request) {
  return raw_request(request);
}

core::Result<core::Unit> Client::notify(
    const protocol::JsonRpcNotification& notification) {
  return raw_notification(notification);
}

std::vector<protocol::Root> Client::list_roots() const { return roots_; }

Client& Client::set_roots(std::vector<protocol::Root> roots) {
  roots_ = std::move(roots);
  if (roots_list_changed_handler_) {
    roots_list_changed_handler_();
  }
  return *this;
}

Client& Client::set_capabilities(protocol::ClientCapabilities capabilities) {
  capabilities_ = std::move(capabilities);
  return *this;
}

Client& Client::on_initialized(InitializedHandler handler) {
  initialized_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_cancelled(CancelledHandler handler) {
  cancelled_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_logging_message(LoggingMessageHandler handler) {
  logging_message_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_tool_list_changed(ListChangedHandler handler) {
  tool_list_changed_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_prompt_list_changed(ListChangedHandler handler) {
  prompt_list_changed_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_resource_list_changed(ListChangedHandler handler) {
  resource_list_changed_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_resource_updated(ResourceUpdatedHandler handler) {
  resource_updated_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_roots_list_changed(ListChangedHandler handler) {
  roots_list_changed_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_list_roots_request(ListRootsRequestHandler handler) {
  roots_list_request_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_create_message_request(CreateMessageRequestHandler handler) {
  sampling_request_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_create_elicitation_request(
    CreateElicitationRequestHandler handler) {
  elicitation_request_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_custom_request(CustomRequestHandler handler) {
  custom_request_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_roots_list_request(RootsListRequestHandler handler) {
  return on_list_roots_request(std::move(handler));
}

Client& Client::on_sampling_request(SamplingRequestHandler handler) {
  return on_create_message_request(std::move(handler));
}

Client& Client::on_elicitation_request(ElicitationRequestHandler handler) {
  return on_create_elicitation_request(std::move(handler));
}

Client& Client::on_raw_notification(RawNotificationHandler handler) {
  raw_notification_handler_ = std::move(handler);
  return *this;
}

Client& Client::on_custom_notification(RawNotificationHandler handler) {
  return on_raw_notification(std::move(handler));
}

core::Result<core::Unit> Client::handle_notification(
    const protocol::JsonRpcNotification& notification) try {
  if (notification.method == std::string(protocol::InitializedMethod) &&
      initialized_handler_) {
    initialized_handler_();
  } else if (notification.method ==
                 std::string(protocol::CancelledNotificationMethod) &&
             cancelled_handler_) {
    const auto params =
        protocol::cancelled_notification_params_from_json(notification.params);
    if (!params) {
      return std::unexpected(make_client_error(
          static_cast<int>(protocol::ErrorCode::InvalidParams),
          "cancelled notification requires a requestId"));
    }
    cancelled_handler_(params->request_id, params->reason);
  } else if (notification.method == "notifications/message" &&
             logging_message_handler_) {
    std::string level;
    std::string message;
    if (notification.params.is_object()) {
      if (notification.params.contains("level") &&
          notification.params.at("level").is_string()) {
        level = notification.params.at("level").get<std::string>();
      }
      if (notification.params.contains("data")) {
        if (notification.params.at("data").is_string()) {
          message = notification.params.at("data").get<std::string>();
        } else {
          message = notification.params.at("data").dump();
        }
      }
    }
    logging_message_handler_(level, message);
  } else if (notification.method == "notifications/tools/list_changed" &&
             tool_list_changed_handler_) {
    tool_list_changed_handler_();
  } else if (notification.method == "notifications/prompts/list_changed" &&
             prompt_list_changed_handler_) {
    prompt_list_changed_handler_();
  } else if (notification.method == "notifications/resources/list_changed" &&
             resource_list_changed_handler_) {
    resource_list_changed_handler_();
  } else if (notification.method == "notifications/resources/updated" &&
             resource_updated_handler_) {
    if (!notification.params.is_object() ||
        !notification.params.contains("uri") ||
        !notification.params.at("uri").is_string()) {
      return std::unexpected(make_client_error(
          static_cast<int>(protocol::ErrorCode::InvalidParams),
          "resource updated notification requires a string uri"));
    }
    resource_updated_handler_(notification.params.at("uri").get<std::string>());
  } else if (notification.method ==
                 std::string(protocol::ProgressNotificationMethod) &&
             progress_handler_) {
    const auto params =
        protocol::progress_notification_params_from_json(notification.params);
    if (!params) {
      return std::unexpected(make_client_error(
          static_cast<int>(protocol::ErrorCode::InvalidParams),
          "progress notification requires valid params"));
    }
    progress_handler_(*params);
  } else if (notification.method ==
                 std::string(protocol::ElicitationCompleteNotificationMethod) &&
             elicitation_complete_handler_) {
    const auto params =
        protocol::elicitation_complete_notification_params_from_json(
            notification.params);
    if (!params) {
      return std::unexpected(make_client_error(
          static_cast<int>(protocol::ErrorCode::InvalidParams),
          "elicitation completion notification requires valid params"));
    }
    elicitation_complete_handler_(params->elicitation_id);
  } else if (notification.method ==
                 std::string(protocol::TasksStatusNotificationMethod) &&
             task_status_handler_) {
    const auto task = protocol::task_from_json(notification.params);
    if (!task) {
      return std::unexpected(make_client_error(
          static_cast<int>(protocol::ErrorCode::InvalidParams),
          "task status notification requires valid task data"));
    }
    task_status_handler_(*task);
  } else if (notification.method == "notifications/roots/list_changed" &&
             roots_list_changed_handler_) {
    roots_list_changed_handler_();
  }

  if (raw_notification_handler_) {
    raw_notification_handler_(notification);
  }
  return core::Unit{};
} catch (const std::exception& ex) {
  return std::unexpected(errors::handler_failed(ex.what()));
} catch (...) {
  return std::unexpected(errors::handler_unknown_exception());
}

core::Result<protocol::JsonRpcResponse> Client::handle_request(
    const protocol::JsonRpcRequest& request) try {
  if (request.method == std::string(protocol::PingMethod)) {
    return protocol::make_response(request.id, protocol::Json::object());
  }

  if (request.method == std::string(protocol::RootsListMethod)) {
    if (roots_list_request_handler_) {
      const auto roots = roots_list_request_handler_();
      if (!roots) {
        return make_error_response(request, roots.error().code,
                                   roots.error().message, roots.error().detail);
      }
      return protocol::make_response(
          request.id, protocol::roots_list_result_to_json(*roots));
    }

    protocol::RootsListResult result;
    result.roots = roots_;
    return protocol::make_response(request.id,
                                   protocol::roots_list_result_to_json(result));
  }

  if (request.method == std::string(protocol::SamplingCreateMessageMethod)) {
    if (!sampling_request_handler_) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "sampling request handler is not configured");
    }

    const auto params =
        protocol::create_message_params_from_json(request.params);
    if (!params) {
      return make_error_response(request, params.error().code,
                                 params.error().message, params.error().detail);
    }

    const auto result = sampling_request_handler_(*params);
    if (!result) {
      return make_error_response(request, result.error().code,
                                 result.error().message, result.error().detail);
    }

    return protocol::make_response(
        request.id, protocol::create_message_result_to_json(*result));
  }

  if (request.method == std::string(protocol::ElicitationCreateMethod)) {
    const auto params =
        protocol::create_elicitation_request_param_from_json(request.params);
    if (!params) {
      return make_error_response(request, params.error().code,
                                 params.error().message, params.error().detail);
    }

    if (!elicitation_request_handler_) {
      return protocol::make_response(
          request.id, protocol::create_elicitation_result_to_json(
                          protocol::CreateElicitationResult{
                              .action = protocol::ElicitationAction::Decline,
                          }));
    }

    const auto result = elicitation_request_handler_(*params);
    if (!result) {
      return make_error_response(request, result.error().code,
                                 result.error().message, result.error().detail);
    }

    return protocol::make_response(
        request.id, protocol::create_elicitation_result_to_json(*result));
  }

  if (custom_request_handler_) {
    const auto result = custom_request_handler_(request);
    if (!result) {
      return make_error_response(request, result.error().code,
                                 result.error().message, result.error().detail);
    }
    return protocol::make_response(request.id, std::move(*result));
  }

  return make_error_response(
      request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
      "method not found", request.method);
} catch (const std::exception& ex) {
  const auto error = errors::handler_failed(ex.what());
  return make_error_response(request, error.code, error.message, error.detail);
} catch (...) {
  const auto error = errors::handler_unknown_exception();
  return make_error_response(request, error.code, error.message, error.detail);
}

core::Result<protocol::Json> Client::raw_request(
    const protocol::JsonRpcRequest& request) {
  const auto response = send_rpc_request(request);
  if (!response) {
    return std::unexpected(response.error());
  }

  return require_result_payload(*response);
}

RequestHandle<protocol::Json> Client::request_async(std::string method,
                                                    protocol::Json params,
                                                    RequestOptions options) {
  protocol::JsonRpcRequest request = protocol::make_request(
      std::move(method), next_request_id(), std::move(params));
  if (options.meta.has_value()) {
    request.meta = std::move(options.meta);
  }

  const auto request_id = request.id;
  return RequestHandle<protocol::Json>::spawn(
      request_id, options.timeout, options.cancellation_token,
      [this, request_id](std::string reason) mutable {
        return notify_cancelled(std::move(request_id), std::move(reason));
      },
      [this, request = std::move(request)]() mutable {
        return raw_request(request);
      });
}

RequestHandle<std::vector<protocol::ToolDefinition>> Client::list_tools_async(
    RequestOptions options) {
  return request_async<std::vector<protocol::ToolDefinition>>(
      std::string(protocol::ToolsListMethod), protocol::Json::object(),
      [](const protocol::Json& payload)
          -> core::Result<std::vector<protocol::ToolDefinition>> {
        const auto result = protocol::tools_list_result_from_json(payload);
        if (!result) {
          return std::unexpected(result.error());
        }
        return result->tools;
      },
      std::move(options));
}

RequestHandle<std::vector<protocol::Prompt>> Client::list_prompts_async(
    RequestOptions options) {
  return request_async<std::vector<protocol::Prompt>>(
      std::string(protocol::PromptsListMethod), protocol::Json::object(),
      [](const protocol::Json& payload)
          -> core::Result<std::vector<protocol::Prompt>> {
        const auto result = protocol::prompts_list_result_from_json(payload);
        if (!result) {
          return std::unexpected(result.error());
        }
        return result->prompts;
      },
      std::move(options));
}

RequestHandle<std::vector<protocol::Resource>> Client::list_resources_async(
    RequestOptions options) {
  return request_async<std::vector<protocol::Resource>>(
      std::string(protocol::ResourcesListMethod), protocol::Json::object(),
      [](const protocol::Json& payload)
          -> core::Result<std::vector<protocol::Resource>> {
        const auto result = protocol::resources_list_result_from_json(payload);
        if (!result) {
          return std::unexpected(result.error());
        }
        return result->resources;
      },
      std::move(options));
}

RequestHandle<std::vector<protocol::ResourceTemplate>>
Client::list_resource_templates_async(RequestOptions options) {
  return request_async<std::vector<protocol::ResourceTemplate>>(
      std::string(protocol::ResourcesTemplatesListMethod),
      protocol::Json::object(),
      [](const protocol::Json& payload)
          -> core::Result<std::vector<protocol::ResourceTemplate>> {
        const auto result =
            protocol::resource_templates_list_result_from_json(payload);
        if (!result) {
          return std::unexpected(result.error());
        }
        return result->resource_templates;
      },
      std::move(options));
}

RequestHandle<protocol::ToolResult> Client::call_tool_async(
    const protocol::ToolCall& call, RequestOptions options) {
  return request_async<protocol::ToolResult>(
      std::string(protocol::ToolsCallMethod), protocol::tool_call_to_json(call),
      [](const protocol::Json& payload) {
        return protocol::tool_result_from_json(payload);
      },
      std::move(options));
}

RequestHandle<protocol::ToolResult> Client::call_tool_async(
    std::string_view name, const protocol::Json& arguments,
    RequestOptions options) {
  protocol::ToolCall call;
  call.name = std::string(name);
  call.arguments = arguments;
  return call_tool_async(call, std::move(options));
}

RequestHandle<protocol::PromptsGetResult> Client::get_prompt_async(
    const protocol::PromptsGetParams& params, RequestOptions options) {
  return request_async<protocol::PromptsGetResult>(
      std::string(protocol::PromptsGetMethod),
      protocol::prompts_get_params_to_json(params),
      [](const protocol::Json& payload) {
        return protocol::prompts_get_result_from_json(payload);
      },
      std::move(options));
}

RequestHandle<protocol::PromptsGetResult> Client::get_prompt_async(
    std::string_view name, const protocol::Json& arguments,
    RequestOptions options) {
  protocol::PromptsGetParams params;
  params.name = std::string(name);
  params.arguments = arguments;
  return get_prompt_async(params, std::move(options));
}

RequestHandle<protocol::ResourcesReadResult> Client::read_resource_async(
    const protocol::ResourcesReadParams& params, RequestOptions options) {
  return request_async<protocol::ResourcesReadResult>(
      std::string(protocol::ResourcesReadMethod),
      protocol::resources_read_params_to_json(params),
      [](const protocol::Json& payload) {
        return protocol::resources_read_result_from_json(payload);
      },
      std::move(options));
}

RequestHandle<protocol::ResourcesReadResult> Client::read_resource_async(
    std::string_view uri, RequestOptions options) {
  return read_resource_async(protocol::ResourcesReadParams{std::string(uri)},
                             std::move(options));
}

RequestHandle<protocol::CreateTaskResult> Client::call_tool_task_async(
    const protocol::ToolCall& call, RequestOptions options) {
  if (!call.task.has_value()) {
    return RequestHandle<protocol::CreateTaskResult>::ready(
        next_request_id(),
        std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "task-aware tool call requires task parameters")));
  }
  if (!supports_server_task_tool_call()) {
    return RequestHandle<protocol::CreateTaskResult>::ready(
        next_request_id(),
        std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::MethodNotFound),
            "server does not support task-aware tool calls")));
  }

  return request_async<protocol::CreateTaskResult>(
      std::string(protocol::ToolsCallMethod), protocol::tool_call_to_json(call),
      [](const protocol::Json& payload) {
        return protocol::create_task_result_from_json(payload);
      },
      std::move(options));
}

RequestHandle<protocol::CompleteResult> Client::complete_async(
    const protocol::CompleteParams& request, RequestOptions options) {
  if (!supports_server_completion()) {
    return RequestHandle<protocol::CompleteResult>::ready(
        next_request_id(),
        std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::MethodNotFound),
            "server does not support completion")));
  }
  return request_async<protocol::CompleteResult>(
      std::string(protocol::CompletionCompleteMethod),
      protocol::complete_params_to_json(request),
      [](const protocol::Json& payload) {
        return protocol::complete_result_from_json(payload);
      },
      std::move(options));
}

RequestHandle<protocol::Json> Client::complete_async(
    const protocol::Json& request, RequestOptions options) {
  if (!supports_server_completion()) {
    return RequestHandle<protocol::Json>::ready(
        next_request_id(),
        std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::MethodNotFound),
            "server does not support completion")));
  }
  return request_async(std::string(protocol::CompletionCompleteMethod), request,
                       std::move(options));
}

RequestHandle<protocol::CreateMessageResult> Client::create_message_async(
    const protocol::CreateMessageParams& request, RequestOptions options) {
  return request_async<protocol::CreateMessageResult>(
      std::string(protocol::SamplingCreateMessageMethod),
      protocol::create_message_params_to_json(request),
      [](const protocol::Json& payload) {
        return protocol::create_message_result_from_json(payload);
      },
      std::move(options));
}

RequestHandle<protocol::Json> Client::create_message_async(
    const protocol::Json& request, RequestOptions options) {
  return request_async(std::string(protocol::SamplingCreateMessageMethod),
                       request, std::move(options));
}

RequestHandle<protocol::CreateElicitationResult>
Client::create_elicitation_async(
    const protocol::CreateElicitationRequestParam& request,
    RequestOptions options) {
  return request_async<protocol::CreateElicitationResult>(
      std::string(protocol::ElicitationCreateMethod),
      protocol::create_elicitation_request_param_to_json(request),
      [](const protocol::Json& payload) {
        return protocol::create_elicitation_result_from_json(payload);
      },
      std::move(options));
}

RequestHandle<protocol::Json> Client::create_elicitation_async(
    const protocol::Json& request, RequestOptions options) {
  return request_async(std::string(protocol::ElicitationCreateMethod), request,
                       std::move(options));
}

RequestHandle<std::vector<protocol::Task>> Client::list_tasks_async(
    RequestOptions options) {
  if (!supports_server_task_list()) {
    return RequestHandle<std::vector<protocol::Task>>::ready(
        next_request_id(),
        std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::MethodNotFound),
            "server does not support task listing")));
  }
  return request_async<std::vector<protocol::Task>>(
      std::string(protocol::TasksListMethod), protocol::Json::object(),
      [](const protocol::Json& payload)
          -> core::Result<std::vector<protocol::Task>> {
        const auto result = protocol::task_list_result_from_json(payload);
        if (!result) {
          return std::unexpected(result.error());
        }
        return result->tasks;
      },
      std::move(options));
}

RequestHandle<protocol::Task> Client::get_task_async(
    const protocol::TaskGetParams& request, RequestOptions options) {
  if (!supports_server_tasks()) {
    return RequestHandle<protocol::Task>::ready(
        next_request_id(),
        std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::MethodNotFound),
            "server does not support tasks")));
  }
  return request_async<protocol::Task>(
      std::string(protocol::TasksGetMethod),
      protocol::task_get_params_to_json(request),
      [](const protocol::Json& payload) {
        return protocol::task_from_json(payload);
      },
      std::move(options));
}

RequestHandle<protocol::Task> Client::get_task_async(std::string_view task_id,
                                                     RequestOptions options) {
  protocol::TaskGetParams params;
  params.task_id = std::string(task_id);
  return get_task_async(params, std::move(options));
}

RequestHandle<protocol::Task> Client::cancel_task_async(
    const protocol::TaskCancelParams& request, RequestOptions options) {
  if (!supports_server_task_cancel()) {
    return RequestHandle<protocol::Task>::ready(
        next_request_id(),
        std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::MethodNotFound),
            "server does not support task cancellation")));
  }
  return request_async<protocol::Task>(
      std::string(protocol::TasksCancelMethod),
      protocol::task_cancel_params_to_json(request),
      [](const protocol::Json& payload) {
        return protocol::task_from_json(payload);
      },
      std::move(options));
}

RequestHandle<protocol::Task> Client::cancel_task_async(
    std::string_view task_id, RequestOptions options) {
  protocol::TaskCancelParams params;
  params.task_id = std::string(task_id);
  return cancel_task_async(params, std::move(options));
}

RequestHandle<protocol::Json> Client::task_result_async(
    const protocol::TaskResultParams& request, RequestOptions options) {
  if (!supports_server_tasks()) {
    return RequestHandle<protocol::Json>::ready(
        next_request_id(),
        std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::MethodNotFound),
            "server does not support tasks")));
  }
  return request_async(std::string(protocol::TasksResultMethod),
                       protocol::task_result_params_to_json(request),
                       std::move(options));
}

RequestHandle<protocol::Json> Client::task_result_async(
    std::string_view task_id, RequestOptions options) {
  protocol::TaskResultParams params;
  params.task_id = std::string(task_id);
  return task_result_async(params, std::move(options));
}

core::Result<core::Unit> Client::raw_notification(
    const protocol::JsonRpcNotification& notification) {
  const auto started = ensure_transport_started();
  if (!started) {
    return std::unexpected(started.error());
  }

  return transport_->send_notification(notification);
}

void Client::stop() noexcept {
  if (transport_) {
    transport_->stop();
  }
  transport_started_ = false;
}

McpClientSession::McpClientSession(std::unique_ptr<Transport> transport,
                                   McpClientSessionOptions options)
    : client_(std::move(transport)), options_(std::move(options)) {}

core::Result<protocol::Json> McpClientSession::initialize() {
  return client_.initialize(options_.client_name, options_.client_version);
}

core::Result<core::Unit> McpClientSession::mark_initialized() {
  return client_.notify_initialized();
}

core::Result<std::vector<protocol::Prompt>>
McpClientSession::discover_prompts() {
  return client_.list_prompts();
}

core::Result<std::vector<protocol::Prompt>>
McpClientSession::discover_all_prompts() {
  return client_.list_all_prompts();
}

core::Result<protocol::PromptsGetResult> McpClientSession::get_prompt(
    const protocol::PromptsGetParams& params) {
  return client_.get_prompt(params);
}

core::Result<std::vector<protocol::Resource>>
McpClientSession::discover_resources() {
  return client_.list_resources();
}

core::Result<std::vector<protocol::Resource>>
McpClientSession::discover_all_resources() {
  return client_.list_all_resources();
}

core::Result<protocol::ResourcesReadResult> McpClientSession::read_resource(
    const protocol::ResourcesReadParams& params) {
  return client_.read_resource(params);
}

core::Result<std::vector<protocol::ResourceTemplate>>
McpClientSession::discover_resource_templates() {
  return client_.list_resource_templates();
}

core::Result<std::vector<protocol::ResourceTemplate>>
McpClientSession::discover_all_resource_templates() {
  return client_.list_all_resource_templates();
}

core::Result<std::vector<protocol::ToolDefinition>>
McpClientSession::discover_tools() {
  return client_.list_tools();
}

core::Result<std::vector<protocol::ToolDefinition>>
McpClientSession::discover_all_tools() {
  return client_.list_all_tools();
}

core::Result<protocol::ToolResult> McpClientSession::call_tool(
    const protocol::ToolCall& call) {
  return client_.call_tool(call);
}

Client& McpClientSession::client() { return client_; }

const Client& McpClientSession::client() const { return client_; }

}  // namespace mcp::client
