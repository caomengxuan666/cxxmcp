#include "mcp/client/http_transport.hpp"

#include "mcp/protocol/serialization.hpp"

#include "httplib.h"

#include <chrono>
#include <ctime>
#include <string>
#include <utility>

namespace mcp::client {

namespace {

core::Error make_transport_error(int code, std::string message, std::string detail = {}) {
    return core::Error{code, std::move(message), std::move(detail)};
}

httplib::Headers to_headers(const std::unordered_map<std::string, std::string>& headers) {
    httplib::Headers result;
    for (const auto& [key, value] : headers) {
        result.emplace(key, value);
    }
    return result;
}

void apply_timeout(httplib::Client& client, std::chrono::milliseconds timeout) {
    const auto sec = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto usec = std::chrono::duration_cast<std::chrono::microseconds>(timeout - sec);
    client.set_connection_timeout(sec.count(), static_cast<time_t>(usec.count()));
    client.set_read_timeout(sec.count(), static_cast<time_t>(usec.count()));
    client.set_write_timeout(sec.count(), static_cast<time_t>(usec.count()));
}

core::Result<protocol::JsonRpcResponse> decode_response(const httplib::Result& response) {
    if (!response) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "http transport request failed",
                                                    httplib::to_string(response.error())));
    }

    if (response->body.empty()) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "http transport returned an empty response body"));
    }

    const auto parsed = protocol::parse_response(response->body);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    return *parsed;
}

} // namespace

HttpTransport::HttpTransport(HttpTransportOptions options)
    : options_(std::move(options)) {
    if (options_.path.empty()) {
        options_.path = "/";
    }
}

core::Result<protocol::JsonRpcResponse> HttpTransport::send(const protocol::JsonRpcRequest& request) {
    httplib::Client client(options_.host, options_.port);
    apply_timeout(client, options_.timeout);

    const auto serialized = protocol::serialize_request(request);
    if (!serialized) {
        return std::unexpected(serialized.error());
    }

    auto headers = to_headers(options_.headers);
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Accept", "application/json");

    const auto response = client.Post(options_.path, headers, *serialized, "application/json");
    return decode_response(response);
}

core::Result<core::Unit> HttpTransport::send_notification(const protocol::JsonRpcNotification& notification) {
    httplib::Client client(options_.host, options_.port);
    apply_timeout(client, options_.timeout);

    const auto serialized = protocol::serialize_notification(notification);
    if (!serialized) {
        return std::unexpected(serialized.error());
    }

    auto headers = to_headers(options_.headers);
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Accept", "application/json");

    const auto response = client.Post(options_.path, headers, *serialized, "application/json");
    if (!response) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "http transport notification failed",
                                                    httplib::to_string(response.error())));
    }

    if (response->status < 200 || response->status >= 300) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "http transport notification returned an error status",
                                                    std::to_string(response->status)));
    }

    return core::Unit{};
}

} // namespace mcp::client
