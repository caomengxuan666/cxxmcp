#include "cxxmcp/app/client_discovery.hpp"

#include "cxxmcp/app/upstream_client.hpp"

#include <utility>

namespace mcp::app {

    ClientDiscoverySession::ClientDiscoverySession(std::unique_ptr<client::Transport> transport,
                                                   client::McpClientSessionOptions options)
        : session_(std::move(transport), std::move(options)) {}

    core::Result<core::Unit> ClientDiscoverySession::initialize() {
        const auto initialized = session_.initialize();
        if (!initialized) {
            return std::unexpected(initialized.error());
        }

        return session_.mark_initialized();
    }

    core::Result<std::vector<protocol::ToolDefinition>> ClientDiscoverySession::discover_tools() {
        return session_.discover_tools();
    }

    core::Result<std::vector<protocol::Prompt>> ClientDiscoverySession::discover_prompts() {
        return session_.discover_prompts();
    }

    core::Result<std::vector<protocol::Resource>> ClientDiscoverySession::discover_resources() {
        return session_.discover_resources();
    }

    core::Result<std::unique_ptr<McpDiscoverySession>> make_client_discovery_session_for_server(
            const McpServerDefinition &server) {
        auto transport = make_client_transport_for_server(server);
        if (!transport) {
            return std::unexpected(transport.error());
        }

        return std::unique_ptr<McpDiscoverySession>(
                new ClientDiscoverySession(std::move(*transport)));
    }

}// namespace mcp::app
