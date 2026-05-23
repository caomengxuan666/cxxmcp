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
using ProgressToken = std::variant<std::int64_t, std::string>;

struct CancelledNotificationParams {
    RequestId request_id;
    std::string reason;
};

struct ProgressNotificationParams {
    ProgressToken progress_token;
    double progress = 0.0;
    std::optional<double> total;
    std::string message;
};

inline Json request_id_to_json(const RequestId& id) {
    return std::visit([](const auto& value) { return Json(value); }, id);
}

inline std::optional<RequestId> request_id_from_json(const Json& json) {
    if (json.is_number_integer()) {
        return static_cast<std::int64_t>(json.get<std::int64_t>());
    }
    if (json.is_string()) {
        return json.get<std::string>();
    }
    return std::nullopt;
}

inline Json progress_token_to_json(const ProgressToken& token) {
    return std::visit([](const auto& value) { return Json(value); }, token);
}

inline std::optional<ProgressToken> progress_token_from_json(const Json& json) {
    if (json.is_number_integer()) {
        return static_cast<std::int64_t>(json.get<std::int64_t>());
    }
    if (json.is_string()) {
        return json.get<std::string>();
    }
    return std::nullopt;
}

inline Json cancelled_notification_params_to_json(const CancelledNotificationParams& params) {
    Json json = Json::object();
    json["requestId"] = request_id_to_json(params.request_id);
    if (!params.reason.empty()) {
        json["reason"] = params.reason;
    }
    return json;
}

inline std::optional<CancelledNotificationParams> cancelled_notification_params_from_json(const Json& json) {
    if (!json.is_object() || !json.contains("requestId")) {
        return std::nullopt;
    }
    const auto request_id = request_id_from_json(json.at("requestId"));
    if (!request_id.has_value()) {
        return std::nullopt;
    }
    CancelledNotificationParams params;
    params.request_id = *request_id;
    if (json.contains("reason")) {
        if (!json.at("reason").is_string()) {
            return std::nullopt;
        }
        params.reason = json.at("reason").get<std::string>();
    }
    return params;
}

inline Json progress_notification_params_to_json(const ProgressNotificationParams& params) {
    Json json = Json::object();
    json["progressToken"] = progress_token_to_json(params.progress_token);
    json["progress"] = params.progress;
    if (params.total.has_value()) {
        json["total"] = *params.total;
    }
    if (!params.message.empty()) {
        json["message"] = params.message;
    }
    return json;
}

inline std::optional<ProgressNotificationParams> progress_notification_params_from_json(const Json& json) {
    if (!json.is_object() || !json.contains("progressToken") || !json.contains("progress")) {
        return std::nullopt;
    }
    const auto progress_token = progress_token_from_json(json.at("progressToken"));
    if (!progress_token.has_value() || !json.at("progress").is_number()) {
        return std::nullopt;
    }
    ProgressNotificationParams params;
    params.progress_token = *progress_token;
    params.progress = json.at("progress").get<double>();
    if (json.contains("total")) {
        if (!json.at("total").is_number()) {
            return std::nullopt;
        }
        params.total = json.at("total").get<double>();
    }
    if (json.contains("message")) {
        if (!json.at("message").is_string()) {
            return std::nullopt;
        }
        params.message = json.at("message").get<std::string>();
    }
    return params;
}

} // namespace mcp::protocol
