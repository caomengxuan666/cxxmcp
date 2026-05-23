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
inline constexpr std::string_view InitializedMethod = "initialized";
inline constexpr std::string_view PingMethod = "ping";

ErrorObject make_error(int code, std::string message, std::optional<Json> data = std::nullopt);
ErrorObject make_error(ErrorCode code, std::string message, std::optional<Json> data = std::nullopt);
JsonRpcResponse make_response(RequestId id, Json result);
JsonRpcResponse make_error_response(std::optional<RequestId> id, ErrorObject error);
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
