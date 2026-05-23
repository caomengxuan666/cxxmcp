#include "mcp/app/serialization.hpp"
#include "mcp/app/services.hpp"

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

mcp::app::Policy approved_policy() {
    mcp::app::Policy policy;
    policy.approval = mcp::app::ApprovalState::approved;
    policy.enabled = true;
    policy.permissions.insert(mcp::app::Permission::filesystem_read);
    return policy;
}

mcp::app::ToolDescriptor echo_tool() {
    return mcp::app::ToolDescriptor{
        .id = "tool.echo",
        .definition = mcp::protocol::ToolDefinition{
            .name = "echo",
            .description = "Echo input",
            .input_schema = Json::object(),
            .streaming = false,
        },
        .source = mcp::app::ToolSource{
            .kind = mcp::app::ToolSourceKind::local_manifest,
            .location = "tools/echo.json",
        },
        .policy = approved_policy(),
        .profile_id = "default",
    };
}

mcp::app::Profile default_profile() {
    return mcp::app::Profile{
        .id = "default",
        .name = "Default",
        .endpoints = {
            mcp::app::Endpoint{.name = "stdio", .url = "stdio://local"},
        },
        .enabled_tool_ids = {"tool.echo"},
        .environment = {{"MCP_ENV", "test"}},
    };
}

mcp::app::ExportBundle sample_bundle() {
    return mcp::app::ExportBundle{
        .profile = default_profile(),
        .tools = {echo_tool()},
    };
}

void test_bundle_json_round_trip() {
    const auto json = mcp::app::to_json(sample_bundle());
    const auto parsed = mcp::app::export_bundle_from_json(json);
    require(parsed.has_value(), "bundle json should parse");
    require(parsed->profile.id == "default", "profile id mismatch");
    require(parsed->profile.endpoints.front().url == "stdio://local", "endpoint url mismatch");
    require(parsed->tools.size() == 1, "tool count mismatch");
    require(parsed->tools.front().definition.name == "echo", "tool definition mismatch");
    require(parsed->tools.front().policy.enabled, "policy enabled mismatch");
}

void test_memory_services() {
    mcp::app::MemoryProfileStore profiles;
    const auto saved = profiles.save(default_profile());
    require(saved.has_value(), "profile save failed");

    auto listed_profiles = profiles.list_profiles();
    require(listed_profiles.size() == 1, "profile count mismatch");

    auto updated_profile = default_profile();
    updated_profile.name = "Renamed";
    const auto updated = profiles.save(updated_profile);
    require(updated.has_value(), "profile update failed");
    listed_profiles = profiles.list_profiles();
    require(listed_profiles.size() == 1, "profile update should replace existing profile");
    require(listed_profiles.front().name == "Renamed", "profile update mismatch");

    mcp::app::MemoryToolCatalog catalog;
    const auto added = catalog.add(echo_tool());
    require(added.has_value(), "tool add failed");

    auto policy = approved_policy();
    policy.permissions.insert(mcp::app::Permission::command_execution);
    const auto policy_updated = catalog.update_policy("tool.echo", policy);
    require(policy_updated.has_value(), "policy update failed");

    const auto tools = catalog.list();
    require(tools.size() == 1, "catalog count mismatch");
    require(tools.front().policy.permissions.contains(mcp::app::Permission::command_execution),
            "policy permission update mismatch");
}

void test_json_import_export_service() {
    const auto path = std::filesystem::temp_directory_path() / "mcp-app-bundle-test.json";

    mcp::app::JsonImportExportService service;
    const auto exported = service.export_bundle(sample_bundle(), path);
    require(exported.has_value(), "bundle export failed");

    const auto imported = service.import_bundle(path);
    require(imported.has_value(), "bundle import failed");
    require(imported->profile.id == "default", "imported profile mismatch");
    require(imported->tools.front().id == "tool.echo", "imported tool mismatch");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
        {"bundle json round trip", test_bundle_json_round_trip},
        {"memory services", test_memory_services},
        {"json import export service", test_json_import_export_service},
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
