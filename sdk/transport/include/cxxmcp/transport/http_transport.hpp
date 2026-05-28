// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Role-generic client transport for Streamable HTTP MCP servers.

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cxxmcp/transport/transport.hpp"

namespace mcp::transport {

/// @brief HTTP auth challenge observed by Streamable HTTP client transport.
struct StreamableHttpAuthChallenge {
  int status_code = 0;
  std::string method;
  std::unordered_map<std::string, std::string> headers;
  std::optional<std::string> www_authenticate;
};

/// @brief Application hook used for one-shot bearer refresh on HTTP 401.
using StreamableHttpAuthRefreshHandler =
    std::function<std::optional<std::string>(
        const StreamableHttpAuthChallenge&)>;

/// @brief Configuration for a Streamable HTTP client transport.
struct StreamableHttpClientTransportOptions {
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
  StreamableHttpAuthRefreshHandler auth_refresh_handler;

  /// Connect, read, and write timeout used by the transport.
  std::chrono::milliseconds timeout{30000};
};

/// @brief Native transport-contract client for Streamable HTTP.
///
/// This class exposes Streamable HTTP through transport::ClientTransport while
/// reusing the established client HTTP implementation. Sending requests starts
/// background workers whose responses are returned by receive(); sending a
/// response completes an inbound server-to-client request previously returned
/// by receive(). receive() blocks until a queued message is available or
/// close() completes. Concurrent receive() calls are not supported.
class StreamableHttpClientTransport final : public ClientTransport {
 public:
  explicit StreamableHttpClientTransport(
      StreamableHttpClientTransportOptions options);
  ~StreamableHttpClientTransport() override;

  StreamableHttpClientTransport(const StreamableHttpClientTransport&) = delete;
  StreamableHttpClientTransport& operator=(
      const StreamableHttpClientTransport&) = delete;

  std::string_view name() const noexcept override;
  protocol::Json diagnostics() const override;
  core::Result<core::Unit> send(TxMessage message) override;
  core::Result<std::optional<RxMessage>> receive() override;
  core::Result<core::Unit> close() override;

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

/// @brief Configuration for a Streamable HTTP server transport.
struct StreamableHttpServerTransportOptions {
  /// Interface address passed to the underlying HTTP server.
  std::string listen_host = "127.0.0.1";

  /// TCP port to listen on. Must be in the range 1..65535.
  int listen_port = 0;

  /// HTTP path for POST, GET/SSE, and DELETE session requests.
  std::string path = "/mcp";

  /// Optional SSE retry interval hint for the priming event.
  std::optional<std::chrono::milliseconds> sse_retry;

  /// Optional Origin allow-list. Empty means Origin is not restricted.
  std::vector<std::string> allowed_origins;

  /// Host allow-list used to reject DNS-rebinding attempts. Empty disables Host
  /// validation. Entries may be hostnames/IP literals or host:port authorities.
  std::vector<std::string> allowed_hosts = {"localhost", "127.0.0.1", "::1"};

  /// Maximum server-to-client events waiting for an SSE stream.
  std::size_t max_pending_sse_events = 1024;

  /// Maximum serialized bytes waiting for an SSE stream.
  std::size_t max_pending_sse_bytes = 4 * 1024 * 1024;

  /// Number of delivered SSE events retained for Last-Event-ID replay.
  std::size_t max_sse_replay_events = 256;

  /// Maximum time to wait for a client response to a server-to-client request.
  /// Set to zero or a negative duration to wait indefinitely.
  std::chrono::milliseconds request_timeout{30000};

  /// Maximum HTTP request body size accepted by the underlying server.
  /// Set to zero to disable the limit.
  std::size_t max_request_body_bytes = 10 * 1024 * 1024;

  /// Socket read timeout for HTTP request handling.
  std::chrono::milliseconds read_timeout{30000};

  /// Socket write timeout for HTTP response handling.
  std::chrono::milliseconds write_timeout{30000};

  /// Maximum active HTTP sessions accepted by this transport. Set to zero to
  /// disable the limit.
  std::size_t max_sessions = 1024;
};

/// @brief Native transport-contract server for Streamable HTTP.
///
/// receive() owns a background legacy HTTP server loop and returns inbound
/// client requests and notifications. Sending a response completes a client
/// request previously returned by receive(); sending a request or notification
/// delivers it to the active HTTP session through the established server HTTP
/// transport. Concurrent receive() calls are not supported.
class StreamableHttpServerTransport final : public ServerTransport {
 public:
  explicit StreamableHttpServerTransport(
      StreamableHttpServerTransportOptions options);
  ~StreamableHttpServerTransport() override;

  StreamableHttpServerTransport(const StreamableHttpServerTransport&) = delete;
  StreamableHttpServerTransport& operator=(
      const StreamableHttpServerTransport&) = delete;

  std::string_view name() const noexcept override;
  protocol::Json diagnostics() const override;
  core::Result<core::Unit> send(TxMessage message) override;
  core::Result<std::optional<RxMessage>> receive() override;
  core::Result<core::Unit> close() override;

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::transport
