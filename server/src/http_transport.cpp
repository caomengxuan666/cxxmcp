#include "mcp/server/http_transport.hpp"

#include "mcp/protocol/serialization.hpp"

#include "httplib.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace mcp::server {
namespace {

core::Error make_transport_error(int code, std::string message, std::string detail = {}) {
    return core::Error{code, std::move(message), std::move(detail)};
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

void write_response(httplib::Response& http_response, const protocol::JsonRpcResponse& response) {
    const auto serialized = protocol::serialize_response(response);
    if (!serialized) {
        http_response.status = 500;
        http_response.set_content(serialized.error().message, "text/plain");
        return;
    }

    http_response.set_content(*serialized, "application/json");
}

void write_error(httplib::Response& http_response,
                 int code,
                 std::string message,
                 std::optional<protocol::RequestId> id = std::nullopt) {
    write_response(http_response,
                   protocol::make_error_response(std::move(id),
                                                 protocol::make_error(code, std::move(message))));
}

} // namespace

HttpTransport::HttpTransport(HttpTransportOptions options)
    : options_(std::move(options)) {
    if (options_.path.empty()) {
        options_.path = "/mcp";
    }
    if (!options_.path.starts_with('/')) {
        options_.path.insert(options_.path.begin(), '/');
    }
}

HttpTransport::~HttpTransport() = default;

core::Result<core::Unit> HttpTransport::start(RequestHandler handler) {
    if (!handler) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                    "http transport handler is not configured"));
    }
    if (options_.listen_port <= 0 || options_.listen_port > 65535) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                    "http transport listen port must be configured",
                                                    std::to_string(options_.listen_port)));
    }

    server_ = std::make_unique<httplib::Server>();
    server_->Post(options_.path, [handler = std::move(handler)](const httplib::Request& request,
                                                               httplib::Response& response) mutable {
        const auto message = protocol::parse_message(request.body);
        if (!message) {
            write_error(response, message.error().code, message.error().message);
            return;
        }

        if (std::holds_alternative<protocol::JsonRpcNotification>(*message)) {
            response.status = 204;
            return;
        }

        const auto* rpc_request = std::get_if<protocol::JsonRpcRequest>(&*message);
        if (rpc_request == nullptr) {
            write_error(response,
                        static_cast<int>(protocol::ErrorCode::InvalidRequest),
                        "http transport expected a JSON-RPC request",
                        request_id_from_message(*message));
            return;
        }

        auto rpc_response = handler(*rpc_request, SessionContext{
                                                      .session_id = "http",
                                                      .remote_address = request.remote_addr,
                                                  });
        if (!rpc_response) {
            rpc_response = protocol::make_error_response(std::optional<protocol::RequestId>{rpc_request->id},
                                                         protocol::make_error(rpc_response.error().code,
                                                                              rpc_response.error().message,
                                                                              rpc_response.error().detail.empty()
                                                                                  ? std::nullopt
                                                                                  : std::optional<protocol::Json>{
                                                                                        rpc_response.error().detail}));
        }

        write_response(response, *rpc_response);
    });

    const auto listening = server_->listen(options_.listen_host, options_.listen_port);
    if (!listening) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "failed to listen for http transport",
                                                    options_.listen_host + ":" + std::to_string(options_.listen_port)));
    }

    return core::Unit{};
}

void HttpTransport::stop() noexcept {
    if (server_) {
        server_->stop();
    }
}

std::string_view HttpTransport::name() const noexcept {
    return "http";
}

} // namespace mcp::server
