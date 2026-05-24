#pragma once

/// @file cxxmcp/protocol/serialization.hpp
/// @brief JSON-RPC method names and message construction/parsing helpers.
///
/// This header ties the protocol DTOs to the JSON-RPC wire format. Method name
/// constants are the canonical strings used in request and notification
/// envelopes; helper functions build, parse, and serialize those envelopes
/// without changing feature-specific payload semantics.

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace mcp::protocol {

    /// @brief JSON-RPC protocol version string placed in message envelopes.
    inline constexpr std::string_view JsonRpcVersion = "2.0";
    /// @brief MCP protocol version advertised during initialization.
    inline constexpr std::string_view McpProtocolVersion = "2025-11-25";

    /// @brief `initialize` request method for lifecycle negotiation.
    inline constexpr std::string_view InitializeMethod = "initialize";
    /// @brief Notification sent after a successful initialize response.
    inline constexpr std::string_view InitializedMethod = "notifications/initialized";
    /// @brief Lightweight liveness request.
    inline constexpr std::string_view PingMethod = "ping";
    /// @brief Lists available prompts.
    inline constexpr std::string_view PromptsListMethod = "prompts/list";
    /// @brief Retrieves a prompt by name and arguments.
    inline constexpr std::string_view PromptsGetMethod = "prompts/get";
    /// @brief Lists concrete resources.
    inline constexpr std::string_view ResourcesListMethod = "resources/list";
    /// @brief Reads resource contents by URI.
    inline constexpr std::string_view ResourcesReadMethod = "resources/read";
    /// @brief Lists URI templates that can produce resources.
    inline constexpr std::string_view ResourcesTemplatesListMethod = "resources/templates/list";
    /// @brief Subscribes to updates for a resource URI.
    inline constexpr std::string_view ResourcesSubscribeMethod = "resources/subscribe";
    /// @brief Removes a resource subscription.
    inline constexpr std::string_view ResourcesUnsubscribeMethod = "resources/unsubscribe";
    /// @brief Lists callable tools.
    inline constexpr std::string_view ToolsListMethod = "tools/list";
    /// @brief Retrieves a tool definition.
    inline constexpr std::string_view ToolsGetMethod = "tools/get";
    /// @brief Calls a named tool with JSON arguments.
    inline constexpr std::string_view ToolsCallMethod = "tools/call";
    /// @brief Completes a prompt or resource-template argument value.
    inline constexpr std::string_view CompletionCompleteMethod = "completion/complete";
    /// @brief Updates the minimum logging level the peer should emit.
    inline constexpr std::string_view LoggingSetLevelMethod = "logging/setLevel";
    /// @brief Requests client-side model sampling.
    inline constexpr std::string_view SamplingCreateMessageMethod = "sampling/createMessage";
    /// @brief Requests user input through MCP elicitation.
    inline constexpr std::string_view ElicitationCreateMethod = "elicitation/create";
    /// @brief Notification that a URL-based elicitation interaction completed.
    inline constexpr std::string_view ElicitationCompleteNotificationMethod = "notifications/elicitation/complete";
    /// @brief Lists asynchronous tasks known to the peer.
    inline constexpr std::string_view TasksListMethod = "tasks/list";
    /// @brief Retrieves a task by id.
    inline constexpr std::string_view TasksGetMethod = "tasks/get";
    /// @brief Requests cancellation of a task.
    inline constexpr std::string_view TasksCancelMethod = "tasks/cancel";
    /// @brief Retrieves the result associated with a completed task.
    inline constexpr std::string_view TasksResultMethod = "tasks/result";
    /// @brief Notification carrying task status updates.
    inline constexpr std::string_view TasksStatusNotificationMethod = "notifications/tasks/status";
    /// @brief Lists client roots available to the server.
    inline constexpr std::string_view RootsListMethod = "roots/list";
    /// @brief JSON-RPC cancellation notification.
    inline constexpr std::string_view CancelledNotificationMethod = "notifications/cancelled";
    /// @brief JSON-RPC progress notification.
    inline constexpr std::string_view ProgressNotificationMethod = "notifications/progress";
    /// @brief Notification that the client root list changed.
    inline constexpr std::string_view RootsListChangedNotificationMethod = "notifications/roots/list_changed";
    /// @brief Notification that the server resource list changed.
    inline constexpr std::string_view ResourcesListChangedNotificationMethod = "notifications/resources/list_changed";
    /// @brief Notification that one subscribed resource was updated.
    inline constexpr std::string_view ResourcesUpdatedNotificationMethod = "notifications/resources/updated";
    /// @brief Notification that the server tool list changed.
    inline constexpr std::string_view ToolsListChangedNotificationMethod = "notifications/tools/list_changed";
    /// @brief Notification that the server prompt list changed.
    inline constexpr std::string_view PromptsListChangedNotificationMethod = "notifications/prompts/list_changed";
    /// @brief Notification carrying a logging message.
    inline constexpr std::string_view LoggingMessageNotificationMethod = "notifications/message";

    /// @brief Builds a JSON-RPC error object.
    /// @param code Numeric JSON-RPC or MCP error code.
    /// @param message Human-readable diagnostic text.
    /// @param data Optional structured error details.
    /// @return Error object ready to place in a response.
    ErrorObject make_error(int code, std::string message, std::optional<Json> data = std::nullopt);

    /// @brief Builds a JSON-RPC error object from a typed error code.
    /// @param code Error code enum value.
    /// @param message Human-readable diagnostic text.
    /// @param data Optional structured error details.
    /// @return Error object ready to place in a response.
    ErrorObject make_error(ErrorCode code, std::string message, std::optional<Json> data = std::nullopt);

    /// @brief Builds a successful JSON-RPC response.
    /// @param id Request id to echo.
    /// @param result Method-specific result object.
    /// @return Response envelope with `result` set.
    JsonRpcResponse make_response(RequestId id, Json result);

    /// @brief Builds an error JSON-RPC response.
    /// @param id Request id to echo, or nullopt for parse-level errors.
    /// @param error Error object to serialize.
    /// @return Response envelope with `error` set.
    JsonRpcResponse make_error_response(std::optional<RequestId> id, ErrorObject error);

    /// @brief Builds a generic JSON-RPC request envelope.
    /// @param method MCP method name.
    /// @param id Request id assigned by the caller.
    /// @param params Method-specific params object.
    /// @return Request envelope ready for serialization.
    JsonRpcRequest make_request(std::string method, RequestId id, Json params = Json::object());

    /// @brief Builds a generic JSON-RPC notification envelope.
    /// @param method MCP notification method name.
    /// @param params Notification params object.
    /// @return Notification envelope ready for serialization.
    JsonRpcNotification make_notification(std::string method, Json params = Json::object());

    /// @brief Builds an `initialize` lifecycle request.
    /// @param id Request id assigned by the caller.
    /// @param params Initialize params object.
    /// @return Request envelope using InitializeMethod.
    JsonRpcRequest make_initialize_request(RequestId id, Json params = Json::object());

    /// @brief Builds an initialized lifecycle notification.
    /// @param params Optional initialized notification params.
    /// @return Notification envelope using InitializedMethod.
    JsonRpcNotification make_initialized_notification(Json params = Json::object());

    /// @brief Builds a `ping` liveness request.
    /// @param id Request id assigned by the caller.
    /// @param params Optional ping params.
    /// @return Request envelope using PingMethod.
    JsonRpcRequest make_ping_request(RequestId id, Json params = Json::object());

    /// @brief Parses any JSON-RPC message shape from text.
    /// @param text UTF-8 JSON text.
    /// @return Parsed request, response, or notification, or an error on invalid JSON-RPC.
    core::Result<JsonRpcMessage> parse_message(std::string_view text);

    /// @brief Serializes any JSON-RPC message shape to JSON text.
    /// @param message Message envelope to encode.
    /// @return JSON text, or an error if the envelope is inconsistent.
    core::Result<std::string> serialize_message(const JsonRpcMessage &message);

    /// @brief Parses a JSON-RPC request envelope from text.
    core::Result<JsonRpcRequest> parse_request(std::string_view text);
    /// @brief Parses a JSON-RPC response envelope from text.
    core::Result<JsonRpcResponse> parse_response(std::string_view text);
    /// @brief Parses a JSON-RPC notification envelope from text.
    core::Result<JsonRpcNotification> parse_notification(std::string_view text);
    /// @brief Serializes a JSON-RPC request envelope to text.
    core::Result<std::string> serialize_request(const JsonRpcRequest &request);
    /// @brief Serializes a JSON-RPC response envelope to text.
    core::Result<std::string> serialize_response(const JsonRpcResponse &response);
    /// @brief Serializes a JSON-RPC notification envelope to text.
    core::Result<std::string> serialize_notification(const JsonRpcNotification &notification);
    /// @brief Serializes a JSON-RPC error response to text.
    /// @param error Error object to encode.
    /// @param id Optional request id, omitted for parse-level errors.
    /// @return JSON-RPC response text containing the error.
    core::Result<std::string> serialize_error(const ErrorObject &error, std::optional<RequestId> id = std::nullopt);

}// namespace mcp::protocol
