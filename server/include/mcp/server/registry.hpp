#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/tool.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcp::server {

struct ToolContext {
    std::string session_id;
    protocol::Json arguments = protocol::Json::object();
};

using ToolHandler = std::function<core::Result<protocol::ToolResult>(const ToolContext&)>;

class ToolRegistry {
public:
    core::Result<core::Unit> add(protocol::ToolDefinition definition, ToolHandler handler);
    core::Result<protocol::ToolResult> call(std::string_view name, protocol::Json arguments) const;
    std::vector<protocol::ToolDefinition> list() const;

private:
    struct Entry {
        protocol::ToolDefinition definition;
        ToolHandler handler;
    };

    std::unordered_map<std::string, Entry> tools_;
};

} // namespace mcp::server

