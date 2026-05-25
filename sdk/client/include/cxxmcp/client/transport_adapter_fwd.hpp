// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Internal factory declarations for client transport compatibility
/// adapters.

#include <memory>

#include "cxxmcp/transport/transport.hpp"

namespace mcp::client {

class Transport;

std::unique_ptr<Transport> make_contract_transport_adapter(
    std::unique_ptr<transport::ClientTransport> transport);

}  // namespace mcp::client
