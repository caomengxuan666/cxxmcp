// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/server/transport_adapter.hpp"

namespace mcp::server {

std::unique_ptr<Transport> make_contract_transport_adapter(
    std::unique_ptr<transport::ServerTransport> transport) {
  return std::make_unique<ContractTransportAdapter>(std::move(transport));
}

}  // namespace mcp::server
