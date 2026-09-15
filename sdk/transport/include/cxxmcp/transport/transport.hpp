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

  /// @brief Waits for the response to one specific outbound request.
  ///
  /// Server transports deliver client responses to server-initiated (reverse)
  /// requests through the same single-consumer receive() queue as inbound
  /// client traffic. A reverse-request caller that waits on receive()
  /// therefore competes with the transport's main receive loop, and the
  /// response can be consumed by the wrong side. Implementations that can
  /// route reverse responses to dedicated waiters override this method so
  /// reverse-request callers bypass the shared queue entirely.
  ///
  /// The default implementation reports unsupported; callers fall back to
  /// the receive() loop.
  /// @param id Request id of the outstanding outbound request.
  /// @return The response message once it arrives, std::nullopt on orderly
  /// close, or an error when the transport does not support direct routing.
  virtual core::Result<std::optional<RxMessage>> receive_response(
      const protocol::RequestId& id) {
    (void)id;
    return mcp::core::unexpected(
        core::Error{static_cast<int>(protocol::ErrorCode::MethodNotFound),
                    "transport does not route reverse responses directly",
                    {}});
  }

  /// @brief Publishes a notification to active subscription listen streams.
  ///
  /// Transports that serve SEP-2575 stateless subscription channels override
  /// this to deliver the notification to matching subscribers. The default
  /// is a no-op.
  virtual void publish_subscription_notification(std::string_view method,
                                                 protocol::Json params) {
    (void)method;
    (void)params;
  }

  /// @brief Closes the transport and unblocks receive() where possible.
  virtual core::Result<core::Unit> close() = 0;

  /// @brief Blocks until the transport is ready to process messages.
  ///
  /// The default is a no-op. Transports that need asynchronous startup
  /// (e.g. binding a socket) override this to block until the underlying
  /// resource is available.
  virtual void wait_until_ready() {}
};

using ClientTransport = Transport<RoleClient>;
using ServerTransport = Transport<RoleServer>;

}  // namespace mcp::transport
