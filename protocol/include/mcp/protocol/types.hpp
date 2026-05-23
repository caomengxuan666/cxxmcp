#pragma once

#include <cstdint>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace mcp::protocol {

using Json = nlohmann::json;
using RequestId = std::variant<std::int64_t, std::string>;

enum class ErrorCode : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    ToolNotFound = -32000,
    ResourceNotFound = -32001,
    PermissionDenied = -32002,
    RateLimited = -32003,
};

struct ErrorObject {
    int code = static_cast<int>(ErrorCode::InternalError);
    std::string message;
    std::optional<Json> data;
};

struct JsonRpcRequest {
    std::string method;
    Json params = Json::object();
    RequestId id;
};

struct JsonRpcResponse {
    std::optional<RequestId> id;
    std::optional<Json> result;
    std::optional<ErrorObject> error;

    bool has_result() const noexcept {
        return result.has_value() && !error.has_value();
    }

    bool has_error() const noexcept {
        return error.has_value() && !result.has_value();
    }
};

struct JsonRpcNotification {
    std::string method;
    Json params = Json::object();
};

using JsonRpcMessage = std::variant<JsonRpcRequest, JsonRpcResponse, JsonRpcNotification>;

} // namespace mcp::protocol
