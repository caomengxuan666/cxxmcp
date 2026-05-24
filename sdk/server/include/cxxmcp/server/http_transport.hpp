#pragma once

#include "cxxmcp/server/transport.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

/// @file
/// @brief Streamable HTTP server transport for MCP JSON-RPC sessions.

namespace httplib {
    class Server;
}

namespace mcp::server {

    /// @brief Configuration for HttpTransport.
    struct HttpTransportOptions {
        /// Interface address passed to the underlying HTTP server.
        std::string listen_host = "127.0.0.1";
        /// TCP port to listen on. Must be in the range 1..65535.
        int listen_port = 0;
        /// HTTP path for POST, GET/SSE, and DELETE session requests.
        std::string path = "/mcp";
        /// Optional Origin allow-list. Empty means Origin is not restricted.
        std::vector<std::string> allowed_origins;
    };

    /// @brief MCP streamable HTTP transport with session-aware SSE delivery.
    ///
    /// HttpTransport accepts JSON-RPC messages over POST, exposes server-initiated
    /// notifications and requests over a GET text/event-stream, and terminates the
    /// session on DELETE. It owns the underlying httplib::Server instance after
    /// start() begins.
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

        HttpTransport(const HttpTransport &) = delete;
        HttpTransport &operator=(const HttpTransport &) = delete;

        /// @brief Start the HTTP server and serve the configured MCP endpoint.
        /// @param handler Required request handler.
        /// @param notification_handler Optional notification handler.
        /// @return core::Unit when the HTTP server exits, or a core::Error for
        /// invalid options, bind/listen failure, parse errors that cannot be
        /// reported to the client, or handler setup failures.
        /// @note This call blocks in the underlying HTTP server until stop() is
        /// called or the server stops listening.
        core::Result<core::Unit> start(RequestHandler handler, NotificationHandler notification_handler = {}) override;

        /// @brief Queue a server-to-client request on the active SSE stream.
        /// @param request Request to serialize and deliver.
        /// @return The posted client response, or a core::Error if there is no
        /// active session, serialization fails, the stream closes, or the transport
        /// is stopped.
        core::Result<protocol::JsonRpcResponse> send_request(const protocol::JsonRpcRequest &request) override;

        /// @brief Return capabilities from the active initialized HTTP session.
        std::optional<protocol::ClientCapabilities> client_capabilities() const override;

        /// @brief Queue an outbound notification for the active SSE stream.
        /// @param notification Notification to serialize.
        /// @return core::Unit on success, or a core::Error for serialization failure
        /// or stopped transport state.
        core::Result<core::Unit> send_notification(const protocol::JsonRpcNotification &notification) override;

        /// @brief Stop the HTTP server and fail pending outbound requests.
        void stop() noexcept override;

        /// @brief Return the diagnostic transport name "http".
        std::string_view name() const noexcept override;

    private:
        void abort_pending_requests(std::string message);

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

        HttpTransportOptions options_;
        std::unique_ptr<httplib::Server> server_;
        mutable std::mutex mutex_;
        std::condition_variable notification_cv_;
        std::deque<QueuedEvent> pending_notifications_;
        std::unordered_map<std::string, std::shared_ptr<PendingRequest>> pending_requests_;
        std::uint64_t next_notification_event_id_ = 1;
        std::uint64_t next_session_id_ = 1;
        bool stopped_ = false;
        std::string session_id_;
        std::optional<protocol::ClientCapabilities> client_capabilities_;
    };

}// namespace mcp::server
