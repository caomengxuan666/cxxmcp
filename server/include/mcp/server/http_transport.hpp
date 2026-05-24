#pragma once

#include "mcp/server/transport.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace httplib {
class Server;
}

namespace mcp::server {

struct HttpTransportOptions {
    std::string listen_host = "127.0.0.1";
    int listen_port = 0;
    std::string path = "/mcp";
};

class HttpTransport final : public Transport {
public:
    explicit HttpTransport(HttpTransportOptions options);
    ~HttpTransport() override;

    HttpTransport(const HttpTransport&) = delete;
    HttpTransport& operator=(const HttpTransport&) = delete;

    core::Result<core::Unit> start(RequestHandler handler, NotificationHandler notification_handler = {}) override;
    core::Result<protocol::JsonRpcResponse> send_request(const protocol::JsonRpcRequest& request) override;
    std::optional<protocol::ClientCapabilities> client_capabilities() const override;
    core::Result<core::Unit> send_notification(const protocol::JsonRpcNotification& notification) override;
    void stop() noexcept override;
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
    bool stopped_ = false;
    std::string session_id_;
    std::optional<protocol::ClientCapabilities> client_capabilities_;
};

} // namespace mcp::server
