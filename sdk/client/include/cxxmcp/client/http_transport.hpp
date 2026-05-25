// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief HTTP client transport for MCP streamable HTTP and SSE-compatible
/// endpoints.

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "cxxmcp/client/client.hpp"

namespace mcp::client {

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

  /// Extra request headers sent on outbound HTTP requests.
  std::unordered_map<std::string, std::string> headers;

  /// Optional bearer token inserted as an Authorization header.
  std::optional<std::string> auth_header;

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

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::client
