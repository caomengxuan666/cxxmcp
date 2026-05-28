// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/capabilities.hpp"
#include "cxxmcp/protocol/types.hpp"
#include "cxxmcp/server/auth.hpp"

/// @file
/// @brief Server-side transport abstraction for MCP JSON-RPC traffic.
///
/// Transports own the server-facing I/O loop and adapt protocol messages into
/// request and notification callbacks. Implementations may be blocking or
/// event-driven, but they all report transport and handler failures through
/// core::Result rather than throwing.

namespace mcp::server {

class Transport;
class ClientPeer;

/// @brief Per-message connection metadata supplied to server handlers.
///
/// The context is passed by value or const reference for the duration of a
/// handler invocation. It does not own the transport; transport is a borrowed
/// pointer that remains valid only while the underlying transport instance is
/// alive and the session is active.
struct SessionContext {
  /// Logical MCP session id when the transport has one.
  std::string session_id;
  /// Best-effort remote endpoint description such as "stdio" or an address.
  std::string remote_address;
  /// Transport-supplied request headers or metadata for this message.
  std::unordered_map<std::string, std::string> headers;
  /// HTTP request method for HTTP-based transports.
  std::optional<std::string> http_method;
  /// Absolute HTTP request URL for HTTP-based transports.
  std::optional<std::string> http_url;
  /// Authenticated principal produced by the configured AuthProvider.
  std::optional<AuthIdentity> auth_identity;
  /// Borrowed transport used to create a ClientPeer for outbound messages.
  Transport* transport = nullptr;
  /// Weak lifetime token supplied by the transport. ClientPeer checks this
  /// before using the borrowed transport pointer so stored contexts fail closed
  /// after the transport has been destroyed.
  std::weak_ptr<void> transport_lifetime;

  /// @brief Return a non-owning peer handle for the client on this session.
  /// @return A ClientPeer bound to transport, or an unavailable peer when no
  /// transport is associated with the context.
  ClientPeer client() const noexcept;
};

/// @brief Callback used by transports to dispatch inbound JSON-RPC requests.
/// @param request Parsed JSON-RPC request. The object is owned by the caller.
/// @param context Session and connection metadata for this request.
/// @return A JSON-RPC response on success, or a core::Error to be translated by
/// the transport into a protocol error response.
using RequestHandler = std::function<core::Result<protocol::JsonRpcResponse>(
    const protocol::JsonRpcRequest&, const SessionContext&)>;

/// @brief Callback used by transports to dispatch inbound JSON-RPC
/// notifications.
/// @param notification Parsed JSON-RPC notification. The object is owned by the
/// caller.
/// @param context Session and connection metadata for this notification.
/// @return core::Unit on success, or a core::Error that stops or rejects the
/// notification according to the transport implementation.
using NotificationHandler = std::function<core::Result<core::Unit>(
    const protocol::JsonRpcNotification&, const SessionContext&)>;

/// @brief Abstract server transport for receiving client JSON-RPC messages.
///
/// A Transport is responsible for accepting inbound client messages, invoking
/// the supplied handlers, serializing responses or errors, and sending server
/// notifications back to the client. The server object owns or otherwise
/// controls the transport lifetime; callbacks receive only borrowed references
/// and must not store SessionContext::transport beyond the active session.
///
/// Implementations define their own threading model. StdioTransport runs its
/// read loop inside start(), while HttpTransport serves callbacks from the HTTP
/// server threads and uses an SSE stream for outbound traffic. Unless a derived
/// transport documents stronger guarantees, callers should serialize calls that
/// mutate shared server state from callbacks.
class Transport {
 public:
  virtual ~Transport() = default;

  /// @brief Weak lifetime token for SessionContext and ClientPeer guards.
  std::weak_ptr<void> lifetime_token() const noexcept {
    return lifetime_token_;
  }

  /// @brief Start accepting inbound messages and dispatch them to handlers.
  /// @param handler Required request handler.
  /// @param notification_handler Optional notification handler.
  /// @return core::Unit when the transport stops cleanly, or a core::Error for
  /// setup, I/O, parse, serialization, or callback failures.
  /// @note Implementations may block until stop() is called or input ends.
  virtual core::Result<core::Unit> start(
      RequestHandler handler,
      NotificationHandler notification_handler = {}) = 0;

  /// @brief Send a JSON-RPC request from the server to the connected client.
  /// @param request Request to serialize and deliver.
  /// @return The client response, or an error if outbound requests are not
  /// supported or delivery fails.
  /// @note The base implementation returns MethodNotFound because not every
  /// transport can correlate outbound requests with client responses.
  virtual core::Result<protocol::JsonRpcResponse> send_request(
      const protocol::JsonRpcRequest& request) {
    (void)request;
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::MethodNotFound),
        "transport does not support outbound requests",
        {},
    });
  }

  /// @brief Send a JSON-RPC request to a specific logical session.
  /// @param session_id Transport-defined session identifier.
  /// @param request Request to serialize and deliver.
  /// @return The client response, or an error if the session is unavailable or
  /// outbound requests are unsupported.
  /// @note Transports without session routing delegate to send_request().
  virtual core::Result<protocol::JsonRpcResponse> send_request_to_session(
      std::string_view session_id, const protocol::JsonRpcRequest& request) {
    (void)session_id;
    return send_request(request);
  }

  /// @brief Return capabilities learned from the client's initialize request.
  /// @return Client capabilities when known; std::nullopt before initialize
  /// or when the transport cannot provide them.
  virtual std::optional<protocol::ClientCapabilities> client_capabilities()
      const {
    return std::nullopt;
  }

  /// @brief Return capabilities learned for a specific logical session.
  /// @param session_id Transport-defined session identifier.
  /// @return Client capabilities when known for the session.
  /// @note Transports without session routing delegate to
  /// client_capabilities().
  virtual std::optional<protocol::ClientCapabilities>
  client_capabilities_for_session(std::string_view session_id) const {
    (void)session_id;
    return client_capabilities();
  }

  /// @brief Send a JSON-RPC notification from the server to the client.
  /// @param notification Notification to serialize and deliver.
  /// @return core::Unit on success, or a core::Error for serialization,
  /// stopped-session, or I/O failures.
  virtual core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) = 0;

  /// @brief Send a JSON-RPC notification to a specific logical session.
  /// @param session_id Transport-defined session identifier.
  /// @param notification Notification to serialize and deliver.
  /// @return core::Unit on success, or an error if the session is unavailable.
  /// @note Transports without session routing delegate to send_notification().
  virtual core::Result<core::Unit> send_notification_to_session(
      std::string_view session_id,
      const protocol::JsonRpcNotification& notification) {
    (void)session_id;
    return send_notification(notification);
  }

  /// @brief Request transport shutdown.
  ///
  /// stop() is best-effort and must not throw. Implementations should unblock
  /// any running start() call or pending outbound operation where possible.
  virtual void stop() noexcept = 0;

  /// @brief Human-readable transport name for diagnostics.
  /// @return A static string view owned by the transport implementation.
  virtual std::string_view name() const noexcept = 0;

 private:
  std::shared_ptr<void> lifetime_token_ = std::make_shared<int>(0);
};

}  // namespace mcp::server
