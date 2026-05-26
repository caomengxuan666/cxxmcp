// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Role-generic MCP transport contract for SDK peer/service layers.

#include <optional>
#include <string_view>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"
#include "cxxmcp/roles.hpp"

namespace mcp::transport {

/// @brief Message types exchanged by a role-generic transport.
template <class Role>
struct MessageTraits {
  using TxMessage = protocol::JsonRpcMessage;
  using RxMessage = protocol::JsonRpcMessage;
};

/// @brief Minimal message-level transport contract shared by MCP roles.
///
/// This contract is intentionally limited to one outbound operation, one
/// inbound operation, explicit close, and diagnostics. Higher-level session,
/// retry, policy, and gateway behavior must live above this interface.
///
/// Implementations must document whether send() may be called concurrently.
/// receive() is sequential: callers must not call receive() concurrently on the
/// same transport unless a concrete implementation explicitly says otherwise.
/// A successful receive() returning std::nullopt means the inbound stream ended
/// cleanly or close() has completed. close() is best-effort, should be
/// idempotent, and should unblock a blocked receive() where the underlying
/// platform transport can do so.
template <class Role>
class Transport {
 public:
  using TxMessage = typename MessageTraits<Role>::TxMessage;
  using RxMessage = typename MessageTraits<Role>::RxMessage;

  virtual ~Transport() = default;

  /// @brief Human-readable transport name for diagnostics.
  virtual std::string_view name() const noexcept = 0;

  /// @brief Structured implementation diagnostics.
  ///
  /// The default keeps the core contract narrow. Transports with useful state
  /// may return keys such as "name", "closed", "inflight", or backend-specific
  /// counters. Diagnostics are not part of the wire protocol.
  virtual protocol::Json diagnostics() const {
    return protocol::Json::object();
  }

  /// @brief Sends one JSON-RPC message to the peer.
  ///
  /// Concurrency safety is implementation-specific and must be documented by
  /// the concrete transport.
  virtual core::Result<core::Unit> send(TxMessage message) = 0;

  /// @brief Receives the next JSON-RPC message from the peer.
  ///
  /// receive() is the sequential inbound side. A successful std::nullopt return
  /// is an orderly end-of-stream signal, not a parse or transport error.
  virtual core::Result<std::optional<RxMessage>> receive() = 0;

  /// @brief Closes the transport and unblocks receive() where possible.
  virtual core::Result<core::Unit> close() = 0;
};

using ClientTransport = Transport<RoleClient>;
using ServerTransport = Transport<RoleServer>;

}  // namespace mcp::transport
