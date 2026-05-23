#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/types.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace mcp::server {

struct SessionContext {
    std::string session_id;
    std::string remote_address;
};

using RequestHandler = std::function<core::Result<protocol::JsonRpcResponse>(
        const protocol::JsonRpcRequest&,
        const SessionContext&)>;

class Transport {
public:
    virtual ~Transport() = default;
    virtual core::Result<core::Unit> start(RequestHandler handler) = 0;
    virtual void stop() noexcept = 0;
    virtual std::string_view name() const noexcept = 0;
};

} // namespace mcp::server

