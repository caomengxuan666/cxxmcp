// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <iosfwd>
#include <mutex>
#include <optional>
#include <string_view>

#include "cxxmcp/server/transport.hpp"

/// @file
/// @brief Server transport that reads newline-delimited JSON-RPC from stdio.

namespace mcp::server {

/// @brief Blocking stdio transport for local MCP clients.
///
/// StdioTransport reads one JSON-RPC message per line from its input stream and
/// writes responses and outbound notifications to its output stream. start()
/// owns the read loop and blocks until EOF, stop(), or an unrecoverable read,
/// write, parse, or callback error.
///
/// The transport does not own custom streams passed to the constructor; callers
/// must keep them alive for the whole transport lifetime. Outbound notification
/// writes are mutex-protected so a handler can call SessionContext::client()
/// while the read loop is active.
///
/// The server stdio loop handles one inbound line at a time and writes the
/// corresponding response before reading the next request. It therefore has no
/// concrete in-flight request registry; duplicate in-flight request-id
/// validation is enforced by asynchronous peer/native/adapter layers.
class StdioTransport final : public Transport {
 public:
  /// @brief Construct a transport using std::cin and std::cout.
  StdioTransport();

  /// @brief Construct a transport over caller-owned streams.
  /// @param input Stream read by start(); must outlive the transport.
  /// @param output Stream used for responses and notifications; must outlive
  /// the transport.
  StdioTransport(std::istream& input, std::ostream& output);

  /// @brief Run the stdio message loop.
  /// @param handler Required request handler invoked synchronously on the
  /// start() caller's thread.
  /// @param notification_handler Optional notification handler invoked
  /// synchronously on the start() caller's thread.
  /// @return core::Unit after clean EOF or stop(), or a core::Error for
  /// invalid configuration, stream failures, serialization failures, or
  /// notification handler errors.
  /// @note Request handler errors are encoded as JSON-RPC error responses and
  /// do not by themselves stop the loop.
  core::Result<core::Unit> start(
      RequestHandler handler,
      NotificationHandler notification_handler = {}) override;

  /// @brief Return capabilities from the most recent initialize request.
  std::optional<protocol::ClientCapabilities> client_capabilities()
      const override;

  /// @brief Write an outbound server notification to the output stream.
  /// @param notification Notification to serialize as a single line.
  /// @return core::Unit on success, or a core::Error when serialization or
  /// writing fails.
  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) override;

  /// @brief Stop the read loop after the current blocking read completes.
  void stop() noexcept override;

  /// @brief Return the diagnostic transport name "stdio".
  std::string_view name() const noexcept override;

 private:
  std::istream* input_;
  std::ostream* output_;
  std::mutex output_mutex_;
  bool running_ = false;
  std::optional<protocol::ClientCapabilities> client_capabilities_;
};

}  // namespace mcp::server
