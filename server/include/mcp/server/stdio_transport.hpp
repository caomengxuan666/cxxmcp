#pragma once

#include "mcp/server/transport.hpp"

#include <iosfwd>
#include <mutex>
#include <optional>
#include <string_view>

namespace mcp::server {

class StdioTransport final : public Transport {
public:
    StdioTransport();
    StdioTransport(std::istream& input, std::ostream& output);

    core::Result<core::Unit> start(RequestHandler handler, NotificationHandler notification_handler = {}) override;
    std::optional<protocol::ClientCapabilities> client_capabilities() const override;
    core::Result<core::Unit> send_notification(const protocol::JsonRpcNotification& notification) override;
    void stop() noexcept override;
    std::string_view name() const noexcept override;

private:
    std::istream* input_;
    std::ostream* output_;
    std::mutex output_mutex_;
    bool running_ = false;
    std::optional<protocol::ClientCapabilities> client_capabilities_;
};

} // namespace mcp::server
