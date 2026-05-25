// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Internal factory declarations for server transport compatibility
/// adapters.

#include <memory>

#include "cxxmcp/transport/transport.hpp"

namespace mcp::server {

class Transport;

std::unique_ptr<Transport> make_contract_transport_adapter(
    std::unique_ptr<transport::ServerTransport> transport);

}  // namespace mcp::server
