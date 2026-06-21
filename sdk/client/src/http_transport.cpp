// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/client/http_transport.hpp"

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
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
constexpr std::string_view StatelessProtocolVersionMeta =
    "io.modelcontextprotocol/protocolVersion";
constexpr std::string_view StatelessClientInfoMeta =
    "io.modelcontextprotocol/clientInfo";
constexpr std::string_view StatelessClientCapabilitiesMeta =
    "io.modelcontextprotocol/clientCapabilities";

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

bool is_http_timeout_error(httplib::Error error) {
  return error == httplib::Error::Timeout || error == httplib::Error::Read;
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

protocol::JsonRpcRequest request_with_stateless_meta(
    const protocol::JsonRpcRequest& request,
    std::string_view protocol_version) {
  protocol::JsonRpcRequest result = request;
  if (result.method == protocol::InitializeMethod) {
    return result;
  }
  if (!result.params.is_object()) {
    result.params = protocol::Json{{"value", std::move(result.params)}};
  }
  auto& meta = result.params["_meta"];
  if (!meta.is_object()) {
    meta = protocol::Json::object();
  }
  if (!meta.contains(std::string(StatelessProtocolVersionMeta))) {
    meta[std::string(StatelessProtocolVersionMeta)] =
        std::string(protocol_version);
  }
  if (!meta.contains(std::string(StatelessClientInfoMeta))) {
    meta[std::string(StatelessClientInfoMeta)] =
        protocol::Json{{"name", "cxxmcp"}, {"version", "0"}};
  }
  if (!meta.contains(std::string(StatelessClientCapabilitiesMeta))) {
    meta[std::string(StatelessClientCapabilitiesMeta)] =
        protocol::Json::object();
  }
  return result;
}

bool header_name_equals(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    char left = lhs[index];
    char right = rhs[index];
    if (left >= 'A' && left <= 'Z') {
      left = static_cast<char>(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = static_cast<char>(right - 'A' + 'a');
    }
    if (left != right) {
      return false;
    }
  }
  return true;
}

bool has_header_name(const httplib::Headers& headers, std::string_view name) {
  for (const auto& [key, _] : headers) {
    if (header_name_equals(key, name)) {
      return true;
    }
  }
  return false;
}

httplib::Headers to_headers(
    const std::unordered_map<std::string, std::string>& headers,
    const std::optional<std::string>& bearer_token = std::nullopt) {
  httplib::Headers result;
  for (const auto& [key, value] : headers) {
    result.emplace(key, value);
  }
  if (bearer_token.has_value() && !bearer_token->empty() &&
      !has_header_name(result, "Authorization")) {
    result.emplace("Authorization", "Bearer " + *bearer_token);
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

std::unordered_map<std::string, std::string> copy_response_headers(
    const httplib::Response& response) {
  std::unordered_map<std::string, std::string> headers;
  for (const auto& header : response.headers) {
    headers.emplace(header.first, header.second);
  }
  return headers;
}

core::Error make_http_auth_error(const httplib::Response& response) {
  std::string detail = std::to_string(response.status);
  if (response.has_header("WWW-Authenticate")) {
    detail = response.get_header_value("WWW-Authenticate");
  }
  return core::Error{
      static_cast<int>(protocol::ErrorCode::PermissionDenied),
      response.status == 403 ? "http transport request has insufficient scope"
                             : "http transport request requires authorization",
      std::move(detail),
      "auth",
  };
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
      return mcp::core::unexpected(make_transport_error(
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
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "unsupported http transport uri scheme", components.scheme));
  }

  if (components.host.empty()) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "http transport endpoint is missing a host"));
  }

  const bool is_ssl = components.scheme == "https";
  int port = is_ssl ? 443 : 80;
  if (!components.port.empty() &&
      !httplib::detail::parse_port(components.port, port)) {
    return mcp::core::unexpected(make_transport_error(
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

struct SseEvent {
  std::string event;  // event type (default: "message")
  std::string data;   // concatenated data lines
  std::string id;     // event ID
  int retry_ms = -1;  // retry interval, -1 if not set
};

std::vector<SseEvent> parse_sse_events(std::string_view body) {
  std::vector<SseEvent> events;
  SseEvent current;

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
      // Blank line = dispatch event (even if data is empty, for priming events)
      events.push_back(std::move(current));
      current = {};
    } else if (line.front() != ':') {
      const auto colon = line.find(':');
      const auto field =
          colon == std::string_view::npos ? line : line.substr(0, colon);
      std::string_view value;
      if (colon != std::string_view::npos && colon + 1 < line.size()) {
        value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') {
          value.remove_prefix(1);
        }
      }
      if (field == "data") {
        if (!current.data.empty()) {
          current.data.push_back('\n');
        }
        current.data.append(value.begin(), value.end());
      } else if (field == "event") {
        current.event.assign(value.begin(), value.end());
      } else if (field == "id") {
        current.id.assign(value.begin(), value.end());
      } else if (field == "retry") {
        // Parse integer retry value
        std::string retry_str(value.begin(), value.end());
        try {
          current.retry_ms = std::stoi(retry_str);
        } catch (...) {
          // ignore unparseable retry
        }
      }
    }

    if (line_end == body.size()) {
      break;
    }
    line_start = line_end + 1;
  }

  // Flush any trailing event (no trailing blank line)
  if (!current.data.empty() || !current.id.empty() || current.retry_ms >= 0) {
    events.push_back(std::move(current));
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
    std::string protocol_version_to_send;
    {
      std::lock_guard lock(mutex);
      if (options.stateless) {
        session_id.clear();
        protocol_version = std::string(protocol::McpProtocolVersion);
        return;
      }
      session_id_to_terminate = session_id;
      protocol_version_to_send = protocol_version;
    }

    if (!session_id_to_terminate.empty() && options_error == std::nullopt) {
      auto client = make_client();
      auto headers = make_base_headers();
      headers.emplace("Accept", "application/json");
      headers.emplace("MCP-Protocol-Version", protocol_version_to_send);
      headers.emplace(std::string(SessionHeader), session_id_to_terminate);
      (void)client.Delete(path, headers);
    }

    reset_session();
  }

  core::Result<protocol::JsonRpcResponse> send(
      const protocol::JsonRpcRequest& request) {
    bool sse_reconnected = false;

    if (options_error.has_value()) {
      return mcp::core::unexpected(*options_error);
    }

    std::string request_protocol_version;
    {
      std::lock_guard lock(mutex);
      request_protocol_version =
          request.protocol_version_override.value_or(protocol_version);
    }
    const auto outbound_request =
        options.stateless
            ? request_with_stateless_meta(request, request_protocol_version)
            : request;

    const auto serialized = protocol::serialize_request(outbound_request);
    if (!serialized) {
      return mcp::core::unexpected(serialized.error());
    }

    bool retried_after_session_reset = false;
    bool retried_after_auth_refresh = false;
    while (true) {
      auto client = make_client();
      const bool include_protocol_version =
          outbound_request.method != protocol::InitializeMethod;
      auto headers = make_headers(
          outbound_request.method, header_name_from_request(outbound_request),
          /*json_body=*/true,
          /*event_stream=*/true, include_protocol_version);
      for (const auto& [key, value] : outbound_request.transport_headers) {
        headers.emplace(key, value);
      }
      if (outbound_request.protocol_version_override.has_value()) {
        headers.erase("MCP-Protocol-Version");
        headers.emplace("MCP-Protocol-Version",
                        *outbound_request.protocol_version_override);
      }
      const auto response =
          client.Post(path, headers, *serialized, "application/json");
      if (!response) {
        if (options.timeout.count() > 0 &&
            is_http_timeout_error(response.error())) {
          return mcp::core::unexpected(make_transport_error(
              static_cast<int>(protocol::ErrorCode::InternalError),
              "http transport request timed out",
              httplib::to_string(response.error())));
        }
        return mcp::core::unexpected(make_transport_error(
            static_cast<int>(protocol::ErrorCode::InternalError),
            "http transport request failed",
            httplib::to_string(response.error())));
      }
      if (response->status == 404) {
        reset_session();
        if (outbound_request.method == protocol::InitializeMethod &&
            !retried_after_session_reset) {
          retried_after_session_reset = true;
          continue;
        }
        return mcp::core::unexpected(make_transport_error(
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "http transport session was terminated",
            std::to_string(response->status)));
      }
      if ((response->status == 401 || response->status == 403) &&
          !retried_after_auth_refresh) {
        auto refreshed =
            refresh_bearer_token_once(*response, outbound_request.method);
        if (refreshed.has_value()) {
          retried_after_auth_refresh = true;
          continue;
        }
      }
      if (response->status == 401 || response->status == 403) {
        return mcp::core::unexpected(make_http_auth_error(*response));
      }

      const auto remembered_session = remember_session(
          *response, outbound_request.method == protocol::InitializeMethod);
      if (!remembered_session) {
        return mcp::core::unexpected(remembered_session.error());
      }

      auto decoded_response = decode_request_response(
          *response, outbound_request.id, sse_reconnected);
      if (!decoded_response) {
        return mcp::core::unexpected(decoded_response.error());
      }
      if (outbound_request.method == protocol::InitializeMethod) {
        remember_protocol_version_from_initialize_response(*decoded_response);
      }

      // Only start the SSE stream if the response is successful.
      // For error responses (e.g. version negotiation), skip stream setup
      // so the error response can be returned for retry logic.
      // Also skip if POST→GET reconnection was already handled (SEP-1699).
      if (!options.stateless && !decoded_response->error.has_value() &&
          !sse_reconnected) {
        const auto stream_started = ensure_stream_started();
        if (!stream_started) {
          return mcp::core::unexpected(stream_started.error());
        }
      }

      return *decoded_response;
    }
  }

  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) {
    if (options_error.has_value()) {
      return mcp::core::unexpected(*options_error);
    }

    auto outbound_notification = notification;
    if (options.stateless) {
      protocol::JsonRpcRequest pseudo_request;
      pseudo_request.method = outbound_notification.method;
      pseudo_request.params = outbound_notification.params;
      pseudo_request.id = std::int64_t{0};
      std::string request_protocol_version;
      {
        std::lock_guard lock(mutex);
        request_protocol_version = protocol_version;
      }
      outbound_notification.params =
          request_with_stateless_meta(pseudo_request, request_protocol_version)
              .params;
    }

    const auto serialized =
        protocol::serialize_notification(outbound_notification);
    if (!serialized) {
      return mcp::core::unexpected(serialized.error());
    }

    bool retried_after_auth_refresh = false;
    httplib::Result response;
    while (true) {
      auto client = make_client();
      auto headers = make_headers(outbound_notification.method, std::nullopt,
                                  /*json_body=*/true, /*event_stream=*/true);
      response = client.Post(path, headers, *serialized, "application/json");
      if (!response || (response->status != 401 && response->status != 403) ||
          retried_after_auth_refresh) {
        break;
      }
      auto refreshed =
          refresh_bearer_token_once(*response, outbound_notification.method);
      if (!refreshed.has_value()) {
        break;
      }
      retried_after_auth_refresh = true;
    }
    if (!response) {
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport notification failed",
          httplib::to_string(response.error())));
    }
    if (response->status == 404) {
      reset_session();
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "http transport session was terminated",
          std::to_string(response->status)));
    }
    if (response->status == 401 || response->status == 403) {
      return mcp::core::unexpected(make_http_auth_error(*response));
    }

    if (!options.stateless) {
      const auto remembered_session =
          remember_session(*response, /*require_session_id=*/false);
      if (!remembered_session) {
        return mcp::core::unexpected(remembered_session.error());
      }
      const auto stream_started_result = ensure_stream_started();
      if (!stream_started_result) {
        return mcp::core::unexpected(stream_started_result.error());
      }
    }

    if (response->status < 200 || response->status >= 300) {
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport notification returned an error status",
          std::to_string(response->status)));
    }

    if (has_event_stream_content_type(*response) && !response->body.empty()) {
      for (const auto& event : parse_sse_events(response->body)) {
        if (event.data.empty()) continue;
        const auto handled = dispatch_event_payload(event.data, std::nullopt);
        if (!handled) {
          return mcp::core::unexpected(handled.error());
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
      protocol_version = std::string(protocol::McpProtocolVersion);
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

  httplib::Headers make_base_headers(
      std::optional<std::string> bearer_override = std::nullopt) const {
    std::unordered_map<std::string, std::string> headers;
    std::optional<std::string> auth_header;
    {
      std::lock_guard lock(mutex);
      headers = options.headers;
      auth_header = bearer_override.has_value() ? std::move(bearer_override)
                                                : options.auth_header;
    }
    return to_headers(headers, auth_header);
  }

  httplib::Headers make_headers(
      std::optional<std::string_view> method, std::optional<std::string> name,
      bool json_body, bool event_stream, bool include_protocol_version = true,
      std::optional<std::string> bearer_override = std::nullopt) const {
    auto headers = make_base_headers(std::move(bearer_override));
    if (json_body) {
      headers.emplace("Content-Type", "application/json");
    }
    headers.emplace("Accept", event_stream
                                  ? "application/json, text/event-stream"
                                  : "application/json");
    if (method.has_value()) {
      headers.emplace(std::string(MethodHeader), std::string(*method));
    }
    if (name.has_value()) {
      headers.emplace(std::string(NameHeader), std::move(*name));
    }

    std::lock_guard lock(mutex);
    if (include_protocol_version) {
      headers.emplace("MCP-Protocol-Version", protocol_version);
    }
    if (!session_id.empty()) {
      headers.emplace(std::string(SessionHeader), session_id);
    }
    return headers;
  }

  std::optional<std::string> refresh_bearer_token_once(
      const httplib::Response& response, std::string_view method) {
    HttpAuthRefreshHandler refresh_handler;
    {
      std::lock_guard lock(mutex);
      refresh_handler = options.auth_refresh_handler;
    }
    if (!refresh_handler ||
        (response.status != 401 && response.status != 403)) {
      return std::nullopt;
    }

    HttpAuthChallenge challenge;
    challenge.status_code = response.status;
    challenge.method = std::string(method);
    challenge.headers = copy_response_headers(response);
    if (response.has_header("WWW-Authenticate")) {
      challenge.www_authenticate =
          response.get_header_value("WWW-Authenticate");
    }

    auto refreshed = refresh_handler(challenge);
    if (!refreshed.has_value() || refreshed->empty()) {
      return std::nullopt;
    }
    {
      std::lock_guard lock(mutex);
      options.auth_header = *refreshed;
    }
    return refreshed;
  }

  core::Result<core::Unit> remember_session(const httplib::Response& response,
                                            bool require_session_id) {
    if (options.stateless) {
      return core::Unit{};
    }
    if (!response.has_header(std::string(SessionHeader))) {
      return core::Unit{};
    }
    const auto response_session_id =
        response.get_header_value(std::string(SessionHeader));
    if (response_session_id.empty()) {
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "http transport response has an empty Mcp-Session-Id header"));
    }
    std::lock_guard lock(mutex);
    session_id = response_session_id;
    return core::Unit{};
  }

  void remember_protocol_version_from_initialize_response(
      const protocol::JsonRpcResponse& response) {
    if (!response.result.has_value() || !response.result->is_object() ||
        !response.result->contains("protocolVersion") ||
        !response.result->at("protocolVersion").is_string()) {
      return;
    }

    const auto negotiated =
        response.result->at("protocolVersion").get<std::string>();
    std::lock_guard lock(mutex);
    protocol_version = negotiated;
  }

  core::Result<core::Unit> ensure_stream_started() {
    {
      std::lock_guard lock(mutex);
      if (!started || stream_started || session_id.empty()) {
        return core::Unit{};
      }
      stream_client = std::make_unique<httplib::Client>(origin);
      apply_timeout(*stream_client, options.timeout);
      auto headers = to_headers(options.headers, options.auth_header);
      headers.emplace("Accept", "text/event-stream");
      headers.emplace("MCP-Protocol-Version", protocol_version);
      headers.emplace(std::string(SessionHeader), session_id);
      sse_client = std::make_unique<httplib::sse::SSEClient>(*stream_client,
                                                             path, headers);
      sse_client->set_reconnect_interval(250);
      sse_client->set_max_reconnect_attempts(-1);
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
      const httplib::Response& response, const protocol::RequestId& request_id,
      bool& sse_reconnected) {
    if (response.status < 200 || response.status >= 300) {
      // Try to parse a JSON-RPC error from the response body.
      // Some servers (e.g. conformance) return version-negotiation errors
      // with a JSON-RPC error body on non-2xx status.
      if (!response.body.empty()) {
        if (auto parsed = protocol::parse_response(response.body);
            parsed && parsed->error.has_value()) {
          return *parsed;
        }
      }
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport request returned an error status",
          std::to_string(response.status)));
    }

    if (response.body.empty()) {
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport returned an empty response body"));
    }

    if (!has_event_stream_content_type(response)) {
      const auto parsed = protocol::parse_response(response.body);
      if (!parsed) {
        return mcp::core::unexpected(parsed.error());
      }
      if (!parsed->id.has_value() || *parsed->id != request_id) {
        return mcp::core::unexpected(make_transport_error(
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "http transport received an unexpected response",
            parsed->id.has_value() ? request_id_to_string(*parsed->id)
                                   : std::string{}));
      }
      return *parsed;
    }

    std::optional<protocol::JsonRpcResponse> final_response;
    const auto events = parse_sse_events(response.body);
    int retry_ms = -1;
    std::string last_event_id;

    for (const auto& event : events) {
      if (!event.id.empty()) {
        last_event_id = event.id;
      }
      if (event.retry_ms >= 0) {
        retry_ms = event.retry_ms;
      }
      if (event.data.empty()) continue;
      const auto handled = dispatch_event_payload(event.data, request_id);
      if (!handled) {
        return mcp::core::unexpected(handled.error());
      }
      if (handled->has_value()) {
        final_response = std::move(**handled);
      }
    }

    // SEP-1699: If no response found but a retry hint was provided,
    // reconnect via GET with Last-Event-ID to receive the response.
    if (!final_response.has_value() && retry_ms > 0 && !last_event_id.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(retry_ms));

      auto get_client = make_client();
      auto get_headers =
          make_headers(std::nullopt, std::nullopt, /*json_body=*/false,
                       /*event_stream=*/true);
      get_headers.emplace("Last-Event-ID", last_event_id);

      auto sse = std::make_unique<httplib::sse::SSEClient>(get_client, path,
                                                           get_headers);
      sse->set_reconnect_interval(0);
      sse->set_max_reconnect_attempts(1);

      std::promise<protocol::JsonRpcResponse> promise;
      auto future = promise.get_future();
      std::atomic<bool> promise_set{false};

      sse->on_event("message", [&](const httplib::sse::SSEMessage& msg) {
        if (msg.data.empty() || promise_set.load()) return;
        auto handled = dispatch_event_payload(msg.data, request_id);
        if (handled && handled->has_value()) {
          promise_set.store(true);
          promise.set_value(std::move(**handled));
        }
      });
      sse->on_error([](httplib::Error) {});
      sse->start_async();

      if (future.wait_for(std::chrono::seconds(10)) ==
          std::future_status::timeout) {
        sse->stop();
        return mcp::core::unexpected(make_transport_error(
            static_cast<int>(protocol::ErrorCode::InternalError),
            "SSE retry reconnection timed out",
            request_id_to_string(request_id)));
      }
      sse->stop();
      sse_reconnected = true;
      return future.get();
    }

    if (!final_response.has_value()) {
      return mcp::core::unexpected(make_transport_error(
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
      return mcp::core::unexpected(message.error());
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
          handled = mcp::core::unexpected(errors::handler_failed(ex.what()));
        } catch (...) {
          handled = mcp::core::unexpected(errors::handler_unknown_exception());
        }
        if (!handled) {
          return mcp::core::unexpected(handled.error());
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
          return mcp::core::unexpected(posted.error());
        }
        return std::optional<protocol::JsonRpcResponse>{};
      }

      core::Result<protocol::JsonRpcResponse> response;
      try {
        response = handler(*request);
      } catch (const std::exception& ex) {
        response = mcp::core::unexpected(errors::handler_failed(ex.what()));
      } catch (...) {
        response = mcp::core::unexpected(errors::handler_unknown_exception());
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
        return mcp::core::unexpected(posted.error());
      }
      return std::optional<protocol::JsonRpcResponse>{};
    }

    const auto* response = std::get_if<protocol::JsonRpcResponse>(&*message);
    if (response == nullptr) {
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "http event stream received an unknown message"));
    }

    if (expected_response_id.has_value()) {
      if (response->id.has_value() && *response->id == *expected_response_id) {
        return std::optional<protocol::JsonRpcResponse>{*response};
      }
      return mcp::core::unexpected(make_transport_error(
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
      return mcp::core::unexpected(serialized.error());
    }

    auto client = make_client();
    auto headers = make_headers(method, std::move(name), /*json_body=*/true,
                                /*event_stream=*/true);
    const auto result =
        client.Post(path, headers, *serialized, "application/json");
    if (!result) {
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport failed to post response",
          httplib::to_string(result.error())));
    }

    if (result->status < 200 || result->status >= 300) {
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "http transport response post returned an error status",
          std::to_string(result->status)));
    }

    const auto remembered_session =
        remember_session(*result, /*require_session_id=*/false);
    if (!remembered_session) {
      return mcp::core::unexpected(remembered_session.error());
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
  std::string protocol_version = std::string(protocol::McpProtocolVersion);
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

void HttpTransport::set_negotiated_protocol_version(std::string version) {
  impl_->protocol_version = std::move(version);
}

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
  if (options.auth_refresh_handler) {
    legacy.auth_refresh_handler =
        [handler = std::move(options.auth_refresh_handler)](
            const client::HttpAuthChallenge& challenge)
        -> std::optional<std::string> {
      StreamableHttpAuthChallenge native_challenge;
      native_challenge.status_code = challenge.status_code;
      native_challenge.method = challenge.method;
      native_challenge.headers = challenge.headers;
      native_challenge.www_authenticate = challenge.www_authenticate;
      return handler(native_challenge);
    };
  }
  legacy.timeout = options.timeout;
  legacy.stateless = options.stateless;
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
      return mcp::core::unexpected(started.error());
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
      return mcp::core::unexpected(make_native_http_error(
          protocol::ErrorCode::InvalidRequest,
          "streamable http client transport cannot send response without id"));
    }
    return complete_server_request(std::move(*response));
  }

  core::Result<std::optional<protocol::JsonRpcMessage>> receive() {
    const auto started = ensure_started();
    if (!started) {
      return mcp::core::unexpected(started.error());
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
      return mcp::core::unexpected(
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
      return mcp::core::unexpected(started.error());
    }
    return core::Unit{};
  }

  core::Result<core::Unit> start_request_thread(
      protocol::JsonRpcRequest request) {
    const auto request_id = request.id;
    bool registered = false;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return mcp::core::unexpected(make_native_http_error(
            protocol::ErrorCode::InvalidRequest,
            "streamable http client transport is closed"));
      }
      const auto [_, inserted] = in_flight_request_ids_.insert(request_id);
      if (!inserted) {
        return mcp::core::unexpected(make_native_http_error(
            protocol::ErrorCode::InvalidRequest,
            "duplicate streamable http request id",
            request_id_to_string_for_native_http(request_id)));
      }
      registered = true;
      ++active_request_workers_;
      request_threads_.emplace_back(
          [this, request_id, request = std::move(request)]() mutable {
            auto response = transport_.send(request);
            if (response) {
              finish_request_worker(request_id, false, false);
              enqueue(protocol::JsonRpcMessage{std::move(*response)});
              return;
            }
            finish_request_worker(request_id, true,
                                  is_timeout_error(response.error()));
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
      if (registered) {
        forget_request_worker(request_id);
      }
      return mcp::core::unexpected(make_native_http_error(
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

  void finish_request_worker(const protocol::RequestId& request_id, bool failed,
                             bool timed_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    in_flight_request_ids_.erase(request_id);
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

  void forget_request_worker(const protocol::RequestId& request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    in_flight_request_ids_.erase(request_id);
    if (active_request_workers_ > 0) {
      --active_request_workers_;
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
        return mcp::core::unexpected(make_native_http_error(
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
  std::set<protocol::RequestId> in_flight_request_ids_;
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
