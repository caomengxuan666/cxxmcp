// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cxxmcp/server/transport.hpp"

/// @file
/// @brief Streamable HTTP server transport for MCP JSON-RPC sessions.

namespace mcp::server {

/// @brief RFC 9728 Protected Resource Metadata configuration for the server.
///
/// When non-empty, the transport serves the metadata document at
/// /.well-known/oauth-protected-resource and at
/// `/.well-known/oauth-protected-resource/<path>` so that OAuth clients can
/// discover the authorization server and required scopes.
struct ProtectedResourceMetadataConfig {
  /// Resource identifier URL (e.g. "https://example.com/mcp").
  std::string resource;
  /// Authorization server issuers that can issue tokens for this resource.
  std::vector<std::string> authorization_servers;
  /// Scopes that may be requested to access this resource.
  std::vector<std::string> scopes_supported;
  /// Human-readable name of the resource.
  std::optional<std::string> resource_name;
  /// URL of human-readable documentation for the resource.
  std::optional<std::string> resource_documentation;
};

/// @brief Configuration for the HTTP WWW-Authenticate challenge header.
struct AuthChallengeConfig {
  /// Authentication scheme (default: "Bearer").
  std::string scheme = "Bearer";
  /// URL of the RFC 9728 Protected Resource Metadata document.
  /// When set, the WWW-Authenticate header includes resource_metadata="<url>".
  std::optional<std::string> resource_metadata_url;
  /// Scopes required to access the resource.
  /// When set, the WWW-Authenticate header includes scope="<scopes>".
  std::optional<std::string> scope;
};

/// @brief Configuration for HttpTransport.
struct HttpTransportOptions {
  /// Interface address passed to the underlying HTTP server.
  std::string listen_host = "127.0.0.1";
  /// TCP port to listen on. Must be in the range 1..65535.
  int listen_port = 0;
  /// HTTP path for POST, GET/SSE, and DELETE session requests.
  std::string path = "/mcp";
  /// Optional SSE retry interval hint for the priming event.
  std::optional<std::chrono::milliseconds> sse_retry;
  /// Optional Origin allow-list. Empty means Origin is not restricted.
  std::vector<std::string> allowed_origins;
  /// Host allow-list used to reject DNS-rebinding attempts. Empty disables Host
  /// validation. Entries may be hostnames/IP literals or host:port authorities.
  std::vector<std::string> allowed_hosts = {"localhost", "127.0.0.1", "::1"};
  /// Maximum server-to-client events waiting for an SSE stream.
  std::size_t max_pending_sse_events = 1024;
  /// Maximum serialized bytes waiting for an SSE stream.
  std::size_t max_pending_sse_bytes = 4 * 1024 * 1024;
  /// Number of delivered SSE events retained for Last-Event-ID replay.
  std::size_t max_sse_replay_events = 256;
  /// Maximum time to wait for a client response to a server-to-client request.
  /// Set to zero or a negative duration to wait indefinitely.
  std::chrono::milliseconds request_timeout{30000};
  /// Maximum HTTP request body size accepted by the underlying server.
  /// Set to zero to disable the limit.
  std::size_t max_request_body_bytes = 10 * 1024 * 1024;
  /// Socket read timeout for HTTP request handling.
  std::chrono::milliseconds read_timeout{30000};
  /// Socket write timeout for HTTP response handling.
  std::chrono::milliseconds write_timeout{30000};
  /// Maximum active HTTP sessions accepted by this transport. Set to zero to
  /// disable the limit.
  std::size_t max_sessions = 1024;
  /// HTTP WWW-Authenticate challenge emitted when authentication fails.
  /// Empty disables the header for custom deployments that emit challenges
  /// through another layer. This is a legacy field; prefer
  /// auth_challenge_config for resource_metadata and scope support.
  std::string auth_challenge = std::string(DefaultAuthChallenge);
  /// Structured auth challenge configuration. When resource_metadata_url or
  /// scope is set, the WWW-Authenticate header includes those parameters.
  /// If auth_challenge_config.scheme is non-empty, it overrides auth_challenge.
  AuthChallengeConfig auth_challenge_config;
  /// RFC 9728 Protected Resource Metadata. When resource is non-empty, the
  /// transport serves the metadata at /.well-known/oauth-protected-resource.
  ProtectedResourceMetadataConfig protected_resource_metadata;
};

/// @brief MCP streamable HTTP transport with session-aware SSE delivery.
///
/// HttpTransport accepts JSON-RPC messages over POST, exposes server-initiated
/// notifications and requests over a GET text/event-stream, and terminates the
/// session on DELETE. It owns the underlying HTTP server instance after start()
/// begins.
///
/// The server transport is stateful. Every successful initialize request
/// creates a distinct MCP session id returned in Mcp-Session-Id. Later POST,
/// GET, and DELETE requests must present that id and the supported
/// MCP-Protocol-Version header. Unknown or terminated sessions are rejected as
/// stale sessions. DELETE removes the session, clears its replay and outbound
/// queues, and fails pending server-to-client requests for that session.
///
/// Outbound queues, Last-Event-ID replay windows, client capabilities, pending
/// server requests, and active SSE stream state are tracked per session.
/// Session-bound ClientPeer instances created from SessionContext route through
/// send_request_to_session() and send_notification_to_session(). The legacy
/// send_request(), send_notification(), and client_capabilities() methods use a
/// default session for single-session compatibility.
///
/// One live real-time SSE stream is accepted per session. A reconnect carrying
/// Last-Event-ID may replay retained events while an old stream is closing.
/// Legacy SSE-compatible behavior is limited to the text/event-stream delivery
/// path; Streamable HTTP POST/GET/DELETE remains the default HTTP transport
/// contract.
///
/// Request and notification handlers are called from the HTTP server's request
/// handling threads. Outbound notifications and server-to-client requests are
/// queued under an internal mutex; send_request() blocks until the client posts
/// the matching response or the stream/session is stopped.
class HttpTransport final : public Transport {
 public:
  /// @brief Construct a transport with normalized HTTP options.
  /// @param options Listen address, port, MCP path, and optional Origin policy.
  explicit HttpTransport(HttpTransportOptions options);

  /// @brief Destroy the transport and its underlying server state.
  ~HttpTransport() override;

  HttpTransport(const HttpTransport&) = delete;
  HttpTransport& operator=(const HttpTransport&) = delete;

  /// @brief Start the HTTP server and serve the configured MCP endpoint.
  /// @param handler Required request handler.
  /// @param notification_handler Optional notification handler.
  /// @return core::Unit when the HTTP server exits, or a core::Error for
  /// invalid options, bind/listen failure, parse errors that cannot be
  /// reported to the client, or handler setup failures.
  /// @note This call blocks in the underlying HTTP server until stop() is
  /// called or the server stops listening.
  core::Result<core::Unit> start(
      RequestHandler handler,
      NotificationHandler notification_handler = {}) override;

  /// @brief Queue a server-to-client request on the active SSE stream.
  /// @param request Request to serialize and deliver.
  /// @return The posted client response, or a core::Error if there is no
  /// active session, serialization fails, the stream closes, or the transport
  /// is stopped.
  core::Result<protocol::JsonRpcResponse> send_request(
      const protocol::JsonRpcRequest& request) override;

  /// @brief Queue a server-to-client request for a specific HTTP session.
  core::Result<protocol::JsonRpcResponse> send_request_to_session(
      std::string_view session_id,
      const protocol::JsonRpcRequest& request) override;

  /// @brief Return capabilities from the active initialized HTTP session.
  std::optional<protocol::ClientCapabilities> client_capabilities()
      const override;

  /// @brief Return capabilities from a specific initialized HTTP session.
  std::optional<protocol::ClientCapabilities> client_capabilities_for_session(
      std::string_view session_id) const override;

  /// @brief Queue an outbound notification for the active SSE stream.
  /// @param notification Notification to serialize.
  /// @return core::Unit on success, or a core::Error for serialization failure
  /// or stopped transport state.
  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) override;

  /// @brief Queue an outbound notification for a specific HTTP session.
  core::Result<core::Unit> send_notification_to_session(
      std::string_view session_id,
      const protocol::JsonRpcNotification& notification) override;

  /// @brief Stop the HTTP server and fail pending outbound requests.
  void stop() noexcept override;

  /// @brief Return the diagnostic transport name "http".
  std::string_view name() const noexcept override;

 private:
  struct HttpServerHolder;

  struct PendingRequest {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    std::optional<protocol::JsonRpcResponse> response;
    std::optional<core::Error> error;
  };

  struct QueuedEvent {
    std::optional<std::uint64_t> event_id;
    std::string payload;
  };

  struct SessionState {
    std::string session_id;
    bool initialized = false;
    std::optional<protocol::ClientCapabilities> client_capabilities;
    std::deque<QueuedEvent> pending_notifications;
    std::size_t pending_notification_bytes = 0;
    std::deque<QueuedEvent> replay_notifications;
    std::unordered_map<std::string, std::shared_ptr<PendingRequest>>
        pending_requests;
    std::uint64_t next_notification_event_id = 1;
    std::size_t active_sse_streams = 0;
  };

  void abort_pending_requests_locked(SessionState& session,
                                     std::string message);
  void clear_outbound_events_locked(SessionState& session);
  core::Result<core::Unit> enqueue_outbound_event_locked(SessionState& session,
                                                         QueuedEvent event);
  void remember_replay_event_locked(SessionState& session,
                                    const QueuedEvent& event);
  SessionState* find_session_locked(std::string_view session_id);
  const SessionState* find_session_locked(std::string_view session_id) const;
  SessionState* select_default_session_locked();
  void close_sse_stream(std::string_view session_id) noexcept;

  HttpTransportOptions options_;
  std::unique_ptr<HttpServerHolder> server_;
  mutable std::mutex mutex_;
  std::condition_variable notification_cv_;
  std::unordered_map<std::string, SessionState> sessions_;
  std::uint64_t next_session_id_ = 1;
  bool stopped_ = false;
  std::string default_session_id_;
};

}  // namespace mcp::server
