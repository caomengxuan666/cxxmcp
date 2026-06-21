// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief HTTP client transport for MCP streamable HTTP and SSE-compatible
/// endpoints.

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "cxxmcp/client/client.hpp"

namespace mcp::client {

/// @brief HTTP auth challenge observed by the client transport.
struct HttpAuthChallenge {
  /// HTTP status code, normally 401 or 403.
  int status_code = 0;
  /// Request method name that received the challenge.
  std::string method;
  /// Response headers copied from the failed HTTP response.
  std::unordered_map<std::string, std::string> headers;
  /// First `WWW-Authenticate` header value, when present.
  std::optional<std::string> www_authenticate;
};

/// @brief Configuration for the client HTTP transport.
struct HttpTransportOptions {
  /// Full HTTP or HTTPS URI for the MCP endpoint.
  ///
  /// When set, this overrides host/port/path and may include a path segment.
  std::string uri;

  /// Remote host name or IP address.
  std::string host;

  /// Remote TCP port.
  int port = 80;

  /// HTTP path used for MCP requests.
  std::string path = "/";

  /// Extra request headers sent on every outbound HTTP request, including
  /// Streamable HTTP POST, SSE GET, and session DELETE requests.
  std::unordered_map<std::string, std::string> headers;

  /// Optional bearer token inserted as `Authorization: Bearer <token>` on
  /// every outbound HTTP request. Empty tokens are ignored. If `headers`
  /// already contains `Authorization`, the explicit header wins.
  std::optional<std::string> auth_header;

  /// Optional refresh hook invoked once for a 401 response before surfacing the
  /// auth failure. The returned token is stored as the new bearer token for the
  /// retry and future requests.
  HttpAuthRefreshHandler auth_refresh_handler;

  /// Enable stateless MCP HTTP mode. When true, the transport does not retain
  /// Mcp-Session-Id, does not open the SSE receive stream, and adds the
  /// required stateless `_meta` fields to non-initialize requests.
  bool stateless = false;

  /// Connect, read, and write timeout used by the transport.
  std::chrono::milliseconds timeout{30000};
};

/// @brief Client transport that exchanges MCP JSON-RPC messages over HTTP.
///
/// The transport implements Client::Transport and can be used directly with
/// Client(std::unique_ptr<Transport>) or indirectly through Client factory
/// helpers.
class HttpTransport final : public Transport {
 public:
  /// @brief Creates an HTTP transport from endpoint options.
  /// @param options Host, port, path, headers, and timeout configuration.
  explicit HttpTransport(HttpTransportOptions options);

  ~HttpTransport() override;

  /// @brief Sends one JSON-RPC request to the HTTP endpoint.
  core::Result<protocol::JsonRpcResponse> send(
      const protocol::JsonRpcRequest& request) override;

  /// @brief Sends a JSON-RPC notification to the HTTP endpoint.
  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) override;

  /// @brief Starts receive-side handling for server-initiated messages.
  /// @param request_handler Handler for inbound server requests.
  /// @param notification_handler Handler for inbound server notifications.
  core::Result<core::Unit> start(
      TransportRequestHandler request_handler,
      TransportNotificationHandler notification_handler = {}) override;

  /// @brief Stops background receive activity and releases transport resources.
  void stop() noexcept override;

  /// @brief Updates the negotiated protocol version for subsequent requests.
  /// Called after version negotiation to ensure headers reflect the agreed
  /// version.
  void set_negotiated_protocol_version(std::string version);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::client
