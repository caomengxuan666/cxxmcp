#pragma once

#include "cxxmcp/app/policy.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/tool.hpp"

#include <string>
#include <vector>

namespace mcp::app {

    enum class ToolSourceKind {
        local_manifest,
        local_plugin,
        remote_mcp_server,
        generated_adapter,
    };

    struct ToolSource {
        ToolSourceKind kind = ToolSourceKind::local_manifest;
        std::string location;
    };

    struct ToolDescriptor {
        std::string id;
        protocol::ToolDefinition definition;
        ToolSource source;
        Policy policy;
        std::string profile_id;
    };

    class ToolCatalog {
    public:
        virtual ~ToolCatalog() = default;
        virtual std::vector<ToolDescriptor> list() const = 0;
        virtual core::Result<core::Unit> add(ToolDescriptor tool) = 0;
        virtual core::Result<core::Unit> update_policy(std::string tool_id, Policy policy) = 0;
    };

}// namespace mcp::app
