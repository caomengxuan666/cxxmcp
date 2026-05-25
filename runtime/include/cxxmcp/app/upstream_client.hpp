// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <memory>

#include "cxxmcp/app/mcp_server.hpp"
#include "cxxmcp/client/client.hpp"
#include "cxxmcp/core/result.hpp"

namespace mcp::app {

core::Result<std::unique_ptr<client::Transport>>
make_client_transport_for_server(const McpServerDefinition& server);

}  // namespace mcp::app
