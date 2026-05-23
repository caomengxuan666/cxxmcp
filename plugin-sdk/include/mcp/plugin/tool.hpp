#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/tool.hpp"

#include <string_view>
#include <vector>

namespace mcp::plugin {

struct ToolExecutionContext {
    std::string_view tool_name;
    protocol::Json arguments = protocol::Json::object();
};

class ToolPlugin {
public:
    virtual ~ToolPlugin() = default;
    virtual std::vector<protocol::ToolDefinition> tools() const = 0;
    virtual core::Result<protocol::ToolResult> call(const ToolExecutionContext& context) = 0;
};

} // namespace mcp::plugin

