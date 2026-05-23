#include "mcp/protocol/serialization.hpp"

#include "jsonrpcpp.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace mcp::protocol {

namespace {

core::Error make_protocol_error(int code, std::string message, std::string detail = {}) {
    return core::Error{code, std::move(message), std::move(detail)};
}

core::Error translate_rpc_error(const jsonrpcpp::Error& error) {
    core::Error translated;
    translated.code = error.code();
    translated.message = error.message();
    if (!error.data().is_null()) {
        translated.detail = error.data().dump();
    }
    return translated;
}

core::Error translate_exception(const jsonrpcpp::RpcEntityException& exception) {
    return translate_rpc_error(exception.error());
}

core::Result<Json> parse_json_document(std::string_view text) {
    const auto document = Json::parse(text.begin(), text.end(), nullptr, false);
    if (document.is_discarded()) {
        return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::ParseError), "Invalid JSON"));
    }

    if (!document.is_object()) {
        return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                   "JSON-RPC message must be an object"));
    }

    return document;
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

core::Result<JsonRpcRequest> from_request(const jsonrpcpp::Request& request) {
    JsonRpcRequest message;
    message.method = request.method();
    message.params = request.params().to_json();
    const auto id = from_rpc_id(request.id());
    if (!id.has_value()) {
        return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                   "Request id must not be null"));
    }
    message.id = *id;
    return message;
}

JsonRpcNotification from_notification(const jsonrpcpp::Notification& notification) {
    JsonRpcNotification message;
    message.method = notification.method();
    message.params = notification.params().to_json();
    return message;
}

JsonRpcResponse from_response(const jsonrpcpp::Response& response) {
    JsonRpcResponse message;
    message.id = from_rpc_id(response.id());
    if (response.error()) {
        message.error = ErrorObject{
            .code = response.error().code(),
            .message = response.error().message(),
            .data = response.error().data().is_null() ? std::optional<Json>{} : std::optional<Json>{response.error().data()},
        };
    } else {
        message.result = response.result();
    }
    return message;
}

std::string dump_json(const jsonrpcpp::Entity& entity) {
    return entity.to_json().dump();
}

} // namespace

ErrorObject make_error(int code, std::string message, std::optional<Json> data) {
    return ErrorObject{code, std::move(message), std::move(data)};
}

ErrorObject make_error(ErrorCode code, std::string message, std::optional<Json> data) {
    return make_error(static_cast<int>(code), std::move(message), std::move(data));
}

JsonRpcResponse make_response(RequestId id, Json result) {
    JsonRpcResponse response;
    response.id = std::move(id);
    response.result = std::move(result);
    return response;
}

JsonRpcResponse make_error_response(std::optional<RequestId> id, ErrorObject error) {
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
    return make_request(std::string(InitializeMethod), std::move(id), std::move(params));
}

JsonRpcNotification make_initialized_notification(Json params) {
    return make_notification(std::string(InitializedMethod), std::move(params));
}

JsonRpcRequest make_ping_request(RequestId id, Json params) {
    return make_request(std::string(PingMethod), std::move(id), std::move(params));
}

core::Result<JsonRpcMessage> parse_message(std::string_view text) {
    try {
        const auto document = parse_json_document(text);
        if (!document) {
            return std::unexpected(document.error());
        }

        if (document->contains("result") && document->contains("error")) {
            return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                       "Response must contain exactly one of result or error"));
        }

        const auto entity = jsonrpcpp::Parser::do_parse_json(*document);
        if (!entity) {
            return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                       "JSON-RPC parser returned no entity"));
        }

        if (entity->is_request()) {
            const auto request = std::dynamic_pointer_cast<jsonrpcpp::Request>(entity);
            if (!request) {
                return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                           "JSON-RPC request could not be decoded"));
            }
            const auto converted = from_request(*request);
            if (!converted) {
                return std::unexpected(converted.error());
            }
            return JsonRpcMessage{*converted};
        }

        if (entity->is_notification()) {
            const auto notification = std::dynamic_pointer_cast<jsonrpcpp::Notification>(entity);
            if (!notification) {
                return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                           "JSON-RPC notification could not be decoded"));
            }
            return JsonRpcMessage{from_notification(*notification)};
        }

        if (entity->is_response()) {
            const auto response = std::dynamic_pointer_cast<jsonrpcpp::Response>(entity);
            if (!response) {
                return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                           "JSON-RPC response could not be decoded"));
            }
            return JsonRpcMessage{from_response(*response)};
        }

        if (entity->is_batch()) {
            return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                       "Batch messages are not supported"));
        }

        return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                   "Unsupported JSON-RPC entity"));
    } catch (const jsonrpcpp::ParseErrorException& exception) {
        return std::unexpected(translate_exception(exception));
    } catch (const jsonrpcpp::InvalidRequestException& exception) {
        return std::unexpected(translate_exception(exception));
    } catch (const jsonrpcpp::RpcEntityException& exception) {
        return std::unexpected(translate_exception(exception));
    } catch (const std::exception& exception) {
        return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InternalError), exception.what()));
    }
}

core::Result<std::string> serialize_message(const JsonRpcMessage& message) {
    return std::visit(
        [](const auto& value) -> core::Result<std::string> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, JsonRpcRequest>) {
                const auto rpc = jsonrpcpp::Request(to_rpc_id(value.id), value.method, jsonrpcpp::Parameter(value.params));
                return dump_json(rpc);
            } else if constexpr (std::is_same_v<T, JsonRpcResponse>) {
                if (value.error.has_value()) {
                    const auto error = jsonrpcpp::Error(value.error->message, value.error->code,
                                                        value.error->data.value_or(Json(nullptr)));
                    const auto rpc = jsonrpcpp::Response(to_rpc_id(value.id), error);
                    return dump_json(rpc);
                }

                if (!value.result.has_value()) {
                    return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                                               "Response must contain exactly one of result or error"));
                }

                const auto rpc = jsonrpcpp::Response(to_rpc_id(value.id), *value.result);
                return dump_json(rpc);
            } else {
                const auto rpc = jsonrpcpp::Notification(value.method, jsonrpcpp::Parameter(value.params));
                return dump_json(rpc);
            }
        },
        message);
}

core::Result<JsonRpcRequest> parse_request(std::string_view text) {
    const auto message = parse_message(text);
    if (!message) {
        return std::unexpected(message.error());
    }

    if (const auto* request = std::get_if<JsonRpcRequest>(&*message)) {
        return *request;
    }

    return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                               "JSON-RPC message is not a request"));
}

core::Result<JsonRpcResponse> parse_response(std::string_view text) {
    const auto message = parse_message(text);
    if (!message) {
        return std::unexpected(message.error());
    }

    if (const auto* response = std::get_if<JsonRpcResponse>(&*message)) {
        return *response;
    }

    return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                               "JSON-RPC message is not a response"));
}

core::Result<JsonRpcNotification> parse_notification(std::string_view text) {
    const auto message = parse_message(text);
    if (!message) {
        return std::unexpected(message.error());
    }

    if (const auto* notification = std::get_if<JsonRpcNotification>(&*message)) {
        return *notification;
    }

    return std::unexpected(make_protocol_error(static_cast<int>(ErrorCode::InvalidRequest),
                                               "JSON-RPC message is not a notification"));
}

core::Result<std::string> serialize_request(const JsonRpcRequest& request) {
    return serialize_message(JsonRpcMessage{request});
}

core::Result<std::string> serialize_response(const JsonRpcResponse& response) {
    return serialize_message(JsonRpcMessage{response});
}

core::Result<std::string> serialize_notification(const JsonRpcNotification& notification) {
    return serialize_message(JsonRpcMessage{notification});
}

core::Result<std::string> serialize_error(const ErrorObject& error, std::optional<RequestId> id) {
    return serialize_response(make_error_response(std::move(id), error));
}

} // namespace mcp::protocol
