#include "mcp/server/http_transport.hpp"

#include "mcp/protocol/serialization.hpp"

#include "httplib.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <unordered_map>
#include <type_traits>
#include <vector>

namespace mcp::server {
namespace {

constexpr std::string_view MethodHeader = "Mcp-Method";
constexpr std::string_view NameHeader = "Mcp-Name";
constexpr int HeaderMismatchCode = -32001;

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

std::string request_id_to_string(const protocol::RequestId& request_id) {
    return std::visit([](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
            return std::string("s:") + value;
        } else {
            return std::string("i:") + std::to_string(value);
        }
    }, request_id);
}

std::string message_to_sse_event(const std::optional<std::uint64_t>& event_id, const std::string& payload) {
    std::string event;
    if (event_id.has_value()) {
        event.append("id: ");
        event.append(std::to_string(*event_id));
        event.push_back('\n');
    }
    event.append("data: ");
    event.append(payload);
    event.append("\n\n");
    return event;
}

std::optional<std::string> header_name_from_request(const protocol::JsonRpcRequest& request) {
    if (!request.params.is_object()) {
        return std::nullopt;
    }

    const auto& params = request.params;
    if ((request.method == protocol::ToolsCallMethod || request.method == protocol::PromptsGetMethod) &&
        params.contains("name") && params.at("name").is_string()) {
        return params.at("name").get<std::string>();
    }

    if ((request.method == protocol::ResourcesReadMethod || request.method == protocol::ResourcesSubscribeMethod ||
         request.method == protocol::ResourcesUnsubscribeMethod) &&
        params.contains("uri") && params.at("uri").is_string()) {
        return params.at("uri").get<std::string>();
    }

    return std::nullopt;
}

core::Result<core::Unit> validate_protocol_version_header(const httplib::Request& request) {
    constexpr std::string_view VersionHeader = "MCP-Protocol-Version";
    if (!request.has_header(std::string(VersionHeader))) {
        return std::unexpected(make_transport_error(HeaderMismatchCode,
                                                    "http transport request missing MCP-Protocol-Version header"));
    }
    const auto version_header = request.get_header_value(std::string(VersionHeader));
    if (version_header != protocol::McpProtocolVersion) {
        return std::unexpected(make_transport_error(HeaderMismatchCode,
                                                    "http transport request MCP-Protocol-Version header mismatch",
                                                    version_header));
    }
    return core::Unit{};
}

core::Result<core::Unit> validate_post_headers(const httplib::Request& request,
                                               const protocol::JsonRpcRequest& rpc_request) {
    if (!request.has_header(std::string(MethodHeader))) {
        return std::unexpected(make_transport_error(HeaderMismatchCode,
                                                    "http transport request missing Mcp-Method header"));
    }
    const auto method_header = request.get_header_value(std::string(MethodHeader));
    if (method_header != rpc_request.method) {
        return std::unexpected(make_transport_error(HeaderMismatchCode,
                                                    "http transport request Mcp-Method header mismatch",
                                                    method_header));
    }

    const auto expected_name = header_name_from_request(rpc_request);
    if (expected_name.has_value()) {
        if (!request.has_header(std::string(NameHeader))) {
            return std::unexpected(make_transport_error(HeaderMismatchCode,
                                                        "http transport request missing Mcp-Name header"));
        }
        const auto name_header = request.get_header_value(std::string(NameHeader));
        if (name_header != *expected_name) {
            return std::unexpected(make_transport_error(HeaderMismatchCode,
                                                        "http transport request Mcp-Name header mismatch",
                                                        name_header));
        }
    }

    return core::Unit{};
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

core::Result<core::Unit> HttpTransport::start(RequestHandler handler, NotificationHandler notification_handler) {
    if (!handler) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                    "http transport handler is not configured"));
    }
    if (options_.listen_port <= 0 || options_.listen_port > 65535) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                    "http transport listen port must be configured",
                                                    std::to_string(options_.listen_port)));
    }

    {
        std::lock_guard lock(mutex_);
        stopped_ = false;
        server_ = std::make_unique<httplib::Server>();
    }

    server_->Post(options_.path,
                  [this, handler = std::move(handler), notification_handler = std::move(notification_handler)](
                      const httplib::Request& request,
                      httplib::Response& response) mutable {
        const auto protocol_version = validate_protocol_version_header(request);
        if (!protocol_version) {
            response.status = 400;
            write_error(response, protocol_version.error().code, protocol_version.error().message, std::nullopt);
            return;
        }

        std::string session_context_id;
        {
            std::lock_guard lock(mutex_);
            session_context_id = session_id_.empty() ? "http-session" : session_id_;
        }

        const auto message = protocol::parse_message(request.body);
        if (!message) {
            write_error(response, message.error().code, message.error().message);
            return;
        }

        if (const auto* notification = std::get_if<protocol::JsonRpcNotification>(&*message)) {
            const auto header_check = validate_post_headers(request, protocol::JsonRpcRequest{
                                                           .method = notification->method,
                                                           .params = notification->params,
                                                           .id = std::int64_t{0},
                                                       });
            if (!header_check) {
                response.status = 400;
                write_error(response,
                            header_check.error().code,
                            header_check.error().message,
                            std::nullopt);
                return;
            }
            if (notification_handler) {
                const auto handled = notification_handler(*notification, SessionContext{
                                                                       .session_id = session_context_id,
                                                                       .remote_address = request.remote_addr,
                                                                       .transport = this,
                                                                   });
                if (!handled) {
                    write_error(response, handled.error().code, handled.error().message);
                    return;
                }
            }
            response.status = 202;
            return;
        }

        if (const auto* rpc_response = std::get_if<protocol::JsonRpcResponse>(&*message)) {
            if (!rpc_response->id.has_value()) {
                write_error(response,
                            static_cast<int>(protocol::ErrorCode::InvalidRequest),
                            "http transport response requires an id");
                return;
            }

            std::shared_ptr<PendingRequest> pending;
            {
                std::lock_guard lock(mutex_);
                const auto it = pending_requests_.find(request_id_to_string(*rpc_response->id));
                if (it != pending_requests_.end()) {
                    pending = it->second;
                    pending_requests_.erase(it);
                }
            }

            if (!pending) {
                write_error(response,
                            static_cast<int>(protocol::ErrorCode::InvalidRequest),
                            "http transport received an unexpected response",
                            *rpc_response->id);
                return;
            }

            {
                std::lock_guard lock(pending->mutex);
                pending->response = *rpc_response;
                pending->ready = true;
            }
            pending->cv.notify_all();
            response.status = 202;
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

        const auto header_check = validate_post_headers(request, *rpc_request);
        if (!header_check) {
            response.status = 400;
            write_error(response, header_check.error().code, header_check.error().message, rpc_request->id);
            return;
        }

        auto rpc_response = handler(*rpc_request, SessionContext{
                                                      .session_id = session_context_id,
                                                      .remote_address = request.remote_addr,
                                                      .transport = this,
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

        if (rpc_request->method == protocol::InitializeMethod) {
            std::lock_guard guard(mutex_);
            if (rpc_request->params.is_object() && rpc_request->params.contains("capabilities")) {
                client_capabilities_ = protocol::client_capabilities_from_json(rpc_request->params.at("capabilities"));
            } else {
                client_capabilities_.reset();
            }
            if (session_id_.empty()) {
                session_id_ = session_context_id;
            }
            response.set_header("Mcp-Session-Id", session_id_);
        } else {
            std::lock_guard guard(mutex_);
            if (!session_id_.empty()) {
                response.set_header("Mcp-Session-Id", session_id_);
            }
        }

        write_response(response, *rpc_response);
    });

    server_->Get(options_.path,
                 [this](const httplib::Request& request, httplib::Response& response) {
        const auto protocol_version = validate_protocol_version_header(request);
        if (!protocol_version) {
            response.status = 400;
            write_error(response, protocol_version.error().code, protocol_version.error().message, std::nullopt);
            return;
        }

        const auto stream_session_id = request.get_header_value("Mcp-Session-Id");
        {
            std::lock_guard lock(mutex_);
            if (session_id_.empty() || stream_session_id != session_id_) {
                response.status = 404;
                return;
            }
        }

        response.set_chunked_content_provider("text/event-stream",
                                              [this, &request, stream_session_id](std::size_t, httplib::DataSink& sink) {
            while (true) {
                std::unique_lock lock(mutex_);
                if (stopped_ || request.is_connection_closed() || !sink.is_writable()) {
                    if (request.is_connection_closed()) {
                        abort_pending_requests("http event stream closed");
                    }
                    sink.done();
                    return false;
                }

                notification_cv_.wait_for(lock, std::chrono::milliseconds(100), [this, &sink, &request, stream_session_id]() {
                    return stopped_ || !pending_notifications_.empty() || !sink.is_writable() ||
                           request.is_connection_closed() || session_id_.empty() || session_id_ != stream_session_id;
                });

                if (stopped_ || request.is_connection_closed() || !sink.is_writable()) {
                    if (request.is_connection_closed()) {
                        abort_pending_requests("http event stream closed");
                    }
                    sink.done();
                    return false;
                }

                if (!pending_notifications_.empty()) {
                    auto event = std::move(pending_notifications_.front());
                    pending_notifications_.pop_front();
                    auto serialized_event = message_to_sse_event(event.event_id, event.payload);
                    lock.unlock();
                    return sink.write(serialized_event.data(), serialized_event.size());
                }
            }
        });
    });

    server_->Delete(options_.path,
                    [this](const httplib::Request& request, httplib::Response& response) {
        const auto protocol_version = validate_protocol_version_header(request);
        if (!protocol_version) {
            response.status = 400;
            write_error(response, protocol_version.error().code, protocol_version.error().message, std::nullopt);
            return;
        }

        std::lock_guard lock(mutex_);
        if (session_id_.empty() || request.get_header_value("Mcp-Session-Id") != session_id_) {
            response.status = 404;
            return;
        }

        abort_pending_requests("http session terminated");
        pending_notifications_.clear();
        next_notification_event_id_ = 1;
        client_capabilities_.reset();
        session_id_.clear();
        notification_cv_.notify_all();
        response.status = 204;
    });

    const auto listening = server_->listen(options_.listen_host, options_.listen_port);
    if (!listening) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "failed to listen for http transport",
                                                    options_.listen_host + ":" + std::to_string(options_.listen_port)));
    }

    return core::Unit{};
}

core::Result<core::Unit> HttpTransport::send_notification(const protocol::JsonRpcNotification& notification) {
    const auto serialized = protocol::serialize_notification(notification);
    if (!serialized) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "failed to serialize http notification"));
    }

    std::lock_guard lock(mutex_);
    if (stopped_) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "http transport is stopped"));
    }

    QueuedEvent event{
        .event_id = next_notification_event_id_++,
        .payload = *serialized,
    };
    pending_notifications_.push_back(std::move(event));
    notification_cv_.notify_all();
    return core::Unit{};
}

core::Result<protocol::JsonRpcResponse> HttpTransport::send_request(const protocol::JsonRpcRequest& request) {
    const auto serialized = protocol::serialize_request(request);
    if (!serialized) {
        return std::unexpected(serialized.error());
    }

    auto pending = std::make_shared<PendingRequest>();
    const auto request_key = request_id_to_string(request.id);
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                        "http transport is stopped"));
        }
        if (session_id_.empty()) {
            return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                                        "http transport has no active session"));
        }
        pending_requests_[request_key] = pending;
        pending_notifications_.push_back(QueuedEvent{
            .event_id = std::nullopt,
            .payload = *serialized,
        });
    }

    notification_cv_.notify_all();

    std::unique_lock lock(pending->mutex);
    pending->cv.wait(lock, [&] { return pending->ready; });
    if (pending->error.has_value()) {
        return std::unexpected(*pending->error);
    }

    return *pending->response;
}

std::optional<protocol::ClientCapabilities> HttpTransport::client_capabilities() const {
    std::lock_guard lock(mutex_);
    return client_capabilities_;
}

void HttpTransport::stop() noexcept {
    httplib::Server* server_to_stop = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        abort_pending_requests("http transport is stopped");
        pending_notifications_.clear();
        next_notification_event_id_ = 1;
        server_to_stop = server_.get();
    }
    notification_cv_.notify_all();
    if (server_to_stop) {
        server_to_stop->stop();
    }
}

void HttpTransport::abort_pending_requests(std::string message) {
    std::unordered_map<std::string, std::shared_ptr<PendingRequest>> pending_requests;
    pending_requests.swap(pending_requests_);
    const auto error_message = std::move(message);

    for (auto& [_, pending] : pending_requests) {
        std::lock_guard pending_lock(pending->mutex);
        pending->error = core::Error{
            static_cast<int>(protocol::ErrorCode::InternalError),
            error_message,
            {},
        };
        pending->ready = true;
        pending->cv.notify_all();
    }
}

std::string_view HttpTransport::name() const noexcept {
    return "http";
}

} // namespace mcp::server
