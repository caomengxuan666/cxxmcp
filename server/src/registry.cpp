#include "mcp/server/registry.hpp"

#include <algorithm>
#include <utility>

namespace mcp::server {

core::Result<core::Unit> ToolRegistry::add(protocol::ToolDefinition definition, ToolHandler handler) {
    if (definition.name.empty()) {
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "tool name must not be empty",
            {},
        });
    }

    if (!handler) {
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "tool handler must be callable",
            {},
        });
    }

    const auto name = definition.name;
    auto [it, inserted] = tools_.emplace(name, Entry{std::move(definition), std::move(handler)});
    if (!inserted) {
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "tool already exists",
            {},
        });
    }

    return core::Unit{};
}

core::Result<protocol::ToolResult> ToolRegistry::call(std::string_view name, protocol::Json arguments) const {
    const auto it = tools_.find(std::string(name));
    if (it == tools_.end()) {
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::ToolNotFound),
            "tool not found",
            std::string(name),
        });
    }

    ToolContext context;
    context.arguments = std::move(arguments);
    const auto result = it->second.handler(context);
    if (!result) {
        return std::unexpected(result.error());
    }

    return *result;
}

std::vector<protocol::ToolDefinition> ToolRegistry::list() const {
    std::vector<protocol::ToolDefinition> tools;
    tools.reserve(tools_.size());
    for (const auto& [name, entry] : tools_) {
        (void)name;
        tools.push_back(entry.definition);
    }

    std::sort(tools.begin(), tools.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.name < rhs.name;
    });
    return tools;
}

} // namespace mcp::server
