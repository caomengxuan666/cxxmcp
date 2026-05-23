#pragma once

#include "mcp/core/result.hpp"
#include "mcp/server/registry.hpp"

#include <string_view>

namespace mcp::adapters {

class Adapter {
public:
    virtual ~Adapter() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual core::Result<core::Unit> register_tools(server::ToolRegistry& registry) = 0;
};

} // namespace mcp::adapters

