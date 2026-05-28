// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/protocol/serialization.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace mcp::protocol {

namespace {

constexpr std::size_t MaxJsonDepth = 128;
constexpr std::size_t MaxJsonAggregateStringBytes = 16 * 1024 * 1024;
constexpr std::size_t MaxJsonAggregateCollectionEntries = 100000;
constexpr std::size_t MaxJsonNodeCount = 200000;

core::Error make_protocol_error(int code, std::string message,
                                std::string detail = {}) {
  return core::Error{code, std::move(message), std::move(detail), "protocol"};
}

core::Result<core::Unit> validate_json_document_limits(const Json& document) {
  struct Frame {
    const Json* value;
    std::size_t depth;
  };

  std::vector<Frame> stack;
  stack.push_back(Frame{&document, 1});
  std::size_t nodes = 0;
  std::size_t aggregate_string_bytes = 0;
  std::size_t aggregate_collection_entries = 0;

  while (!stack.empty()) {
    const auto frame = stack.back();
    stack.pop_back();
    if (frame.depth > MaxJsonDepth) {
      return mcp::core::unexpected(
          make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                              "JSON-RPC message exceeds maximum JSON depth"));
    }
    if (++nodes > MaxJsonNodeCount) {
      return mcp::core::unexpected(
          make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                              "JSON-RPC message exceeds maximum JSON nodes"));
    }

    if (frame.value->is_string()) {
      aggregate_string_bytes +=
          frame.value->get_ref<const std::string&>().size();
      if (aggregate_string_bytes > MaxJsonAggregateStringBytes) {
        return mcp::core::unexpected(make_protocol_error(
            static_cast<int>(ErrorCode::InvalidRequest),
            "JSON-RPC message exceeds maximum aggregate string size"));
      }
      continue;
    }

    if (frame.value->is_array()) {
      aggregate_collection_entries += frame.value->size();
      if (aggregate_collection_entries > MaxJsonAggregateCollectionEntries) {
        return mcp::core::unexpected(make_protocol_error(
            static_cast<int>(ErrorCode::InvalidRequest),
            "JSON-RPC message exceeds maximum aggregate collection entries"));
      }
      for (const auto& item : *frame.value) {
        stack.push_back(Frame{&item, frame.depth + 1});
      }
      continue;
    }

    if (frame.value->is_object()) {
      aggregate_collection_entries += frame.value->size();
      if (aggregate_collection_entries > MaxJsonAggregateCollectionEntries) {
        return mcp::core::unexpected(make_protocol_error(
            static_cast<int>(ErrorCode::InvalidRequest),
            "JSON-RPC message exceeds maximum aggregate collection entries"));
      }
      for (auto it = frame.value->begin(); it != frame.value->end(); ++it) {
        aggregate_string_bytes += it.key().size();
        if (aggregate_string_bytes > MaxJsonAggregateStringBytes) {
          return mcp::core::unexpected(make_protocol_error(
              static_cast<int>(ErrorCode::InvalidRequest),
              "JSON-RPC message exceeds maximum aggregate string size"));
        }
        stack.push_back(Frame{&it.value(), frame.depth + 1});
      }
    }
  }

  return core::Unit{};
}

core::Result<Json> parse_json_document(std::string_view text) {
  const auto document = Json::parse(text.begin(), text.end(), nullptr, false);
  if (document.is_discarded()) {
    return mcp::core::unexpected(make_protocol_error(
        static_cast<int>(ErrorCode::ParseError), "Invalid JSON"));
  }

  if (document.is_array()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "Batch messages are not supported"));
  }

  if (!document.is_object()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "JSON-RPC message must be an object"));
  }

  const auto limits = validate_json_document_limits(document);
  if (!limits) {
    return mcp::core::unexpected(limits.error());
  }

  return document;
}

core::Result<core::Unit> validate_jsonrpc_version(const Json& document) {
  if (!document.contains(std::string("jsonrpc"))) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "JSON-RPC version must be 2.0"));
  }
  const auto& jsonrpc = document.at(std::string("jsonrpc"));
  if (!jsonrpc.is_string() || jsonrpc.get<std::string>() != JsonRpcVersion) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "JSON-RPC version must be 2.0"));
  }
  return core::Unit{};
}

bool has_member(const Json& document, std::string_view name) {
  return document.find(std::string(name)) != document.end();
}

core::Result<std::optional<Json>> meta_from_document(const Json& document) {
  if (!has_member(document, "_meta")) {
    return std::optional<Json>{};
  }
  const auto& meta = document.at(std::string("_meta"));
  if (!meta.is_object()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "_meta must be an object"));
  }
  return std::optional<Json>{meta};
}

Json normalized_params(Json params) {
  if (params.is_null()) {
    return Json::object();
  }
  return params;
}

core::Result<Json> params_from_document_or_empty(const Json& document) {
  if (!has_member(document, "params")) {
    return Json::object();
  }
  const auto params = normalized_params(document.at(std::string("params")));
  if (!params.is_object() && !params.is_array()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "params must be an object or array"));
  }
  return params;
}

core::Result<std::optional<Json>> meta_from_params(const Json& params) {
  if (!params.is_object() || !has_member(params, "_meta")) {
    return std::optional<Json>{};
  }
  const auto& meta = params.at(std::string("_meta"));
  if (!meta.is_object()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "params _meta must be an object"));
  }
  return std::optional<Json>{meta};
}

core::Result<Json> params_with_meta(Json params,
                                    const std::optional<Json>& meta) {
  params = normalized_params(std::move(params));
  if (!meta.has_value()) {
    return params;
  }
  if (!params.is_object()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "params must be an object when _meta is present"));
  }
  params["_meta"] = *meta;
  return params;
}

bool should_serialize_params(const Json& params) {
  return !params.is_object() || !params.empty();
}

const Json& empty_object_json() {
  static const Json value = Json::object();
  return value;
}

const Json& normalized_params_ref(const Json& params) {
  if (params.is_null()) {
    return empty_object_json();
  }
  return params;
}

void append_separator(std::string& output, bool& first) {
  if (!first) {
    output.push_back(',');
  }
  first = false;
}

void append_json_property(std::string& output, bool& first,
                          std::string_view name, const Json& value) {
  append_separator(output, first);
  output += Json(std::string(name)).dump();
  output.push_back(':');
  output += value.dump();
}

void append_string_property(std::string& output, bool& first,
                            std::string_view name, std::string_view value) {
  append_separator(output, first);
  output += Json(std::string(name)).dump();
  output.push_back(':');
  output += Json(std::string(value)).dump();
}

void append_int_property(std::string& output, bool& first,
                         std::string_view name, int value) {
  append_separator(output, first);
  output += Json(std::string(name)).dump();
  output.push_back(':');
  output += std::to_string(value);
}

core::Result<core::Unit> append_params_property(
    std::string& output, bool& first, const Json& params,
    const std::optional<Json>& meta) {
  if (!meta.has_value()) {
    const Json& normalized = normalized_params_ref(params);
    if (should_serialize_params(normalized)) {
      append_json_property(output, first, "params", normalized);
    }
    return core::Unit{};
  }

  Json merged = normalized_params(params);
  if (!merged.is_object()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "params must be an object when _meta is present"));
  }
  merged["_meta"] = *meta;
  append_json_property(output, first, "params", merged);
  return core::Unit{};
}

std::string serialize_error_object(const ErrorObject& error) {
  std::string output;
  output.reserve(64 + error.message.size());
  output.push_back('{');
  bool first = true;
  append_int_property(output, first, "code", error.code);
  append_string_property(output, first, "message", error.message);
  if (error.data.has_value()) {
    append_json_property(output, first, "data", *error.data);
  }
  output.push_back('}');
  return output;
}

core::Result<std::string> serialize_request_direct(
    const JsonRpcRequest& request) {
  std::string output;
  output.reserve(64 + request.method.size());
  output.push_back('{');
  bool first = true;
  append_string_property(output, first, "jsonrpc", JsonRpcVersion);
  append_json_property(output, first, "id", request_id_to_json(request.id));
  append_string_property(output, first, "method", request.method);
  const auto params =
      append_params_property(output, first, request.params, request.meta);
  if (!params.has_value()) {
    return mcp::core::unexpected(params.error());
  }
  output.push_back('}');
  return output;
}

core::Result<std::string> serialize_response_direct(
    const JsonRpcResponse& response) {
  std::string output;
  output.reserve(96);
  output.push_back('{');
  bool first = true;
  append_string_property(output, first, "jsonrpc", JsonRpcVersion);
  if (response.id.has_value()) {
    append_json_property(output, first, "id", request_id_to_json(*response.id));
  } else {
    append_json_property(output, first, "id", nullptr);
  }
  if (response.meta.has_value()) {
    append_json_property(output, first, "_meta", *response.meta);
  }
  if (response.error.has_value()) {
    append_separator(output, first);
    output += Json("error").dump();
    output.push_back(':');
    output += serialize_error_object(*response.error);
    output.push_back('}');
    return output;
  }

  if (!response.result.has_value()) {
    return mcp::core::unexpected(make_protocol_error(
        static_cast<int>(ErrorCode::InvalidRequest),
        "Response must contain exactly one of result or error"));
  }

  append_json_property(output, first, "result", *response.result);
  output.push_back('}');
  return output;
}

core::Result<std::string> serialize_notification_direct(
    const JsonRpcNotification& notification) {
  std::string output;
  output.reserve(48 + notification.method.size());
  output.push_back('{');
  bool first = true;
  append_string_property(output, first, "jsonrpc", JsonRpcVersion);
  append_string_property(output, first, "method", notification.method);
  const auto params = append_params_property(output, first, notification.params,
                                             notification.meta);
  if (!params.has_value()) {
    return mcp::core::unexpected(params.error());
  }
  output.push_back('}');
  return output;
}

core::Result<RequestId> required_request_id_from_document(
    const Json& document) {
  if (!has_member(document, "id")) {
    return mcp::core::unexpected(make_protocol_error(
        static_cast<int>(ErrorCode::InvalidRequest), "id is missing"));
  }
  const auto id = request_id_from_json(document.at(std::string("id")));
  if (!id.has_value()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "Request id must be an integer or string"));
  }
  return *id;
}

core::Result<std::optional<RequestId>> response_id_from_document(
    const Json& document) {
  if (!has_member(document, "id")) {
    return mcp::core::unexpected(make_protocol_error(
        static_cast<int>(ErrorCode::InvalidRequest), "id is missing"));
  }
  const auto& id_json = document.at(std::string("id"));
  if (id_json.is_null()) {
    return std::optional<RequestId>{};
  }
  const auto id = request_id_from_json(id_json);
  if (!id.has_value()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "Response id must be null, integer, or string"));
  }
  return std::optional<RequestId>{*id};
}

core::Result<ErrorObject> error_object_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "error must be an object"));
  }
  if (!has_member(json, "code") ||
      !json.at(std::string("code")).is_number_integer()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "error code is missing or invalid"));
  }
  if (!has_member(json, "message") ||
      !json.at(std::string("message")).is_string()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "error message is missing or invalid"));
  }

  ErrorObject error;
  error.code = json.at(std::string("code")).get<int>();
  error.message = json.at(std::string("message")).get<std::string>();
  if (has_member(json, "data")) {
    error.data = json.at(std::string("data"));
  }
  return error;
}

core::Result<JsonRpcRequest> request_from_document(const Json& document) {
  JsonRpcRequest message;
  const auto& method = document.at(std::string("method"));
  if (!method.is_string()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "method must be a string value"));
  }
  message.method = method.get<std::string>();
  if (message.method.empty()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "method must not be empty"));
  }
  const auto params = params_from_document_or_empty(document);
  if (!params) {
    return mcp::core::unexpected(params.error());
  }
  message.params = *params;
  const auto meta = meta_from_params(message.params);
  if (!meta) {
    return mcp::core::unexpected(meta.error());
  }
  message.meta = *meta;
  const auto id = required_request_id_from_document(document);
  if (!id) {
    return mcp::core::unexpected(id.error());
  }
  message.id = *id;
  return message;
}

core::Result<JsonRpcNotification> notification_from_document(
    const Json& document) {
  JsonRpcNotification message;
  const auto& method = document.at(std::string("method"));
  if (!method.is_string()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "method must be a string value"));
  }
  message.method = method.get<std::string>();
  if (message.method.empty()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "method must not be empty"));
  }
  const auto params = params_from_document_or_empty(document);
  if (!params) {
    return mcp::core::unexpected(params.error());
  }
  message.params = *params;
  const auto meta = meta_from_params(message.params);
  if (!meta) {
    return mcp::core::unexpected(meta.error());
  }
  message.meta = *meta;
  return message;
}

core::Result<JsonRpcResponse> response_from_document(const Json& document) {
  JsonRpcResponse message;
  const auto id = response_id_from_document(document);
  if (!id) {
    return mcp::core::unexpected(id.error());
  }
  message.id = *id;
  const auto meta = meta_from_document(document);
  if (!meta) {
    return mcp::core::unexpected(meta.error());
  }
  message.meta = *meta;
  if (has_member(document, "error")) {
    const auto error =
        error_object_from_json(document.at(std::string("error")));
    if (!error) {
      return mcp::core::unexpected(error.error());
    }
    message.error = *error;
  } else {
    message.result = document.at(std::string("result"));
  }
  return message;
}

}  // namespace

ErrorObject make_error(int code, std::string message,
                       std::optional<Json> data) {
  return ErrorObject{code, std::move(message), std::move(data)};
}

ErrorObject make_error(ErrorCode code, std::string message,
                       std::optional<Json> data) {
  return make_error(static_cast<int>(code), std::move(message),
                    std::move(data));
}

JsonRpcResponse make_response(RequestId id, Json result) {
  JsonRpcResponse response;
  response.id = std::move(id);
  response.result = std::move(result);
  return response;
}

JsonRpcResponse make_error_response(std::optional<RequestId> id,
                                    ErrorObject error) {
  JsonRpcResponse response;
  response.id = std::move(id);
  response.error = std::move(error);
  return response;
}

JsonRpcRequest make_request(std::string method, RequestId id, Json params) {
  JsonRpcRequest request;
  request.method = std::move(method);
  request.params = std::move(params);
  request.id = std::move(id);
  return request;
}

JsonRpcNotification make_notification(std::string method, Json params) {
  JsonRpcNotification notification;
  notification.method = std::move(method);
  notification.params = std::move(params);
  return notification;
}

JsonRpcRequest make_initialize_request(RequestId id, Json params) {
  return make_request(InitializeMethod, std::move(id), std::move(params));
}

JsonRpcNotification make_initialized_notification(Json params) {
  return make_notification(InitializedMethod, std::move(params));
}

JsonRpcRequest make_ping_request(RequestId id, Json params) {
  return make_request(PingMethod, std::move(id), std::move(params));
}

core::Result<JsonRpcMessage> parse_message(std::string_view text) {
  try {
    const auto document = parse_json_document(text);
    if (!document) {
      return mcp::core::unexpected(document.error());
    }

    const auto version = validate_jsonrpc_version(*document);
    if (!version) {
      return mcp::core::unexpected(version.error());
    }

    if (has_member(*document, "result") && has_member(*document, "error")) {
      return mcp::core::unexpected(make_protocol_error(
          static_cast<int>(ErrorCode::InvalidRequest),
          "Response must contain exactly one of result or error"));
    }

    if (has_member(*document, "method")) {
      if (has_member(*document, "id")) {
        const auto converted = request_from_document(*document);
        if (!converted) {
          return mcp::core::unexpected(converted.error());
        }
        return JsonRpcMessage{*converted};
      }

      const auto converted = notification_from_document(*document);
      if (!converted) {
        return mcp::core::unexpected(converted.error());
      }
      return JsonRpcMessage{*converted};
    }

    if (has_member(*document, "result") || has_member(*document, "error")) {
      const auto converted = response_from_document(*document);
      if (!converted) {
        return mcp::core::unexpected(converted.error());
      }
      return JsonRpcMessage{*converted};
    }

    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "Unsupported JSON-RPC entity"));
  } catch (const std::exception& exception) {
    return mcp::core::unexpected(make_protocol_error(
        static_cast<int>(ErrorCode::InternalError), exception.what()));
  }
}

core::Result<std::string> serialize_message(const JsonRpcMessage& message) {
  return std::visit(
      [](const auto& value) -> core::Result<std::string> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, JsonRpcRequest>) {
          return serialize_request_direct(value);
        } else if constexpr (std::is_same_v<T, JsonRpcResponse>) {
          return serialize_response_direct(value);
        } else {
          return serialize_notification_direct(value);
        }
      },
      message);
}

core::Result<JsonRpcRequest> parse_request(std::string_view text) {
  const auto message = parse_message(text);
  if (!message) {
    return mcp::core::unexpected(message.error());
  }

  if (const auto* request = std::get_if<JsonRpcRequest>(&*message)) {
    return *request;
  }

  return mcp::core::unexpected(
      make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                          "JSON-RPC message is not a request"));
}

core::Result<JsonRpcResponse> parse_response(std::string_view text) {
  const auto message = parse_message(text);
  if (!message) {
    return mcp::core::unexpected(message.error());
  }

  if (const auto* response = std::get_if<JsonRpcResponse>(&*message)) {
    return *response;
  }

  return mcp::core::unexpected(
      make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                          "JSON-RPC message is not a response"));
}

core::Result<JsonRpcNotification> parse_notification(std::string_view text) {
  const auto message = parse_message(text);
  if (!message) {
    return mcp::core::unexpected(message.error());
  }

  if (const auto* notification = std::get_if<JsonRpcNotification>(&*message)) {
    return *notification;
  }

  return mcp::core::unexpected(
      make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                          "JSON-RPC message is not a notification"));
}

core::Result<std::string> serialize_request(const JsonRpcRequest& request) {
  return serialize_request_direct(request);
}

core::Result<std::string> serialize_response(const JsonRpcResponse& response) {
  return serialize_response_direct(response);
}

core::Result<std::string> serialize_notification(
    const JsonRpcNotification& notification) {
  return serialize_notification_direct(notification);
}

core::Result<std::string> serialize_error(const ErrorObject& error,
                                          std::optional<RequestId> id) {
  return serialize_response(make_error_response(std::move(id), error));
}

}  // namespace mcp::protocol
