// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/protocol/serialization.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "jsonrpcpp.hpp"

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

core::Error translate_rpc_error(const jsonrpcpp::Error& error) {
  core::Error translated;
  translated.code = error.code();
  translated.message = error.message();
  translated.category = "protocol";
  if (!error.data().is_null()) {
    translated.detail = error.data().dump();
  }
  return translated;
}

core::Error translate_exception(
    const jsonrpcpp::RpcEntityException& exception) {
  return translate_rpc_error(exception.error());
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
  if (!document.contains("jsonrpc")) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "JSON-RPC version must be 2.0"));
  }
  if (!document.at("jsonrpc").is_string() ||
      document.at("jsonrpc").get<std::string>() != JsonRpcVersion) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "JSON-RPC version must be 2.0"));
  }
  return core::Unit{};
}

jsonrpcpp::Id to_rpc_id(const RequestId& id) {
  return std::visit([](const auto& value) { return jsonrpcpp::Id(value); }, id);
}

jsonrpcpp::Id to_rpc_id(const std::optional<RequestId>& id) {
  if (!id.has_value()) {
    return jsonrpcpp::Id();
  }
  return to_rpc_id(*id);
}

core::Result<std::optional<Json>> meta_from_document(const Json& document) {
  if (!document.contains("_meta")) {
    return std::optional<Json>{};
  }
  if (!document.at("_meta").is_object()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "_meta must be an object"));
  }
  return std::optional<Json>{document.at("_meta")};
}

Json normalized_params(Json params) {
  if (params.is_null()) {
    return Json::object();
  }
  return params;
}

core::Result<std::optional<Json>> meta_from_params(const Json& params) {
  if (!params.is_object() || !params.contains("_meta")) {
    return std::optional<Json>{};
  }
  if (!params.at("_meta").is_object()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "params _meta must be an object"));
  }
  return std::optional<Json>{params.at("_meta")};
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
  append_json_property(output, first, "id", request_id_to_json(request.id));
  append_string_property(output, first, "jsonrpc", JsonRpcVersion);
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
  if (response.id.has_value()) {
    append_json_property(output, first, "id", request_id_to_json(*response.id));
  } else {
    append_json_property(output, first, "id", nullptr);
  }
  append_string_property(output, first, "jsonrpc", JsonRpcVersion);
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

std::optional<RequestId> from_rpc_id(const jsonrpcpp::Id& id) {
  using ValueType = jsonrpcpp::Id::value_t;

  switch (id.type()) {
    case ValueType::string:
      return id.string_id();
    case ValueType::integer:
      return static_cast<std::int64_t>(id.int_id());
    case ValueType::null:
    default:
      return std::nullopt;
  }
}

core::Result<JsonRpcRequest> from_request(const jsonrpcpp::Request& request,
                                          const Json& document) {
  (void)document;
  JsonRpcRequest message;
  message.method = request.method();
  message.params = normalized_params(request.params().to_json());
  const auto meta = meta_from_params(message.params);
  if (!meta) {
    return mcp::core::unexpected(meta.error());
  }
  message.meta = *meta;
  const auto id = from_rpc_id(request.id());
  if (!id.has_value()) {
    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "Request id must not be null"));
  }
  message.id = *id;
  return message;
}

core::Result<JsonRpcNotification> from_notification(
    const jsonrpcpp::Notification& notification, const Json& document) {
  (void)document;
  JsonRpcNotification message;
  message.method = notification.method();
  message.params = normalized_params(notification.params().to_json());
  const auto meta = meta_from_params(message.params);
  if (!meta) {
    return mcp::core::unexpected(meta.error());
  }
  message.meta = *meta;
  return message;
}

core::Result<JsonRpcResponse> from_response(const jsonrpcpp::Response& response,
                                            const Json& document) {
  JsonRpcResponse message;
  message.id = from_rpc_id(response.id());
  const auto meta = meta_from_document(document);
  if (!meta) {
    return mcp::core::unexpected(meta.error());
  }
  message.meta = *meta;
  if (response.error()) {
    ErrorObject error;
    error.code = response.error().code();
    error.message = response.error().message();
    error.data = response.error().data().is_null()
                     ? std::optional<Json>{}
                     : std::optional<Json>{response.error().data()};
    message.error = std::move(error);
  } else {
    message.result = response.result();
  }
  return message;
}

std::string dump_json(const jsonrpcpp::Entity& entity) {
  return entity.to_json().dump();
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
  return make_request(std::string(InitializeMethod), std::move(id),
                      std::move(params));
}

JsonRpcNotification make_initialized_notification(Json params) {
  return make_notification(std::string(InitializedMethod), std::move(params));
}

JsonRpcRequest make_ping_request(RequestId id, Json params) {
  return make_request(std::string(PingMethod), std::move(id),
                      std::move(params));
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

    if (document->contains("result") && document->contains("error")) {
      return mcp::core::unexpected(make_protocol_error(
          static_cast<int>(ErrorCode::InvalidRequest),
          "Response must contain exactly one of result or error"));
    }

    const auto entity = jsonrpcpp::Parser::do_parse_json(*document);
    if (!entity) {
      return mcp::core::unexpected(
          make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                              "JSON-RPC parser returned no entity"));
    }

    if (entity->is_request()) {
      const auto request =
          std::dynamic_pointer_cast<jsonrpcpp::Request>(entity);
      if (!request) {
        return mcp::core::unexpected(
            make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                "JSON-RPC request could not be decoded"));
      }
      const auto converted = from_request(*request, *document);
      if (!converted) {
        return mcp::core::unexpected(converted.error());
      }
      return JsonRpcMessage{*converted};
    }

    if (entity->is_notification()) {
      const auto notification =
          std::dynamic_pointer_cast<jsonrpcpp::Notification>(entity);
      if (!notification) {
        return mcp::core::unexpected(
            make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                "JSON-RPC notification could not be decoded"));
      }
      const auto converted = from_notification(*notification, *document);
      if (!converted) {
        return mcp::core::unexpected(converted.error());
      }
      return JsonRpcMessage{*converted};
    }

    if (entity->is_response()) {
      const auto response =
          std::dynamic_pointer_cast<jsonrpcpp::Response>(entity);
      if (!response) {
        return mcp::core::unexpected(
            make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                "JSON-RPC response could not be decoded"));
      }
      const auto converted = from_response(*response, *document);
      if (!converted) {
        return mcp::core::unexpected(converted.error());
      }
      return JsonRpcMessage{*converted};
    }

    if (entity->is_batch()) {
      return mcp::core::unexpected(
          make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                              "Batch messages are not supported"));
    }

    return mcp::core::unexpected(
        make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                            "Unsupported JSON-RPC entity"));
  } catch (const jsonrpcpp::ParseErrorException& exception) {
    return mcp::core::unexpected(translate_exception(exception));
  } catch (const jsonrpcpp::InvalidRequestException& exception) {
    return mcp::core::unexpected(translate_exception(exception));
  } catch (const jsonrpcpp::RpcEntityException& exception) {
    return mcp::core::unexpected(translate_exception(exception));
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
