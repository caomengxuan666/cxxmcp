#pragma once

#include "mcp/client/client.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mcp::client {

struct ProcessStdioTransportOptions {
    std::string command;
    std::vector<std::string> args;
    std::string cwd;
    std::unordered_map<std::string, std::string> env;
};

class ProcessStdioTransport final : public Transport {
public:
    explicit ProcessStdioTransport(ProcessStdioTransportOptions options);
    ~ProcessStdioTransport() override;

    ProcessStdioTransport(const ProcessStdioTransport&) = delete;
    ProcessStdioTransport& operator=(const ProcessStdioTransport&) = delete;

    core::Result<protocol::JsonRpcResponse> send(const protocol::JsonRpcRequest& request) override;
    core::Result<core::Unit> send_notification(const protocol::JsonRpcNotification& notification) override;
    core::Result<core::Unit> start(TransportRequestHandler request_handler,
                                   TransportNotificationHandler notification_handler = {}) override;

private:
    class Impl;

    ProcessStdioTransportOptions options_;
    std::unique_ptr<Impl> impl_;
};

} // namespace mcp::client
