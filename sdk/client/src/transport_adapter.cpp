// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/client/transport_adapter.hpp"

namespace mcp::client {

std::unique_ptr<Transport> make_contract_transport_adapter(
    std::unique_ptr<transport::ClientTransport> transport) {
  return std::make_unique<ContractTransportAdapter>(std::move(transport));
}

std::unique_ptr<Transport> make_contract_transport_adapter(
    transport::ClientTransport& transport) {
  return std::make_unique<ContractTransportAdapter>(transport);
}

}  // namespace mcp::client
