#include "mcp/client/stdio_transport.hpp"

#include "mcp/protocol/serialization.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <type_traits>
#include <variant>

namespace mcp::client {

namespace {

core::Error make_transport_error(int code, std::string message, std::string detail = {}) {
    return core::Error{code, std::move(message), std::move(detail)};
}

std::string request_id_to_string(const protocol::RequestId& request_id) {
    return std::visit([](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
            return value;
        } else {
            return std::to_string(value);
        }
    }, request_id);
}

core::Result<core::Unit> write_response(std::ostream& output, const protocol::JsonRpcResponse& response) {
    const auto serialized = protocol::serialize_response(response);
    if (!serialized) {
        return std::unexpected(serialized.error());
    }

    output << *serialized << '\n';
    output.flush();
    if (!output.good()) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "failed to write stdio response"));
    }

    return core::Unit{};
}

} // namespace

StdioTransport::StdioTransport()
    : StdioTransport(std::cin, std::cout) {}

StdioTransport::StdioTransport(std::istream& input, std::ostream& output)
    : input_(&input),
      output_(&output) {}

core::Result<core::Unit> StdioTransport::start(TransportRequestHandler request_handler,
                                               TransportNotificationHandler notification_handler) {
    request_handler_ = std::move(request_handler);
    notification_handler_ = std::move(notification_handler);
    started_ = true;
    return core::Unit{};
}

core::Result<protocol::JsonRpcResponse> StdioTransport::send(const protocol::JsonRpcRequest& request) {
    if (!input_ || !output_) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "stdio transport streams are not configured"));
    }

    const auto serialized = protocol::serialize_request(request);
    if (!serialized) {
        return std::unexpected(serialized.error());
    }

    *output_ << *serialized << '\n';
    output_->flush();
    if (!output_->good()) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "failed to write stdio request"));
    }

    if (!started_) {
        std::string line;
        if (!std::getline(*input_, line)) {
            return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                        "failed to read stdio response"));
        }

        return protocol::parse_response(line);
    }

    std::string line;
    while (std::getline(*input_, line)) {
        if (line.empty()) {
            continue;
        }

        const auto message = protocol::parse_message(line);
        if (!message) {
            return std::unexpected(message.error());
        }

        if (const auto* notification = std::get_if<protocol::JsonRpcNotification>(&*message)) {
            if (notification_handler_) {
                const auto handled = notification_handler_(*notification);
                if (!handled) {
                    return std::unexpected(handled.error());
                }
            }
            continue;
        }

        if (const auto* incoming_request = std::get_if<protocol::JsonRpcRequest>(&*message)) {
            if (!request_handler_) {
                return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::MethodNotFound),
                                                            "stdio transport request handler is not configured",
                                                            incoming_request->method));
            }

            auto handled = request_handler_(*incoming_request);
            if (!handled) {
                handled = protocol::make_error_response(std::optional<protocol::RequestId>{incoming_request->id},
                                                        protocol::make_error(handled.error().code,
                                                                             handled.error().message,
                                                                             handled.error().detail.empty()
                                                                                 ? std::nullopt
                                                                                 : std::optional<protocol::Json>{
                                                                                       handled.error().detail}));
            }

            const auto written = write_response(*output_, *handled);
            if (!written) {
                return std::unexpected(written.error());
            }
            continue;
        }

        const auto* response = std::get_if<protocol::JsonRpcResponse>(&*message);
        if (!response) {
            return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                        "stdio transport received an unknown message"));
        }

        if (response->id.has_value() && *response->id == request.id) {
            return *response;
        }
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                    "stdio transport received an unexpected response",
                                                    response->id.has_value() ? request_id_to_string(*response->id)
                                                                             : std::string{}));
    }

    return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                "failed to read stdio response"));
}

core::Result<core::Unit> StdioTransport::send_notification(const protocol::JsonRpcNotification& notification) {
    if (!output_) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "stdio transport output stream is not configured"));
    }

    const auto serialized = protocol::serialize_notification(notification);
    if (!serialized) {
        return std::unexpected(serialized.error());
    }

    *output_ << *serialized << '\n';
    output_->flush();
    if (!output_->good()) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "failed to write stdio notification"));
    }

    return core::Unit{};
}

} // namespace mcp::client
