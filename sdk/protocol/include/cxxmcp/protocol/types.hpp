// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/types.hpp
/// @brief Shared JSON, JSON-RPC, error, cancellation, and progress model types.
///
/// These declarations describe the transport-level envelopes that carry MCP
/// method payloads. Feature-specific headers define the `params` and `result`
/// objects used inside these JSON-RPC messages.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace mcp::protocol {

/// @brief JSON value type used by all protocol DTOs.
using Json = nlohmann::json;

/// @brief Returns true for finite JSON floating-point values accepted by MCP.
inline bool protocol_number_is_finite(double value) noexcept {
  return std::isfinite(value);
}

/// @brief Protocol `_meta` object used by request params, results, and
/// notifications.
///
/// The SDK keeps metadata as JSON for forward compatibility, while helper
/// functions below provide typed access to well-known MCP metadata members.
using Meta = Json;

/// @brief Shared pagination params for MCP list requests that use `cursor`.
struct PaginatedRequestParams {
  /// Optional cursor from a prior list result.
  std::optional<std::string> cursor;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

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
///
/// Aligned with the MCP 2025-11-25 specification for standard error codes.
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
  ResourceNotFound = -32002,
  /// The caller is not allowed to perform the requested operation.
  PermissionDenied = -32005,
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
using JsonRpcMessage =
    std::variant<JsonRpcRequest, JsonRpcResponse, JsonRpcNotification>;

/// @brief Preferred icon variant for clients with light or dark surfaces.
enum class IconTheme {
  /// Icon intended for a light UI theme.
  Light,
  /// Icon intended for a dark UI theme.
  Dark,
};

/// @brief Icon descriptor used by tools, resources, resource templates, and
/// prompts.
struct Icon {
  /// Required icon source URI or data URI.
  std::string src;
  /// Optional MIME type such as `image/png` or `image/svg+xml`.
  std::string mime_type;
  /// Optional size hints such as `16x16`, `32x32`, or `any`.
  std::vector<std::string> sizes;
  /// Optional theme specialization.
  std::optional<IconTheme> theme;

  /// @brief Creates an icon with the required source field.
  static Icon from_src(std::string value) {
    Icon icon;
    icon.src = std::move(value);
    return icon;
  }

  /// @brief Sets the optional MIME type on an lvalue icon.
  Icon& with_mime_type(std::string value) & {
    mime_type = std::move(value);
    return *this;
  }

  /// @brief Sets the optional MIME type while preserving fluent temporary use.
  Icon&& with_mime_type(std::string value) && {
    mime_type = std::move(value);
    return std::move(*this);
  }

  /// @brief Sets optional size hints on an lvalue icon.
  Icon& with_sizes(std::vector<std::string> values) & {
    sizes = std::move(values);
    return *this;
  }

  /// @brief Sets optional size hints while preserving fluent temporary use.
  Icon&& with_sizes(std::vector<std::string> values) && {
    sizes = std::move(values);
    return std::move(*this);
  }

  /// @brief Sets the optional theme on an lvalue icon.
  Icon& with_theme(IconTheme value) & {
    theme = value;
    return *this;
  }

  /// @brief Sets the optional theme while preserving fluent temporary use.
  Icon&& with_theme(IconTheme value) && {
    theme = value;
    return std::move(*this);
  }
};

/// @brief Identifier used to associate progress notifications with a request.
using ProgressToken = std::variant<std::int64_t, std::string>;

/// @brief Parameters for `notifications/cancelled`.
struct CancelledNotificationParams {
  /// Id of the JSON-RPC request being cancelled.
  RequestId request_id;
  /// Optional human-readable cancellation reason.
  std::optional<std::string> reason;
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
  std::optional<std::string> message;
};

/// @brief Converts a RequestId to the JSON scalar used by JSON-RPC.
/// @param id Request id to encode.
/// @return A JSON integer or string.
inline Json request_id_to_json(const RequestId& id) {
  return std::visit([](const auto& value) { return Json(value); }, id);
}

/// @brief Parses a JSON-RPC request id.
/// @param json JSON value to inspect.
/// @return A RequestId when the value is an integer or string; otherwise
/// nullopt.
inline std::optional<RequestId> request_id_from_json(const Json& json) {
  if (json.is_number_integer()) {
    return static_cast<std::int64_t>(json.get<std::int64_t>());
  }
  if (json.is_string()) {
    return json.get<std::string>();
  }
  return std::nullopt;
}

/// @brief Converts an icon theme enum to the lowercase wire value.
inline std::string_view icon_theme_to_string(IconTheme theme) noexcept {
  switch (theme) {
    case IconTheme::Light:
      return "light";
    case IconTheme::Dark:
      return "dark";
  }
  return "light";
}

/// @brief Parses a lowercase icon theme wire value.
inline std::optional<IconTheme> icon_theme_from_string(
    std::string_view value) noexcept {
  if (value == "light") {
    return IconTheme::Light;
  }
  if (value == "dark") {
    return IconTheme::Dark;
  }
  return std::nullopt;
}

/// @brief Converts a progress token to the JSON scalar form used in metadata.
/// @param token Progress token to encode.
/// @return A JSON integer or string.
inline Json progress_token_to_json(const ProgressToken& token) {
  return std::visit([](const auto& value) { return Json(value); }, token);
}

/// @brief Parses a progress token from JSON.
/// @param json JSON value to inspect.
/// @return A ProgressToken when the value is an integer or string; otherwise
/// nullopt.
inline std::optional<ProgressToken> progress_token_from_json(const Json& json) {
  if (json.is_number_integer()) {
    return static_cast<std::int64_t>(json.get<std::int64_t>());
  }
  if (json.is_string()) {
    return json.get<std::string>();
  }
  return std::nullopt;
}

/// @brief Returns true when a value is a valid protocol `_meta` object.
inline bool meta_is_object(const Json& meta) noexcept {
  return meta.is_object();
}

/// @brief Returns true when a JSON object key is part of a typed DTO shape.
inline bool json_key_is_known(
    std::string_view key, std::initializer_list<std::string_view> known_keys) {
  for (const auto known : known_keys) {
    if (key == known) {
      return true;
    }
  }
  return false;
}

/// @brief Collects unknown object members so typed DTOs can preserve future
/// protocol fields and vendor extensions.
inline Json collect_json_extensions(
    const Json& json, std::initializer_list<std::string_view> known_keys) {
  Json extensions = Json::object();
  if (!json.is_object()) {
    return extensions;
  }
  for (const auto& item : json.items()) {
    if (!json_key_is_known(item.key(), known_keys)) {
      extensions[item.key()] = item.value();
    }
  }
  return extensions;
}

/// @brief Overload accepting a vector of string keys.
inline Json collect_json_extensions(
    const Json& json, const std::vector<std::string>& known_keys) {
  Json extensions = Json::object();
  if (!json.is_object()) {
    return extensions;
  }
  for (const auto& item : json.items()) {
    bool found = false;
    for (const auto& key : known_keys) {
      if (item.key() == key) {
        found = true;
        break;
      }
    }
    if (!found) {
      extensions[item.key()] = item.value();
    }
  }
  return extensions;
}

/// @brief Flattens extension members into a JSON object without overwriting
/// typed fields.
inline void append_json_extensions(Json& json, const Json& extensions) {
  if (!json.is_object() || !extensions.is_object()) {
    return;
  }
  for (const auto& item : extensions.items()) {
    if (!json.contains(item.key())) {
      json[item.key()] = item.value();
    }
  }
}

/// @brief Serializes shared pagination params for MCP list requests.
inline Json paginated_request_params_to_json(
    const PaginatedRequestParams& params) {
  Json json = Json::object();
  if (params.cursor.has_value()) {
    json["cursor"] = *params.cursor;
  }
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  append_json_extensions(json, params.extensions);
  return json;
}

/// @brief Parses shared pagination params for MCP list requests.
///
/// Returns nullopt when `params` is not an object, `cursor` is not a string, or
/// `_meta` is present but is not an object.
inline std::optional<PaginatedRequestParams> paginated_request_params_from_json(
    const Json& json) {
  if (json.is_null()) {
    return PaginatedRequestParams{};
  }
  if (!json.is_object()) {
    return std::nullopt;
  }

  PaginatedRequestParams params;
  if (json.contains("cursor")) {
    if (!json.at("cursor").is_string()) {
      return std::nullopt;
    }
    params.cursor = json.at("cursor").get<std::string>();
  }
  if (json.contains("_meta")) {
    if (!meta_is_object(json.at("_meta"))) {
      return std::nullopt;
    }
    params.meta = json.at("_meta");
  }
  params.extensions = collect_json_extensions(json, {"cursor", "_meta"});
  return params;
}

/// @brief Creates a metadata object carrying a progress token.
inline Meta meta_with_progress_token(ProgressToken token) {
  Meta meta = Meta::object();
  meta["progressToken"] = progress_token_to_json(token);
  return meta;
}

/// @brief Reads `progressToken` from a metadata object.
///
/// Invalid metadata shapes or invalid token values return nullopt.
inline std::optional<ProgressToken> meta_progress_token(const Json& meta) {
  if (!meta.is_object() || !meta.contains("progressToken")) {
    return std::nullopt;
  }
  return progress_token_from_json(meta.at("progressToken"));
}

/// @brief Sets `progressToken` on an existing metadata object.
/// @return False if `meta` is not an object.
inline bool set_meta_progress_token(Json& meta, ProgressToken token) {
  if (!meta.is_object()) {
    return false;
  }
  meta["progressToken"] = progress_token_to_json(token);
  return true;
}

}  // namespace mcp::protocol
