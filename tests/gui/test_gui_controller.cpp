#include "mcp/app/services.hpp"
#include "mcp/app/tool_management.hpp"
#include "mcp/gui/controller.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using mcp::protocol::Json;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

mcp::app::Policy policy(bool enabled) {
    mcp::app::Policy result;
    result.approval = mcp::app::ApprovalState::approved;
    result.enabled = enabled;
    result.permissions.insert(mcp::app::Permission::filesystem_read);
    return result;
}

mcp::app::ToolDescriptor tool(std::string id, std::string name, bool enabled, std::string profile_id = "default") {
    return mcp::app::ToolDescriptor{
        .id = std::move(id),
        .definition = mcp::protocol::ToolDefinition{
            .name = std::move(name),
            .description = "Test tool",
            .input_schema = Json::object(),
            .streaming = false,
        },
        .source = mcp::app::ToolSource{
            .kind = mcp::app::ToolSourceKind::local_manifest,
            .location = "tools/test.json",
        },
        .policy = policy(enabled),
        .profile_id = std::move(profile_id),
    };
}

mcp::app::ResourceDescriptor resource(std::string id,
                                      std::string name,
                                      std::string description,
                                      std::string uri,
                                      std::string location = "resources/test.json") {
    return mcp::app::ResourceDescriptor{
        .id = std::move(id),
        .name = std::move(name),
        .description = std::move(description),
        .uri = std::move(uri),
        .source = mcp::app::ResourceSource{
            .kind = mcp::app::ResourceSourceKind::local_manifest,
            .location = std::move(location),
        },
    };
}

mcp::app::PromptDescriptor prompt(std::string id,
                                  std::string name,
                                  std::string description,
                                  std::string template_text,
                                  std::string location = "prompts/test.txt") {
    return mcp::app::PromptDescriptor{
        .id = std::move(id),
        .name = std::move(name),
        .description = std::move(description),
        .template_text = std::move(template_text),
        .source = mcp::app::PromptSource{
            .kind = mcp::app::PromptSourceKind::local_manifest,
            .location = std::move(location),
        },
    };
}

mcp::app::Profile profile(std::string id = "default", std::string name = "Default", std::vector<std::string> enabled = {}) {
    return mcp::app::Profile{
        .id = std::move(id),
        .name = std::move(name),
        .endpoints = {mcp::app::Endpoint{.name = "stdio", .url = "stdio://local"}},
        .enabled_tool_ids = std::move(enabled),
        .environment = {},
    };
}

struct GuiHarness {
    GuiHarness(std::vector<mcp::app::ToolDescriptor> tools = {tool("tool.echo", "echo", true)},
               std::vector<mcp::app::Profile> profiles = {profile()},
               std::vector<mcp::app::ResourceDescriptor> resources = {
                   resource("resource.docs", "docs", "Documentation", "mcp://resources/docs")},
               std::vector<mcp::app::PromptDescriptor> prompts = {
                   prompt("prompt.summary", "summary", "Summarize workspace", "Summarize the current workspace.")})
        : tools_store(std::move(tools)),
          resources_store(std::move(resources)),
          prompts_store(std::move(prompts)),
          profiles_store(std::move(profiles)),
          management(tools_store, profiles_store),
          controller(mcp::gui::GuiServices{
              .management = management,
              .tools = tools_store,
              .resources = resources_store,
              .prompts = prompts_store,
              .profiles = profiles_store,
              .bundles = bundles,
          }) {}

    mcp::app::MemoryToolCatalog tools_store;
    mcp::app::MemoryResourceCatalog resources_store;
    mcp::app::MemoryPromptCatalog prompts_store;
    mcp::app::MemoryProfileStore profiles_store;
    mcp::app::ToolManagementService management;
    mcp::app::JsonImportExportService bundles;
    mcp::gui::GuiController controller;
};

void test_snapshot_and_profile_selection() {
    GuiHarness harness({
        tool("tool.echo", "echo", true),
        tool("tool.sleep", "sleep", false),
    }, {
        profile("default", "Default", {"tool.echo"}),
        profile("dev", "Dev", {}),
    });

    const auto snapshot = harness.controller.snapshot();
    require(snapshot.has_value(), "snapshot should succeed");
    require(snapshot->profiles.size() == 2, "profile count mismatch");
    require(snapshot->selected_profile_id == "default", "default profile selection mismatch");
    require(snapshot->selected_tool_id == "tool.echo", "default tool selection mismatch");
    require(snapshot->tools.size() == 2, "tool count mismatch");
    require(snapshot->resources.size() == 1, "resource count mismatch");
    require(snapshot->prompts.size() == 1, "prompt count mismatch");
    require(snapshot->selected_resource_id == "resource.docs", "default resource selection mismatch");
    require(snapshot->selected_prompt_id == "prompt.summary", "default prompt selection mismatch");

    const auto selected = harness.controller.select_profile("dev");
    require(selected.has_value(), "select profile should succeed");
    require(selected->selected_profile_id == "dev", "selected profile mismatch");
    require(selected->status == "Selected profile dev", "status mismatch");

    const auto tool_selected = harness.controller.select_tool("tool.sleep");
    require(tool_selected.has_value(), "select tool should succeed");
    require(tool_selected->selected_tool_id == "tool.sleep", "selected tool mismatch");
    require(tool_selected->status == "Selected tool tool.sleep", "tool status mismatch");

    const auto resource_selected = harness.controller.select_resource("resource.docs");
    require(resource_selected.has_value(), "select resource should succeed");
    require(resource_selected->selected_resource_id == "resource.docs", "selected resource mismatch");
    require(resource_selected->status == "Selected resource resource.docs", "resource status mismatch");

    const auto prompt_selected = harness.controller.select_prompt("prompt.summary");
    require(prompt_selected.has_value(), "select prompt should succeed");
    require(prompt_selected->selected_prompt_id == "prompt.summary", "selected prompt mismatch");
    require(prompt_selected->status == "Selected prompt prompt.summary", "prompt status mismatch");
}

void test_enable_disable_tool_flow() {
    GuiHarness harness({tool("tool.echo", "echo", false)}, {profile()});

    const auto enabled = harness.controller.enable_tool("tool.echo");
    require(enabled.has_value(), "enable should succeed");
    require(enabled->tools.front().enabled, "tool should be enabled in snapshot");
    require(harness.profiles_store.list_profiles().front().enabled_tool_ids.size() == 1, "profile binding missing");

    const auto disabled = harness.controller.disable_tool("tool.echo");
    require(disabled.has_value(), "disable should succeed");
    require(!disabled->tools.front().enabled, "tool should be disabled in snapshot");
    require(harness.profiles_store.list_profiles().front().enabled_tool_ids.empty(), "profile binding should be removed");
}

void test_policy_update_flow() {
    GuiHarness harness({tool("tool.echo", "echo", false)}, {profile()});

    mcp::app::Policy policy;
    policy.approval = mcp::app::ApprovalState::denied;
    policy.permissions.insert(mcp::app::Permission::filesystem_read);
    policy.permissions.insert(mcp::app::Permission::command_execution);

    const auto updated = harness.controller.update_tool_policy("tool.echo", policy);
    require(updated.has_value(), "policy update should succeed");
    require(updated->selected_tool_id == "tool.echo", "selected tool should remain on updated tool");
    require(updated->tools.front().approval == mcp::app::ApprovalState::denied, "approval mismatch");
    require(updated->tools.front().permissions.size() == 2, "permission count mismatch");
}

void test_import_and_export_bundle() {
    const auto import_path = std::filesystem::temp_directory_path() / "mcp-gui-import.json";
    const auto export_path = std::filesystem::temp_directory_path() / "mcp-gui-export.json";
    std::error_code ec;
    std::filesystem::remove(import_path, ec);
    std::filesystem::remove(export_path, ec);

    mcp::app::JsonImportExportService bundles;
    const auto bundle_written = bundles.export_bundle(
        mcp::app::ExportBundle{
            .profile = profile("imported", "Imported", {"tool.imported"}),
            .tools = {tool("tool.imported", "imported", true, "imported")},
        },
        import_path);
    require(bundle_written.has_value(), "bundle setup failed");

    GuiHarness harness({}, {});
    const auto imported = harness.controller.import_bundle(import_path);
    require(imported.has_value(), "import should succeed");
    require(imported->selected_profile_id == "imported", "imported profile selection mismatch");
    require(imported->tools.size() == 1, "imported tool count mismatch");

    const auto exported = harness.controller.export_bundle(export_path);
    require(exported.has_value(), "export should succeed");
    require(std::filesystem::exists(export_path), "exported file missing");

    std::filesystem::remove(import_path, ec);
    std::filesystem::remove(export_path, ec);
}

void test_missing_profile_fails() {
    GuiHarness harness({}, {});

    const auto snapshot = harness.controller.enable_tool("tool.echo");
    require(!snapshot.has_value(), "enable with no profile should fail");
    require(snapshot.error().message == "no profiles configured", "missing profile error mismatch");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
        {"snapshot and profile selection", test_snapshot_and_profile_selection},
        {"enable disable flow", test_enable_disable_tool_flow},
        {"policy update flow", test_policy_update_flow},
        {"import and export bundle", test_import_and_export_bundle},
        {"missing profile fails", test_missing_profile_fails},
    };

    std::size_t failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << ex.what() << '\n';
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << tests.size() << " test(s) passed\n";
    return 0;
}
