// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Role-aware peer execution boundaries for MCP client and server SDK
/// users.
///
/// Peer<RoleClient> and Peer<RoleServer> are the SDK-facing MCP execution
/// boundary. They expose role-generic message dispatch loops so
/// Transport<Role> can be the public service boundary while concrete Client and
/// Server types remain lower-level convenience APIs.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/client/client.hpp"
#include "cxxmcp/client/session.hpp"
#include "cxxmcp/client/transport_adapter_fwd.hpp"
#include "cxxmcp/config.hpp"
#include "cxxmcp/error.hpp"
#include "cxxmcp/handler.hpp"
#include "cxxmcp/roles.hpp"
#include "cxxmcp/server/peer.hpp"
#include "cxxmcp/server/server.hpp"
#include "cxxmcp/server/transport_adapter_fwd.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp {

namespace detail {

inline core::Error peer_dispatch_error(std::string_view message) {
  return errors::make(protocol::ErrorCode::InvalidRequest,
                      std::string(message));
}

inline core::Error peer_transport_error(std::string_view message) {
  return errors::make(protocol::ErrorCode::InvalidRequest, std::string(message),
                      {}, "transport");
}

inline protocol::ErrorObject peer_error_object_from_core_error(
    const core::Error& error) {
  return errors::to_json_rpc_error(error);
}

inline protocol::JsonRpcResponse peer_error_response(
    const protocol::JsonRpcRequest& request, const core::Error& error) {
  return protocol::make_error_response(
      request.id, peer_error_object_from_core_error(error));
}

inline std::string peer_request_cancellation_key(
    const protocol::RequestId& request_id) {
  return protocol::request_id_to_json(request_id).dump();
}

inline core::Result<protocol::Json> peer_require_result_payload(
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
    return std::unexpected(errors::make(protocol::ErrorCode::InvalidRequest,
                                        "response did not contain a result", {},
                                        "protocol"));
  }
  return *response.result;
}

template <class Transport, class Dispatch>
inline core::Result<core::Unit> serve_transport_loop(
    Transport& transport, CancellationToken cancellation, Dispatch dispatch) {
  while (!cancellation.cancelled()) {
    auto received = transport.receive();
    if (!received) {
      return std::unexpected(received.error());
    }
    if (!received->has_value()) {
      return core::Unit{};
    }

    auto dispatched = dispatch(received->value());
    if (!dispatched) {
      return std::unexpected(dispatched.error());
    }
    if (dispatched->has_value()) {
      auto sent = transport.send(std::move(dispatched->value()));
      if (!sent) {
        return std::unexpected(sent.error());
      }
    }
  }
  return core::Unit{};
}

inline void keep_first_service_error(std::optional<core::Error>& first_error,
                                     std::mutex& mutex,
                                     core::Error error) noexcept {
  std::lock_guard lock(mutex);
  if (!first_error.has_value()) {
    first_error = std::move(error);
  }
}

inline std::unique_ptr<client::Transport> make_peer_client_transport_adapter(
    std::unique_ptr<transport::ClientTransport>& transport) {
  if (!transport) {
    return client::make_contract_transport_adapter(
        std::unique_ptr<transport::ClientTransport>{});
  }
  return client::make_contract_transport_adapter(*transport);
}

inline protocol::ClientCapabilities default_peer_client_capabilities(
    const std::optional<protocol::ClientCapabilities>& capabilities) {
  if (capabilities.has_value()) {
    return *capabilities;
  }

  protocol::ClientCapabilities defaults;
  defaults.roots.enabled = true;
  defaults.roots.list_changed = true;
  defaults.sampling.enabled = true;
  defaults.sampling.tools = true;
  defaults.sampling.context = true;
  defaults.elicitation.form = true;
  defaults.elicitation.form_schema_validation = true;
  defaults.elicitation.url = true;
  return defaults;
}

inline protocol::Json make_peer_initialize_params(
    std::string client_name, std::string client_version,
    const std::optional<protocol::ClientCapabilities>& capabilities) {
  protocol::Json params = protocol::Json::object();
  params["protocolVersion"] = std::string(protocol::McpProtocolVersion);
  params["capabilities"] = protocol::client_capabilities_to_json(
      default_peer_client_capabilities(capabilities));
  params["clientInfo"] = protocol::Json{
      {"name", std::move(client_name)},
      {"version", std::move(client_version)},
  };
  return params;
}

inline core::Result<protocol::Json> require_peer_initialize_payload(
    const protocol::Json& payload) {
  if (!payload.is_object()) {
    return std::unexpected(errors::make(protocol::ErrorCode::InvalidRequest,
                                        "initialize response must be an object",
                                        {}, "protocol"));
  }
  if (!payload.contains("protocolVersion") ||
      !payload.at("protocolVersion").is_string()) {
    return std::unexpected(
        errors::make(protocol::ErrorCode::InvalidRequest,
                     "initialize response requires a string protocolVersion",
                     {}, "protocol"));
  }
  return payload;
}

inline core::Result<core::Unit> validate_peer_server_initialize_params(
    const protocol::Json& params) {
  if (!params.is_object()) {
    return std::unexpected(errors::make(protocol::ErrorCode::InvalidParams,
                                        "initialize params must be an object"));
  }
  if (!params.contains("protocolVersion") ||
      !params.at("protocolVersion").is_string()) {
    return std::unexpected(
        errors::make(protocol::ErrorCode::InvalidParams,
                     "initialize requires a string protocolVersion"));
  }

  return core::Unit{};
}

inline protocol::Json make_peer_server_initialize_result(
    const server::ServerInfo& info,
    const protocol::ServerCapabilities& capabilities,
    std::string_view protocol_version = protocol::McpProtocolVersion) {
  protocol::Json result = protocol::Json::object();
  result["protocolVersion"] = std::string(protocol_version);
  result["capabilities"] = protocol::server_capabilities_to_json(capabilities);
  result["serverInfo"] = protocol::Json{
      {"name", info.name},
      {"version", info.version},
  };
  if (!info.instructions.empty()) {
    result["instructions"] = info.instructions;
  }
  return result;
}

}  // namespace detail

/// @brief Role-specialized MCP peer boundary.
template <class Role>
class Peer;

/// @brief Client-side peer boundary for talking to an MCP server.
template <>
class Peer<RoleClient> {
 public:
  /// @brief Creates a client peer from an owned transport.
  explicit Peer(std::unique_ptr<client::Transport> transport)
      : client_(std::move(transport)) {}

  /// @brief Creates a client peer from an owned role-generic transport.
  explicit Peer(std::unique_ptr<transport::ClientTransport> transport)
      : native_transport_(std::move(transport)),
        client_(detail::make_peer_client_transport_adapter(native_transport_)) {
  }

  /// @brief Creates a client peer from an existing client implementation.
  explicit Peer(client::Client client) : client_(std::move(client)) {}

  static Peer connect_streamable_http(
      client::Client::StreamableHttpEndpoint endpoint) {
    return Peer(client::Client::connect_streamable_http(std::move(endpoint)));
  }

  static Peer connect_legacy_sse(
      client::Client::StreamableHttpEndpoint endpoint) {
    return Peer(client::Client::connect_legacy_sse(std::move(endpoint)));
  }

  static Peer connect_stdio(client::Client::StdioEndpoint endpoint) {
    return Peer(client::Client::connect_stdio(std::move(endpoint)));
  }

  CXXMCP_DEPRECATED(
      "client() is a compatibility escape hatch; prefer ClientPeer methods")
  client::Client& client() noexcept { return client_; }

  CXXMCP_DEPRECATED(
      "client() is a compatibility escape hatch; prefer ClientPeer methods")
  const client::Client& client() const noexcept { return client_; }

  void stop() noexcept {
    if (native_transport_) {
      (void)native_transport_->close();
      return;
    }
    client_.stop();
  }

  core::Result<protocol::Json> initialize(std::string client_name = "cxxmcp",
                                          std::string client_version = "0") {
    if (native_transport_) {
      auto payload =
          request_json(std::string(protocol::InitializeMethod),
                       detail::make_peer_initialize_params(
                           std::move(client_name), std::move(client_version),
                           client_capabilities_));
      if (!payload) {
        return std::unexpected(payload.error());
      }
      return detail::require_peer_initialize_payload(*payload);
    }
    return client_.initialize(std::move(client_name),
                              std::move(client_version));
  }

  core::Result<core::Unit> notify_initialized() {
    return raw_notification(protocol::make_notification(
        std::string(protocol::InitializedMethod), protocol::Json::object()));
  }

  core::Result<core::Unit> notify_cancelled(protocol::RequestId request_id,
                                            std::string reason = {}) {
    protocol::CancelledNotificationParams params;
    params.request_id = std::move(request_id);
    params.reason = std::move(reason);
    return raw_notification(protocol::make_notification(
        std::string(protocol::CancelledNotificationMethod),
        protocol::cancelled_notification_params_to_json(params)));
  }

  core::Result<core::Unit> notify_progress(
      protocol::ProgressToken progress_token, double progress,
      std::optional<double> total = std::nullopt, std::string message = {}) {
    protocol::ProgressNotificationParams params;
    params.progress_token = std::move(progress_token);
    params.progress = progress;
    params.total = total;
    params.message = std::move(message);
    return raw_notification(protocol::make_notification(
        std::string(protocol::ProgressNotificationMethod),
        protocol::progress_notification_params_to_json(params)));
  }

  core::Result<core::Unit> notify_roots_list_changed() {
    return raw_notification(protocol::make_notification(
        std::string(protocol::RootsListChangedNotificationMethod),
        protocol::Json::object()));
  }

  core::Result<core::Unit> ping() {
    return request_unit(std::string(protocol::PingMethod),
                        protocol::Json::object());
  }

  std::vector<protocol::Root> list_roots() const {
    if (native_transport_) {
      return roots_;
    }
    return client_.list_roots();
  }

  Peer& set_roots(std::vector<protocol::Root> roots) {
    roots_ = roots;
    client_.set_roots(std::move(roots));
    return *this;
  }

  Peer& set_capabilities(protocol::ClientCapabilities capabilities) {
    client_capabilities_ = capabilities;
    client_.set_capabilities(std::move(capabilities));
    return *this;
  }

  Peer& on_initialized(client::Client::InitializedHandler handler) {
    initialized_handler_ = handler;
    client_.on_initialized(std::move(handler));
    return *this;
  }

  Peer& on_cancelled(client::Client::CancelledHandler handler) {
    cancelled_handler_ = handler;
    client_.on_cancelled(std::move(handler));
    return *this;
  }

  Peer& on_logging_message(client::Client::LoggingMessageHandler handler) {
    logging_message_handler_ = handler;
    client_.on_logging_message(std::move(handler));
    return *this;
  }

  Peer& on_tool_list_changed(client::Client::ListChangedHandler handler) {
    tool_list_changed_handler_ = handler;
    client_.on_tool_list_changed(std::move(handler));
    return *this;
  }

  Peer& on_prompt_list_changed(client::Client::ListChangedHandler handler) {
    prompt_list_changed_handler_ = handler;
    client_.on_prompt_list_changed(std::move(handler));
    return *this;
  }

  Peer& on_resource_list_changed(client::Client::ListChangedHandler handler) {
    resource_list_changed_handler_ = handler;
    client_.on_resource_list_changed(std::move(handler));
    return *this;
  }

  Peer& on_resource_updated(client::Client::ResourceUpdatedHandler handler) {
    resource_updated_handler_ = handler;
    client_.on_resource_updated(std::move(handler));
    return *this;
  }

  Peer& on_progress(client::Client::ProgressHandler handler) {
    progress_handler_ = handler;
    client_.on_progress(std::move(handler));
    return *this;
  }

  Peer& on_elicitation_complete(
      client::Client::ElicitationCompleteHandler handler) {
    elicitation_complete_handler_ = handler;
    client_.on_elicitation_complete(std::move(handler));
    return *this;
  }

  Peer& on_task_status(client::Client::TaskStatusHandler handler) {
    task_status_handler_ = handler;
    client_.on_task_status(std::move(handler));
    return *this;
  }

  Peer& on_roots_list_changed(client::Client::ListChangedHandler handler) {
    roots_list_changed_handler_ = handler;
    client_.on_roots_list_changed(std::move(handler));
    return *this;
  }

  Peer& on_list_roots_request(client::Client::ListRootsRequestHandler handler) {
    roots_list_request_handler_ = handler;
    client_.on_list_roots_request(std::move(handler));
    return *this;
  }

  Peer& on_create_message_request(
      client::Client::CreateMessageRequestHandler handler) {
    sampling_request_handler_ = handler;
    client_.on_create_message_request(std::move(handler));
    return *this;
  }

  Peer& on_create_elicitation_request(
      client::Client::CreateElicitationRequestHandler handler) {
    elicitation_request_handler_ = handler;
    client_.on_create_elicitation_request(std::move(handler));
    return *this;
  }

  Peer& on_custom_request(client::Client::CustomRequestHandler handler) {
    custom_request_handler_ = handler;
    client_.on_custom_request(std::move(handler));
    return *this;
  }

  Peer& on_raw_notification(client::Client::RawNotificationHandler handler) {
    raw_notification_handler_ = handler;
    client_.on_raw_notification(std::move(handler));
    return *this;
  }

  Peer& set_handler(const client::ClientHandler& handler) {
    if (handler.on_initialized) {
      on_initialized(handler.on_initialized);
    }
    if (handler.on_cancelled) {
      on_cancelled(handler.on_cancelled);
    }
    if (handler.on_logging_message) {
      on_logging_message(handler.on_logging_message);
    }
    if (handler.on_tool_list_changed) {
      on_tool_list_changed(handler.on_tool_list_changed);
    }
    if (handler.on_prompt_list_changed) {
      on_prompt_list_changed(handler.on_prompt_list_changed);
    }
    if (handler.on_resource_list_changed) {
      on_resource_list_changed(handler.on_resource_list_changed);
    }
    if (handler.on_resource_updated) {
      on_resource_updated(handler.on_resource_updated);
    }
    if (handler.on_progress) {
      on_progress(handler.on_progress);
    }
    if (handler.on_elicitation_complete) {
      on_elicitation_complete(handler.on_elicitation_complete);
    }
    if (handler.on_task_status) {
      on_task_status(handler.on_task_status);
    }
    if (handler.on_roots_list_changed) {
      on_roots_list_changed(handler.on_roots_list_changed);
    }
    if (handler.on_list_roots_request) {
      on_list_roots_request(handler.on_list_roots_request);
    }
    if (handler.on_create_message_request) {
      on_create_message_request(handler.on_create_message_request);
    }
    if (handler.on_create_elicitation_request) {
      on_create_elicitation_request(handler.on_create_elicitation_request);
    }
    if (handler.on_custom_request) {
      on_custom_request(handler.on_custom_request);
    }
    if (handler.on_roots_list_request) {
      on_list_roots_request(handler.on_roots_list_request);
    }
    if (handler.on_sampling_request) {
      on_create_message_request(handler.on_sampling_request);
    }
    if (handler.on_elicitation_request) {
      on_create_elicitation_request(handler.on_elicitation_request);
    }
    if (handler.on_raw_notification) {
      on_raw_notification(handler.on_raw_notification);
    }
    if (handler.on_custom_notification) {
      on_raw_notification(handler.on_custom_notification);
    }
    return *this;
  }

  Peer& set_handler(const client::ClientHandlerInterface& handler) {
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
    on_resource_updated([&handler](const std::string& uri) {
      handler.on_resource_updated(uri);
    });
    on_progress([&handler](const protocol::ProgressNotificationParams& params) {
      handler.on_progress(params);
    });
    on_elicitation_complete([&handler](std::string_view elicitation_id) {
      handler.on_elicitation_complete(elicitation_id);
    });
    on_task_status([&handler](const protocol::Task& task) {
      handler.on_task_status(task);
    });
    on_roots_list_changed([&handler]() { handler.on_roots_list_changed(); });
    on_list_roots_request(
        [&handler]() -> core::Result<protocol::RootsListResult> {
          const auto response = handler.on_list_roots_request();
          if (response.has_value()) {
            return std::move(*response);
          }
          return std::unexpected(client::handler_method_not_found(
              "client handler does not handle list_roots"));
        });
    on_create_message_request(
        [&handler](const protocol::CreateMessageParams& params)
            -> core::Result<protocol::CreateMessageResult> {
          const auto response = handler.on_create_message_request(params);
          if (response.has_value()) {
            return std::move(*response);
          }
          return std::unexpected(client::handler_method_not_found(
              "client handler does not handle create_message"));
        });
    on_create_elicitation_request(
        [&handler](const protocol::CreateElicitationRequestParam& params)
            -> core::Result<protocol::CreateElicitationResult> {
          const auto response = handler.on_create_elicitation_request(params);
          if (response.has_value()) {
            return std::move(*response);
          }
          return std::unexpected(client::handler_method_not_found(
              "client handler does not handle elicitation"));
        });
    on_custom_request([&handler](const protocol::JsonRpcRequest& request)
                          -> core::Result<protocol::Json> {
      const auto response = handler.on_custom_request(request);
      if (response.has_value()) {
        return std::move(*response);
      }
      return std::unexpected(client::handler_method_not_found(
          "client handler does not handle custom request"));
    });
    on_raw_notification(
        [&handler](const protocol::JsonRpcNotification& notification) {
          handler.on_raw_notification(notification);
        });
    return *this;
  }

  core::Result<std::vector<protocol::ToolDefinition>> list_tools() {
    auto payload = request_json(std::string(protocol::ToolsListMethod),
                                protocol::Json::object());
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto result = protocol::tools_list_result_from_json(*payload);
    if (!result) {
      return std::unexpected(result.error());
    }
    return result->tools;
  }

  core::Result<std::vector<protocol::ToolDefinition>> list_all_tools() {
    return list_all_pages<protocol::ToolDefinition>(
        std::string(protocol::ToolsListMethod),
        [](const protocol::Json& payload) {
          return protocol::tools_list_result_from_json(payload);
        },
        [](const protocol::ToolsListResult& page,
           std::vector<protocol::ToolDefinition>& all) {
          all.insert(all.end(), page.tools.begin(), page.tools.end());
        });
  }

  core::Result<protocol::ToolResult> call_tool(const protocol::ToolCall& call) {
    auto payload = request_json(std::string(protocol::ToolsCallMethod),
                                protocol::tool_call_to_json(call));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return protocol::tool_result_from_json(*payload);
  }

  core::Result<protocol::CreateTaskResult> call_tool_task(
      const protocol::ToolCall& call) {
    if (!call.task.has_value()) {
      return std::unexpected(errors::make(
          protocol::ErrorCode::InvalidRequest,
          "task-aware tool call requires task parameters", {}, "protocol"));
    }
    auto payload = request_json(std::string(protocol::ToolsCallMethod),
                                protocol::tool_call_to_json(call));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return protocol::create_task_result_from_json(*payload);
  }

  core::Result<protocol::ToolResult> call_tool(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object()) {
    protocol::ToolCall call;
    call.name = std::string(name);
    call.arguments = arguments;
    return call_tool(call);
  }

  core::Result<std::vector<protocol::Prompt>> list_prompts() {
    auto payload = request_json(std::string(protocol::PromptsListMethod),
                                protocol::Json::object());
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto result = protocol::prompts_list_result_from_json(*payload);
    if (!result) {
      return std::unexpected(result.error());
    }
    return result->prompts;
  }

  core::Result<std::vector<protocol::Prompt>> list_all_prompts() {
    return list_all_pages<protocol::Prompt>(
        std::string(protocol::PromptsListMethod),
        [](const protocol::Json& payload) {
          return protocol::prompts_list_result_from_json(payload);
        },
        [](const protocol::PromptsListResult& page,
           std::vector<protocol::Prompt>& all) {
          all.insert(all.end(), page.prompts.begin(), page.prompts.end());
        });
  }

  core::Result<protocol::PromptsGetResult> get_prompt(
      const protocol::PromptsGetParams& params) {
    auto payload = request_json(std::string(protocol::PromptsGetMethod),
                                protocol::prompts_get_params_to_json(params));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return protocol::prompts_get_result_from_json(*payload);
  }

  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object()) {
    protocol::PromptsGetParams params;
    params.name = std::string(name);
    params.arguments = arguments;
    return get_prompt(params);
  }

  core::Result<std::vector<protocol::Resource>> list_resources() {
    auto payload = request_json(std::string(protocol::ResourcesListMethod),
                                protocol::Json::object());
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto result = protocol::resources_list_result_from_json(*payload);
    if (!result) {
      return std::unexpected(result.error());
    }
    return result->resources;
  }

  core::Result<std::vector<protocol::Resource>> list_all_resources() {
    return list_all_pages<protocol::Resource>(
        std::string(protocol::ResourcesListMethod),
        [](const protocol::Json& payload) {
          return protocol::resources_list_result_from_json(payload);
        },
        [](const protocol::ResourcesListResult& page,
           std::vector<protocol::Resource>& all) {
          all.insert(all.end(), page.resources.begin(), page.resources.end());
        });
  }

  core::Result<std::vector<protocol::ResourceTemplate>>
  list_resource_templates() {
    auto payload =
        request_json(std::string(protocol::ResourcesTemplatesListMethod),
                     protocol::Json::object());
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto result =
        protocol::resource_templates_list_result_from_json(*payload);
    if (!result) {
      return std::unexpected(result.error());
    }
    return result->resource_templates;
  }

  core::Result<std::vector<protocol::ResourceTemplate>>
  list_all_resource_templates() {
    return list_all_pages<protocol::ResourceTemplate>(
        std::string(protocol::ResourcesTemplatesListMethod),
        [](const protocol::Json& payload) {
          return protocol::resource_templates_list_result_from_json(payload);
        },
        [](const protocol::ResourceTemplatesListResult& page,
           std::vector<protocol::ResourceTemplate>& all) {
          all.insert(all.end(), page.resource_templates.begin(),
                     page.resource_templates.end());
        });
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      const protocol::ResourcesReadParams& params) {
    auto payload =
        request_json(std::string(protocol::ResourcesReadMethod),
                     protocol::resources_read_params_to_json(params));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return protocol::resources_read_result_from_json(*payload);
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri) {
    return read_resource(protocol::ResourcesReadParams{std::string(uri)});
  }

  core::Result<protocol::CompleteResult> complete(
      const protocol::CompleteParams& request) {
    auto payload = request_json(std::string(protocol::CompletionCompleteMethod),
                                protocol::complete_params_to_json(request));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return protocol::complete_result_from_json(*payload);
  }

  core::Result<protocol::Json> complete(const protocol::Json& request) {
    return request_json(std::string(protocol::CompletionCompleteMethod),
                        request);
  }

  core::Result<protocol::CompletionResult> complete_prompt_argument(
      std::string_view prompt_name, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    protocol::CompletionArgument argument;
    argument.name = std::string(argument_name);
    argument.value = std::move(current_value);
    protocol::CompleteParams params;
    params.ref =
        protocol::prompt_completion_reference(std::string(prompt_name));
    params.argument = std::move(argument);
    params.context = std::move(context);
    const auto result = complete(params);
    if (!result) {
      return std::unexpected(result.error());
    }
    return result->completion;
  }

  core::Result<protocol::CompletionResult> complete_resource_argument(
      std::string_view uri_template, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
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

  core::Result<std::vector<std::string>> complete_prompt_simple(
      std::string_view prompt_name, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    const auto completion =
        complete_prompt_argument(prompt_name, argument_name,
                                 std::move(current_value), std::move(context));
    if (!completion) {
      return std::unexpected(completion.error());
    }
    return completion->values;
  }

  core::Result<std::vector<std::string>> complete_resource_simple(
      std::string_view uri_template, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    const auto completion = complete_resource_argument(
        uri_template, argument_name, std::move(current_value),
        std::move(context));
    if (!completion) {
      return std::unexpected(completion.error());
    }
    return completion->values;
  }

  core::Result<protocol::CreateMessageResult> create_message(
      const protocol::CreateMessageParams& request) {
    auto payload =
        request_json(std::string(protocol::SamplingCreateMessageMethod),
                     protocol::create_message_params_to_json(request));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return protocol::create_message_result_from_json(*payload);
  }

  core::Result<protocol::Json> create_message(const protocol::Json& request) {
    return request_json(std::string(protocol::SamplingCreateMessageMethod),
                        request);
  }

  core::Result<protocol::CreateElicitationResult> create_elicitation(
      const protocol::CreateElicitationRequestParam& request) {
    auto payload = request_json(
        std::string(protocol::ElicitationCreateMethod),
        protocol::create_elicitation_request_param_to_json(request));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return protocol::create_elicitation_result_from_json(*payload);
  }

  core::Result<protocol::Json> create_elicitation(
      const protocol::Json& request) {
    return request_json(std::string(protocol::ElicitationCreateMethod),
                        request);
  }

  core::Result<std::vector<protocol::Task>> list_tasks() {
    auto payload = request_json(std::string(protocol::TasksListMethod),
                                protocol::Json::object());
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto result = protocol::task_list_result_from_json(*payload);
    if (!result) {
      return std::unexpected(result.error());
    }
    return result->tasks;
  }

  core::Result<std::vector<protocol::Task>> list_all_tasks() {
    return list_all_pages<protocol::Task>(
        std::string(protocol::TasksListMethod),
        [](const protocol::Json& payload) {
          return protocol::task_list_result_from_json(payload);
        },
        [](const protocol::TaskListResult& page,
           std::vector<protocol::Task>& all) {
          all.insert(all.end(), page.tasks.begin(), page.tasks.end());
        });
  }

  core::Result<protocol::Task> get_task(std::string_view task_id) {
    protocol::TaskGetParams params;
    params.task_id = std::string(task_id);
    auto payload = request_json(std::string(protocol::TasksGetMethod),
                                protocol::task_get_params_to_json(params));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return protocol::task_from_json(*payload);
  }

  core::Result<protocol::Task> cancel_task(std::string_view task_id) {
    protocol::TaskCancelParams params;
    params.task_id = std::string(task_id);
    auto payload = request_json(std::string(protocol::TasksCancelMethod),
                                protocol::task_cancel_params_to_json(params));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return protocol::task_from_json(*payload);
  }

  core::Result<protocol::Json> task_result(std::string_view task_id) {
    protocol::TaskResultParams params;
    params.task_id = std::string(task_id);
    return request_json(std::string(protocol::TasksResultMethod),
                        protocol::task_result_params_to_json(params));
  }

  core::Result<core::Unit> set_level(std::string_view level) {
    const auto parsed = protocol::logging_level_from_string(std::string(level));
    if (!parsed.has_value()) {
      return std::unexpected(errors::make(protocol::ErrorCode::InvalidRequest,
                                          "logging/setLevel level is invalid",
                                          {}, "protocol"));
    }
    protocol::LoggingSetLevelParams params;
    params.level = *parsed;
    return request_unit(std::string(protocol::LoggingSetLevelMethod),
                        protocol::logging_set_level_params_to_json(params));
  }

  core::Result<core::Unit> subscribe(std::string_view uri) {
    protocol::ResourcesSubscribeParams params;
    params.uri = std::string(uri);
    return request_unit(std::string(protocol::ResourcesSubscribeMethod),
                        protocol::resources_subscribe_params_to_json(params));
  }

  core::Result<core::Unit> unsubscribe(std::string_view uri) {
    protocol::ResourcesUnsubscribeParams params;
    params.uri = std::string(uri);
    return request_unit(std::string(protocol::ResourcesUnsubscribeMethod),
                        protocol::resources_unsubscribe_params_to_json(params));
  }

  core::Result<protocol::Json> raw_request(
      const protocol::JsonRpcRequest& request) {
    if (native_transport_) {
      const auto response = send_native_request(request);
      if (!response) {
        return std::unexpected(response.error());
      }
      return detail::peer_require_result_payload(*response);
    }
    return client_.raw_request(request);
  }

  RequestHandle<protocol::Json> request_async(
      std::string method, protocol::Json params = protocol::Json::object(),
      RequestOptions options = {}) {
    if (native_transport_) {
      protocol::JsonRpcRequest request = protocol::make_request(
          std::move(method), next_peer_request_id(), std::move(params));
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
    return client_.request_async(std::move(method), std::move(params),
                                 std::move(options));
  }

  template <class T, class Parser>
  RequestHandle<T> request_async(std::string method, protocol::Json params,
                                 Parser parser, RequestOptions options = {}) {
    if (native_transport_) {
      protocol::JsonRpcRequest request = protocol::make_request(
          std::move(method), next_peer_request_id(), std::move(params));
      if (options.meta.has_value()) {
        request.meta = std::move(options.meta);
      }

      const auto request_id = request.id;
      return RequestHandle<T>::spawn(
          request_id, options.timeout, options.cancellation_token,
          [this, request_id](std::string reason) mutable {
            return notify_cancelled(std::move(request_id), std::move(reason));
          },
          [this, request = std::move(request),
           parser = std::move(parser)]() mutable -> core::Result<T> {
            auto payload = raw_request(request);
            if (!payload) {
              return std::unexpected(payload.error());
            }
            return parser(*payload);
          });
    }
    return client_.request_async<T>(std::move(method), std::move(params),
                                    std::move(parser), std::move(options));
  }

  RequestHandle<std::vector<protocol::ToolDefinition>> list_tools_async(
      RequestOptions options = {}) {
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

  RequestHandle<std::vector<protocol::Prompt>> list_prompts_async(
      RequestOptions options = {}) {
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

  RequestHandle<std::vector<protocol::Resource>> list_resources_async(
      RequestOptions options = {}) {
    return request_async<std::vector<protocol::Resource>>(
        std::string(protocol::ResourcesListMethod), protocol::Json::object(),
        [](const protocol::Json& payload)
            -> core::Result<std::vector<protocol::Resource>> {
          const auto result =
              protocol::resources_list_result_from_json(payload);
          if (!result) {
            return std::unexpected(result.error());
          }
          return result->resources;
        },
        std::move(options));
  }

  RequestHandle<std::vector<protocol::ResourceTemplate>>
  list_resource_templates_async(RequestOptions options = {}) {
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

  RequestHandle<protocol::ToolResult> call_tool_async(
      const protocol::ToolCall& call, RequestOptions options = {}) {
    return request_async<protocol::ToolResult>(
        std::string(protocol::ToolsCallMethod),
        protocol::tool_call_to_json(call),
        [](const protocol::Json& payload) {
          return protocol::tool_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::ToolResult> call_tool_async(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object(),
      RequestOptions options = {}) {
    protocol::ToolCall call;
    call.name = std::string(name);
    call.arguments = arguments;
    return call_tool_async(call, std::move(options));
  }

  RequestHandle<protocol::CreateTaskResult> call_tool_task_async(
      const protocol::ToolCall& call, RequestOptions options = {}) {
    if (!call.task.has_value()) {
      return RequestHandle<protocol::CreateTaskResult>::ready(
          next_peer_request_id(),
          std::unexpected(
              errors::make(protocol::ErrorCode::InvalidRequest,
                           "task-aware tool call requires task parameters", {},
                           "protocol")));
    }

    return request_async<protocol::CreateTaskResult>(
        std::string(protocol::ToolsCallMethod),
        protocol::tool_call_to_json(call),
        [](const protocol::Json& payload) {
          return protocol::create_task_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::PromptsGetResult> get_prompt_async(
      const protocol::PromptsGetParams& params, RequestOptions options = {}) {
    return request_async<protocol::PromptsGetResult>(
        std::string(protocol::PromptsGetMethod),
        protocol::prompts_get_params_to_json(params),
        [](const protocol::Json& payload) {
          return protocol::prompts_get_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::PromptsGetResult> get_prompt_async(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object(),
      RequestOptions options = {}) {
    protocol::PromptsGetParams params;
    params.name = std::string(name);
    params.arguments = arguments;
    return get_prompt_async(params, std::move(options));
  }

  RequestHandle<protocol::ResourcesReadResult> read_resource_async(
      const protocol::ResourcesReadParams& params,
      RequestOptions options = {}) {
    return request_async<protocol::ResourcesReadResult>(
        std::string(protocol::ResourcesReadMethod),
        protocol::resources_read_params_to_json(params),
        [](const protocol::Json& payload) {
          return protocol::resources_read_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::ResourcesReadResult> read_resource_async(
      std::string_view uri, RequestOptions options = {}) {
    return read_resource_async(protocol::ResourcesReadParams{std::string(uri)},
                               std::move(options));
  }

  RequestHandle<protocol::CompleteResult> complete_async(
      const protocol::CompleteParams& request, RequestOptions options = {}) {
    return request_async<protocol::CompleteResult>(
        std::string(protocol::CompletionCompleteMethod),
        protocol::complete_params_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::complete_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Json> complete_async(const protocol::Json& request,
                                               RequestOptions options = {}) {
    return request_async(std::string(protocol::CompletionCompleteMethod),
                         request, std::move(options));
  }

  RequestHandle<protocol::CreateMessageResult> create_message_async(
      const protocol::CreateMessageParams& request,
      RequestOptions options = {}) {
    return request_async<protocol::CreateMessageResult>(
        std::string(protocol::SamplingCreateMessageMethod),
        protocol::create_message_params_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::create_message_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Json> create_message_async(
      const protocol::Json& request, RequestOptions options = {}) {
    return request_async(std::string(protocol::SamplingCreateMessageMethod),
                         request, std::move(options));
  }

  RequestHandle<protocol::CreateElicitationResult> create_elicitation_async(
      const protocol::CreateElicitationRequestParam& request,
      RequestOptions options = {}) {
    return request_async<protocol::CreateElicitationResult>(
        std::string(protocol::ElicitationCreateMethod),
        protocol::create_elicitation_request_param_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::create_elicitation_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Json> create_elicitation_async(
      const protocol::Json& request, RequestOptions options = {}) {
    return request_async(std::string(protocol::ElicitationCreateMethod),
                         request, std::move(options));
  }

  RequestHandle<std::vector<protocol::Task>> list_tasks_async(
      RequestOptions options = {}) {
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

  RequestHandle<protocol::Task> get_task_async(
      const protocol::TaskGetParams& request, RequestOptions options = {}) {
    return request_async<protocol::Task>(
        std::string(protocol::TasksGetMethod),
        protocol::task_get_params_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::task_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Task> get_task_async(std::string_view task_id,
                                               RequestOptions options = {}) {
    protocol::TaskGetParams params;
    params.task_id = std::string(task_id);
    return get_task_async(params, std::move(options));
  }

  RequestHandle<protocol::Task> cancel_task_async(
      const protocol::TaskCancelParams& request, RequestOptions options = {}) {
    return request_async<protocol::Task>(
        std::string(protocol::TasksCancelMethod),
        protocol::task_cancel_params_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::task_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Task> cancel_task_async(std::string_view task_id,
                                                  RequestOptions options = {}) {
    protocol::TaskCancelParams params;
    params.task_id = std::string(task_id);
    return cancel_task_async(params, std::move(options));
  }

  RequestHandle<protocol::Json> task_result_async(
      const protocol::TaskResultParams& request, RequestOptions options = {}) {
    return request_async(std::string(protocol::TasksResultMethod),
                         protocol::task_result_params_to_json(request),
                         std::move(options));
  }

  RequestHandle<protocol::Json> task_result_async(std::string_view task_id,
                                                  RequestOptions options = {}) {
    protocol::TaskResultParams params;
    params.task_id = std::string(task_id);
    return task_result_async(params, std::move(options));
  }

  core::Result<core::Unit> raw_notification(
      const protocol::JsonRpcNotification& notification) {
    if (native_transport_) {
      return native_transport_->send(protocol::JsonRpcMessage{notification});
    }
    return client_.raw_notification(notification);
  }

  /// @brief Dispatches one inbound role-generic transport message.
  ///
  /// Requests produce a JSON-RPC response message, notifications produce no
  /// outbound message, and standalone responses remain the responsibility of
  /// request-handle correlation paths.
  core::Result<std::optional<protocol::JsonRpcMessage>> dispatch_message(
      const protocol::JsonRpcMessage& message) {
    if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      auto handled = native_transport_ ? handle_native_request(*request)
                                       : client_.handle_request(*request);
      if (!handled) {
        return protocol::JsonRpcMessage{protocol::make_error_response(
            request->id,
            detail::peer_error_object_from_core_error(handled.error()))};
      }
      return protocol::JsonRpcMessage{std::move(*handled)};
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      const auto handled = native_transport_
                               ? handle_native_notification(*notification)
                               : client_.handle_notification(*notification);
      if (!handled) {
        return std::unexpected(handled.error());
      }
      return std::nullopt;
    }

    return std::unexpected(detail::peer_dispatch_error(
        "client peer cannot dispatch an uncorrelated response"));
  }

  /// @brief Runs a sequential receive loop over a role-generic client
  /// transport.
  core::Result<core::Unit> serve_transport(
      transport::ClientTransport& transport,
      CancellationToken cancellation = {}) {
    return detail::serve_transport_loop(
        transport, cancellation,
        [this](const protocol::JsonRpcMessage& message) {
          return dispatch_message(message);
        });
  }

 private:
  std::int64_t next_peer_request_id() noexcept {
    return next_request_id_->fetch_add(1, std::memory_order_relaxed);
  }

  static protocol::JsonRpcResponse native_error_response(
      const protocol::JsonRpcRequest& request, const core::Error& error) {
    return protocol::make_error_response(
        std::optional<protocol::RequestId>{request.id},
        protocol::make_error(
            error.code, error.message,
            error.detail.empty()
                ? std::nullopt
                : std::optional<protocol::Json>{error.detail}));
  }

  static protocol::JsonRpcResponse native_error_response(
      const protocol::JsonRpcRequest& request, protocol::ErrorCode code,
      std::string message, std::string detail = {}) {
    return protocol::make_error_response(
        std::optional<protocol::RequestId>{request.id},
        protocol::make_error(
            code, std::move(message),
            detail.empty() ? std::nullopt
                           : std::optional<protocol::Json>{std::move(detail)}));
  }

  core::Result<core::Unit> handle_native_notification(
      const protocol::JsonRpcNotification& notification) try {
    if (notification.method == std::string(protocol::InitializedMethod) &&
        initialized_handler_) {
      initialized_handler_();
    } else if (notification.method ==
                   std::string(protocol::CancelledNotificationMethod) &&
               cancelled_handler_) {
      const auto params = protocol::cancelled_notification_params_from_json(
          notification.params);
      if (!params) {
        return std::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "cancelled notification requires a requestId"));
      }
      cancelled_handler_(params->request_id, params->reason);
    } else if (notification.method ==
                   std::string(protocol::LoggingMessageNotificationMethod) &&
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
    } else if (notification.method ==
                   std::string(protocol::ToolsListChangedNotificationMethod) &&
               tool_list_changed_handler_) {
      tool_list_changed_handler_();
    } else if (notification.method ==
                   std::string(
                       protocol::PromptsListChangedNotificationMethod) &&
               prompt_list_changed_handler_) {
      prompt_list_changed_handler_();
    } else if (notification.method ==
                   std::string(
                       protocol::ResourcesListChangedNotificationMethod) &&
               resource_list_changed_handler_) {
      resource_list_changed_handler_();
    } else if (notification.method ==
                   std::string(protocol::ResourcesUpdatedNotificationMethod) &&
               resource_updated_handler_) {
      if (!notification.params.is_object() ||
          !notification.params.contains("uri") ||
          !notification.params.at("uri").is_string()) {
        return std::unexpected(errors::make(
            protocol::ErrorCode::InvalidParams,
            "resource updated notification requires a string uri"));
      }
      resource_updated_handler_(
          notification.params.at("uri").get<std::string>());
    } else if (notification.method ==
                   std::string(protocol::ProgressNotificationMethod) &&
               progress_handler_) {
      const auto params =
          protocol::progress_notification_params_from_json(notification.params);
      if (!params) {
        return std::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "progress notification requires valid params"));
      }
      progress_handler_(*params);
    } else if (notification.method ==
                   std::string(
                       protocol::ElicitationCompleteNotificationMethod) &&
               elicitation_complete_handler_) {
      const auto params =
          protocol::elicitation_complete_notification_params_from_json(
              notification.params);
      if (!params) {
        return std::unexpected(errors::make(
            protocol::ErrorCode::InvalidParams,
            "elicitation completion notification requires valid params"));
      }
      elicitation_complete_handler_(params->elicitation_id);
    } else if (notification.method ==
                   std::string(protocol::TasksStatusNotificationMethod) &&
               task_status_handler_) {
      const auto task = protocol::task_from_json(notification.params);
      if (!task) {
        return std::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "task status notification requires valid task data"));
      }
      task_status_handler_(*task);
    } else if (notification.method ==
                   std::string(protocol::RootsListChangedNotificationMethod) &&
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

  core::Result<protocol::JsonRpcResponse> handle_native_request(
      const protocol::JsonRpcRequest& request) try {
    if (request.method == std::string(protocol::PingMethod)) {
      return protocol::make_response(request.id, protocol::Json::object());
    }

    if (request.method == std::string(protocol::RootsListMethod)) {
      if (roots_list_request_handler_) {
        const auto roots = roots_list_request_handler_();
        if (!roots) {
          return native_error_response(request, roots.error());
        }
        return protocol::make_response(
            request.id, protocol::roots_list_result_to_json(*roots));
      }

      protocol::RootsListResult result;
      result.roots = roots_;
      return protocol::make_response(
          request.id, protocol::roots_list_result_to_json(result));
    }

    if (request.method == std::string(protocol::SamplingCreateMessageMethod)) {
      if (!sampling_request_handler_) {
        return native_error_response(
            request, protocol::ErrorCode::MethodNotFound,
            "sampling request handler is not configured");
      }
      const auto params =
          protocol::create_message_params_from_json(request.params);
      if (!params) {
        return native_error_response(request, params.error());
      }
      const auto result = sampling_request_handler_(*params);
      if (!result) {
        return native_error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::create_message_result_to_json(*result));
    }

    if (request.method == std::string(protocol::ElicitationCreateMethod)) {
      if (!elicitation_request_handler_) {
        return native_error_response(
            request, protocol::ErrorCode::MethodNotFound,
            "elicitation request handler is not configured");
      }
      const auto params =
          protocol::create_elicitation_request_param_from_json(request.params);
      if (!params) {
        return native_error_response(request, params.error());
      }
      const auto result = elicitation_request_handler_(*params);
      if (!result) {
        return native_error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::create_elicitation_result_to_json(*result));
    }

    if (custom_request_handler_) {
      const auto result = custom_request_handler_(request);
      if (!result) {
        return native_error_response(request, result.error());
      }
      return protocol::make_response(request.id, std::move(*result));
    }

    return native_error_response(request, protocol::ErrorCode::MethodNotFound,
                                 "method not found", request.method);
  } catch (const std::exception& ex) {
    return native_error_response(request, errors::handler_failed(ex.what()));
  } catch (...) {
    return native_error_response(request, errors::handler_unknown_exception());
  }

  core::Result<protocol::Json> request_json(std::string method,
                                            protocol::Json params) {
    return raw_request(protocol::make_request(
        std::move(method), next_peer_request_id(), std::move(params)));
  }

  core::Result<core::Unit> request_unit(std::string method,
                                        protocol::Json params) {
    auto payload = request_json(std::move(method), std::move(params));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    return core::Unit{};
  }

  static protocol::Json cursor_params(
      const std::optional<std::string>& cursor) {
    protocol::Json params = protocol::Json::object();
    if (cursor.has_value()) {
      params["cursor"] = *cursor;
    }
    return params;
  }

  template <class Item, class Parser, class Append>
  core::Result<std::vector<Item>> list_all_pages(std::string method,
                                                 Parser parser, Append append) {
    std::vector<Item> all;
    std::optional<std::string> cursor;
    do {
      auto payload = request_json(method, cursor_params(cursor));
      if (!payload) {
        return std::unexpected(payload.error());
      }
      auto page = parser(*payload);
      if (!page) {
        return std::unexpected(page.error());
      }
      append(*page, all);
      cursor = page->next_cursor;
    } while (cursor.has_value() && !cursor->empty());
    return all;
  }

  core::Result<protocol::JsonRpcResponse> send_native_request(
      const protocol::JsonRpcRequest& request) {
    auto sent = native_transport_->send(protocol::JsonRpcMessage{request});
    if (!sent) {
      return std::unexpected(sent.error());
    }

    while (true) {
      auto received = native_transport_->receive();
      if (!received) {
        return std::unexpected(received.error());
      }
      if (!received->has_value()) {
        return std::unexpected(detail::peer_transport_error(
            "client peer transport closed before response"));
      }

      if (auto* response =
              std::get_if<protocol::JsonRpcResponse>(&received->value())) {
        if (response->id.has_value() && *response->id == request.id) {
          return *response;
        }
        return std::unexpected(detail::peer_transport_error(
            "client peer transport received unexpected response id"));
      }

      auto dispatched = dispatch_message(received->value());
      if (!dispatched) {
        return std::unexpected(dispatched.error());
      }
      if (dispatched->has_value()) {
        sent = native_transport_->send(std::move(dispatched->value()));
        if (!sent) {
          return std::unexpected(sent.error());
        }
      }
    }
  }

  std::unique_ptr<transport::ClientTransport> native_transport_;
  std::shared_ptr<std::atomic<std::int64_t>> next_request_id_ =
      std::make_shared<std::atomic<std::int64_t>>(1);
  std::optional<protocol::ClientCapabilities> client_capabilities_;
  std::vector<protocol::Root> roots_;
  client::Client::InitializedHandler initialized_handler_;
  client::Client::CancelledHandler cancelled_handler_;
  client::Client::LoggingMessageHandler logging_message_handler_;
  client::Client::ListChangedHandler tool_list_changed_handler_;
  client::Client::ListChangedHandler prompt_list_changed_handler_;
  client::Client::ListChangedHandler resource_list_changed_handler_;
  client::Client::ResourceUpdatedHandler resource_updated_handler_;
  client::Client::ProgressHandler progress_handler_;
  client::Client::ElicitationCompleteHandler elicitation_complete_handler_;
  client::Client::TaskStatusHandler task_status_handler_;
  client::Client::ListChangedHandler roots_list_changed_handler_;
  client::Client::RootsListRequestHandler roots_list_request_handler_;
  client::Client::SamplingRequestHandler sampling_request_handler_;
  client::Client::ElicitationRequestHandler elicitation_request_handler_;
  client::Client::CustomRequestHandler custom_request_handler_;
  client::Client::RawNotificationHandler raw_notification_handler_;
  client::Client client_;
};

/// @brief Server-side peer boundary for exposing MCP capabilities.
template <>
class Peer<RoleServer> {
 public:
  /// @brief Creates a server peer from options.
  explicit Peer(server::ServerOptions options = {})
      : server_(std::make_unique<server::Server>(std::move(options))) {}

  /// @brief Creates a server peer from an owned server implementation.
  explicit Peer(std::unique_ptr<server::Server> server)
      : server_(std::move(server)) {}

  static core::Result<Peer> build(server::ServerBuilder builder) {
    auto built = builder.build();
    if (!built) {
      return std::unexpected(built.error());
    }
    return Peer(std::move(*built));
  }

  CXXMCP_DEPRECATED(
      "server() is a compatibility escape hatch; prefer ServerPeer methods")
  server::Server& server() noexcept { return *server_; }

  CXXMCP_DEPRECATED(
      "server() is a compatibility escape hatch; prefer ServerPeer methods")
  const server::Server& server() const noexcept { return *server_; }

  server::ServerInfo get_info() const { return server_->get_info(); }

  const protocol::ServerCapabilities& capabilities() const noexcept {
    return server_->capabilities();
  }

  Peer& set_completion_handler(server::Server::JsonHandler handler) {
    completion_handler_ = handler;
    server_->set_completion_handler(std::move(handler));
    return *this;
  }

  Peer& set_sampling_handler(server::Server::JsonHandler handler) {
    sampling_handler_ = handler;
    server_->set_sampling_handler(std::move(handler));
    return *this;
  }

  Peer& set_logging_handler(server::Server::LoggingHandler handler) {
    logging_handler_ = handler;
    server_->set_logging_handler(std::move(handler));
    return *this;
  }

  Peer& set_raw_request_handler(server::Server::RawRequestHandler handler) {
    raw_request_handler_ = handler;
    server_->set_raw_request_handler(std::move(handler));
    return *this;
  }

  Peer& set_raw_notification_handler(
      server::Server::RawNotificationHandler handler) {
    native_notification_state_ = true;
    raw_notification_handler_ = handler;
    server_->set_raw_notification_handler(std::move(handler));
    return *this;
  }

  Peer& set_custom_request_handler(server::Server::RawRequestHandler handler) {
    raw_request_handler_ = handler;
    server_->set_custom_request_handler(std::move(handler));
    return *this;
  }

  Peer& set_custom_notification_handler(
      server::Server::RawNotificationHandler handler) {
    native_notification_state_ = true;
    raw_notification_handler_ = handler;
    server_->set_custom_notification_handler(std::move(handler));
    return *this;
  }

  Peer& set_task_list_handler(server::Server::TaskListHandler handler) {
    task_list_handler_ = handler;
    server_->set_task_list_handler(std::move(handler));
    return *this;
  }

  Peer& set_task_get_handler(server::Server::TaskGetHandler handler) {
    task_get_handler_ = handler;
    server_->set_task_get_handler(std::move(handler));
    return *this;
  }

  Peer& set_task_cancel_handler(server::Server::TaskCancelHandler handler) {
    task_cancel_handler_ = handler;
    server_->set_task_cancel_handler(std::move(handler));
    return *this;
  }

  Peer& set_task_result_handler(server::Server::TaskResultHandler handler) {
    task_result_handler_ = handler;
    server_->set_task_result_handler(std::move(handler));
    return *this;
  }

  Peer& set_progress_handler(server::Server::ProgressHandler handler) {
    native_notification_state_ = true;
    progress_handler_ = handler;
    server_->set_progress_handler(std::move(handler));
    return *this;
  }

  Peer& set_roots_list_changed_handler(
      server::Server::RootsListChangedHandler handler) {
    native_notification_state_ = true;
    roots_list_changed_handler_ = handler;
    server_->set_roots_list_changed_handler(std::move(handler));
    return *this;
  }

  Peer& set_tool_list_changed_handler(
      server::Server::ListChangedHandler handler) {
    native_notification_state_ = true;
    tool_list_changed_handler_ = handler;
    server_->set_tool_list_changed_handler(std::move(handler));
    return *this;
  }

  Peer& set_prompt_list_changed_handler(
      server::Server::ListChangedHandler handler) {
    native_notification_state_ = true;
    prompt_list_changed_handler_ = handler;
    server_->set_prompt_list_changed_handler(std::move(handler));
    return *this;
  }

  Peer& set_resource_list_changed_handler(
      server::Server::ListChangedHandler handler) {
    native_notification_state_ = true;
    resource_list_changed_handler_ = handler;
    server_->set_resource_list_changed_handler(std::move(handler));
    return *this;
  }

  Peer& set_resource_updated_handler(
      server::Server::ResourceUpdatedHandler handler) {
    native_notification_state_ = true;
    resource_updated_handler_ = handler;
    server_->set_resource_updated_handler(std::move(handler));
    return *this;
  }

  Peer& set_handler(const server::ServerHandler& handler) {
    if (handler.on_completion) {
      set_completion_handler(handler.on_completion);
    }
    if (handler.on_sampling) {
      set_sampling_handler(handler.on_sampling);
    }
    if (handler.on_logging) {
      set_logging_handler(handler.on_logging);
    }
    if (handler.on_raw_request) {
      set_raw_request_handler(handler.on_raw_request);
    }
    if (handler.on_raw_notification) {
      set_raw_notification_handler(handler.on_raw_notification);
    }
    if (handler.on_custom_request) {
      set_custom_request_handler(handler.on_custom_request);
    }
    if (handler.on_custom_notification) {
      set_custom_notification_handler(handler.on_custom_notification);
    }
    if (handler.on_task_list) {
      set_task_list_handler(handler.on_task_list);
    }
    if (handler.on_task_get) {
      set_task_get_handler(handler.on_task_get);
    }
    if (handler.on_task_cancel) {
      set_task_cancel_handler(handler.on_task_cancel);
    }
    if (handler.on_task_result) {
      set_task_result_handler(handler.on_task_result);
    }
    if (handler.on_progress) {
      set_progress_handler(handler.on_progress);
    }
    if (handler.on_roots_list_changed) {
      set_roots_list_changed_handler(handler.on_roots_list_changed);
    }
    if (handler.on_tool_list_changed) {
      set_tool_list_changed_handler(handler.on_tool_list_changed);
    }
    if (handler.on_prompt_list_changed) {
      set_prompt_list_changed_handler(handler.on_prompt_list_changed);
    }
    if (handler.on_resource_list_changed) {
      set_resource_list_changed_handler(handler.on_resource_list_changed);
    }
    if (handler.on_resource_updated) {
      set_resource_updated_handler(handler.on_resource_updated);
    }
    return *this;
  }

  Peer& set_handler(const server::ServerHandlerInterface& handler) {
    server_->set_handler(handler);
    return *this;
  }

  std::vector<protocol::ToolDefinition> list_tools() const {
    return server_->list_tools();
  }

  core::Result<protocol::ToolDefinition> get_tool(std::string_view name) const {
    return server_->get_tool(name);
  }

  core::Result<protocol::ToolResult> call_tool(
      std::string_view name,
      protocol::Json arguments = protocol::Json::object(),
      const std::string& session_id = {}) const {
    return server_->call_tool(name, std::move(arguments), session_id);
  }

  std::vector<protocol::Prompt> list_prompts() const {
    return server_->list_prompts();
  }

  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view name,
      protocol::Json arguments = protocol::Json::object(),
      const std::string& session_id = {}) const {
    return server_->get_prompt(name, std::move(arguments), session_id);
  }

  std::vector<protocol::Resource> list_resources() const {
    return server_->list_resources();
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri, protocol::Json params = protocol::Json::object(),
      const std::string& session_id = {}) const {
    return server_->read_resource(uri, std::move(params), session_id);
  }

  std::vector<protocol::ResourceTemplate> list_resource_templates() const {
    return server_->list_resource_templates();
  }

  core::Result<protocol::Json> initialize() { return server_->initialize(); }

  core::Result<protocol::Json> ping(
      const server::SessionContext& context = {}) {
    return server_->ping(context);
  }

  core::Result<protocol::JsonRpcResponse> handle_request(
      const protocol::JsonRpcRequest& request,
      const server::SessionContext& context = {},
      transport::ServerTransport* native_transport = nullptr) try {
    if (request.method == protocol::InitializeMethod) {
      const auto valid =
          detail::validate_peer_server_initialize_params(request.params);
      if (!valid) {
        return detail::peer_error_response(request, valid.error());
      }
      const auto requested_version =
          request.params.at("protocolVersion").get<std::string>();
      return protocol::make_response(
          request.id,
          detail::make_peer_server_initialize_result(
              get_info(), capabilities(),
              protocol::negotiate_protocol_version(requested_version)));
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

    if (request.method == protocol::ToolsListMethod) {
      protocol::Json result = protocol::Json::object();
      result["tools"] = protocol::Json::array();
      for (const auto& tool : list_tools()) {
        result["tools"].push_back(protocol::tool_definition_to_json(tool));
      }
      return protocol::make_response(request.id, std::move(result));
    }

    if (request.method == protocol::ToolsGetMethod) {
      if (!request.params.is_object() || !request.params.contains("name") ||
          !request.params.at("name").is_string()) {
        return detail::peer_error_response(
            request, errors::make(protocol::ErrorCode::InvalidRequest,
                                  "tools/get requires a string name"));
      }

      const auto tool = get_tool(request.params.at("name").get<std::string>());
      if (!tool) {
        return detail::peer_error_response(request, tool.error());
      }
      return protocol::make_response(request.id,
                                     protocol::tool_definition_to_json(*tool));
    }

    if (request.method == protocol::ToolsCallMethod) {
      const auto call = protocol::tool_call_from_json(request.params);
      if (!call) {
        return detail::peer_error_response(request, call.error());
      }
      if (call->task.has_value()) {
        const auto valid = server_->tools().validate(*call);
        if (!valid) {
          return detail::peer_error_response(request, valid.error());
        }
        const auto task_manager = server_->task_manager();
        if (!task_manager) {
          return detail::peer_error_response(
              request, errors::make(protocol::ErrorCode::MethodNotFound,
                                    "task processor is not configured"));
        }
        const auto task =
            task_manager->submit_tool_call(server_->tools(), *call, context);
        if (!task) {
          return detail::peer_error_response(request, task.error());
        }
        return protocol::make_response(
            request.id, protocol::create_task_result_to_json(*task));
      }

      const auto request_cancellation =
          begin_peer_request_cancellation(request.id);
      const std::shared_ptr<void> request_cancellation_cleanup(
          nullptr, [this, request_id = request.id](void*) noexcept {
            end_peer_request_cancellation(request_id);
          });
      const auto result =
          server_->tools().call(*call, context, request_cancellation);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }

      return protocol::make_response(request.id,
                                     protocol::tool_result_to_json(*result));
    }

    if (request.method == protocol::PromptsListMethod) {
      protocol::PromptsListResult result;
      result.prompts = list_prompts();
      return protocol::make_response(
          request.id, protocol::prompts_list_result_to_json(result));
    }

    if (request.method == protocol::PromptsGetMethod) {
      const auto params =
          protocol::prompts_get_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(request, params.error());
      }

      const auto result =
          server_->prompts().get(params->name, params->arguments, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }

      return protocol::make_response(
          request.id, protocol::prompts_get_result_to_json(*result));
    }

    if (request.method == protocol::ResourcesListMethod) {
      protocol::ResourcesListResult result;
      result.resources = list_resources();
      return protocol::make_response(
          request.id, protocol::resources_list_result_to_json(result));
    }

    if (request.method == protocol::ResourcesReadMethod) {
      const auto params =
          protocol::resources_read_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(request, params.error());
      }

      const auto result =
          server_->resources().read(params->uri, request.params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }

      return protocol::make_response(
          request.id, protocol::resources_read_result_to_json(*result));
    }

    if (request.method == protocol::ResourcesTemplatesListMethod) {
      protocol::ResourceTemplatesListResult result;
      result.resource_templates = list_resource_templates();
      return protocol::make_response(
          request.id, protocol::resource_templates_list_result_to_json(result));
    }

    if (request.method == protocol::ResourcesSubscribeMethod ||
        request.method == protocol::ResourcesUnsubscribeMethod) {
      if (!capabilities().resources.subscribe) {
        return detail::peer_error_response(
            request, errors::make(protocol::ErrorCode::MethodNotFound,
                                  "resource subscriptions are not enabled"));
      }
      if (!request.params.is_object() || !request.params.contains("uri") ||
          !request.params.at("uri").is_string()) {
        return detail::peer_error_response(
            request,
            errors::make(protocol::ErrorCode::InvalidRequest,
                         "resource subscription requires a string uri"));
      }
      const auto subscription = server_->set_resource_subscription(
          subscription_context_for(context, native_transport),
          request.params.at("uri").get<std::string>(),
          request.method == protocol::ResourcesSubscribeMethod);
      if (!subscription) {
        return detail::peer_error_response(request, subscription.error());
      }
      return protocol::make_response(request.id, protocol::Json::object());
    }

    if (request.method == protocol::CompletionCompleteMethod &&
        completion_handler_) {
      const auto result = completion_handler_(request.params);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(request.id, *result);
    }

    if (request.method == protocol::SamplingCreateMessageMethod &&
        sampling_handler_) {
      const auto result = sampling_handler_(request.params);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(request.id, *result);
    }

    if (request.method == protocol::LoggingSetLevelMethod && logging_handler_) {
      if (!request.params.is_object() || !request.params.contains("level") ||
          !request.params.at("level").is_string()) {
        return detail::peer_error_response(
            request, errors::make(protocol::ErrorCode::InvalidRequest,
                                  "logging/setLevel requires a string level"));
      }
      logging_handler_(request.params.at("level").get<std::string>(),
                       "logging level changed");
      return protocol::make_response(request.id, protocol::Json::object());
    }

    if (request.method == protocol::TasksListMethod && task_list_handler_) {
      const auto params = protocol::task_list_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(request, params.error());
      }
      const auto result = task_list_handler_(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::task_list_result_to_json(*result));
    }

    if (request.method == protocol::TasksGetMethod && task_get_handler_) {
      const auto params = protocol::task_get_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(request, params.error());
      }
      const auto result = task_get_handler_(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(request.id,
                                     protocol::task_to_json(*result));
    }

    if (request.method == protocol::TasksCancelMethod && task_cancel_handler_) {
      const auto params =
          protocol::task_cancel_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(request, params.error());
      }
      const auto result = task_cancel_handler_(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(request.id,
                                     protocol::task_to_json(*result));
    }

    if (request.method == protocol::TasksResultMethod && task_result_handler_) {
      const auto params =
          protocol::task_result_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(request, params.error());
      }
      const auto result = task_result_handler_(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(request.id, *result);
    }

    return server_->handle_request(request, context);
  } catch (const std::exception& ex) {
    return detail::peer_error_response(request,
                                       errors::handler_failed(ex.what()));
  } catch (...) {
    return detail::peer_error_response(request,
                                       errors::handler_unknown_exception());
  }

  core::Result<core::Unit> handle_notification(
      const protocol::JsonRpcNotification& notification,
      const server::SessionContext& context = {}) {
    if (notification.method == protocol::CancelledNotificationMethod ||
        native_notification_state_) {
      return handle_native_notification(notification, context);
    }
    return server_->handle_notification(notification, context);
  }

  core::Result<core::Unit> add_transport(
      std::unique_ptr<server::Transport> transport) {
    return server_->add_transport(std::move(transport));
  }

  /// @brief Adds an owned role-generic server transport.
  core::Result<core::Unit> add_transport(
      std::unique_ptr<transport::ServerTransport> transport) {
    if (!transport) {
      return std::unexpected(detail::peer_dispatch_error(
          "server peer transport must not be null"));
    }
    native_transports_.push_back(std::move(transport));
    native_context_transports_.push_back(
        server::make_contract_transport_adapter(*native_transports_.back()));
    const auto attached =
        server_->add_session_transport(*native_context_transports_.back());
    if (!attached) {
      native_context_transports_.pop_back();
      native_transports_.pop_back();
      return std::unexpected(attached.error());
    }
    return core::Unit{};
  }

  core::Result<core::Unit> start(CancellationToken cancellation = {}) {
    if (native_transports_.empty()) {
      return server_->start();
    }

    std::vector<std::thread> workers;
    workers.reserve(native_transports_.size());
    std::mutex error_mutex;
    std::optional<core::Error> first_error;

    for (std::size_t index = 0; index < native_transports_.size(); ++index) {
      auto* transport_ptr = native_transports_[index].get();
      auto* context_transport_ptr = native_context_transports_[index].get();
      workers.emplace_back([this, transport_ptr, context_transport_ptr,
                            cancellation, &error_mutex,
                            &first_error]() noexcept {
        server::SessionContext context;
        context.remote_address = std::string(transport_ptr->name());
        context.transport = context_transport_ptr;
        const auto served =
            serve_transport(*transport_ptr, context, cancellation);
        if (!served) {
          detail::keep_first_service_error(first_error, error_mutex,
                                           served.error());
        }
      });
    }

    for (auto& worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }

    if (first_error.has_value()) {
      return std::unexpected(*first_error);
    }
    return core::Unit{};
  }

  void stop() noexcept {
    for (auto& transport : native_transports_) {
      if (transport) {
        (void)transport->close();
      }
    }
    server_->stop();
  }

  core::Result<core::Unit> notify_roots_list_changed() {
    return server_->notify_roots_list_changed();
  }

  core::Result<core::Unit> notify_tool_list_changed() {
    return server_->notify_tool_list_changed();
  }

  core::Result<core::Unit> notify_prompt_list_changed() {
    return server_->notify_prompt_list_changed();
  }

  core::Result<core::Unit> notify_resource_list_changed() {
    return server_->notify_resource_list_changed();
  }

  core::Result<core::Unit> notify_resource_updated(std::string_view uri) {
    return server_->notify_resource_updated(uri);
  }

  core::Result<core::Unit> notify_progress(
      const protocol::ProgressNotificationParams& params) {
    return server_->notify_progress(params);
  }

  core::Result<core::Unit> notify_logging_message(
      const protocol::LoggingMessageNotificationParams& params) {
    return server_->notify_logging_message(params);
  }

  core::Result<core::Unit> notify_elicitation_complete(
      std::string elicitation_id) {
    return server_->notify_elicitation_complete(std::move(elicitation_id));
  }

  core::Result<core::Unit> notify_task_status(const protocol::Task& task) {
    return server_->notify_task_status(task);
  }

  /// @brief Dispatches one inbound role-generic transport message.
  ///
  /// Requests produce a JSON-RPC response message, notifications produce no
  /// outbound message, and standalone responses remain the responsibility of
  /// request-handle correlation paths.
  core::Result<std::optional<protocol::JsonRpcMessage>> dispatch_message(
      const protocol::JsonRpcMessage& message,
      const server::SessionContext& context = {},
      transport::ServerTransport* native_transport = nullptr) {
    if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      auto handled = handle_request(*request, context, native_transport);
      if (!handled) {
        return protocol::JsonRpcMessage{protocol::make_error_response(
            request->id,
            detail::peer_error_object_from_core_error(handled.error()))};
      }
      return protocol::JsonRpcMessage{std::move(*handled)};
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      const auto handled = handle_notification(*notification, context);
      if (!handled) {
        return std::unexpected(handled.error());
      }
      return std::nullopt;
    }

    return std::unexpected(detail::peer_dispatch_error(
        "server peer cannot dispatch an uncorrelated response"));
  }

  /// @brief Runs a sequential receive loop over a role-generic server
  /// transport.
  core::Result<core::Unit> serve_transport(
      transport::ServerTransport& transport,
      const server::SessionContext& context = {},
      CancellationToken cancellation = {}) {
    return detail::serve_transport_loop(
        transport, cancellation,
        [this, &context, &transport](const protocol::JsonRpcMessage& message) {
          return dispatch_message(message, context, &transport);
        });
  }

 private:
  server::SessionContext subscription_context_for(
      const server::SessionContext& context,
      const transport::ServerTransport* native_transport) const {
    server::SessionContext subscription_context = context;
    if (subscription_context.transport || native_transport == nullptr) {
      return subscription_context;
    }

    for (std::size_t index = 0; index < native_transports_.size(); ++index) {
      if (native_transports_[index].get() == native_transport &&
          index < native_context_transports_.size()) {
        subscription_context.transport =
            native_context_transports_[index].get();
        break;
      }
    }
    return subscription_context;
  }

  CancellationToken begin_peer_request_cancellation(
      const protocol::RequestId& request_id) {
    CancellationSource source;
    auto token = source.token();
    std::lock_guard lock(*peer_request_cancellations_mutex_);
    (*peer_request_cancellations_)[detail::peer_request_cancellation_key(
        request_id)] = std::move(source);
    return token;
  }

  void end_peer_request_cancellation(
      const protocol::RequestId& request_id) noexcept {
    std::lock_guard lock(*peer_request_cancellations_mutex_);
    peer_request_cancellations_->erase(
        detail::peer_request_cancellation_key(request_id));
  }

  void cancel_peer_request(const protocol::RequestId& request_id) noexcept {
    std::lock_guard lock(*peer_request_cancellations_mutex_);
    const auto it = peer_request_cancellations_->find(
        detail::peer_request_cancellation_key(request_id));
    if (it != peer_request_cancellations_->end()) {
      it->second.cancel();
    }
  }

  core::Result<core::Unit> handle_native_notification(
      const protocol::JsonRpcNotification& notification,
      const server::SessionContext& context) try {
    if (notification.method == protocol::CancelledNotificationMethod) {
      const auto cancelled = protocol::cancelled_notification_params_from_json(
          notification.params);
      if (!cancelled) {
        return std::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "cancelled notification requires valid params"));
      }
      cancel_peer_request(cancelled->request_id);
      return server_->handle_notification(notification, context);
    }

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
    } else if (notification.method == protocol::ProgressNotificationMethod &&
               progress_handler_) {
      const auto params =
          protocol::progress_notification_params_from_json(notification.params);
      if (!params) {
        return std::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "progress notification requires valid params"));
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
        return std::unexpected(errors::make(
            protocol::ErrorCode::InvalidParams,
            "resource updated notification requires a string uri"));
      }
      const auto result = resource_updated_handler_(
          notification.params.at("uri").get<std::string>(), context);
      if (!result) {
        return std::unexpected(result.error());
      }
    }

    return core::Unit{};
  } catch (const std::exception& ex) {
    return std::unexpected(errors::handler_failed(ex.what()));
  } catch (...) {
    return std::unexpected(errors::handler_unknown_exception());
  }

  std::unique_ptr<server::Server> server_;
  std::vector<std::unique_ptr<transport::ServerTransport>> native_transports_;
  std::vector<std::unique_ptr<server::Transport>> native_context_transports_;
  std::shared_ptr<std::mutex> peer_request_cancellations_mutex_ =
      std::make_shared<std::mutex>();
  std::shared_ptr<std::unordered_map<std::string, CancellationSource>>
      peer_request_cancellations_ = std::make_shared<
          std::unordered_map<std::string, CancellationSource>>();
  bool native_notification_state_ = false;
  server::Server::RawRequestHandler raw_request_handler_;
  server::Server::JsonHandler completion_handler_;
  server::Server::JsonHandler sampling_handler_;
  server::Server::LoggingHandler logging_handler_;
  server::Server::TaskListHandler task_list_handler_;
  server::Server::TaskGetHandler task_get_handler_;
  server::Server::TaskCancelHandler task_cancel_handler_;
  server::Server::TaskResultHandler task_result_handler_;
  server::Server::RawNotificationHandler raw_notification_handler_;
  server::Server::ProgressHandler progress_handler_;
  server::Server::RootsListChangedHandler roots_list_changed_handler_;
  server::Server::ListChangedHandler tool_list_changed_handler_;
  server::Server::ListChangedHandler prompt_list_changed_handler_;
  server::Server::ListChangedHandler resource_list_changed_handler_;
  server::Server::ResourceUpdatedHandler resource_updated_handler_;
};

using ClientPeer = Peer<RoleClient>;
using ServerPeer = Peer<RoleServer>;

}  // namespace mcp
