// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/client/http_transport.hpp"

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/transport/http_transport.hpp"
#include "httplib.h"

namespace mcp::client {

namespace {

constexpr std::string_view SessionHeader = "Mcp-Session-Id";
constexpr std::string_view MethodHeader = "Mcp-Method";
constexpr std::string_view NameHeader = "Mcp-Name";

core::Error make_transport_error(int code, std::string message,
                                 std::string detail = {}) {
  return core::Error{code, std::move(message), std::move(detail), "transport"};
}

std::string request_id_to_string(const protocol::RequestId& request_id) {
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

httplib::Headers to_headers(
    const std::unordered_map<std::string, std::string>& headers) {
  httplib::Headers result;
  for (const auto& [key, value] : headers) {
    result.emplace(key, value);
  }
  return result;
}

void apply_timeout(httplib::Client& client, std::chrono::milliseconds timeout) {
  const auto sec = std::chrono::duration_cast<std::chrono::seconds>(timeout);
  const auto usec =
      std::chrono::duration_cast<std::chrono::microseconds>(timeout - sec);
  client.set_connection_timeout(sec.count(), static_cast<time_t>(usec.count()));
  client.set_read_timeout(sec.count(), static_cast<time_t>(usec.count()));
  client.set_write_timeout(sec.count(), static_cast<time_t>(usec.count()));
}

bool has_event_stream_content_type(const httplib::Response& response) {
  if (!response.has_header("Content-Type")) {
    return false;
  }
  return response.get_header_value("Content-Type").find("text/event-stream") !=
         std::string::npos;
}

struct ResolvedEndpoint {
  std::string origin;
  std::string path;
};

core::Result<ResolvedEndpoint> resolve_endpoint(
    const HttpTransportOptions& options) {
  httplib::detail::UrlComponents components;
  if (!options.uri.empty()) {
    if (!httplib::detail::parse_url(options.uri, components) ||
        components.host.empty()) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "invalid http transport uri", options.uri));
    }
  } else {
    components.host = options.host;
    components.port = std::to_string(options.port);
    components.path = options.path;
  }

  if (!components.scheme.empty() && components.scheme != "http" &&
      components.scheme != "https") {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "unsupported http transport uri scheme", components.scheme));
  }

  if (components.host.empty()) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "http transport endpoint is missing a host"));
  }

  const bool is_ssl = components.scheme == "https";
  int port = is_ssl ? 443 : 80;
  if (!components.port.empty() &&
      !httplib::detail::parse_port(components.port, port)) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "invalid http transport port", components.port));
  }

  std::string path = components.path.empty() ? "/" : components.path;
  if (!path.empty() && path.front() != '/') {
    path.insert(path.begin(), '/');
  }
  if (!components.query.empty()) {
    path.push_back('?');
    path += components.query;
  }

  std::string origin = is_ssl ? "https://" : "http://";
  origin +=
      httplib::detail::make_host_and_port_string(components.host, port, is_ssl);

  return ResolvedEndpoint{std::move(origin), std::move(path)};
}

std::vector<std::string> parse_sse_data_events(std::string_view body) {
  std::vector<std::string> events;
  std::string data;

  std::size_t line_start = 0;
  while (line_start <= body.size()) {
    auto line_end = body.find('\n', line_start);
    if (line_end == std::string_view::npos) {
      line_end = body.size();
    }

    std::string_view line = body.substr(line_start, line_end - line_start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    if (line.empty()) {
      if (!data.empty()) {
        events.push_back(std::move(data));
        data.clear();
      }
    } else if (!line.empty() && line.front() != ':') {
      const auto colon = line.find(':');
      const auto field =
          colon == std::string_view::npos ? line : line.substr(0, colon);
      if (field == "data") {
        std::string_view value;
        if (colon != std::string_view::npos && colon + 1 < line.size()) {
          value = line.substr(colon + 1);
          if (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
          }
        }
        if (!data.empty()) {
          data.push_back('\n');
        }
        data.append(value.begin(), value.end());
      }
    }

    if (line_end == body.size()) {
      break;
    }
    line_start = line_end + 1;
  }

  if (!data.empty()) {
    events.push_back(std::move(data));
  }

  return events;
}

}  // namespace

struct HttpTransport::Impl {
  explicit Impl(HttpTransportOptions options) : options(std::move(options)) {
    const auto resolved = resolve_endpoint(this->options);
    if (resolved) {
      origin = std::move(resolved->origin);
      path = std::move(resolved->path);
    } else {
      options_error = resolved.error();
    }
  }

  ~Impl() { stop(); }

  core::Result<core::Unit> start(
      TransportRequestHandler request_handler,
      TransportNotificationHandler notification_handler) {
    std::lock_guard lock(mutex);
    this->request_handler = std::move(request_handler);
    this->notification_handler = std::move(notification_handler);
    started = true;
    return core::Unit{};
  }

  void stop() noexcept {
    std::string session_id_to_terminate;
    {
      std::lock_guard lock(mutex);
      session_id_to_terminate = session_id;
    }

    if (!session_id_to_terminate.empty() && options_error == std::nullopt) {
      auto client = make_client();
      auto headers = to_headers(options.headers);
      headers.emplace("Accept", "application/json");
      headers.emplace("MCP-Protocol-Version",
                      std::string(protocol::McpProtocolVersion));
      headers.emplace(std::string(SessionHeader), session_id_to_terminate);
      (void)client.Delete(path, headers);
    }

    reset_session();
  }

  core::Result<protocol::JsonRpcResponse> send(
      const protocol::JsonRpcRequest& request) {
    if (options_error.has_value()) {
      return std::unexpected(*options_error);
    }

    const auto serialized = protocol::serialize_request(request);
    if (!serialized) {
      return std::unexpected(serialized.error());
    }

    bool retried_after_session_reset = false;
    while (true) {
      auto client = make_client();
      const bool include_protocol_version =
          request.method != protocol::InitializeMethod;
      auto headers =
          make_headers(request.method, header_name_from_request(request),
                       /*json_body=*/true,
                       /*event_stream=*/true, include_protocol_version);
      const auto response =
          client.Post(path, headers, *serialized, "application/json");
      if (!response) {
        return std::unexpected(make_transport_error(
            static_cast<int>(protocol::ErrorCode::InternalError),
            "http transport request failed",
            httplib::to_string(response.error())));
      }
      if (response->status == 404) {
        reset_session();
        if (request.method == protocol::InitializeMethod &&
            !retried_after_session_reset) {
          retried_after_session_reset = true;
          continue;
        }
        return std::unexpected(make_transport_error(
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "http transport session was terminated",
            std::to_string(response->status)));
      }

      const auto remembered_session = remember_session(
          *response, request.method == protocol::InitializeMethod);
      if (!remembered_session) {
        return std::unexpected(remembered_session.error());
      }
      const auto stream_started_result = ensure_stream_started();
      if (!stream_started_result) {
        return std::unexpected(stream_started_result.error());
      }

      return decode_request_response(*response, request.id);
    }
  }

  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) {
    if (options_error.has_value()) {
      return std::unexpected(*options_error);
    }

    const auto serialized = protocol::serialize_notification(notification);
    if (!serialized) {
      return std::unexpected(serialized.error());
    }

    auto client = make_client();
    auto headers = make_headers(notification.method, std::nullopt,
                                /*json_body=*/true, /*event_stream=*/true);
    const auto response =
        client.Post(path, headers, *serialized, "application/json");
    if (!response) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport notification failed",
          httplib::to_string(response.error())));
    }
    if (response->status == 404) {
      reset_session();
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "http transport session was terminated",
          std::to_string(response->status)));
    }

    const auto remembered_session =
        remember_session(*response, /*require_session_id=*/false);
    if (!remembered_session) {
      return std::unexpected(remembered_session.error());
    }
    const auto stream_started_result = ensure_stream_started();
    if (!stream_started_result) {
      return std::unexpected(stream_started_result.error());
    }

    if (response->status < 200 || response->status >= 300) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport notification returned an error status",
          std::to_string(response->status)));
    }

    if (has_event_stream_content_type(*response) && !response->body.empty()) {
      for (const auto& event : parse_sse_data_events(response->body)) {
        const auto handled = dispatch_event_payload(event, std::nullopt);
        if (!handled) {
          return std::unexpected(handled.error());
        }
      }
    }

    return core::Unit{};
  }

  void reset_session() noexcept {
    std::unique_ptr<httplib::Client> stream_client_to_stop;
    std::unique_ptr<httplib::sse::SSEClient> sse_to_stop;
    {
      std::lock_guard lock(mutex);
      stream_started = false;
      session_id.clear();
      sse_to_stop = std::move(sse_client);
      stream_client_to_stop = std::move(stream_client);
    }

    if (sse_to_stop) {
      sse_to_stop->stop();
    }
    if (stream_client_to_stop) {
      stream_client_to_stop->stop();
    }
  }

  httplib::Client make_client() const {
    httplib::Client client(origin);
    apply_timeout(client, options.timeout);
    return client;
  }

  httplib::Headers make_headers(std::optional<std::string_view> method,
                                std::optional<std::string> name, bool json_body,
                                bool event_stream,
                                bool include_protocol_version = true) const {
    auto headers = to_headers(options.headers);
    if (options.auth_header.has_value()) {
      headers.emplace("Authorization", "Bearer " + *options.auth_header);
    }
    if (json_body) {
      headers.emplace("Content-Type", "application/json");
    }
    headers.emplace("Accept", event_stream
                                  ? "application/json, text/event-stream"
                                  : "application/json");
    if (include_protocol_version) {
      headers.emplace("MCP-Protocol-Version",
                      std::string(protocol::McpProtocolVersion));
    }
    if (method.has_value()) {
      headers.emplace(std::string(MethodHeader), std::string(*method));
    }
    if (name.has_value()) {
      headers.emplace(std::string(NameHeader), std::move(*name));
    }

    std::lock_guard lock(mutex);
    if (!session_id.empty()) {
      headers.emplace(std::string(SessionHeader), session_id);
    }
    return headers;
  }

  core::Result<core::Unit> remember_session(const httplib::Response& response,
                                            bool require_session_id) {
    if (!response.has_header(std::string(SessionHeader))) {
      return core::Unit{};
    }
    const auto response_session_id =
        response.get_header_value(std::string(SessionHeader));
    if (response_session_id.empty()) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "http transport response has an empty Mcp-Session-Id header"));
    }
    std::lock_guard lock(mutex);
    session_id = response_session_id;
    return core::Unit{};
  }

  core::Result<core::Unit> ensure_stream_started() {
    {
      std::lock_guard lock(mutex);
      if (!started || stream_started || session_id.empty()) {
        return core::Unit{};
      }
      stream_client = std::make_unique<httplib::Client>(origin);
      apply_timeout(*stream_client, options.timeout);
      auto headers = to_headers(options.headers);
      headers.emplace("Accept", "text/event-stream");
      headers.emplace("MCP-Protocol-Version",
                      std::string(protocol::McpProtocolVersion));
      headers.emplace(std::string(SessionHeader), session_id);
      sse_client = std::make_unique<httplib::sse::SSEClient>(*stream_client,
                                                             path, headers);
      sse_client->set_reconnect_interval(250);
      sse_client->on_message([this](const httplib::sse::SSEMessage& message) {
        const auto handled = dispatch_event_payload(message.data, std::nullopt);
        (void)handled;
      });
      sse_client->on_error([](httplib::Error) {});
      sse_client->start_async();
      stream_started = true;
    }
    return core::Unit{};
  }

  core::Result<protocol::JsonRpcResponse> decode_request_response(
      const httplib::Response& response,
      const protocol::RequestId& request_id) {
    if (response.status < 200 || response.status >= 300) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport request returned an error status",
          std::to_string(response.status)));
    }

    if (response.body.empty()) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport returned an empty response body"));
    }

    if (!has_event_stream_content_type(response)) {
      const auto parsed = protocol::parse_response(response.body);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      if (!parsed->id.has_value() || *parsed->id != request_id) {
        return std::unexpected(make_transport_error(
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "http transport received an unexpected response",
            parsed->id.has_value() ? request_id_to_string(*parsed->id)
                                   : std::string{}));
      }
      return *parsed;
    }

    std::optional<protocol::JsonRpcResponse> final_response;
    for (const auto& event : parse_sse_data_events(response.body)) {
      const auto handled = dispatch_event_payload(event, request_id);
      if (!handled) {
        return std::unexpected(handled.error());
      }
      if (handled->has_value()) {
        final_response = std::move(**handled);
      }
    }

    if (!final_response.has_value()) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http event stream did not contain a response",
          request_id_to_string(request_id)));
    }

    return *final_response;
  }

  core::Result<std::optional<protocol::JsonRpcResponse>> dispatch_event_payload(
      std::string_view payload,
      std::optional<protocol::RequestId> expected_response_id) {
    const auto message = protocol::parse_message(payload);
    if (!message) {
      return std::unexpected(message.error());
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&*message)) {
      TransportNotificationHandler handler;
      {
        std::lock_guard lock(mutex);
        handler = notification_handler;
      }
      if (handler) {
        core::Result<core::Unit> handled;
        try {
          handled = handler(*notification);
        } catch (const std::exception& ex) {
          handled = std::unexpected(errors::handler_failed(ex.what()));
        } catch (...) {
          handled = std::unexpected(errors::handler_unknown_exception());
        }
        if (!handled) {
          return std::unexpected(handled.error());
        }
      }
      return std::optional<protocol::JsonRpcResponse>{};
    }

    if (const auto* request =
            std::get_if<protocol::JsonRpcRequest>(&*message)) {
      TransportRequestHandler handler;
      {
        std::lock_guard lock(mutex);
        handler = request_handler;
      }
      if (!handler) {
        const auto response = protocol::make_error_response(
            std::optional<protocol::RequestId>{request->id},
            protocol::make_error(
                protocol::ErrorCode::MethodNotFound,
                "http transport request handler is not configured"));
        const auto posted = post_response(response, request->method,
                                          header_name_from_request(*request));
        if (!posted) {
          return std::unexpected(posted.error());
        }
        return std::optional<protocol::JsonRpcResponse>{};
      }

      core::Result<protocol::JsonRpcResponse> response;
      try {
        response = handler(*request);
      } catch (const std::exception& ex) {
        response = std::unexpected(errors::handler_failed(ex.what()));
      } catch (...) {
        response = std::unexpected(errors::handler_unknown_exception());
      }
      if (!response) {
        response = protocol::make_error_response(
            std::optional<protocol::RequestId>{request->id},
            protocol::make_error(
                response.error().code, response.error().message,
                response.error().detail.empty()
                    ? std::nullopt
                    : std::optional<protocol::Json>{response.error().detail}));
      }

      const auto posted = post_response(*response, request->method,
                                        header_name_from_request(*request));
      if (!posted) {
        return std::unexpected(posted.error());
      }
      return std::optional<protocol::JsonRpcResponse>{};
    }

    const auto* response = std::get_if<protocol::JsonRpcResponse>(&*message);
    if (response == nullptr) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "http event stream received an unknown message"));
    }

    if (expected_response_id.has_value()) {
      if (response->id.has_value() && *response->id == *expected_response_id) {
        return std::optional<protocol::JsonRpcResponse>{*response};
      }
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "http event stream received an unexpected response",
          response->id.has_value() ? request_id_to_string(*response->id)
                                   : std::string{}));
    }

    return std::optional<protocol::JsonRpcResponse>{};
  }

  core::Result<core::Unit> post_response(
      const protocol::JsonRpcResponse& response, std::string_view method,
      std::optional<std::string> name) {
    const auto serialized = protocol::serialize_response(response);
    if (!serialized) {
      return std::unexpected(serialized.error());
    }

    auto client = make_client();
    auto headers = make_headers(method, std::move(name), /*json_body=*/true,
                                /*event_stream=*/false);
    const auto result =
        client.Post(path, headers, *serialized, "application/json");
    if (!result) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport failed to post response",
          httplib::to_string(result.error())));
    }

    if (result->status < 200 || result->status >= 300) {
      return std::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport response post returned an error status",
          std::to_string(result->status)));
    }

    const auto remembered_session =
        remember_session(*result, /*require_session_id=*/false);
    if (!remembered_session) {
      return std::unexpected(remembered_session.error());
    }
    return core::Unit{};
  }

  HttpTransportOptions options;
  std::optional<core::Error> options_error;
  std::string origin;
  std::string path;
  mutable std::mutex mutex;
  TransportRequestHandler request_handler;
  TransportNotificationHandler notification_handler;
  bool started = false;
  std::string session_id;
  bool stream_started = false;
  std::unique_ptr<httplib::Client> stream_client;
  std::unique_ptr<httplib::sse::SSEClient> sse_client;
};

HttpTransport::HttpTransport(HttpTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

HttpTransport::~HttpTransport() = default;

core::Result<protocol::JsonRpcResponse> HttpTransport::send(
    const protocol::JsonRpcRequest& request) {
  return impl_->send(request);
}

core::Result<core::Unit> HttpTransport::send_notification(
    const protocol::JsonRpcNotification& notification) {
  return impl_->send_notification(notification);
}

core::Result<core::Unit> HttpTransport::start(
    TransportRequestHandler request_handler,
    TransportNotificationHandler notification_handler) {
  return impl_->start(std::move(request_handler),
                      std::move(notification_handler));
}

void HttpTransport::stop() noexcept { impl_->stop(); }

}  // namespace mcp::client

namespace mcp::transport {
namespace {

core::Error make_native_http_error(protocol::ErrorCode code,
                                   std::string message,
                                   std::string detail = {}) {
  return core::Error{
      static_cast<int>(code),
      std::move(message),
      std::move(detail),
      "transport",
  };
}

client::HttpTransportOptions to_legacy_options(
    StreamableHttpClientTransportOptions options) {
  client::HttpTransportOptions legacy;
  legacy.uri = std::move(options.uri);
  legacy.host = std::move(options.host);
  legacy.port = options.port;
  legacy.path = std::move(options.path);
  legacy.headers = std::move(options.headers);
  legacy.auth_header = std::move(options.auth_header);
  legacy.timeout = options.timeout;
  return legacy;
}

std::string request_id_to_string_for_native_http(
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

class StreamableHttpClientTransport::Impl {
 public:
  explicit Impl(StreamableHttpClientTransportOptions options)
      : transport_(to_legacy_options(std::move(options))) {}

  ~Impl() { (void)close(); }

  protocol::Json diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return protocol::Json{
        {"name", "streamable-http"},
        {"closed", closed_},
        {"queued", inbound_.size()},
        {"pendingServerRequests", pending_server_requests_.size()},
        {"requestWorkers", request_threads_.size()},
        {"activeRequestWorkers", active_request_workers_},
        {"completedRequestWorkers", completed_request_workers_},
        {"failedRequestWorkers", failed_request_workers_},
        {"timedOutRequestWorkers", timed_out_request_workers_},
    };
  }

  core::Result<core::Unit> send(protocol::JsonRpcMessage message) {
    const auto started = ensure_started();
    if (!started) {
      return std::unexpected(started.error());
    }

    if (auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      return start_request_thread(std::move(*request));
    }

    if (auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      return transport_.send_notification(*notification);
    }

    auto* response = std::get_if<protocol::JsonRpcResponse>(&message);
    if (response == nullptr || !response->id.has_value()) {
      return std::unexpected(make_native_http_error(
          protocol::ErrorCode::InvalidRequest,
          "streamable http client transport cannot send response without id"));
    }
    return complete_server_request(std::move(*response));
  }

  core::Result<std::optional<protocol::JsonRpcMessage>> receive() {
    const auto started = ensure_started();
    if (!started) {
      return std::unexpected(started.error());
    }

    std::unique_lock<std::mutex> lock(mutex_);
    receive_cv_.wait(lock, [this] { return closed_ || !inbound_.empty(); });
    if (inbound_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(inbound_.front());
    inbound_.pop_front();
    return message;
  }

  core::Result<core::Unit> close() {
    std::map<protocol::RequestId, std::shared_ptr<PendingServerRequest>>
        pending;
    std::vector<std::thread> request_threads;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return core::Unit{};
      }
      closed_ = true;
      pending.swap(pending_server_requests_);
      request_threads.swap(request_threads_);
    }

    for (auto& [_, request] : pending) {
      {
        std::lock_guard<std::mutex> request_lock(request->mutex);
        request->response = protocol::make_error_response(
            std::optional<protocol::RequestId>{request->id},
            protocol::make_error(protocol::ErrorCode::InternalError,
                                 "streamable http transport closed"));
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
    return core::Unit{};
  }

 private:
  struct PendingServerRequest {
    explicit PendingServerRequest(protocol::RequestId request_id)
        : id(std::move(request_id)) {}

    protocol::RequestId id;
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<protocol::JsonRpcResponse> response;
  };

  core::Result<core::Unit> ensure_started() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return std::unexpected(
          make_native_http_error(protocol::ErrorCode::InvalidRequest,
                                 "streamable http client transport is closed"));
    }
    if (started_) {
      return core::Unit{};
    }
    started_ = true;

    auto started = transport_.start(
        [this](const protocol::JsonRpcRequest& request) {
          return handle_server_request(request);
        },
        [this](const protocol::JsonRpcNotification& notification) {
          return handle_server_notification(notification);
        });
    if (!started) {
      started_ = false;
      return std::unexpected(started.error());
    }
    return core::Unit{};
  }

  core::Result<core::Unit> start_request_thread(
      protocol::JsonRpcRequest request) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return std::unexpected(make_native_http_error(
            protocol::ErrorCode::InvalidRequest,
            "streamable http client transport is closed"));
      }
      ++active_request_workers_;
      request_threads_.emplace_back(
          [this, request = std::move(request)]() mutable {
            auto response = transport_.send(request);
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
      return std::unexpected(make_native_http_error(
          protocol::ErrorCode::InternalError,
          "failed to start streamable http request worker", ex.what()));
    }
    return core::Unit{};
  }

  core::Result<protocol::JsonRpcResponse> handle_server_request(
      const protocol::JsonRpcRequest& request) {
    auto pending = std::make_shared<PendingServerRequest>(request.id);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return protocol::make_error_response(
            std::optional<protocol::RequestId>{request.id},
            protocol::make_error(protocol::ErrorCode::InternalError,
                                 "streamable http transport closed"));
      }
      const auto [_, inserted] =
          pending_server_requests_.emplace(request.id, pending);
      if (!inserted) {
        return protocol::make_error_response(
            std::optional<protocol::RequestId>{request.id},
            protocol::make_error(protocol::ErrorCode::InvalidRequest,
                                 "duplicate server request id"));
      }
      inbound_.push_back(protocol::JsonRpcMessage{request});
    }

    receive_cv_.notify_one();

    std::unique_lock<std::mutex> pending_lock(pending->mutex);
    pending->cv.wait(pending_lock,
                     [&pending] { return pending->response.has_value(); });
    return std::move(*pending->response);
  }

  core::Result<core::Unit> handle_server_notification(
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

  core::Result<core::Unit> complete_server_request(
      protocol::JsonRpcResponse response) {
    const auto id = *response.id;
    std::shared_ptr<PendingServerRequest> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = pending_server_requests_.find(id);
      if (it == pending_server_requests_.end()) {
        return std::unexpected(make_native_http_error(
            protocol::ErrorCode::InvalidRequest,
            "streamable http client transport has no pending server request",
            request_id_to_string_for_native_http(id)));
      }
      pending = it->second;
      pending_server_requests_.erase(it);
    }

    {
      std::lock_guard<std::mutex> pending_lock(pending->mutex);
      pending->response = std::move(response);
    }
    pending->cv.notify_all();
    return core::Unit{};
  }

  client::HttpTransport transport_;
  mutable std::mutex mutex_;
  std::condition_variable receive_cv_;
  std::deque<protocol::JsonRpcMessage> inbound_;
  std::map<protocol::RequestId, std::shared_ptr<PendingServerRequest>>
      pending_server_requests_;
  std::vector<std::thread> request_threads_;
  std::size_t active_request_workers_ = 0;
  std::size_t completed_request_workers_ = 0;
  std::size_t failed_request_workers_ = 0;
  std::size_t timed_out_request_workers_ = 0;
  bool started_ = false;
  bool closed_ = false;
};

StreamableHttpClientTransport::StreamableHttpClientTransport(
    StreamableHttpClientTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

StreamableHttpClientTransport::~StreamableHttpClientTransport() = default;

std::string_view StreamableHttpClientTransport::name() const noexcept {
  return "streamable-http";
}

protocol::Json StreamableHttpClientTransport::diagnostics() const {
  return impl_->diagnostics();
}

core::Result<core::Unit> StreamableHttpClientTransport::send(
    TxMessage message) {
  return impl_->send(std::move(message));
}

core::Result<std::optional<StreamableHttpClientTransport::RxMessage>>
StreamableHttpClientTransport::receive() {
  return impl_->receive();
}

core::Result<core::Unit> StreamableHttpClientTransport::close() {
  return impl_->close();
}

}  // namespace mcp::transport
