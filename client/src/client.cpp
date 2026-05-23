#include "mcp/client/client.hpp"
#include "mcp/client/http_transport.hpp"
#include "mcp/client/process_stdio_transport.hpp"
#include "mcp/client/session.hpp"
#include "mcp/client/stdio_transport.hpp"

#include "mcp/protocol/completion.hpp"
#include "mcp/protocol/logging.hpp"
#include "mcp/protocol/prompt.hpp"
#include "mcp/protocol/resource.hpp"
#include "mcp/protocol/serialization.hpp"
#include "mcp/protocol/sampling.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mcp::client {

namespace {

core::Error make_client_error(int code, std::string message, std::string detail = {}) {
    return core::Error{code, std::move(message), std::move(detail)};
}

core::Result<protocol::Json> require_result_payload(const protocol::JsonRpcResponse& response) {
    if (response.error.has_value()) {
        return std::unexpected(core::Error{
            response.error->code,
            response.error->message,
            response.error->data.has_value() ? response.error->data->dump() : std::string{},
        });
    }

    if (!response.result.has_value()) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                 "response did not contain a result"));
    }

    return *response.result;
}

protocol::Json cursor_params(const std::optional<std::string>& cursor) {
    protocol::Json params = protocol::Json::object();
    if (cursor.has_value()) {
        params["cursor"] = *cursor;
    }
    return params;
}

} // namespace

core::Result<core::Unit> Transport::send_notification(const protocol::JsonRpcNotification&) {
    return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                             "client transport does not support notifications"));
}

Client::Client(std::unique_ptr<Transport> transport)
    : transport_(std::move(transport)) {}

Client Client::connect_streamable_http(StreamableHttpEndpoint endpoint) {
    return Client(std::make_unique<HttpTransport>(HttpTransportOptions{
        .host = std::move(endpoint.host),
        .port = endpoint.port,
        .path = std::move(endpoint.path),
        .headers = std::move(endpoint.headers),
        .timeout = endpoint.timeout,
    }));
}

Client Client::connect_legacy_sse(StreamableHttpEndpoint endpoint) {
    endpoint.headers.emplace("Accept", "application/json, text/event-stream");
    return connect_streamable_http(std::move(endpoint));
}

Client Client::connect_stdio(StdioEndpoint endpoint) {
    if (endpoint.command.empty()) {
        return Client(std::make_unique<StdioTransport>());
    }
    return Client(std::make_unique<ProcessStdioTransport>(ProcessStdioTransportOptions{
        .command = std::move(endpoint.command),
        .args = std::move(endpoint.args),
        .cwd = std::move(endpoint.cwd),
        .env = std::move(endpoint.env),
    }));
}

core::Result<protocol::Json> Client::send_request(std::string method, protocol::Json params) {
    if (!transport_) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                 "client transport is not configured"));
    }

    auto response = transport_->send(protocol::JsonRpcRequest{
        .method = std::move(method),
        .params = std::move(params),
        .id = next_request_id_++,
    });
    if (!response) {
        return std::unexpected(response.error());
    }

    return require_result_payload(*response);
}

core::Result<protocol::Json> Client::initialize(std::string client_name, std::string client_version) {
    protocol::Json params = protocol::Json::object();
    params["protocolVersion"] = std::string(protocol::McpProtocolVersion);
    params["capabilities"] = protocol::Json{
        {"roots", protocol::Json{{"listChanged", true}}},
        {"sampling", protocol::Json::object()},
        {"elicitation", protocol::Json::object()},
    };
    params["clientInfo"] = protocol::Json{
        {"name", std::move(client_name)},
        {"version", std::move(client_version)},
    };
    return send_request(std::string(protocol::InitializeMethod), std::move(params));
}

core::Result<core::Unit> Client::notify_initialized() {
    return raw_notification(protocol::JsonRpcNotification{
        .method = std::string(protocol::InitializedMethod),
        .params = protocol::Json::object(),
    });
}

core::Result<core::Unit> Client::notify_cancelled(protocol::RequestId request_id, std::string reason) {
    return raw_notification(protocol::JsonRpcNotification{
        .method = "notifications/cancelled",
        .params = protocol::cancelled_notification_params_to_json(protocol::CancelledNotificationParams{
            .request_id = std::move(request_id),
            .reason = std::move(reason),
        }),
    });
}

core::Result<core::Unit> Client::notify_progress(protocol::ProgressToken progress_token,
                                                 double progress,
                                                 std::optional<double> total) {
    return raw_notification(protocol::JsonRpcNotification{
        .method = "notifications/progress",
        .params = protocol::progress_notification_params_to_json(protocol::ProgressNotificationParams{
            .progress_token = std::move(progress_token),
            .progress = progress,
            .total = total,
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
    const auto payload = send_request(std::string(protocol::PingMethod), protocol::Json::object());
    if (!payload) {
        return std::unexpected(payload.error());
    }
    return core::Unit{};
}

core::Result<std::vector<protocol::Prompt>> Client::list_prompts() {
    const auto payload = send_request(std::string(protocol::PromptsListMethod), protocol::Json::object());
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
        const auto payload = send_request(std::string(protocol::PromptsListMethod), cursor_params(cursor));
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

core::Result<protocol::PromptsGetResult> Client::get_prompt(const protocol::PromptsGetParams& params) {
    if (!transport_) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                 "client transport is not configured"));
    }

    const auto response = transport_->send(protocol::JsonRpcRequest{
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

core::Result<protocol::PromptsGetResult> Client::get_prompt(std::string_view name,
                                                            const protocol::Json& arguments) {
    return get_prompt(protocol::PromptsGetParams{
        .name = std::string(name),
        .arguments = arguments,
    });
}

core::Result<std::vector<protocol::Resource>> Client::list_resources() {
    if (!transport_) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                 "client transport is not configured"));
    }

    const auto response = transport_->send(protocol::JsonRpcRequest{
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
        const auto payload = send_request(std::string(protocol::ResourcesListMethod), cursor_params(cursor));
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

core::Result<protocol::ResourcesReadResult> Client::read_resource(const protocol::ResourcesReadParams& params) {
    if (!transport_) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                 "client transport is not configured"));
    }

    const auto response = transport_->send(protocol::JsonRpcRequest{
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

core::Result<protocol::ResourcesReadResult> Client::read_resource(std::string_view uri) {
    return read_resource(protocol::ResourcesReadParams{std::string(uri)});
}

core::Result<std::vector<protocol::ResourceTemplate>> Client::list_resource_templates() {
    const auto payload = send_request("resources/templates/list", protocol::Json::object());
    if (!payload) {
        return std::unexpected(payload.error());
    }
    const auto templates = protocol::resource_templates_list_result_from_json(*payload);
    if (!templates) {
        return std::unexpected(templates.error());
    }
    return templates->resource_templates;
}

core::Result<std::vector<protocol::ResourceTemplate>> Client::list_all_resource_templates() {
    std::vector<protocol::ResourceTemplate> all;
    std::optional<std::string> cursor;
    do {
        const auto payload = send_request("resources/templates/list", cursor_params(cursor));
        if (!payload) {
            return std::unexpected(payload.error());
        }
        const auto page = protocol::resource_templates_list_result_from_json(*payload);
        if (!page) {
            return std::unexpected(page.error());
        }
        all.insert(all.end(), page->resource_templates.begin(), page->resource_templates.end());
        cursor = page->next_cursor;
    } while (cursor.has_value() && !cursor->empty());
    return all;
}

core::Result<std::vector<protocol::ToolDefinition>> Client::list_tools() {
    if (!transport_) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                 "client transport is not configured"));
    }

    const auto response = transport_->send(protocol::JsonRpcRequest{
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

    if (!payload->is_object() || !payload->contains("tools") || !payload->at("tools").is_array()) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
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
        if (!payload->is_object() || !payload->contains("tools") || !payload->at("tools").is_array()) {
            return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
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
                return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                         "tools/list nextCursor must be a string"));
            }
            cursor = payload->at("nextCursor").get<std::string>();
        }
    } while (cursor.has_value() && !cursor->empty());
    return all;
}

core::Result<protocol::ToolResult> Client::call_tool(const protocol::ToolCall& call) {
    if (!transport_) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                 "client transport is not configured"));
    }

    protocol::Json params = protocol::Json::object();
    params["name"] = call.name;
    params["arguments"] = call.arguments;

    const auto response = transport_->send(protocol::JsonRpcRequest{
        .method = "tools/call",
        .params = std::move(params),
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

core::Result<protocol::ToolResult> Client::call_raw(std::string_view name, const protocol::Json& arguments) {
    return call_tool(protocol::ToolCall{
        .name = std::string(name),
        .arguments = arguments,
    });
}

core::Result<protocol::CompleteResult> Client::complete(const protocol::CompleteParams& request) {
    const auto payload = send_request(std::string(protocol::CompletionCompleteMethod),
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

core::Result<protocol::CreateMessageResult> Client::create_message(const protocol::CreateMessageParams& request) {
    const auto payload = send_request(std::string(protocol::SamplingCreateMessageMethod),
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

core::Result<protocol::Json> Client::create_message(const protocol::Json& request) {
    return send_request("sampling/createMessage", request);
}

core::Result<protocol::Json> Client::create_elicitation(const protocol::Json& request) {
    return send_request("elicitation/create", request);
}

core::Result<core::Unit> Client::set_level(const protocol::LoggingSetLevelParams& params) {
    const auto result = send_request(std::string(protocol::LoggingSetLevelMethod),
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
    const auto result = send_request("resources/subscribe", protocol::Json{{"uri", std::string(uri)}});
    if (!result) {
        return std::unexpected(result.error());
    }
    return core::Unit{};
}

core::Result<core::Unit> Client::unsubscribe(std::string_view uri) {
    const auto result = send_request("resources/unsubscribe", protocol::Json{{"uri", std::string(uri)}});
    if (!result) {
        return std::unexpected(result.error());
    }
    return core::Unit{};
}

core::Result<protocol::Json> Client::request(const protocol::JsonRpcRequest& request) {
    return raw_request(request);
}

core::Result<core::Unit> Client::notify(const protocol::JsonRpcNotification& notification) {
    return raw_notification(notification);
}

std::vector<protocol::Root> Client::list_roots() const {
    return roots_;
}

Client& Client::set_roots(std::vector<protocol::Root> roots) {
    roots_ = std::move(roots);
    if (roots_list_changed_handler_) {
        roots_list_changed_handler_();
    }
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

Client& Client::on_raw_notification(RawNotificationHandler handler) {
    raw_notification_handler_ = std::move(handler);
    return *this;
}

core::Result<core::Unit> Client::handle_notification(const protocol::JsonRpcNotification& notification) {
    if (notification.method == std::string(protocol::InitializedMethod) && initialized_handler_) {
        initialized_handler_();
    } else if (notification.method == std::string(protocol::CancelledNotificationMethod) && cancelled_handler_) {
        const auto params = protocol::cancelled_notification_params_from_json(notification.params);
        if (!params) {
            return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InvalidParams),
                                                     "cancelled notification requires a requestId"));
        }
        cancelled_handler_(params->request_id, params->reason);
    } else if (notification.method == "notifications/message" && logging_message_handler_) {
        std::string level;
        std::string message;
        if (notification.params.is_object()) {
            if (notification.params.contains("level") && notification.params.at("level").is_string()) {
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
    } else if (notification.method == "notifications/tools/list_changed" && tool_list_changed_handler_) {
        tool_list_changed_handler_();
    } else if (notification.method == "notifications/prompts/list_changed" && prompt_list_changed_handler_) {
        prompt_list_changed_handler_();
    } else if (notification.method == "notifications/resources/list_changed" && resource_list_changed_handler_) {
        resource_list_changed_handler_();
    } else if (notification.method == "notifications/resources/updated" && resource_updated_handler_) {
        if (!notification.params.is_object() || !notification.params.contains("uri") ||
            !notification.params.at("uri").is_string()) {
            return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InvalidParams),
                                                     "resource updated notification requires a string uri"));
        }
        resource_updated_handler_(notification.params.at("uri").get<std::string>());
    } else if (notification.method == "notifications/roots/list_changed" && roots_list_changed_handler_) {
        roots_list_changed_handler_();
    }

    if (raw_notification_handler_) {
        raw_notification_handler_(notification);
    }
    return core::Unit{};
}

core::Result<protocol::Json> Client::raw_request(const protocol::JsonRpcRequest& request) {
    if (!transport_) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                 "client transport is not configured"));
    }

    const auto response = transport_->send(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return require_result_payload(*response);
}

core::Result<core::Unit> Client::raw_notification(const protocol::JsonRpcNotification& notification) {
    if (!transport_) {
        return std::unexpected(make_client_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                 "client transport is not configured"));
    }

    return transport_->send_notification(notification);
}

McpClientSession::McpClientSession(std::unique_ptr<Transport> transport,
                                   McpClientSessionOptions options)
    : client_(std::move(transport)),
      options_(std::move(options)) {}

core::Result<protocol::Json> McpClientSession::initialize() {
    protocol::Json params = protocol::Json::object();
    params["protocolVersion"] = std::string(protocol::McpProtocolVersion);
    params["capabilities"] = protocol::Json::object();
    params["clientInfo"] = protocol::Json{
        {"name", options_.client_name},
        {"version", options_.client_version},
    };

    return client_.raw_request(protocol::JsonRpcRequest{
        .method = std::string(protocol::InitializeMethod),
        .params = std::move(params),
        .id = std::int64_t{0},
    });
}

core::Result<core::Unit> McpClientSession::mark_initialized() {
    return client_.notify_initialized();
}

core::Result<std::vector<protocol::Prompt>> McpClientSession::discover_prompts() {
    return client_.list_prompts();
}

core::Result<std::vector<protocol::Prompt>> McpClientSession::discover_all_prompts() {
    return client_.list_all_prompts();
}

core::Result<protocol::PromptsGetResult> McpClientSession::get_prompt(const protocol::PromptsGetParams& params) {
    return client_.get_prompt(params);
}

core::Result<std::vector<protocol::Resource>> McpClientSession::discover_resources() {
    return client_.list_resources();
}

core::Result<std::vector<protocol::Resource>> McpClientSession::discover_all_resources() {
    return client_.list_all_resources();
}

core::Result<protocol::ResourcesReadResult> McpClientSession::read_resource(
        const protocol::ResourcesReadParams& params) {
    return client_.read_resource(params);
}

core::Result<std::vector<protocol::ResourceTemplate>> McpClientSession::discover_resource_templates() {
    return client_.list_resource_templates();
}

core::Result<std::vector<protocol::ResourceTemplate>> McpClientSession::discover_all_resource_templates() {
    return client_.list_all_resource_templates();
}

core::Result<std::vector<protocol::ToolDefinition>> McpClientSession::discover_tools() {
    return client_.list_tools();
}

core::Result<std::vector<protocol::ToolDefinition>> McpClientSession::discover_all_tools() {
    return client_.list_all_tools();
}

core::Result<protocol::ToolResult> McpClientSession::call_tool(const protocol::ToolCall& call) {
    return client_.call_tool(call);
}

Client& McpClientSession::client() {
    return client_;
}

const Client& McpClientSession::client() const {
    return client_;
}

} // namespace mcp::client
