#pragma once

#include "mcp/client/client.hpp"

#include <iosfwd>

namespace mcp::client {

class StdioTransport final : public Transport {
public:
    StdioTransport();
    StdioTransport(std::istream& input, std::ostream& output);

    core::Result<protocol::JsonRpcResponse> send(const protocol::JsonRpcRequest& request) override;

private:
    std::istream* input_;
    std::ostream* output_;
};

} // namespace mcp::client
