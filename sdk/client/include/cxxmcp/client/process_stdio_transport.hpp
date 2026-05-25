// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Client transport that launches an MCP server process and talks over
/// stdio.

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cxxmcp/client/client.hpp"

namespace mcp::client {

/// @brief Configuration for a child-process stdio transport.
struct ProcessStdioTransportOptions {
  /// Executable to launch.
  std::string command;

  /// Command-line arguments passed to the executable.
  std::vector<std::string> args;

  /// Optional working directory for the child process.
  std::string cwd;

  /// Extra environment variables for the child process.
  std::unordered_map<std::string, std::string> env;

  /// Optional maximum time to wait for a JSON-RPC response from the child.
  /// A null value waits indefinitely.
  std::optional<std::chrono::milliseconds> request_timeout =
      std::chrono::seconds(30);
};

/// @brief Client transport that owns a child process and exchanges JSON-RPC
/// over stdio.
///
/// The transport is move-only through unique ownership by Client. It starts the
/// child process on demand and routes server-initiated requests and
/// notifications through callbacks registered with start().
class ProcessStdioTransport final : public Transport {
 public:
  /// @brief Creates a child-process stdio transport.
  /// @param options Command, arguments, working directory, and environment.
  explicit ProcessStdioTransport(ProcessStdioTransportOptions options);

  ~ProcessStdioTransport() override;

  ProcessStdioTransport(const ProcessStdioTransport&) = delete;
  ProcessStdioTransport& operator=(const ProcessStdioTransport&) = delete;

  /// @brief Sends one JSON-RPC request to the child process and waits for a
  /// response.
  core::Result<protocol::JsonRpcResponse> send(
      const protocol::JsonRpcRequest& request) override;

  /// @brief Sends one JSON-RPC notification to the child process.
  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) override;

  /// @brief Starts the child process and receive loop when needed.
  /// @param request_handler Handler for server-to-client requests.
  /// @param notification_handler Handler for server-to-client notifications.
  core::Result<core::Unit> start(
      TransportRequestHandler request_handler,
      TransportNotificationHandler notification_handler = {}) override;

  /// @brief Stops the child process and receive loop.
  void stop() noexcept override;

 private:
  class Impl;

  ProcessStdioTransportOptions options_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::client
