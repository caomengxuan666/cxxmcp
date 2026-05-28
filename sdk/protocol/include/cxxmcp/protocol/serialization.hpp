// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/serialization.hpp
/// @brief JSON-RPC method names and message construction/parsing helpers.
///
/// This header ties the protocol DTOs to the JSON-RPC wire format. Method name
/// constants are the canonical strings used in request and notification
/// envelopes; helper functions build, parse, and serialize those envelopes
/// without changing feature-specific payload semantics.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief JSON-RPC protocol version string placed in message envelopes.
inline const std::string JsonRpcVersion = "2.0";
inline const std::string McpProtocolVersion2025_11_25 = "2025-11-25";
inline const std::string McpProtocolVersion2025_06_18 = "2025-06-18";
inline const std::string McpProtocolVersion2025_03_26 = "2025-03-26";
inline const std::string McpProtocolVersion2024_11_05 = "2024-11-05";
/// @brief Latest MCP protocol version advertised during initialization.
inline const std::string McpProtocolVersion = McpProtocolVersion2025_11_25;
/// @brief Protocol versions accepted by this SDK during initialization.
inline const std::array<std::string, 4> McpSupportedProtocolVersions{
    McpProtocolVersion2024_11_05, McpProtocolVersion2025_03_26,
    McpProtocolVersion2025_06_18, McpProtocolVersion2025_11_25};

/// @brief Returns true when a peer protocol version is supported.
inline bool is_supported_protocol_version(std::string_view version) noexcept {
  for (const auto& supported : McpSupportedProtocolVersions) {
    if (version == supported) {
      return true;
    }
  }
  return false;
}

/// @brief Returns the peer-requested version when known.
inline std::optional<std::string_view> negotiate_protocol_version(
    std::string_view requested) noexcept {
  for (const auto& supported : McpSupportedProtocolVersions) {
    if (requested == supported) {
      return std::string_view(supported);
    }
  }
  return std::nullopt;
}

/// @brief `initialize` request method for lifecycle negotiation.
inline const std::string InitializeMethod = "initialize";
/// @brief Notification sent after a successful initialize response.
inline const std::string InitializedMethod = "notifications/initialized";
/// @brief Lightweight liveness request.
inline const std::string PingMethod = "ping";
/// @brief Lists available prompts.
inline const std::string PromptsListMethod = "prompts/list";
/// @brief Retrieves a prompt by name and arguments.
inline const std::string PromptsGetMethod = "prompts/get";
/// @brief Lists concrete resources.
inline const std::string ResourcesListMethod = "resources/list";
/// @brief Reads resource contents by URI.
inline const std::string ResourcesReadMethod = "resources/read";
/// @brief Lists URI templates that can produce resources.
inline const std::string ResourcesTemplatesListMethod =
    "resources/templates/list";
/// @brief Subscribes to updates for a resource URI.
inline const std::string ResourcesSubscribeMethod = "resources/subscribe";
/// @brief Removes a resource subscription.
inline const std::string ResourcesUnsubscribeMethod = "resources/unsubscribe";
/// @brief Lists callable tools.
inline const std::string ToolsListMethod = "tools/list";
/// @brief Retrieves a tool definition.
inline const std::string ToolsGetMethod = "tools/get";
/// @brief Calls a named tool with JSON arguments.
inline const std::string ToolsCallMethod = "tools/call";
/// @brief Completes a prompt or resource-template argument value.
inline const std::string CompletionCompleteMethod = "completion/complete";
/// @brief Updates the minimum logging level the peer should emit.
inline const std::string LoggingSetLevelMethod = "logging/setLevel";
/// @brief Requests client-side model sampling.
inline const std::string SamplingCreateMessageMethod = "sampling/createMessage";
/// @brief Requests user input through MCP elicitation.
inline const std::string ElicitationCreateMethod = "elicitation/create";
/// @brief Notification that a URL-based elicitation interaction completed.
inline const std::string ElicitationCompleteNotificationMethod =
    "notifications/elicitation/complete";
/// @brief Lists asynchronous tasks known to the peer.
inline const std::string TasksListMethod = "tasks/list";
/// @brief Retrieves a task by id.
inline const std::string TasksGetMethod = "tasks/get";
/// @brief Requests cancellation of a task.
inline const std::string TasksCancelMethod = "tasks/cancel";
/// @brief Retrieves the result associated with a completed task.
inline const std::string TasksResultMethod = "tasks/result";
/// @brief Server-to-client request to create a new task.
inline const std::string TasksCreateMethod = "tasks/create";
/// @brief Notification carrying task status updates.
inline const std::string TasksStatusNotificationMethod =
    "notifications/tasks/status";
/// @brief Lists client roots available to the server.
inline const std::string RootsListMethod = "roots/list";
/// @brief JSON-RPC cancellation notification.
inline const std::string CancelledNotificationMethod =
    "notifications/cancelled";
/// @brief JSON-RPC progress notification.
inline const std::string ProgressNotificationMethod = "notifications/progress";
/// @brief Notification that the client root list changed.
inline const std::string RootsListChangedNotificationMethod =
    "notifications/roots/list_changed";
/// @brief Notification that the server resource list changed.
inline const std::string ResourcesListChangedNotificationMethod =
    "notifications/resources/list_changed";
/// @brief Notification that one subscribed resource was updated.
inline const std::string ResourcesUpdatedNotificationMethod =
    "notifications/resources/updated";
/// @brief Notification that the server tool list changed.
inline const std::string ToolsListChangedNotificationMethod =
    "notifications/tools/list_changed";
/// @brief Notification that the server prompt list changed.
inline const std::string PromptsListChangedNotificationMethod =
    "notifications/prompts/list_changed";
/// @brief Notification carrying a logging message.
inline const std::string LoggingMessageNotificationMethod =
    "notifications/message";

/// @brief Builds a JSON-RPC error object.
/// @param code Numeric JSON-RPC or MCP error code.
/// @param message Human-readable diagnostic text.
/// @param data Optional structured error details.
/// @return Error object ready to place in a response.
ErrorObject make_error(int code, std::string message,
                       std::optional<Json> data = std::nullopt);

/// @brief Builds a JSON-RPC error object from a typed error code.
/// @param code Error code enum value.
/// @param message Human-readable diagnostic text.
/// @param data Optional structured error details.
/// @return Error object ready to place in a response.
ErrorObject make_error(ErrorCode code, std::string message,
                       std::optional<Json> data = std::nullopt);

/// @brief Builds a successful JSON-RPC response.
/// @param id Request id to echo.
/// @param result Method-specific result object.
/// @return Response envelope with `result` set.
JsonRpcResponse make_response(RequestId id, Json result);

/// @brief Builds an error JSON-RPC response.
/// @param id Request id to echo, or nullopt for parse-level errors.
/// @param error Error object to serialize.
/// @return Response envelope with `error` set.
JsonRpcResponse make_error_response(std::optional<RequestId> id,
                                    ErrorObject error);

/// @brief Builds a generic JSON-RPC request envelope.
/// @param method MCP method name.
/// @param id Request id assigned by the caller.
/// @param params Method-specific params object.
/// @return Request envelope ready for serialization.
JsonRpcRequest make_request(std::string method, RequestId id,
                            Json params = Json::object());

/// @brief Builds a generic JSON-RPC notification envelope.
/// @param method MCP notification method name.
/// @param params Notification params object.
/// @return Notification envelope ready for serialization.
JsonRpcNotification make_notification(std::string method,
                                      Json params = Json::object());

/// @brief Builds an `initialize` lifecycle request.
/// @param id Request id assigned by the caller.
/// @param params Initialize params object.
/// @return Request envelope using InitializeMethod.
JsonRpcRequest make_initialize_request(RequestId id,
                                       Json params = Json::object());

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
/// @return Parsed request, response, or notification, or an error on invalid
/// JSON-RPC.
core::Result<JsonRpcMessage> parse_message(std::string_view text);

/// @brief Serializes any JSON-RPC message shape to JSON text.
/// @param message Message envelope to encode.
/// @return JSON text, or an error if the envelope is inconsistent.
core::Result<std::string> serialize_message(const JsonRpcMessage& message);

/// @brief Parses a JSON-RPC request envelope from text.
core::Result<JsonRpcRequest> parse_request(std::string_view text);
/// @brief Parses a JSON-RPC response envelope from text.
core::Result<JsonRpcResponse> parse_response(std::string_view text);
/// @brief Parses a JSON-RPC notification envelope from text.
core::Result<JsonRpcNotification> parse_notification(std::string_view text);
/// @brief Serializes a JSON-RPC request envelope to text.
core::Result<std::string> serialize_request(const JsonRpcRequest& request);
/// @brief Serializes a JSON-RPC response envelope to text.
core::Result<std::string> serialize_response(const JsonRpcResponse& response);
/// @brief Serializes a JSON-RPC notification envelope to text.
core::Result<std::string> serialize_notification(
    const JsonRpcNotification& notification);
/// @brief Serializes a JSON-RPC error response to text.
/// @param error Error object to encode.
/// @param id Optional request id, omitted for parse-level errors.
/// @return JSON-RPC response text containing the error.
core::Result<std::string> serialize_error(
    const ErrorObject& error, std::optional<RequestId> id = std::nullopt);

}  // namespace mcp::protocol
