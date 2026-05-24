#pragma once

#include "mcp/client/client.hpp"

#include <iosfwd>

namespace mcp::client {

class StdioTransport final : public Transport {
public:
    StdioTransport();
    StdioTransport(std::istream& input, std::ostream& output);

    core::Result<protocol::JsonRpcResponse> send(const protocol::JsonRpcRequest& request) override;
    core::Result<core::Unit> send_notification(const protocol::JsonRpcNotification& notification) override;
    core::Result<core::Unit> start(TransportRequestHandler request_handler,
                                   TransportNotificationHandler notification_handler = {}) override;

private:
    std::istream* input_;
    std::ostream* output_;
    bool started_ = false;
    TransportRequestHandler request_handler_;
    TransportNotificationHandler notification_handler_;
};

} // namespace mcp::client
