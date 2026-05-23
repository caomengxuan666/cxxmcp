#pragma once

#include "mcp/app/policy.hpp"
#include "mcp/app/profile.hpp"
#include "mcp/app/tool_catalog.hpp"
#include "mcp/core/result.hpp"

#include <string>
#include <vector>

namespace mcp::app {

class ToolManagementService final {
public:
    ToolManagementService(ToolCatalog& catalog, ProfileStore& profiles);

    core::Result<core::Unit> enable_tool(std::string profile_id, std::string tool_id);
    core::Result<core::Unit> disable_tool(std::string profile_id, std::string tool_id);
    core::Result<core::Unit> update_policy(std::string tool_id, Policy policy);
    core::Result<std::vector<ToolDescriptor>> list_profile_tools(std::string profile_id) const;

private:
    ToolCatalog& catalog_;
    ProfileStore& profiles_;
};

} // namespace mcp::app
