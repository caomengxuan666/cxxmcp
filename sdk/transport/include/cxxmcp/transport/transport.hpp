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
/// send() is the outbound side and must be safe for the concurrency guarantees
/// documented by each implementation. receive() is the inbound sequential side:
/// a returned std::nullopt means the stream was closed cleanly.
template <class Role>
class Transport {
 public:
  using TxMessage = typename MessageTraits<Role>::TxMessage;
  using RxMessage = typename MessageTraits<Role>::RxMessage;

  virtual ~Transport() = default;

  /// @brief Human-readable transport name for diagnostics.
  virtual std::string_view name() const noexcept = 0;

  /// @brief Sends one JSON-RPC message to the peer.
  virtual core::Result<core::Unit> send(TxMessage message) = 0;

  /// @brief Receives the next JSON-RPC message from the peer.
  virtual core::Result<std::optional<RxMessage>> receive() = 0;

  /// @brief Closes the transport and unblocks receive() where possible.
  virtual core::Result<core::Unit> close() = 0;
};

using ClientTransport = Transport<RoleClient>;
using ServerTransport = Transport<RoleServer>;

}  // namespace mcp::transport
