// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/server/http_transport.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/transport/http_transport.hpp"
#include "httplib.h"

namespace mcp::server {
namespace {

constexpr std::string_view SessionHeader = "Mcp-Session-Id";
constexpr std::string_view ProtocolVersionHeader = "MCP-Protocol-Version";
constexpr std::string_view MethodHeader = "Mcp-Method";
constexpr std::string_view NameHeader = "Mcp-Name";
constexpr std::string_view LastEventIdHeader = "Last-Event-ID";
constexpr int HeaderMismatchCode = -32001;

core::Error make_transport_error(int code, std::string message,
                                 std::string detail = {}) {
  return core::Error{code, std::move(message), std::move(detail), "transport"};
}

std::optional<protocol::RequestId> request_id_from_message(
    const protocol::JsonRpcMessage& message) {
  if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
    return request->id;
  }

  if (const auto* response = std::get_if<protocol::JsonRpcResponse>(&message)) {
    return response->id;
  }

  return std::nullopt;
}

std::string request_id_to_string(const protocol::RequestId& request_id) {
  return std::visit(
      [](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return std::string("s:") + value;
        } else {
          return std::string("i:") + std::to_string(value);
        }
      },
      request_id);
}

std::string message_to_sse_event(const std::optional<std::uint64_t>& event_id,
                                 const std::string& payload) {
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

std::string sse_priming_event(std::chrono::milliseconds retry) {
  std::string event;
  event.append("id: 0\n");
  event.append("retry: ");
  event.append(std::to_string(retry.count()));
  event.append("\n\n");
  return event;
}

core::Result<std::optional<std::uint64_t>> parse_last_event_id_header(
    const httplib::Request& request) {
  if (!request.has_header(std::string(LastEventIdHeader))) {
    return std::optional<std::uint64_t>{};
  }

  const auto value = request.get_header_value(std::string(LastEventIdHeader));
  if (value.empty()) {
    return std::optional<std::uint64_t>{};
  }
  if (!std::all_of(value.begin(), value.end(),
                   [](char ch) { return ch >= '0' && ch <= '9'; })) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport Last-Event-ID header must be numeric", value));
  }

  try {
    return static_cast<std::uint64_t>(std::stoull(value));
  } catch (const std::out_of_range&) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport Last-Event-ID header is out of range", value));
  }
}

std::optional<std::string> header_name_from_request(
    const protocol::JsonRpcRequest& request) {
  if (!request.params.is_object()) {
    return std::nullopt;
  }

  const auto& params = request.params;
  if ((request.method == protocol::ToolsCallMethod ||
       request.method == protocol::PromptsGetMethod) &&
      params.contains("name") && params.at("name").is_string()) {
    return params.at("name").get<std::string>();
  }

  if ((request.method == protocol::ResourcesReadMethod ||
       request.method == protocol::ResourcesSubscribeMethod ||
       request.method == protocol::ResourcesUnsubscribeMethod) &&
      params.contains("uri") && params.at("uri").is_string()) {
    return params.at("uri").get<std::string>();
  }

  return std::nullopt;
}

core::Result<core::Unit> validate_protocol_version_header(
    const httplib::Request& request) {
  constexpr std::string_view VersionHeader = "MCP-Protocol-Version";
  if (!request.has_header(std::string(VersionHeader))) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport request missing MCP-Protocol-Version header"));
  }
  const auto version_header =
      request.get_header_value(std::string(VersionHeader));
  if (!protocol::is_supported_protocol_version(version_header)) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport request MCP-Protocol-Version header mismatch",
        version_header));
  }
  return core::Unit{};
}

core::Result<core::Unit> validate_origin_header(
    const httplib::Request& request,
    const std::vector<std::string>& allowed_origins) {
  if (allowed_origins.empty() || !request.has_header("Origin")) {
    return core::Unit{};
  }

  const auto origin = request.get_header_value("Origin");
  if (std::find(allowed_origins.begin(), allowed_origins.end(), origin) ==
      allowed_origins.end()) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport request Origin header is not allowed", origin));
  }

  return core::Unit{};
}

core::Result<core::Unit> validate_post_headers(
    const httplib::Request& request,
    const protocol::JsonRpcRequest& rpc_request) {
  if (!request.has_header(std::string(MethodHeader))) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport request missing Mcp-Method header"));
  }
  const auto method_header =
      request.get_header_value(std::string(MethodHeader));
  if (method_header != rpc_request.method) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode, "http transport request Mcp-Method header mismatch",
        method_header));
  }

  const auto expected_name = header_name_from_request(rpc_request);
  if (expected_name.has_value()) {
    if (!request.has_header(std::string(NameHeader))) {
      return std::unexpected(make_transport_error(
          HeaderMismatchCode,
          "http transport request missing Mcp-Name header"));
    }
    const auto name_header = request.get_header_value(std::string(NameHeader));
    if (name_header != *expected_name) {
      return std::unexpected(make_transport_error(
          HeaderMismatchCode, "http transport request Mcp-Name header mismatch",
          name_header));
    }
  }

  return core::Unit{};
}

core::Result<core::Unit> validate_initialize_protocol_header(
    const httplib::Request& request,
    const protocol::JsonRpcRequest& rpc_request) {
  if (!request.has_header(std::string(ProtocolVersionHeader))) {
    return core::Unit{};
  }

  if (!rpc_request.params.is_object() ||
      !rpc_request.params.contains("protocolVersion") ||
      !rpc_request.params.at("protocolVersion").is_string()) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport initialize request missing protocolVersion"));
  }

  const auto header_value =
      request.get_header_value(std::string(ProtocolVersionHeader));
  const auto body_value =
      rpc_request.params.at("protocolVersion").get<std::string>();
  if (header_value != body_value) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport request MCP-Protocol-Version header mismatch",
        header_value));
  }
  if (!protocol::is_supported_protocol_version(header_value)) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport request MCP-Protocol-Version header mismatch",
        header_value));
  }

  return core::Unit{};
}

core::Result<core::Unit> validate_session_header(
    const httplib::Request& request, const std::string& expected_session_id) {
  if (expected_session_id.empty()) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode, "http transport has no active session"));
  }

  if (!request.has_header(std::string(SessionHeader))) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport request missing Mcp-Session-Id header"));
  }

  const auto session_header =
      request.get_header_value(std::string(SessionHeader));
  if (session_header != expected_session_id) {
    return std::unexpected(make_transport_error(
        HeaderMismatchCode,
        "http transport request Mcp-Session-Id header mismatch",
        session_header));
  }

  return core::Unit{};
}

void write_response(httplib::Response& http_response,
                    const protocol::JsonRpcResponse& response) {
  const auto serialized = protocol::serialize_response(response);
  if (!serialized) {
    http_response.status = 500;
    http_response.set_content(serialized.error().message, "text/plain");
    return;
  }

  http_response.set_content(*serialized, "application/json");
}

void write_error(httplib::Response& http_response, int code,
                 std::string message,
                 std::optional<protocol::RequestId> id = std::nullopt) {
  write_response(
      http_response,
      protocol::make_error_response(
          std::move(id), protocol::make_error(code, std::move(message))));
}

}  // namespace

HttpTransport::HttpTransport(HttpTransportOptions options)
    : options_(std::move(options)) {
  if (options_.path.empty()) {
    options_.path = "/mcp";
  }
  if (!core::starts_with(options_.path, '/')) {
    options_.path.insert(options_.path.begin(), '/');
  }
}

void HttpTransport::HttpServerDeleter::operator()(void* server) const noexcept {
  delete static_cast<httplib::Server*>(server);
}

HttpTransport::~HttpTransport() = default;

core::Result<core::Unit> HttpTransport::start(
    RequestHandler handler, NotificationHandler notification_handler) {
  if (!handler) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "http transport handler is not configured"));
  }
  if (options_.listen_port <= 0 || options_.listen_port > 65535) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "http transport listen port must be configured",
        std::to_string(options_.listen_port)));
  }

  {
    std::lock_guard lock(mutex_);
    stopped_ = false;
    sessions_.clear();
    default_session_id_.clear();
    server_.reset(new httplib::Server());
  }

  auto* http_server = static_cast<httplib::Server*>(server_.get());

  http_server->Post(options_.path, [this, handler = std::move(handler),
                                    notification_handler =
                                        std::move(notification_handler)](
                                       const httplib::Request& request,
                                       httplib::Response& response) mutable {
    const auto origin =
        validate_origin_header(request, options_.allowed_origins);
    if (!origin) {
      response.status = 400;
      write_error(response, origin.error().code, origin.error().message,
                  std::nullopt);
      return;
    }

    const auto message = protocol::parse_message(request.body);
    if (!message) {
      response.status = 400;
      write_error(response, message.error().code, message.error().message);
      return;
    }

    const auto* rpc_request = std::get_if<protocol::JsonRpcRequest>(&*message);
    const bool initialize_request =
        rpc_request != nullptr &&
        rpc_request->method == protocol::InitializeMethod;
    if (!initialize_request) {
      std::string expected_session_id;
      {
        std::lock_guard lock(mutex_);
        if (request.has_header(std::string(SessionHeader))) {
          const auto header =
              request.get_header_value(std::string(SessionHeader));
          if (find_session_locked(header) != nullptr) {
            expected_session_id = header;
          }
        }
      }

      const auto session_header =
          validate_session_header(request, expected_session_id);
      if (!session_header) {
        response.status = 404;
        write_error(response, session_header.error().code,
                    session_header.error().message, std::nullopt);
        return;
      }

      const auto protocol_version = validate_protocol_version_header(request);
      if (!protocol_version) {
        response.status = 400;
        write_error(response, protocol_version.error().code,
                    protocol_version.error().message, std::nullopt);
        return;
      }
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&*message)) {
      const auto header_check = validate_post_headers(
          request, protocol::make_request(notification->method, std::int64_t{0},
                                          notification->params));
      if (!header_check) {
        response.status = 400;
        write_error(response, header_check.error().code,
                    header_check.error().message, std::nullopt);
        return;
      }
      if (notification_handler) {
        std::string active_session_id;
        {
          std::lock_guard lock(mutex_);
          active_session_id =
              request.has_header(std::string(SessionHeader))
                  ? request.get_header_value(std::string(SessionHeader))
                  : default_session_id_;
        }
        SessionContext context;
        context.session_id = active_session_id;
        context.remote_address = request.remote_addr;
        context.transport = this;
        core::Result<core::Unit> handled;
        try {
          handled = notification_handler(*notification, context);
        } catch (const std::exception& ex) {
          handled = std::unexpected(errors::handler_failed(ex.what()));
        } catch (...) {
          handled = std::unexpected(errors::handler_unknown_exception());
        }
        if (!handled) {
          write_error(response, handled.error().code, handled.error().message);
          return;
        }
      }
      response.status = 202;
      return;
    }

    if (const auto* rpc_response =
            std::get_if<protocol::JsonRpcResponse>(&*message)) {
      if (!rpc_response->id.has_value()) {
        write_error(response,
                    static_cast<int>(protocol::ErrorCode::InvalidRequest),
                    "http transport response requires an id");
        return;
      }

      std::shared_ptr<PendingRequest> pending;
      {
        std::lock_guard lock(mutex_);
        const auto session_id =
            request.get_header_value(std::string(SessionHeader));
        auto* session = find_session_locked(session_id);
        if (session != nullptr) {
          const auto it = session->pending_requests.find(
              request_id_to_string(*rpc_response->id));
          if (it != session->pending_requests.end()) {
            pending = it->second;
            session->pending_requests.erase(it);
          }
        }
      }

      if (!pending) {
        response.status = 400;
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
      write_error(response, header_check.error().code,
                  header_check.error().message, rpc_request->id);
      return;
    }

    if (initialize_request) {
      const auto init_version_check =
          validate_initialize_protocol_header(request, *rpc_request);
      if (!init_version_check) {
        response.status = 400;
        write_error(response, init_version_check.error().code,
                    init_version_check.error().message, rpc_request->id);
        return;
      }
    }

    std::string session_context_id;
    bool initialize_session = false;
    {
      std::lock_guard lock(mutex_);
      if (initialize_request) {
        session_context_id =
            "mcp-session-" + std::to_string(next_session_id_++);
        initialize_session = true;
      } else {
        session_context_id =
            request.has_header(std::string(SessionHeader))
                ? request.get_header_value(std::string(SessionHeader))
                : default_session_id_;
      }
    }

    SessionContext context;
    context.session_id = session_context_id;
    context.remote_address = request.remote_addr;
    context.transport = this;
    core::Result<protocol::JsonRpcResponse> rpc_response;
    try {
      rpc_response = handler(*rpc_request, context);
    } catch (const std::exception& ex) {
      rpc_response = std::unexpected(errors::handler_failed(ex.what()));
    } catch (...) {
      rpc_response = std::unexpected(errors::handler_unknown_exception());
    }
    if (!rpc_response) {
      rpc_response = protocol::make_error_response(
          std::optional<protocol::RequestId>{rpc_request->id},
          protocol::make_error(rpc_response.error().code,
                               rpc_response.error().message,
                               rpc_response.error().detail.empty()
                                   ? std::nullopt
                                   : std::optional<protocol::Json>{
                                         rpc_response.error().detail}));
    }

    if (rpc_request->method == protocol::InitializeMethod) {
      if (!rpc_response->error.has_value()) {
        std::lock_guard guard(mutex_);
        auto& session = sessions_[session_context_id];
        session.session_id = session_context_id;
        if (rpc_request->params.is_object() &&
            rpc_request->params.contains("capabilities")) {
          session.client_capabilities = protocol::client_capabilities_from_json(
              rpc_request->params.at("capabilities"));
        } else {
          session.client_capabilities.reset();
        }
        if (initialize_session || default_session_id_.empty()) {
          default_session_id_ = session_context_id;
        }
        response.set_header(std::string(SessionHeader), session_context_id);
      }
    } else {
      std::lock_guard guard(mutex_);
      if (!session_context_id.empty()) {
        response.set_header(std::string(SessionHeader), session_context_id);
      }
    }

    write_response(response, *rpc_response);
  });

  http_server->Get(options_.path, [this](const httplib::Request& request,
                                         httplib::Response& response) {
    const auto origin =
        validate_origin_header(request, options_.allowed_origins);
    if (!origin) {
      response.status = 400;
      write_error(response, origin.error().code, origin.error().message,
                  std::nullopt);
      return;
    }

    if (!request.has_header(std::string(SessionHeader))) {
      response.status = 400;
      write_error(response, HeaderMismatchCode,
                  "http transport request missing Mcp-Session-Id header",
                  std::nullopt);
      return;
    }

    const auto stream_session_id =
        request.get_header_value(std::string(SessionHeader));
    {
      std::lock_guard lock(mutex_);
      if (find_session_locked(stream_session_id) == nullptr) {
        response.status = 404;
        return;
      }
    }

    const auto protocol_version = validate_protocol_version_header(request);
    if (!protocol_version) {
      response.status = 400;
      write_error(response, protocol_version.error().code,
                  protocol_version.error().message, std::nullopt);
      return;
    }

    const auto last_event_id = parse_last_event_id_header(request);
    if (!last_event_id) {
      response.status = 400;
      write_error(response, last_event_id.error().code,
                  last_event_id.error().message, std::nullopt);
      return;
    }

    std::vector<QueuedEvent> replay_events;
    {
      std::lock_guard lock(mutex_);
      auto* session = find_session_locked(stream_session_id);
      if (session == nullptr) {
        response.status = 404;
        return;
      }
      if (session->active_sse_streams > 0 && !last_event_id->has_value()) {
        response.status = 409;
        write_error(response, HeaderMismatchCode,
                    "http transport session already has an active SSE stream",
                    std::nullopt);
        return;
      }
      if (last_event_id->has_value()) {
        const auto last_seen_event_id = **last_event_id;
        const auto last_assigned_event_id =
            session->next_notification_event_id - 1;
        if (last_seen_event_id > last_assigned_event_id) {
          response.status = 409;
          write_error(response, HeaderMismatchCode,
                      "http transport Last-Event-ID is ahead of session state",
                      std::nullopt);
          return;
        }
        if (last_seen_event_id < last_assigned_event_id) {
          if (session->replay_notifications.empty()) {
            response.status = 409;
            write_error(response, HeaderMismatchCode,
                        "http transport Last-Event-ID is outside replay window",
                        std::nullopt);
            return;
          }
          const auto oldest_event_id =
              session->replay_notifications.front().event_id;
          if (!oldest_event_id.has_value() ||
              last_seen_event_id < (*oldest_event_id - 1)) {
            response.status = 409;
            write_error(response, HeaderMismatchCode,
                        "http transport Last-Event-ID is outside replay window",
                        std::nullopt);
            return;
          }
        }
        for (const auto& event : session->replay_notifications) {
          if (event.event_id.has_value() &&
              *event.event_id > last_seen_event_id) {
            replay_events.push_back(event);
          }
        }
      }
      session->active_sse_streams += 1;
    }

    response.set_chunked_content_provider(
        "text/event-stream",
        [this, &request, stream_session_id,
         replay_events = std::move(replay_events),
         replay_index = std::size_t{0},
         stream_closed = false](std::size_t, httplib::DataSink& sink) mutable {
          const auto close_stream = [&]() {
            if (!stream_closed) {
              close_sse_stream(stream_session_id);
              stream_closed = true;
            }
          };
          const auto close_stream_locked = [&]() {
            if (!stream_closed) {
              auto* session = find_session_locked(stream_session_id);
              if (session != nullptr && session->active_sse_streams > 0) {
                session->active_sse_streams -= 1;
              }
              stream_closed = true;
            }
          };
          if (options_.sse_retry.has_value()) {
            const auto priming = sse_priming_event(*options_.sse_retry);
            if (!sink.write(priming.data(), priming.size())) {
              close_stream();
              sink.done();
              return false;
            }
          }

          while (true) {
            std::unique_lock lock(mutex_);
            if (stopped_ || request.is_connection_closed() ||
                !sink.is_writable()) {
              close_stream_locked();
              sink.done();
              return false;
            }

            if (replay_index < replay_events.size()) {
              auto event = replay_events[replay_index++];
              auto serialized_event =
                  message_to_sse_event(event.event_id, event.payload);
              lock.unlock();
              const bool written =
                  sink.write(serialized_event.data(), serialized_event.size());
              if (!written) {
                close_stream();
              }
              return written;
            }

            notification_cv_.wait_for(
                lock, std::chrono::milliseconds(100),
                [this, &sink, &request, stream_session_id, &replay_events,
                 &replay_index]() {
                  const auto* session = find_session_locked(stream_session_id);
                  return stopped_ || replay_index < replay_events.size() ||
                         session == nullptr ||
                         !session->pending_notifications.empty() ||
                         !sink.is_writable() || request.is_connection_closed();
                });

            if (stopped_ || request.is_connection_closed() ||
                !sink.is_writable()) {
              close_stream_locked();
              sink.done();
              return false;
            }

            auto* session = find_session_locked(stream_session_id);
            if (session == nullptr) {
              close_stream_locked();
              sink.done();
              return false;
            }

            if (!session->pending_notifications.empty()) {
              auto event = std::move(session->pending_notifications.front());
              session->pending_notifications.pop_front();
              session->pending_notification_bytes -= event.payload.size();
              remember_replay_event_locked(*session, event);
              auto serialized_event =
                  message_to_sse_event(event.event_id, event.payload);
              lock.unlock();
              const bool written =
                  sink.write(serialized_event.data(), serialized_event.size());
              if (!written) {
                close_stream();
              }
              return written;
            }
          }
        });
  });

  http_server->Delete(options_.path, [this](const httplib::Request& request,
                                            httplib::Response& response) {
    const auto origin =
        validate_origin_header(request, options_.allowed_origins);
    if (!origin) {
      response.status = 400;
      write_error(response, origin.error().code, origin.error().message,
                  std::nullopt);
      return;
    }

    if (!request.has_header(std::string(SessionHeader))) {
      response.status = 400;
      write_error(response, HeaderMismatchCode,
                  "http transport request missing Mcp-Session-Id header",
                  std::nullopt);
      return;
    }

    const auto stream_session_id =
        request.get_header_value(std::string(SessionHeader));
    {
      std::lock_guard lock(mutex_);
      if (find_session_locked(stream_session_id) == nullptr) {
        response.status = 404;
        return;
      }
    }

    const auto protocol_version = validate_protocol_version_header(request);
    if (!protocol_version) {
      response.status = 400;
      write_error(response, protocol_version.error().code,
                  protocol_version.error().message, std::nullopt);
      return;
    }

    {
      std::lock_guard lock(mutex_);
      auto* session = find_session_locked(stream_session_id);
      if (session != nullptr) {
        abort_pending_requests_locked(*session, "http session terminated");
        sessions_.erase(stream_session_id);
      }
      if (default_session_id_ == stream_session_id) {
        default_session_id_ =
            sessions_.empty() ? std::string{} : sessions_.begin()->first;
      }
      notification_cv_.notify_all();
    }
    response.status = 204;
  });

  const auto listening =
      http_server->listen(options_.listen_host, options_.listen_port);
  if (!listening) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to listen for http transport",
        options_.listen_host + ":" + std::to_string(options_.listen_port)));
  }

  return core::Unit{};
}

core::Result<core::Unit> HttpTransport::send_notification(
    const protocol::JsonRpcNotification& notification) {
  std::string session_id;
  {
    std::lock_guard lock(mutex_);
    const auto* session = select_default_session_locked();
    if (session != nullptr) {
      session_id = session->session_id;
    }
  }
  return send_notification_to_session(session_id, notification);
}

core::Result<core::Unit> HttpTransport::send_notification_to_session(
    std::string_view session_id,
    const protocol::JsonRpcNotification& notification) {
  const auto serialized = protocol::serialize_notification(notification);
  if (!serialized) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to serialize http notification"));
  }

  std::lock_guard lock(mutex_);
  if (stopped_) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "http transport is stopped"));
  }
  auto* session = find_session_locked(session_id);
  if (session == nullptr) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "http transport has no active session"));
  }

  QueuedEvent event;
  event.payload = *serialized;
  const auto enqueued =
      enqueue_outbound_event_locked(*session, std::move(event));
  if (!enqueued) {
    return enqueued;
  }
  notification_cv_.notify_all();
  return core::Unit{};
}

core::Result<protocol::JsonRpcResponse> HttpTransport::send_request(
    const protocol::JsonRpcRequest& request) {
  std::string session_id;
  {
    std::lock_guard lock(mutex_);
    const auto* session = select_default_session_locked();
    if (session != nullptr) {
      session_id = session->session_id;
    }
  }
  return send_request_to_session(session_id, request);
}

core::Result<protocol::JsonRpcResponse> HttpTransport::send_request_to_session(
    std::string_view session_id, const protocol::JsonRpcRequest& request) {
  const auto serialized = protocol::serialize_request(request);
  if (!serialized) {
    return std::unexpected(serialized.error());
  }

  auto pending = std::make_shared<PendingRequest>();
  const auto request_key = request_id_to_string(request.id);
  {
    std::lock_guard lock(mutex_);
    if (stopped_) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport is stopped"));
    }
    auto* session = find_session_locked(session_id);
    if (session == nullptr) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "http transport has no active session"));
    }
    if (session->pending_requests.find(request_key) !=
        session->pending_requests.end()) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "http transport already has a pending request with this id"));
    }
    session->pending_requests[request_key] = pending;
    QueuedEvent event;
    event.payload = *serialized;
    const auto enqueued =
        enqueue_outbound_event_locked(*session, std::move(event));
    if (!enqueued) {
      session->pending_requests.erase(request_key);
      return std::unexpected(enqueued.error());
    }
  }

  notification_cv_.notify_all();

  std::unique_lock lock(pending->mutex);
  if (options_.request_timeout.count() > 0) {
    const bool ready = pending->cv.wait_for(lock, options_.request_timeout,
                                            [&] { return pending->ready; });
    if (!ready) {
      lock.unlock();
      {
        std::lock_guard state_lock(mutex_);
        auto* session = find_session_locked(session_id);
        if (session != nullptr) {
          session->pending_requests.erase(request_key);
        }
      }
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport request timed out", request_key));
    }
  } else {
    pending->cv.wait(lock, [&] { return pending->ready; });
  }
  if (pending->error.has_value()) {
    return std::unexpected(*pending->error);
  }
  if (!pending->response.has_value()) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "http transport response missing"));
  }

  return *pending->response;
}

std::optional<protocol::ClientCapabilities> HttpTransport::client_capabilities()
    const {
  std::lock_guard lock(mutex_);
  const SessionState* session = nullptr;
  if (!default_session_id_.empty()) {
    session = find_session_locked(default_session_id_);
  }
  if (session == nullptr && !sessions_.empty()) {
    session = &sessions_.begin()->second;
  }
  return session == nullptr ? std::nullopt : session->client_capabilities;
}

std::optional<protocol::ClientCapabilities>
HttpTransport::client_capabilities_for_session(
    std::string_view session_id) const {
  std::lock_guard lock(mutex_);
  const auto* session = find_session_locked(session_id);
  return session == nullptr ? std::nullopt : session->client_capabilities;
}

void HttpTransport::stop() noexcept {
  httplib::Server* server_to_stop = nullptr;
  {
    std::lock_guard lock(mutex_);
    if (stopped_) {
      return;
    }
    stopped_ = true;
    for (auto& [_, session] : sessions_) {
      abort_pending_requests_locked(session, "http transport is stopped");
      clear_outbound_events_locked(session);
    }
    sessions_.clear();
    default_session_id_.clear();
    server_to_stop = static_cast<httplib::Server*>(server_.get());
  }
  notification_cv_.notify_all();
  if (server_to_stop) {
    server_to_stop->stop();
  }
}

void HttpTransport::clear_outbound_events_locked(SessionState& session) {
  session.pending_notifications.clear();
  session.pending_notification_bytes = 0;
  session.replay_notifications.clear();
  session.next_notification_event_id = 1;
}

core::Result<core::Unit> HttpTransport::enqueue_outbound_event_locked(
    SessionState& session, QueuedEvent event) {
  if (options_.max_pending_sse_events > 0 &&
      session.pending_notifications.size() >= options_.max_pending_sse_events) {
    return std::unexpected(
        make_transport_error(static_cast<int>(protocol::ErrorCode::RateLimited),
                             "http transport outbound SSE queue is full"));
  }
  if (options_.max_pending_sse_bytes > 0) {
    const auto max_bytes = options_.max_pending_sse_bytes;
    if (session.pending_notification_bytes >= max_bytes ||
        event.payload.size() > max_bytes - session.pending_notification_bytes) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::RateLimited),
          "http transport outbound SSE byte queue is full"));
    }
  }

  if (!event.event_id.has_value()) {
    event.event_id = session.next_notification_event_id++;
  }
  session.pending_notification_bytes += event.payload.size();
  session.pending_notifications.push_back(std::move(event));
  return core::Unit{};
}

void HttpTransport::remember_replay_event_locked(SessionState& session,
                                                 const QueuedEvent& event) {
  if (!event.event_id.has_value() || options_.max_sse_replay_events == 0) {
    return;
  }

  session.replay_notifications.push_back(event);
  while (session.replay_notifications.size() > options_.max_sse_replay_events) {
    session.replay_notifications.pop_front();
  }
}

HttpTransport::SessionState* HttpTransport::find_session_locked(
    std::string_view session_id) {
  const auto it = sessions_.find(std::string(session_id));
  return it == sessions_.end() ? nullptr : &it->second;
}

const HttpTransport::SessionState* HttpTransport::find_session_locked(
    std::string_view session_id) const {
  const auto it = sessions_.find(std::string(session_id));
  return it == sessions_.end() ? nullptr : &it->second;
}

HttpTransport::SessionState* HttpTransport::select_default_session_locked() {
  if (!default_session_id_.empty()) {
    auto* session = find_session_locked(default_session_id_);
    if (session != nullptr) {
      return session;
    }
  }
  if (sessions_.empty()) {
    return nullptr;
  }
  return &sessions_.begin()->second;
}

void HttpTransport::close_sse_stream(std::string_view session_id) noexcept {
  std::lock_guard lock(mutex_);
  auto* session = find_session_locked(session_id);
  if (session != nullptr && session->active_sse_streams > 0) {
    session->active_sse_streams -= 1;
  }
}

void HttpTransport::abort_pending_requests_locked(SessionState& session,
                                                  std::string message) {
  std::unordered_map<std::string, std::shared_ptr<PendingRequest>>
      pending_requests;
  pending_requests.swap(session.pending_requests);
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

std::string_view HttpTransport::name() const noexcept { return "http"; }

}  // namespace mcp::server

namespace mcp::transport {
namespace {

core::Error make_native_server_http_error(protocol::ErrorCode code,
                                          std::string message,
                                          std::string detail = {}) {
  return core::Error{
      static_cast<int>(code),
      std::move(message),
      std::move(detail),
      "transport",
  };
}

server::HttpTransportOptions to_legacy_options(
    StreamableHttpServerTransportOptions options) {
  server::HttpTransportOptions legacy;
  legacy.listen_host = std::move(options.listen_host);
  legacy.listen_port = options.listen_port;
  legacy.path = std::move(options.path);
  legacy.sse_retry = options.sse_retry;
  legacy.allowed_origins = std::move(options.allowed_origins);
  legacy.max_pending_sse_events = options.max_pending_sse_events;
  legacy.max_pending_sse_bytes = options.max_pending_sse_bytes;
  legacy.max_sse_replay_events = options.max_sse_replay_events;
  legacy.request_timeout = options.request_timeout;
  return legacy;
}

std::string request_id_to_string_for_native_server_http(
    const protocol::RequestId& request_id) {
  return std::visit(
      [](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return value;
        } else {
          return std::to_string(value);
        }
      },
      request_id);
}

}  // namespace

class StreamableHttpServerTransport::Impl {
 public:
  explicit Impl(StreamableHttpServerTransportOptions options)
      : transport_(to_legacy_options(std::move(options))) {}

  ~Impl() { (void)close(); }

  protocol::Json diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return protocol::Json{
        {"name", "streamable-http-server"},
        {"closed", closed_},
        {"started", started_},
        {"queued", inbound_.size()},
        {"pendingClientRequests", pending_client_requests_.size()},
        {"requestWorkers", request_threads_.size()},
        {"activeRequestWorkers", active_request_workers_},
        {"completedRequestWorkers", completed_request_workers_},
        {"failedRequestWorkers", failed_request_workers_},
        {"timedOutRequestWorkers", timed_out_request_workers_},
    };
  }

  core::Result<core::Unit> send(protocol::JsonRpcMessage message) {
    if (auto* response = std::get_if<protocol::JsonRpcResponse>(&message)) {
      if (!response->id.has_value()) {
        return std::unexpected(
            make_native_server_http_error(protocol::ErrorCode::InvalidRequest,
                                          "streamable http server transport "
                                          "cannot send response without id"));
      }
      return complete_client_request(std::move(*response));
    }

    const auto started = ensure_started();
    if (!started) {
      return std::unexpected(started.error());
    }

    if (auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      return transport_.send_notification(*notification);
    }

    auto* request = std::get_if<protocol::JsonRpcRequest>(&message);
    if (request == nullptr) {
      return std::unexpected(make_native_server_http_error(
          protocol::ErrorCode::InvalidRequest,
          "streamable http server transport cannot send unknown message"));
    }
    return start_request_thread(std::move(*request));
  }

  core::Result<std::optional<protocol::JsonRpcMessage>> receive() {
    const auto started = ensure_started();
    if (!started) {
      return std::unexpected(started.error());
    }

    std::unique_lock<std::mutex> lock(mutex_);
    receive_cv_.wait(lock, [this] {
      return closed_ || startup_error_.has_value() || !inbound_.empty();
    });
    if (startup_error_.has_value()) {
      return std::unexpected(*startup_error_);
    }
    if (inbound_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(inbound_.front());
    inbound_.pop_front();
    return message;
  }

  core::Result<core::Unit> close() {
    std::map<protocol::RequestId, std::shared_ptr<PendingClientRequest>>
        pending;
    std::vector<std::thread> request_threads;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return core::Unit{};
      }
      closed_ = true;
      pending.swap(pending_client_requests_);
      request_threads.swap(request_threads_);
    }

    for (auto& [_, request] : pending) {
      {
        std::lock_guard<std::mutex> request_lock(request->mutex);
        request->response = protocol::make_error_response(
            std::optional<protocol::RequestId>{request->id},
            protocol::make_error(protocol::ErrorCode::InternalError,
                                 "streamable http server transport closed"));
      }
      request->cv.notify_all();
    }

    receive_cv_.notify_all();
    transport_.stop();
    for (auto& thread : request_threads) {
      if (thread.joinable() && thread.get_id() != std::this_thread::get_id()) {
        thread.join();
      }
    }
    if (server_thread_.joinable() &&
        server_thread_.get_id() != std::this_thread::get_id()) {
      server_thread_.join();
    }
    return core::Unit{};
  }

 private:
  struct PendingClientRequest {
    explicit PendingClientRequest(protocol::RequestId request_id)
        : id(std::move(request_id)) {}

    protocol::RequestId id;
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<protocol::JsonRpcResponse> response;
  };

  core::Result<core::Unit> ensure_started() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return std::unexpected(make_native_server_http_error(
          protocol::ErrorCode::InvalidRequest,
          "streamable http server transport is closed"));
    }
    if (started_) {
      return core::Unit{};
    }
    started_ = true;
    try {
      server_thread_ = std::thread([this]() {
        const auto started = transport_.start(
            [this](const protocol::JsonRpcRequest& request,
                   const server::SessionContext&) {
              return handle_client_request(request);
            },
            [this](const protocol::JsonRpcNotification& notification,
                   const server::SessionContext&) {
              return handle_client_notification(notification);
            });
        if (!started) {
          {
            std::lock_guard<std::mutex> state_lock(mutex_);
            startup_error_ = started.error();
            closed_ = true;
          }
          receive_cv_.notify_all();
        }
      });
    } catch (const std::system_error& ex) {
      started_ = false;
      return std::unexpected(make_native_server_http_error(
          protocol::ErrorCode::InternalError,
          "failed to start streamable http server worker", ex.what()));
    }
    return core::Unit{};
  }

  core::Result<core::Unit> start_request_thread(
      protocol::JsonRpcRequest request) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return std::unexpected(make_native_server_http_error(
            protocol::ErrorCode::InvalidRequest,
            "streamable http server transport is closed"));
      }
      ++active_request_workers_;
      request_threads_.emplace_back(
          [this, request = std::move(request)]() mutable {
            auto response = transport_.send_request(request);
            if (response) {
              finish_request_worker(false, false);
              enqueue(protocol::JsonRpcMessage{std::move(*response)});
              return;
            }
            finish_request_worker(true, is_timeout_error(response.error()));
            enqueue(protocol::JsonRpcMessage{protocol::make_error_response(
                std::optional<protocol::RequestId>{request.id},
                protocol::make_error(response.error().code,
                                     response.error().message,
                                     response.error().detail.empty()
                                         ? std::nullopt
                                         : std::optional<protocol::Json>{
                                               response.error().detail}))});
          });
    } catch (const std::system_error& ex) {
      finish_request_worker(true, false);
      return std::unexpected(make_native_server_http_error(
          protocol::ErrorCode::InternalError,
          "failed to start streamable http server request worker", ex.what()));
    }
    return core::Unit{};
  }

  core::Result<protocol::JsonRpcResponse> handle_client_request(
      const protocol::JsonRpcRequest& request) {
    auto pending = std::make_shared<PendingClientRequest>(request.id);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return protocol::make_error_response(
            std::optional<protocol::RequestId>{request.id},
            protocol::make_error(protocol::ErrorCode::InternalError,
                                 "streamable http server transport closed"));
      }
      const auto [_, inserted] =
          pending_client_requests_.emplace(request.id, pending);
      if (!inserted) {
        return protocol::make_error_response(
            std::optional<protocol::RequestId>{request.id},
            protocol::make_error(protocol::ErrorCode::InvalidRequest,
                                 "duplicate client request id"));
      }
      inbound_.push_back(protocol::JsonRpcMessage{request});
    }

    receive_cv_.notify_one();

    std::unique_lock<std::mutex> pending_lock(pending->mutex);
    pending->cv.wait(pending_lock,
                     [&pending] { return pending->response.has_value(); });
    return std::move(*pending->response);
  }

  core::Result<core::Unit> handle_client_notification(
      const protocol::JsonRpcNotification& notification) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return core::Unit{};
      }
      inbound_.push_back(protocol::JsonRpcMessage{notification});
    }
    receive_cv_.notify_one();
    return core::Unit{};
  }

  void enqueue(protocol::JsonRpcMessage message) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return;
      }
      inbound_.push_back(std::move(message));
    }
    receive_cv_.notify_one();
  }

  static bool is_timeout_error(const core::Error& error) {
    return error.message.find("timed out") != std::string::npos;
  }

  void finish_request_worker(bool failed, bool timed_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_request_workers_ > 0) {
      --active_request_workers_;
    }
    ++completed_request_workers_;
    if (failed) {
      ++failed_request_workers_;
    }
    if (timed_out) {
      ++timed_out_request_workers_;
    }
  }

  core::Result<core::Unit> complete_client_request(
      protocol::JsonRpcResponse response) {
    const auto id = *response.id;
    std::shared_ptr<PendingClientRequest> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = pending_client_requests_.find(id);
      if (it == pending_client_requests_.end()) {
        return std::unexpected(make_native_server_http_error(
            protocol::ErrorCode::InvalidRequest,
            "streamable http server transport has no pending client request",
            request_id_to_string_for_native_server_http(id)));
      }
      pending = it->second;
      pending_client_requests_.erase(it);
    }

    {
      std::lock_guard<std::mutex> pending_lock(pending->mutex);
      pending->response = std::move(response);
    }
    pending->cv.notify_all();
    return core::Unit{};
  }

  server::HttpTransport transport_;
  mutable std::mutex mutex_;
  std::condition_variable receive_cv_;
  std::deque<protocol::JsonRpcMessage> inbound_;
  std::map<protocol::RequestId, std::shared_ptr<PendingClientRequest>>
      pending_client_requests_;
  std::vector<std::thread> request_threads_;
  std::size_t active_request_workers_ = 0;
  std::size_t completed_request_workers_ = 0;
  std::size_t failed_request_workers_ = 0;
  std::size_t timed_out_request_workers_ = 0;
  std::thread server_thread_;
  std::optional<core::Error> startup_error_;
  bool started_ = false;
  bool closed_ = false;
};

StreamableHttpServerTransport::StreamableHttpServerTransport(
    StreamableHttpServerTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

StreamableHttpServerTransport::~StreamableHttpServerTransport() = default;

std::string_view StreamableHttpServerTransport::name() const noexcept {
  return "streamable-http-server";
}

protocol::Json StreamableHttpServerTransport::diagnostics() const {
  return impl_->diagnostics();
}

core::Result<core::Unit> StreamableHttpServerTransport::send(
    TxMessage message) {
  return impl_->send(std::move(message));
}

core::Result<std::optional<StreamableHttpServerTransport::RxMessage>>
StreamableHttpServerTransport::receive() {
  return impl_->receive();
}

core::Result<core::Unit> StreamableHttpServerTransport::close() {
  return impl_->close();
}

}  // namespace mcp::transport
