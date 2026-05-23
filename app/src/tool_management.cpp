#include "mcp/app/tool_management.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mcp::app {

namespace {

core::Error make_tool_management_error(std::string message, std::string detail = {}) {
    return core::Error{1, std::move(message), std::move(detail)};
}

core::Result<Profile> find_profile(const ProfileStore& profiles, const std::string& profile_id) {
    const auto all_profiles = profiles.list_profiles();
    const auto it = std::find_if(all_profiles.begin(), all_profiles.end(), [&](const auto& profile) {
        return profile.id == profile_id;
    });
    if (it == all_profiles.end()) {
        return std::unexpected(make_tool_management_error("profile not found", profile_id));
    }

    return *it;
}

core::Result<ToolDescriptor> find_tool(const ToolCatalog& catalog, const std::string& tool_id) {
    const auto tools = catalog.list();
    const auto it = std::find_if(tools.begin(), tools.end(), [&](const auto& tool) {
        return tool.id == tool_id;
    });
    if (it == tools.end()) {
        return std::unexpected(make_tool_management_error("tool not found", tool_id));
    }

    return *it;
}

} // namespace

ToolManagementService::ToolManagementService(ToolCatalog& catalog, ProfileStore& profiles)
    : catalog_(catalog), profiles_(profiles) {}

core::Result<core::Unit> ToolManagementService::enable_tool(std::string profile_id, std::string tool_id) {
    auto profile = find_profile(profiles_, profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }

    auto tool = find_tool(catalog_, tool_id);
    if (!tool) {
        return std::unexpected(tool.error());
    }

    tool->policy.enabled = true;
    auto policy_updated = catalog_.update_policy(tool->id, tool->policy);
    if (!policy_updated) {
        return std::unexpected(policy_updated.error());
    }

    const auto already_enabled =
        std::find(profile->enabled_tool_ids.begin(), profile->enabled_tool_ids.end(), tool->id) !=
        profile->enabled_tool_ids.end();
    if (!already_enabled) {
        profile->enabled_tool_ids.push_back(tool->id);
    }

    auto saved = profiles_.save(std::move(*profile));
    if (!saved) {
        return std::unexpected(saved.error());
    }

    return core::Unit{};
}

core::Result<core::Unit> ToolManagementService::disable_tool(std::string profile_id, std::string tool_id) {
    auto profile = find_profile(profiles_, profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }

    auto tool = find_tool(catalog_, tool_id);
    if (!tool) {
        return std::unexpected(tool.error());
    }

    tool->policy.enabled = false;
    auto policy_updated = catalog_.update_policy(tool->id, tool->policy);
    if (!policy_updated) {
        return std::unexpected(policy_updated.error());
    }

    std::erase(profile->enabled_tool_ids, tool->id);

    auto saved = profiles_.save(std::move(*profile));
    if (!saved) {
        return std::unexpected(saved.error());
    }

    return core::Unit{};
}

core::Result<core::Unit> ToolManagementService::update_policy(std::string tool_id, Policy policy) {
    auto tool = find_tool(catalog_, tool_id);
    if (!tool) {
        return std::unexpected(tool.error());
    }

    auto updated = catalog_.update_policy(std::move(tool_id), std::move(policy));
    if (!updated) {
        return std::unexpected(updated.error());
    }

    return core::Unit{};
}

core::Result<std::vector<ToolDescriptor>> ToolManagementService::list_profile_tools(std::string profile_id) const {
    auto profile = find_profile(profiles_, profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }

    const std::unordered_set<std::string> enabled_ids(profile->enabled_tool_ids.begin(), profile->enabled_tool_ids.end());
    std::vector<ToolDescriptor> bound_tools;
    for (const auto& tool : catalog_.list()) {
        if (enabled_ids.contains(tool.id)) {
            bound_tools.push_back(tool);
        }
    }

    return bound_tools;
}

} // namespace mcp::app
