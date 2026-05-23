#pragma once

#include "mcp/client/client.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

namespace mcp::client {

struct HttpTransportOptions {
    std::string host;
    int port = 80;
    std::string path = "/";
    std::unordered_map<std::string, std::string> headers;
    std::chrono::milliseconds timeout{30000};
};

class HttpTransport final : public Transport {
public:
    explicit HttpTransport(HttpTransportOptions options);

    core::Result<protocol::JsonRpcResponse> send(const protocol::JsonRpcRequest& request) override;
    core::Result<core::Unit> send_notification(const protocol::JsonRpcNotification& notification) override;

private:
    HttpTransportOptions options_;
};

} // namespace mcp::client
