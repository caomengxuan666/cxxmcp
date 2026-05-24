#pragma once

/// @file cxxmcp/protocol/types.hpp
/// @brief Shared JSON, JSON-RPC, error, cancellation, and progress model types.
///
/// These declarations describe the transport-level envelopes that carry MCP
/// method payloads. Feature-specific headers define the `params` and `result`
/// objects used inside these JSON-RPC messages.

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace mcp::protocol {

    /// @brief JSON value type used by all protocol DTOs.
    using Json = nlohmann::json;

    /// @brief JSON-RPC request or response identifier.
    ///
    /// JSON-RPC permits string and integer ids. Notifications intentionally do not
    /// carry a RequestId because they never receive a response.
    using RequestId = std::variant<std::int64_t, std::string>;

    /// @brief JSON-RPC and MCP-specific error codes used in ErrorObject.
    ///
    /// Values in the JSON-RPC reserved range keep their standard meaning. Negative
    /// MCP-specific values represent SDK-level protocol failures such as missing
    /// tools, permission failures, or URL elicitation requirements.
    enum class ErrorCode : int {
    /// Invalid JSON text was received.
    ParseError = -32700,
    /// The JSON value was not a valid JSON-RPC request.
    InvalidRequest = -32600,
    /// The JSON-RPC method name is unknown to the peer.
    MethodNotFound = -32601,
    /// The request `params` object failed protocol validation.
    InvalidParams = -32602,
    /// The peer failed while handling an otherwise valid request.
    InternalError = -32603,
    /// The named tool does not exist or is not available.
    ToolNotFound = -32000,
    /// The requested resource URI does not exist or is not available.
    ResourceNotFound = -32001,
    /// The caller is not allowed to perform the requested operation.
    PermissionDenied = -32002,
    /// The request was rejected by rate limiting policy.
    RateLimited = -32003,
    /// The operation requires URL-based elicitation before it can continue.
    UrlElicitationRequired = -32042,
};

    /// @brief JSON-RPC error object.
    struct ErrorObject {
        /// Numeric JSON-RPC error code.
        int code = static_cast<int>(ErrorCode::InternalError);
        /// Human-readable diagnostic message.
        std::string message;
        /// Optional structured error details for machine handling.
        std::optional<Json> data;
    };

    /// @brief JSON-RPC request envelope carrying an MCP method invocation.
    struct JsonRpcRequest {
        /// JSON-RPC method name such as `tools/call` or `initialize`.
        std::string method;
        /// Method-specific `params` object. Empty means the request has no fields.
        Json params = Json::object();
        /// Request id that must be echoed by the response.
        RequestId id;
        /// Optional protocol metadata, serialized outside the feature payload.
        std::optional<Json> meta;
    };

    /// @brief JSON-RPC response envelope for either success or failure.
    struct JsonRpcResponse {
        /// Response id matching the request id, or absent for parse-level failures.
        std::optional<RequestId> id;
        /// Successful method result. Mutually exclusive with `error`.
        std::optional<Json> result;
        /// JSON-RPC error object. Mutually exclusive with `result`.
        std::optional<ErrorObject> error;
        /// Optional protocol metadata associated with the response.
        std::optional<Json> meta;

        /// @brief Returns true when this response contains a successful result.
        bool has_result() const noexcept {
            return result.has_value() && !error.has_value();
        }

        /// @brief Returns true when this response contains an error object.
        bool has_error() const noexcept {
            return error.has_value() && !result.has_value();
        }
    };

    /// @brief JSON-RPC notification envelope for one-way MCP messages.
    struct JsonRpcNotification {
        /// Notification method name such as `notifications/progress`.
        std::string method;
        /// Method-specific notification `params` object.
        Json params = Json::object();
        /// Optional protocol metadata associated with the notification.
        std::optional<Json> meta;
    };

    /// @brief Variant over the JSON-RPC message shapes accepted by MCP transports.
    using JsonRpcMessage = std::variant<JsonRpcRequest, JsonRpcResponse, JsonRpcNotification>;

    /// @brief Identifier used to associate progress notifications with a request.
    using ProgressToken = std::variant<std::int64_t, std::string>;

    /// @brief Parameters for `notifications/cancelled`.
    struct CancelledNotificationParams {
        /// Id of the JSON-RPC request being cancelled.
        RequestId request_id;
        /// Optional human-readable cancellation reason.
        std::string reason;
    };

    /// @brief Parameters for `notifications/progress`.
    struct ProgressNotificationParams {
        /// Token supplied by the original request metadata.
        ProgressToken progress_token;
        /// Current progress value.
        double progress = 0.0;
        /// Optional total value using the same unit as `progress`.
        std::optional<double> total;
        /// Optional human-readable status text.
        std::string message;
    };

    /// @brief Converts a RequestId to the JSON scalar used by JSON-RPC.
    /// @param id Request id to encode.
    /// @return A JSON integer or string.
    inline Json request_id_to_json(const RequestId &id) {
        return std::visit([](const auto &value) { return Json(value); }, id);
    }

    /// @brief Parses a JSON-RPC request id.
    /// @param json JSON value to inspect.
    /// @return A RequestId when the value is an integer or string; otherwise nullopt.
    inline std::optional<RequestId> request_id_from_json(const Json &json) {
        if (json.is_number_integer()) {
            return static_cast<std::int64_t>(json.get<std::int64_t>());
        }
        if (json.is_string()) {
            return json.get<std::string>();
        }
        return std::nullopt;
    }

    /// @brief Converts a progress token to the JSON scalar form used in metadata.
    /// @param token Progress token to encode.
    /// @return A JSON integer or string.
    inline Json progress_token_to_json(const ProgressToken &token) {
        return std::visit([](const auto &value) { return Json(value); }, token);
    }

    /// @brief Parses a progress token from JSON.
    /// @param json JSON value to inspect.
    /// @return A ProgressToken when the value is an integer or string; otherwise nullopt.
    inline std::optional<ProgressToken> progress_token_from_json(const Json &json) {
        if (json.is_number_integer()) {
            return static_cast<std::int64_t>(json.get<std::int64_t>());
        }
        if (json.is_string()) {
            return json.get<std::string>();
        }
        return std::nullopt;
    }

    /// @brief Serializes cancellation notification parameters.
    /// @param params Cancellation payload to encode.
    /// @return JSON object suitable for `notifications/cancelled` params.
    inline Json cancelled_notification_params_to_json(const CancelledNotificationParams &params) {
        Json json = Json::object();
        json["requestId"] = request_id_to_json(params.request_id);
        if (!params.reason.empty()) {
            json["reason"] = params.reason;
        }
        return json;
    }

    /// @brief Parses cancellation notification parameters.
    /// @param json JSON value to validate.
    /// @return Parsed parameters, or nullopt when required fields are invalid.
    inline std::optional<CancelledNotificationParams> cancelled_notification_params_from_json(const Json &json) {
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

    /// @brief Serializes progress notification parameters.
    /// @param params Progress payload to encode.
    /// @return JSON object suitable for `notifications/progress` params.
    inline Json progress_notification_params_to_json(const ProgressNotificationParams &params) {
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

    /// @brief Parses progress notification parameters.
    /// @param json JSON value to validate.
    /// @return Parsed parameters, or nullopt when required fields are invalid.
    inline std::optional<ProgressNotificationParams> progress_notification_params_from_json(const Json &json) {
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

}// namespace mcp::protocol
