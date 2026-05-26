// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Role-generic client transport for child-process stdio MCP servers.

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cxxmcp/transport/transport.hpp"

namespace mcp::transport {

/// @brief Configuration for a child-process stdio client transport.
///
/// The implementation launches the command directly, not through a shell.
struct ProcessStdioClientTransportOptions {
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

/// @brief Native transport-contract client for a child MCP server over stdio.
///
/// This class exposes process stdio through transport::ClientTransport while
/// reusing the established client process implementation. send() serializes
/// outbound writes. Sending a request waits for the matching response and
/// queues it for receive(); sending a response completes an inbound
/// server-to-client request previously returned by receive(). receive() blocks
/// until a queued message is available or close() completes. Concurrent
/// receive() calls are not supported.
class ProcessStdioClientTransport final : public ClientTransport {
 public:
  explicit ProcessStdioClientTransport(
      ProcessStdioClientTransportOptions options);
  ~ProcessStdioClientTransport() override;

  ProcessStdioClientTransport(const ProcessStdioClientTransport&) = delete;
  ProcessStdioClientTransport& operator=(const ProcessStdioClientTransport&) =
      delete;

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
