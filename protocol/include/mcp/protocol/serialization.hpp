#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace mcp::protocol {

inline constexpr std::string_view JsonRpcVersion = "2.0";
inline constexpr std::string_view McpProtocolVersion = "2025-11-25";
inline constexpr std::string_view InitializeMethod = "initialize";
inline constexpr std::string_view InitializedMethod = "notifications/initialized";
inline constexpr std::string_view PingMethod = "ping";
inline constexpr std::string_view PromptsListMethod = "prompts/list";
inline constexpr std::string_view PromptsGetMethod = "prompts/get";
inline constexpr std::string_view ResourcesListMethod = "resources/list";
inline constexpr std::string_view ResourcesReadMethod = "resources/read";
inline constexpr std::string_view ResourcesTemplatesListMethod = "resources/templates/list";
inline constexpr std::string_view ResourcesSubscribeMethod = "resources/subscribe";
inline constexpr std::string_view ResourcesUnsubscribeMethod = "resources/unsubscribe";
inline constexpr std::string_view ToolsListMethod = "tools/list";
inline constexpr std::string_view ToolsGetMethod = "tools/get";
inline constexpr std::string_view ToolsCallMethod = "tools/call";
inline constexpr std::string_view CompletionCompleteMethod = "completion/complete";
inline constexpr std::string_view LoggingSetLevelMethod = "logging/setLevel";
inline constexpr std::string_view SamplingCreateMessageMethod = "sampling/createMessage";
inline constexpr std::string_view ElicitationCreateMethod = "elicitation/create";
inline constexpr std::string_view ElicitationCompleteNotificationMethod = "notifications/elicitation/complete";
inline constexpr std::string_view TasksListMethod = "tasks/list";
inline constexpr std::string_view TasksGetMethod = "tasks/get";
inline constexpr std::string_view TasksCancelMethod = "tasks/cancel";
inline constexpr std::string_view TasksResultMethod = "tasks/result";
inline constexpr std::string_view TasksStatusNotificationMethod = "notifications/tasks/status";
inline constexpr std::string_view RootsListMethod = "roots/list";
inline constexpr std::string_view CancelledNotificationMethod = "notifications/cancelled";
inline constexpr std::string_view ProgressNotificationMethod = "notifications/progress";
inline constexpr std::string_view RootsListChangedNotificationMethod = "notifications/roots/list_changed";
inline constexpr std::string_view ResourcesListChangedNotificationMethod = "notifications/resources/list_changed";
inline constexpr std::string_view ResourcesUpdatedNotificationMethod = "notifications/resources/updated";
inline constexpr std::string_view ToolsListChangedNotificationMethod = "notifications/tools/list_changed";
inline constexpr std::string_view PromptsListChangedNotificationMethod = "notifications/prompts/list_changed";
inline constexpr std::string_view LoggingMessageNotificationMethod = "notifications/message";

ErrorObject make_error(int code, std::string message, std::optional<Json> data = std::nullopt);
ErrorObject make_error(ErrorCode code, std::string message, std::optional<Json> data = std::nullopt);
JsonRpcResponse make_response(RequestId id, Json result);
JsonRpcResponse make_error_response(std::optional<RequestId> id, ErrorObject error);
JsonRpcRequest make_request(std::string method, RequestId id, Json params = Json::object());
JsonRpcNotification make_notification(std::string method, Json params = Json::object());
JsonRpcRequest make_initialize_request(RequestId id, Json params = Json::object());
JsonRpcNotification make_initialized_notification(Json params = Json::object());
JsonRpcRequest make_ping_request(RequestId id, Json params = Json::object());

core::Result<JsonRpcMessage> parse_message(std::string_view text);
core::Result<std::string> serialize_message(const JsonRpcMessage& message);

core::Result<JsonRpcRequest> parse_request(std::string_view text);
core::Result<JsonRpcResponse> parse_response(std::string_view text);
core::Result<JsonRpcNotification> parse_notification(std::string_view text);
core::Result<std::string> serialize_request(const JsonRpcRequest& request);
core::Result<std::string> serialize_response(const JsonRpcResponse& response);
core::Result<std::string> serialize_notification(const JsonRpcNotification& notification);
core::Result<std::string> serialize_error(const ErrorObject& error, std::optional<RequestId> id = std::nullopt);

} // namespace mcp::protocol
