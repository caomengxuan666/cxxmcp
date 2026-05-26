// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Client transport that exchanges MCP JSON-RPC over C++ streams.

#include <iosfwd>

#include "cxxmcp/client/client.hpp"

namespace mcp::client {

/// @brief Client transport backed by std::istream and std::ostream.
///
/// This transport is useful for tests, embedded integrations, or process
/// environments where the streams are supplied by the caller instead of
/// launching a child process.
///
/// send() is a single synchronous request/response exchange. The concrete
/// stdio layer does not maintain an in-flight request registry, so duplicate
/// in-flight request-id validation belongs to Peer/request-handle or
/// role-generic transport adapters that add asynchronous correlation.
class StdioTransport final : public Transport {
 public:
  /// @brief Creates a stdio transport using standard input and output streams.
  StdioTransport();

  /// @brief Creates a stdio transport using caller-owned streams.
  /// @param input Stream read by the transport. The caller must keep it alive.
  /// @param output Stream written by the transport. The caller must keep it
  /// alive.
  StdioTransport(std::istream& input, std::ostream& output);

  /// @brief Sends one JSON-RPC request over the output stream and waits for a
  /// response.
  core::Result<protocol::JsonRpcResponse> send(
      const protocol::JsonRpcRequest& request) override;

  /// @brief Sends one JSON-RPC notification over the output stream.
  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) override;

  /// @brief Starts receive-side dispatch for stream input.
  /// @param request_handler Handler for inbound requests read from the stream.
  /// @param notification_handler Handler for inbound notifications read from
  /// the stream.
  core::Result<core::Unit> start(
      TransportRequestHandler request_handler,
      TransportNotificationHandler notification_handler = {}) override;

 private:
  std::istream* input_;
  std::ostream* output_;
  bool started_ = false;
  TransportRequestHandler request_handler_;
  TransportNotificationHandler notification_handler_;
};

}  // namespace mcp::client
