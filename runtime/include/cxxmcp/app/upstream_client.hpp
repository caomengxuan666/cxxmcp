#pragma once

#include "cxxmcp/app/mcp_server.hpp"
#include "cxxmcp/client/client.hpp"
#include "cxxmcp/core/result.hpp"

#include <memory>

namespace mcp::app {

    core::Result<std::unique_ptr<client::Transport>> make_client_transport_for_server(
            const McpServerDefinition &server);

}// namespace mcp::app
