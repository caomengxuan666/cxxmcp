#pragma once

#include "mcp/app/server_management.hpp"
#include "mcp/client/session.hpp"

#include <memory>

namespace mcp::app {

class ClientDiscoverySession final : public McpDiscoverySession {
public:
    explicit ClientDiscoverySession(std::unique_ptr<client::Transport> transport,
                                    client::McpClientSessionOptions options = {});

    core::Result<core::Unit> initialize() override;
    core::Result<std::vector<protocol::ToolDefinition>> discover_tools() override;
    core::Result<std::vector<protocol::Prompt>> discover_prompts() override;
    core::Result<std::vector<protocol::Resource>> discover_resources() override;

private:
    client::McpClientSession session_;
};

core::Result<std::unique_ptr<McpDiscoverySession>> make_client_discovery_session_for_server(
        const McpServerDefinition& server);

} // namespace mcp::app
