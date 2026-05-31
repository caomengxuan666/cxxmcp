// Copyright (c) 2025 [arookieofc]

#pragma once

/// @file
/// @brief WebSocket transport implementations for MCP client and server peers.
///
/// These transports wrap the WebSocket support built into cpp-httplib, providing
/// a full-duplex, message-oriented channel for JSON-RPC over WebSocket.
/// The client transport supports automatic reconnection with exponential backoff.
///
/// Requires `CXXMCP_ENABLE_WEBSOCKET` (which in turn requires
/// `CXXMCP_ENABLE_HTTP`, since both share the cpp-httplib dependency).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"
#include "cxxmcp/roles.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp::transport {

/// @brief Configuration for the WebSocket client transport.
struct WebSocketClientTransportOptions {
  /// Full ws:// or wss:// URI for the MCP endpoint.
  /// If set, host/port/path are derived from this value.
  std::string uri;

  /// Remote host (used when @ref uri is empty).
  std::string host = "127.0.0.1";

  /// Remote port (used when @ref uri is empty).
  int port = 80;

  /// WebSocket path (used when @ref uri is empty).
  std::string path = "/mcp";

  /// Extra HTTP headers sent during the WebSocket upgrade handshake.
  std::unordered_map<std::string, std::string> headers;

  /// Optional Authorization header value (e.g. "Bearer <token>").
  std::optional<std::string> auth_header;

  /// Per-operation timeout for the WebSocket connection.
  std::chrono::milliseconds timeout{30000};

  /// Enable automatic reconnection on disconnect.
  bool auto_reconnect = true;

  /// Maximum consecutive reconnection attempts. 0 = unlimited.
  int max_reconnect_attempts = 0;

  /// Initial backoff delay before the first reconnect attempt.
  std::chrono::milliseconds reconnect_initial_delay{1000};

  /// Maximum backoff delay (cap for exponential growth).
  std::chrono::milliseconds reconnect_max_delay{30000};

  /// Backoff multiplier applied after each failed attempt.
  double reconnect_backoff_multiplier = 2.0;

  /// WebSocket ping interval in seconds (0 = disabled).
  time_t ping_interval_sec = 30;

  /// Maximum missed pongs before considering the connection dead.
  int max_missed_pongs = 3;
};

/// @brief Configuration for the WebSocket server transport.
struct WebSocketServerTransportOptions {
  /// Interface address to bind.
  std::string listen_host = "127.0.0.1";

  /// TCP port to listen on.
  int listen_port = 0;

  /// WebSocket endpoint path pattern (e.g. "/mcp").
  std::string path = "/mcp";

  /// Maximum concurrent WebSocket connections. 0 = unlimited.
  std::size_t max_connections = 1024;

  /// WebSocket ping interval in seconds (0 = disabled).
  time_t ping_interval_sec = 30;

  /// Maximum missed pongs before dropping a client.
  int max_missed_pongs = 3;
};

/// @brief WebSocket-based MCP client transport.
///
/// Provides a full-duplex JSON-RPC channel over WebSocket with built-in
/// automatic reconnection using exponential backoff.
///
/// Thread safety: send() is safe to call from multiple threads. receive() is
/// sequential and must not be called concurrently. close() is safe to call
/// from any thread and will unblock a blocked receive().
///
/// @code
/// WebSocketClientTransportOptions options;
/// options.uri = "ws://127.0.0.1:3001/mcp";
/// WebSocketClientTransport transport(std::move(options));
///
/// auto req = make_initialize_request("my-client", "1.0.0");
/// auto sent = transport.send(JsonRpcMessage{std::move(req)});
/// auto received = transport.receive();
/// @endcode
class WebSocketClientTransport final : public ClientTransport {
 public:
  /// @brief Constructs a client transport with the given options.
  /// @param options Connection and reconnection configuration.
  explicit WebSocketClientTransport(WebSocketClientTransportOptions options);

  /// @brief Stops the reader thread and closes the WebSocket connection.
  ~WebSocketClientTransport() override;

  WebSocketClientTransport(const WebSocketClientTransport&) = delete;
  WebSocketClientTransport& operator=(const WebSocketClientTransport&) = delete;

  /// @brief Returns "websocket-client".
  std::string_view name() const noexcept override;

  /// @brief Returns structured diagnostics including connection state and stats.
  protocol::Json diagnostics() const override;

  /// @brief Sends a JSON-RPC message over the WebSocket connection.
  ///
  /// For requests, the response is delivered through receive() once the peer
  /// replies. If the connection is down and auto_reconnect is enabled, an
  /// immediate reconnection attempt is made before sending.
  core::Result<core::Unit> send(TxMessage message) override;

  /// @brief Blocks until the next inbound JSON-RPC message is available.
  ///
  /// Returns std::nullopt when the transport is closed.
  core::Result<std::optional<RxMessage>> receive() override;

  /// @brief Closes the transport and unblocks any blocked receive().
  core::Result<core::Unit> close() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/// @brief WebSocket-based MCP server transport.
///
/// Accepts WebSocket connections on a configurable path and provides a
/// full-duplex JSON-RCP channel for each connected client. Responses are
/// automatically routed to the connection that sent the corresponding request.
///
/// Thread safety: send() is safe to call from multiple threads. receive() is
/// sequential. close() is safe to call from any thread.
///
/// @code
/// WebSocketServerTransportOptions options;
/// options.listen_port = 3001;
/// options.path = "/mcp";
/// WebSocketServerTransport transport(std::move(options));
///
/// transport.wait_until_ready();
/// while (auto msg = transport.receive()) {
///   // handle message
/// }
/// @endcode
class WebSocketServerTransport final : public ServerTransport {
 public:
  /// @brief Constructs a server transport with the given options.
  /// @param options Listen address and WebSocket configuration.
  explicit WebSocketServerTransport(WebSocketServerTransportOptions options);

  /// @brief Stops the HTTP server and closes all WebSocket connections.
  ~WebSocketServerTransport() override;

  WebSocketServerTransport(const WebSocketServerTransport&) = delete;
  WebSocketServerTransport& operator=(const WebSocketServerTransport&) = delete;

  /// @brief Returns "websocket-server".
  std::string_view name() const noexcept override;

  /// @brief Returns structured diagnostics including connection count and stats.
  protocol::Json diagnostics() const override;

  /// @brief Sends a JSON-RPC message to the appropriate connected client.
  ///
  /// Responses are routed to the connection that sent the original request.
  /// Notifications are sent to the most recently active connection.
  core::Result<core::Unit> send(TxMessage message) override;

  /// @brief Blocks until the next inbound JSON-RPC message from any client.
  ///
  /// Returns std::nullopt when the transport is closed.
  core::Result<std::optional<RxMessage>> receive() override;

  /// @brief Closes the transport, all connections, and unblocks receive().
  core::Result<core::Unit> close() override;

  /// @brief Blocks until the server socket is bound and listening.
  void wait_until_ready() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::transport
