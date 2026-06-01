// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Server-side handler invocation contexts.

#include <optional>
#include <string>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/protocol/completion.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/server/peer.hpp"

namespace mcp::server {

/// @brief Copyable cooperative cancellation token for server handlers.
using CancellationToken = mcp::CancellationToken;

/// @brief Invocation context passed to tool handlers.
///
/// ToolContext copies the SessionContext metadata for the active request and
/// adds tool arguments. It does not own the transport; client() returns a
/// non-owning SessionClient for optional server-to-client calls during
/// handling.
struct ToolContext : SessionContext {
  /// @brief Return a non-owning peer handle for this invocation's client.
  SessionClient client() const noexcept { return session_client(*this); }

  /// JSON arguments supplied with the tool call.
  protocol::Json arguments = protocol::Json::object();
  /// Optional task request metadata supplied with the tool call.
  std::optional<protocol::TaskRequestParameters> task;
  /// Cooperative cancellation token for task-aware executions.
  CancellationToken cancellation;

  /// @brief Convenience check for cancellation-aware handlers.
  bool cancelled() const noexcept { return cancellation.cancelled(); }
};

/// @brief Invocation context passed to prompt handlers.
struct PromptContext : SessionContext {
  /// @brief Return a non-owning peer handle for this invocation's client.
  SessionClient client() const noexcept { return session_client(*this); }

  /// JSON arguments supplied with the prompt request.
  protocol::Json arguments = protocol::Json::object();
  /// Cooperative cancellation token for this request.
  CancellationToken cancellation;

  /// @brief Convenience check for cancellation-aware handlers.
  bool cancelled() const noexcept { return cancellation.cancelled(); }
};

/// @brief Invocation context passed to resource read handlers.
struct ResourceContext : SessionContext {
  /// @brief Return a non-owning peer handle for this invocation's client.
  SessionClient client() const noexcept { return session_client(*this); }

  /// Requested resource URI.
  std::string uri;
  /// Raw resource read parameters supplied by the client.
  protocol::Json params = protocol::Json::object();
  /// Cooperative cancellation token for this request.
  CancellationToken cancellation;

  /// @brief Convenience check for cancellation-aware handlers.
  bool cancelled() const noexcept { return cancellation.cancelled(); }
};

/// @brief Completion request context passed to typed completion handlers.
struct CompletionContext : SessionContext {
  /// Parsed `completion/complete` request parameters.
  protocol::CompleteParams params;
  /// Cooperative cancellation token for this request.
  CancellationToken cancellation;

  /// @brief Convenience check for cancellation-aware completion handlers.
  bool cancelled() const noexcept { return cancellation.cancelled(); }
};

}  // namespace mcp::server
