#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/capabilities.hpp"
#include "mcp/server/auth.hpp"
#include "mcp/server/rate_limit.hpp"
#include "mcp/server/registry.hpp"
#include "mcp/server/transport.hpp"

#include <memory>
#include <string>
#include <vector>

namespace mcp::server {

struct ServerOptions {
    protocol::ServerCapabilities capabilities;
    std::string server_name = "MCPServer.cpp";
    std::string server_version = "2.0.0";
    std::string instructions;
};

class Server {
public:
    explicit Server(ServerOptions options);

    ToolRegistry& tools() noexcept;
    const ToolRegistry& tools() const noexcept;

    core::Result<protocol::JsonRpcResponse> handle_request(const protocol::JsonRpcRequest& request,
                                                           const SessionContext& context);

    core::Result<core::Unit> add_transport(std::unique_ptr<Transport> transport);
    void set_auth_provider(std::unique_ptr<AuthProvider> auth_provider);
    void set_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter);
    core::Result<core::Unit> start();
    void stop() noexcept;

private:
    ServerOptions options_;
    ToolRegistry tools_;
    std::unique_ptr<AuthProvider> auth_provider_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    std::vector<std::unique_ptr<Transport>> transports_;
};

class ServerBuilder {
public:
    ServerBuilder& with_capabilities(protocol::ServerCapabilities capabilities);
    ServerBuilder& with_transport(std::unique_ptr<Transport> transport);
    ServerBuilder& with_auth_provider(std::unique_ptr<AuthProvider> auth_provider);
    ServerBuilder& with_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter);
    core::Result<std::unique_ptr<Server>> build();

private:
    ServerOptions options_;
    std::unique_ptr<AuthProvider> auth_provider_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    std::vector<std::unique_ptr<Transport>> transports_;
};

} // namespace mcp::server
