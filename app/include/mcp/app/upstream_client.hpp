#pragma once

#include "mcp/app/mcp_server.hpp"
#include "mcp/core/result.hpp"
#include "mcp/client/client.hpp"

#include <memory>

namespace mcp::app {

core::Result<std::unique_ptr<client::Transport>> make_client_transport_for_server(
        const McpServerDefinition& server);

} // namespace mcp::app
