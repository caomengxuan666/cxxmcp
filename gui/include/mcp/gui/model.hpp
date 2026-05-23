#pragma once

#include "mcp/app/policy.hpp"
#include "mcp/app/prompt_catalog.hpp"
#include "mcp/app/resource_catalog.hpp"
#include "mcp/app/tool_catalog.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mcp::gui {

struct ProfileRow {
    std::string id;
    std::string name;
    std::size_t endpoint_count = 0;
    std::size_t enabled_tool_count = 0;
};

struct ToolRow {
    std::string id;
    std::string name;
    std::string description;
    std::string profile_id;
    app::ToolSourceKind source_kind = app::ToolSourceKind::local_manifest;
    bool enabled = false;
    app::ApprovalState approval = app::ApprovalState::pending;
    std::vector<app::Permission> permissions;
};

struct ResourceRow {
    std::string id;
    std::string name;
    std::string description;
    std::string uri;
    app::ResourceSourceKind source_kind = app::ResourceSourceKind::local_manifest;
    std::string source_location;
};

struct PromptRow {
    std::string id;
    std::string name;
    std::string description;
    std::string template_text;
    app::PromptSourceKind source_kind = app::PromptSourceKind::local_manifest;
    std::string source_location;
};

struct GuiSnapshot {
    std::vector<ProfileRow> profiles;
    std::vector<ToolRow> tools;
    std::vector<ResourceRow> resources;
    std::vector<PromptRow> prompts;
    std::string selected_profile_id;
    std::string selected_tool_id;
    std::string selected_resource_id;
    std::string selected_prompt_id;
    std::string status;
};

} // namespace mcp::gui
