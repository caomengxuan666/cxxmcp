// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/client/client.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cxxmcp/client/http_transport.hpp"
#include "cxxmcp/client/process_stdio_transport.hpp"
#include "cxxmcp/client/session.hpp"
#include "cxxmcp/client/stdio_transport.hpp"
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
  return core::Error{code, std::move(message), std::move(detail)};
}

core::Result<protocol::Json> require_result_payload(
    const protocol::JsonRpcResponse& response) {
  if (response.error.has_value()) {
    return std::unexpected(core::Error{
        response.error->code,
        response.error->message,
        response.error->data.has_value() ? response.error->data->dump()
                                         : std::string{},
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

  const auto version = payload->at("protocolVersion").get<std::string>();
  if (!protocol::is_supported_protocol_version(version)) {
    return std::unexpected(
        make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                          "unsupported MCP protocol version", version));
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

  return protocol::ClientCapabilities{
      .roots = {.enabled = true, .list_changed = true},
      .sampling = {.enabled = true},
      .elicitation = {.form = true, .url = true},
  };
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

Client Client::connect_streamable_http(StreamableHttpEndpoint endpoint) {
  return Client(std::make_unique<HttpTransport>(HttpTransportOptions{
      .uri = std::move(endpoint.uri),
      .host = std::move(endpoint.host),
      .port = endpoint.port,
      .path = std::move(endpoint.path),
      .headers = std::move(endpoint.headers),
      .auth_header = std::move(endpoint.auth_header),
      .timeout = endpoint.timeout,
  }));
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
  return Client(
      std::make_unique<ProcessStdioTransport>(ProcessStdioTransportOptions{
          .command = std::move(endpoint.command),
          .args = std::move(endpoint.args),
          .cwd = std::move(endpoint.cwd),
          .env = std::move(endpoint.env),
      }));
}

core::Result<protocol::Json> Client::send_request(std::string method,
                                                  protocol::Json params) {
  const auto response = send_rpc_request(protocol::JsonRpcRequest{
      .method = std::move(method),
      .params = std::move(params),
      .id = next_request_id_++,
  });
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
    const auto initialize_response = transport_->send(protocol::JsonRpcRequest{
        .method = std::string(protocol::InitializeMethod),
        .params = *last_initialize_params_,
        .id = next_request_id_++,
    });
    if (!initialize_response) {
      return std::unexpected(initialize_response.error());
    }

    const auto initialized_payload =
        require_initialize_payload(*initialize_response);
    if (!initialized_payload) {
      return std::unexpected(initialized_payload.error());
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
              transport_->send(protocol::JsonRpcRequest{
                  .method = std::string(protocol::InitializeMethod),
                  .params = *last_initialize_params_,
                  .id = next_request_id_++,
              });
          if (!initialize_response) {
            return std::unexpected(initialize_response.error());
          }

          const auto initialized_payload =
              require_initialize_payload(*initialize_response);
          if (!initialized_payload) {
            return std::unexpected(initialized_payload.error());
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
  const auto response = send_rpc_request(protocol::JsonRpcRequest{
      .method = std::string(protocol::InitializeMethod),
      .params = std::move(params),
      .id = next_request_id_++,
  });
  if (!response) {
    return std::unexpected(response.error());
  }
  return require_initialize_payload(*response);
}

core::Result<core::Unit> Client::notify_initialized() {
  return raw_notification(protocol::JsonRpcNotification{
      .method = std::string(protocol::InitializedMethod),
      .params = protocol::Json::object(),
  });
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
  return raw_notification(protocol::JsonRpcNotification{
      .method = "notifications/cancelled",
      .params = protocol::cancelled_notification_params_to_json(
          protocol::CancelledNotificationParams{
              .request_id = std::move(request_id),
              .reason = std::move(reason),
          }),
  });
}

core::Result<core::Unit> Client::notify_progress(
    protocol::ProgressToken progress_token, double progress,
    std::optional<double> total, std::string message) {
  return raw_notification(protocol::JsonRpcNotification{
      .method = "notifications/progress",
      .params = protocol::progress_notification_params_to_json(
          protocol::ProgressNotificationParams{
              .progress_token = std::move(progress_token),
              .progress = progress,
              .total = total,
              .message = std::move(message),
          }),
  });
}

core::Result<core::Unit> Client::notify_roots_list_changed() {
  return raw_notification(protocol::JsonRpcNotification{
      .method = "notifications/roots/list_changed",
      .params = protocol::Json::object(),
  });
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
  const auto response = send_rpc_request(protocol::JsonRpcRequest{
      .method = std::string(protocol::PromptsGetMethod),
      .params = protocol::prompts_get_params_to_json(params),
      .id = next_request_id_++,
  });
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
  return get_prompt(protocol::PromptsGetParams{
      .name = std::string(name),
      .arguments = arguments,
  });
}

core::Result<std::vector<protocol::Resource>> Client::list_resources() {
  const auto response = send_rpc_request(protocol::JsonRpcRequest{
      .method = std::string(protocol::ResourcesListMethod),
      .params = protocol::Json::object(),
      .id = next_request_id_++,
  });
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
  const auto response = send_rpc_request(protocol::JsonRpcRequest{
      .method = std::string(protocol::ResourcesReadMethod),
      .params = protocol::resources_read_params_to_json(params),
      .id = next_request_id_++,
  });
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
  const auto response = send_rpc_request(protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = protocol::Json::object(),
      .id = next_request_id_++,
  });
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
  const auto response = send_rpc_request(protocol::JsonRpcRequest{
      .method = std::string(protocol::ToolsCallMethod),
      .params = protocol::tool_call_to_json(call),
      .id = next_request_id_++,
  });
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

  const auto response = send_rpc_request(protocol::JsonRpcRequest{
      .method = std::string(protocol::ToolsCallMethod),
      .params = protocol::tool_call_to_json(call),
      .id = next_request_id_++,
  });
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
  return call_tool(protocol::ToolCall{
      .name = std::string(name),
      .arguments = arguments,
  });
}

core::Result<protocol::CompleteResult> Client::complete(
    const protocol::CompleteParams& request) {
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
  return send_request("completion/complete", request);
}

core::Result<protocol::CompletionResult> Client::complete_prompt_argument(
    std::string_view prompt_name, std::string_view argument_name,
    std::string current_value, protocol::Json context) {
  const auto result = complete(protocol::CompleteParams{
      .ref = protocol::prompt_completion_reference(std::string(prompt_name)),
      .argument =
          protocol::CompletionArgument{
              .name = std::string(argument_name),
              .value = std::move(current_value),
          },
      .context = std::move(context),
  });
  if (!result) {
    return std::unexpected(result.error());
  }
  return result->completion;
}

core::Result<protocol::CompletionResult> Client::complete_resource_argument(
    std::string_view uri_template, std::string_view argument_name,
    std::string current_value, protocol::Json context) {
  const auto result = complete(protocol::CompleteParams{
      .ref = protocol::resource_completion_reference(std::string(uri_template)),
      .argument =
          protocol::CompletionArgument{
              .name = std::string(argument_name),
              .value = std::move(current_value),
          },
      .context = std::move(context),
  });
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
  return get_task(protocol::TaskGetParams{.task_id = std::string(task_id)});
}

core::Result<protocol::Task> Client::cancel_task(
    const protocol::TaskCancelParams& request) {
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
  return cancel_task(
      protocol::TaskCancelParams{.task_id = std::string(task_id)});
}

core::Result<protocol::Json> Client::task_result(
    const protocol::TaskResultParams& request) {
  return send_request(std::string(protocol::TasksResultMethod),
                      protocol::task_result_params_to_json(request));
}

core::Result<protocol::Json> Client::task_result(std::string_view task_id) {
  return task_result(
      protocol::TaskResultParams{.task_id = std::string(task_id)});
}

core::Result<core::Unit> Client::set_level(
    const protocol::LoggingSetLevelParams& params) {
  const auto result =
      send_request(std::string(protocol::LoggingSetLevelMethod),
                   protocol::logging_set_level_params_to_json(params));
  if (!result) {
    return std::unexpected(result.error());
  }
  return core::Unit{};
}

core::Result<core::Unit> Client::set_level(std::string_view level) {
  return set_level(protocol::LoggingSetLevelParams{
      .level = *protocol::logging_level_from_string(std::string(level)),
  });
}

core::Result<core::Unit> Client::subscribe(std::string_view uri) {
  const auto result = send_request("resources/subscribe",
                                   protocol::Json{{"uri", std::string(uri)}});
  if (!result) {
    return std::unexpected(result.error());
  }
  return core::Unit{};
}

core::Result<core::Unit> Client::unsubscribe(std::string_view uri) {
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
    const protocol::JsonRpcNotification& notification) {
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
}

core::Result<protocol::JsonRpcResponse> Client::handle_request(
    const protocol::JsonRpcRequest& request) {
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

    return protocol::make_response(
        request.id,
        protocol::roots_list_result_to_json(protocol::RootsListResult{
            .roots = roots_,
        }));
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
    if (!elicitation_request_handler_) {
      return make_error_response(
          request, static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "elicitation request handler is not configured");
    }

    const auto params =
        protocol::create_elicitation_request_param_from_json(request.params);
    if (!params) {
      return make_error_response(request, params.error().code,
                                 params.error().message, params.error().detail);
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
  protocol::JsonRpcRequest request{
      .method = std::move(method),
      .params = std::move(params),
      .id = next_request_id_++,
  };
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
  return call_tool_async(
      protocol::ToolCall{.name = std::string(name), .arguments = arguments},
      std::move(options));
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
  return get_prompt_async(protocol::PromptsGetParams{.name = std::string(name),
                                                     .arguments = arguments},
                          std::move(options));
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
        next_request_id_++,
        std::unexpected(make_client_error(
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "task-aware tool call requires task parameters")));
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
  return get_task_async(
      protocol::TaskGetParams{.task_id = std::string(task_id)},
      std::move(options));
}

RequestHandle<protocol::Task> Client::cancel_task_async(
    const protocol::TaskCancelParams& request, RequestOptions options) {
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
  return cancel_task_async(
      protocol::TaskCancelParams{.task_id = std::string(task_id)},
      std::move(options));
}

RequestHandle<protocol::Json> Client::task_result_async(
    const protocol::TaskResultParams& request, RequestOptions options) {
  return request_async(std::string(protocol::TasksResultMethod),
                       protocol::task_result_params_to_json(request),
                       std::move(options));
}

RequestHandle<protocol::Json> Client::task_result_async(
    std::string_view task_id, RequestOptions options) {
  return task_result_async(
      protocol::TaskResultParams{.task_id = std::string(task_id)},
      std::move(options));
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
