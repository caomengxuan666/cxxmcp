#include "mcp/gui/controller.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mcp::gui {

namespace {

core::Error make_gui_error(std::string message, std::string detail = {}) {
    return core::Error{1, std::move(message), std::move(detail)};
}

std::vector<app::Permission> sorted_permissions(const std::unordered_set<app::Permission>& permissions) {
    std::vector<app::Permission> result(permissions.begin(), permissions.end());
    std::sort(result.begin(), result.end(), [](app::Permission lhs, app::Permission rhs) {
        return std::to_underlying(lhs) < std::to_underlying(rhs);
    });
    return result;
}

ProfileRow to_profile_row(const app::Profile& profile) {
    return ProfileRow{
        .id = profile.id,
        .name = profile.name,
        .endpoint_count = profile.endpoints.size(),
        .enabled_tool_count = profile.enabled_tool_ids.size(),
    };
}

ResourceRow to_resource_row(const app::ResourceDescriptor& resource) {
    return ResourceRow{
        .id = resource.id,
        .name = resource.name,
        .description = resource.description,
        .uri = resource.uri,
        .source_kind = resource.source.kind,
        .source_location = resource.source.location,
    };
}

PromptRow to_prompt_row(const app::PromptDescriptor& prompt) {
    return PromptRow{
        .id = prompt.id,
        .name = prompt.name,
        .description = prompt.description,
        .template_text = prompt.template_text,
        .source_kind = prompt.source.kind,
        .source_location = prompt.source.location,
    };
}

ToolRow to_tool_row(const app::ToolDescriptor& tool) {
    return ToolRow{
        .id = tool.id,
        .name = tool.definition.name,
        .description = tool.definition.description,
        .profile_id = tool.profile_id,
        .source_kind = tool.source.kind,
        .enabled = tool.policy.enabled,
        .approval = tool.policy.approval,
        .permissions = sorted_permissions(tool.policy.permissions),
    };
}

} // namespace

GuiController::GuiController(GuiServices services)
    : services_(services) {}

core::Result<GuiSnapshot> GuiController::snapshot() {
    return build_snapshot({});
}

core::Result<GuiSnapshot> GuiController::select_profile(std::string profile_id) {
    const auto profiles = services_.profiles.list_profiles();
    const auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return profile.id == profile_id;
    });
    if (it == profiles.end()) {
        return std::unexpected(make_gui_error("profile not found", std::move(profile_id)));
    }

    selected_profile_id_ = std::move(profile_id);
    return build_snapshot("Selected profile " + selected_profile_id_);
}

core::Result<GuiSnapshot> GuiController::select_tool(std::string tool_id) {
    const auto tools = services_.tools.list();
    const auto it = std::find_if(tools.begin(), tools.end(), [&](const auto& tool) {
        return tool.id == tool_id;
    });
    if (it == tools.end()) {
        return std::unexpected(make_gui_error("tool not found", std::move(tool_id)));
    }

    selected_tool_id_ = std::move(tool_id);
    return build_snapshot("Selected tool " + selected_tool_id_);
}

core::Result<GuiSnapshot> GuiController::select_resource(std::string resource_id) {
    const auto resources = services_.resources.list();
    const auto it = std::find_if(resources.begin(), resources.end(), [&](const auto& resource) {
        return resource.id == resource_id;
    });
    if (it == resources.end()) {
        return std::unexpected(make_gui_error("resource not found", std::move(resource_id)));
    }

    selected_resource_id_ = std::move(resource_id);
    return build_snapshot("Selected resource " + selected_resource_id_);
}

core::Result<GuiSnapshot> GuiController::select_prompt(std::string prompt_id) {
    const auto prompts = services_.prompts.list();
    const auto it = std::find_if(prompts.begin(), prompts.end(), [&](const auto& prompt) {
        return prompt.id == prompt_id;
    });
    if (it == prompts.end()) {
        return std::unexpected(make_gui_error("prompt not found", std::move(prompt_id)));
    }

    selected_prompt_id_ = std::move(prompt_id);
    return build_snapshot("Selected prompt " + selected_prompt_id_);
}

core::Result<GuiSnapshot> GuiController::enable_tool(std::string tool_id) {
    const auto profile_id = selected_or_default_profile_id();
    if (!profile_id) {
        return std::unexpected(profile_id.error());
    }

    const auto enabled = services_.management.enable_tool(*profile_id, tool_id);
    if (!enabled) {
        return std::unexpected(enabled.error());
    }

    selected_tool_id_ = tool_id;
    return build_snapshot("Enabled tool " + tool_id);
}

core::Result<GuiSnapshot> GuiController::disable_tool(std::string tool_id) {
    const auto profile_id = selected_or_default_profile_id();
    if (!profile_id) {
        return std::unexpected(profile_id.error());
    }

    const auto disabled = services_.management.disable_tool(*profile_id, tool_id);
    if (!disabled) {
        return std::unexpected(disabled.error());
    }

    selected_tool_id_ = tool_id;
    return build_snapshot("Disabled tool " + tool_id);
}

core::Result<GuiSnapshot> GuiController::update_tool_policy(std::string tool_id, app::Policy policy) {
    const auto tools = services_.tools.list();
    const auto it = std::find_if(tools.begin(), tools.end(), [&](const auto& tool) {
        return tool.id == tool_id;
    });
    if (it == tools.end()) {
        return std::unexpected(make_gui_error("tool not found", std::move(tool_id)));
    }

    policy.enabled = it->policy.enabled;
    const auto updated = services_.management.update_policy(tool_id, std::move(policy));
    if (!updated) {
        return std::unexpected(updated.error());
    }

    selected_tool_id_ = std::move(tool_id);
    return build_snapshot("Updated policy for " + selected_tool_id_);
}

core::Result<GuiSnapshot> GuiController::import_bundle(const std::filesystem::path& path) {
    const auto imported = services_.bundles.import_bundle(path);
    if (!imported) {
        return std::unexpected(imported.error());
    }

    const auto saved = services_.profiles.save(imported->profile);
    if (!saved) {
        return std::unexpected(saved.error());
    }

    for (const auto& tool : imported->tools) {
        const auto added = services_.tools.add(tool);
        if (!added) {
            return std::unexpected(added.error());
        }
    }

    selected_profile_id_ = imported->profile.id;
    return build_snapshot("Imported bundle " + imported->profile.id);
}

core::Result<GuiSnapshot> GuiController::export_bundle(const std::filesystem::path& path) {
    const auto profile_id = selected_or_default_profile_id();
    if (!profile_id) {
        return std::unexpected(profile_id.error());
    }

    const auto profiles = services_.profiles.list_profiles();
    const auto profile = std::find_if(profiles.begin(), profiles.end(), [&](const auto& item) {
        return item.id == *profile_id;
    });
    if (profile == profiles.end()) {
        return std::unexpected(make_gui_error("profile not found", *profile_id));
    }

    const auto tools = services_.management.list_profile_tools(*profile_id);
    if (!tools) {
        return std::unexpected(tools.error());
    }

    const auto exported = services_.bundles.export_bundle(app::ExportBundle{.profile = *profile, .tools = *tools}, path);
    if (!exported) {
        return std::unexpected(exported.error());
    }

    return build_snapshot("Exported bundle " + *profile_id);
}

core::Result<std::string> GuiController::selected_or_default_profile_id() {
    const auto profiles = services_.profiles.list_profiles();
    if (!selected_profile_id_.empty()) {
        const auto exists = std::any_of(profiles.begin(), profiles.end(), [&](const auto& profile) {
            return profile.id == selected_profile_id_;
        });
        if (exists) {
            return selected_profile_id_;
        }
    }

    if (profiles.empty()) {
        return std::unexpected(make_gui_error("no profiles configured"));
    }

    selected_profile_id_ = profiles.front().id;
    return selected_profile_id_;
}

core::Result<GuiSnapshot> GuiController::build_snapshot(std::string status) {
    GuiSnapshot snapshot;
    snapshot.status = std::move(status);

    const auto profiles = services_.profiles.list_profiles();
    snapshot.profiles.reserve(profiles.size());
    for (const auto& profile : profiles) {
        snapshot.profiles.push_back(to_profile_row(profile));
    }

    if (!selected_profile_id_.empty()) {
        const auto exists = std::any_of(profiles.begin(), profiles.end(), [&](const auto& profile) {
            return profile.id == selected_profile_id_;
        });
        if (!exists) {
            selected_profile_id_.clear();
        }
    }
    if (selected_profile_id_.empty() && !profiles.empty()) {
        selected_profile_id_ = profiles.front().id;
    }
    snapshot.selected_profile_id = selected_profile_id_;

    const auto tools = services_.tools.list();
    snapshot.tools.reserve(tools.size());
    for (const auto& tool : tools) {
        snapshot.tools.push_back(to_tool_row(tool));
    }

    const auto resources = services_.resources.list();
    snapshot.resources.reserve(resources.size());
    for (const auto& resource : resources) {
        snapshot.resources.push_back(to_resource_row(resource));
    }

    const auto prompts = services_.prompts.list();
    snapshot.prompts.reserve(prompts.size());
    for (const auto& prompt : prompts) {
        snapshot.prompts.push_back(to_prompt_row(prompt));
    }

    if (!selected_tool_id_.empty()) {
        const auto exists = std::any_of(tools.begin(), tools.end(), [&](const auto& tool) {
            return tool.id == selected_tool_id_;
        });
        if (!exists) {
            selected_tool_id_.clear();
        }
    }
    if (selected_tool_id_.empty() && !tools.empty()) {
        selected_tool_id_ = tools.front().id;
    }
    snapshot.selected_tool_id = selected_tool_id_;

    if (!selected_resource_id_.empty()) {
        const auto exists = std::any_of(resources.begin(), resources.end(), [&](const auto& resource) {
            return resource.id == selected_resource_id_;
        });
        if (!exists) {
            selected_resource_id_.clear();
        }
    }
    if (selected_resource_id_.empty() && !resources.empty()) {
        selected_resource_id_ = resources.front().id;
    }
    snapshot.selected_resource_id = selected_resource_id_;

    if (!selected_prompt_id_.empty()) {
        const auto exists = std::any_of(prompts.begin(), prompts.end(), [&](const auto& prompt) {
            return prompt.id == selected_prompt_id_;
        });
        if (!exists) {
            selected_prompt_id_.clear();
        }
    }
    if (selected_prompt_id_.empty() && !prompts.empty()) {
        selected_prompt_id_ = prompts.front().id;
    }
    snapshot.selected_prompt_id = selected_prompt_id_;

    return snapshot;
}

} // namespace mcp::gui
