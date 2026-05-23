#pragma once

#include "mcp/app/import_export.hpp"
#include "mcp/app/prompt_catalog.hpp"
#include "mcp/app/profile.hpp"
#include "mcp/app/resource_catalog.hpp"
#include "mcp/app/tool_catalog.hpp"
#include "mcp/app/tool_management.hpp"
#include "mcp/core/result.hpp"
#include "mcp/gui/model.hpp"

#include <filesystem>
#include <string>

namespace mcp::gui {

struct GuiServices {
    app::ToolManagementService& management;
    app::ToolCatalog& tools;
    app::ResourceCatalog& resources;
    app::PromptCatalog& prompts;
    app::ProfileStore& profiles;
    app::ImportExportService& bundles;
};

class GuiController final {
public:
    explicit GuiController(GuiServices services);

    core::Result<GuiSnapshot> snapshot();
    core::Result<GuiSnapshot> select_profile(std::string profile_id);
    core::Result<GuiSnapshot> select_tool(std::string tool_id);
    core::Result<GuiSnapshot> select_resource(std::string resource_id);
    core::Result<GuiSnapshot> select_prompt(std::string prompt_id);
    core::Result<GuiSnapshot> enable_tool(std::string tool_id);
    core::Result<GuiSnapshot> disable_tool(std::string tool_id);
    core::Result<GuiSnapshot> update_tool_policy(std::string tool_id, app::Policy policy);
    core::Result<GuiSnapshot> import_bundle(const std::filesystem::path& path);
    core::Result<GuiSnapshot> export_bundle(const std::filesystem::path& path);

private:
    core::Result<std::string> selected_or_default_profile_id();
    core::Result<GuiSnapshot> build_snapshot(std::string status);

    GuiServices services_;
    std::string selected_profile_id_;
    std::string selected_tool_id_;
    std::string selected_resource_id_;
    std::string selected_prompt_id_;
};

} // namespace mcp::gui
