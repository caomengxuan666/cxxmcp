#include "mcp/server/stdio_transport.hpp"

#include "mcp/protocol/serialization.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace mcp::server {

namespace {

core::Error make_transport_error(int code, std::string message, std::string detail = {}) {
    return core::Error{code, std::move(message), std::move(detail)};
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

core::Result<core::Unit> write_error(std::ostream& output,
                                     int code,
                                     std::string message,
                                     std::optional<protocol::RequestId> id = std::nullopt) {
    return write_response(output,
                          protocol::make_error_response(std::move(id),
                                                        protocol::make_error(code, std::move(message))));
}

std::optional<protocol::RequestId> request_id_from_message(const protocol::JsonRpcMessage& message) {
    if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
        return request->id;
    }

    if (const auto* response = std::get_if<protocol::JsonRpcResponse>(&message)) {
        return response->id;
    }

    return std::nullopt;
}

} // namespace

StdioTransport::StdioTransport()
    : StdioTransport(std::cin, std::cout) {}

StdioTransport::StdioTransport(std::istream& input, std::ostream& output)
    : input_(&input),
      output_(&output) {}

core::Result<core::Unit> StdioTransport::start(RequestHandler handler) {
    if (!input_ || !output_) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "stdio transport streams are not configured"));
    }

    if (!handler) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                    "stdio transport handler is not configured"));
    }

    running_ = true;

    std::string line;
    while (running_ && std::getline(*input_, line)) {
        if (line.empty()) {
            continue;
        }

        const auto message = protocol::parse_message(line);
        if (!message) {
            const auto written = write_error(*output_, message.error().code, message.error().message);
            if (!written) {
                running_ = false;
                return std::unexpected(written.error());
            }
            continue;
        }

        if (std::holds_alternative<protocol::JsonRpcNotification>(*message)) {
            continue;
        }

        const auto* request = std::get_if<protocol::JsonRpcRequest>(&*message);
        if (!request) {
            const auto written = write_error(*output_,
                                             static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                             "stdio transport expected a JSON-RPC request",
                                             request_id_from_message(*message));
            if (!written) {
                running_ = false;
                return std::unexpected(written.error());
            }
            continue;
        }

        auto response = handler(*request, SessionContext{
                                              .session_id = "stdio",
                                              .remote_address = "stdio",
                                          });
        if (!response) {
            response = protocol::make_error_response(std::optional<protocol::RequestId>{request->id},
                                                     protocol::make_error(response.error().code,
                                                                          response.error().message,
                                                                          response.error().detail.empty()
                                                                              ? std::nullopt
                                                                              : std::optional<protocol::Json>{response.error().detail}));
        }

        const auto written = write_response(*output_, *response);
        if (!written) {
            running_ = false;
            return std::unexpected(written.error());
        }
    }

    running_ = false;
    if (input_->bad()) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "failed to read stdio request"));
    }

    return core::Unit{};
}

void StdioTransport::stop() noexcept {
    running_ = false;
}

std::string_view StdioTransport::name() const noexcept {
    return "stdio";
}

} // namespace mcp::server
