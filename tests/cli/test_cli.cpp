#include "mcp/app/services.hpp"
#include "mcp/cli/commands.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
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

mcp::app::Policy approved_policy(bool enabled) {
    mcp::app::Policy policy;
    policy.approval = mcp::app::ApprovalState::approved;
    policy.enabled = enabled;
    policy.permissions.insert(mcp::app::Permission::filesystem_read);
    return policy;
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
        .policy = approved_policy(enabled),
        .profile_id = std::move(profile_id),
    };
}

mcp::app::Profile profile(std::string id = "default", std::string name = "Default") {
    return mcp::app::Profile{
        .id = std::move(id),
        .name = std::move(name),
        .endpoints = {mcp::app::Endpoint{.name = "stdio", .url = "stdio://local"}},
        .enabled_tool_ids = {"tool.echo"},
        .environment = {},
    };
}

class CliHarness {
public:
    explicit CliHarness(std::vector<mcp::app::ToolDescriptor> tools = {tool("tool.echo", "echo", true)},
                        std::vector<mcp::app::Profile> profiles = {profile()})
        : tools_(std::move(tools)),
          profiles_(std::move(profiles)),
          management_(tools_, profiles_),
          cli_(mcp::cli::CommandServices{.management = management_, .tools = tools_, .profiles = profiles_,
                                         .bundles = bundles_}) {}

    int run(std::initializer_list<std::string_view> args) {
        std::vector<std::string_view> values(args);
        const auto result = cli_.run(values, out, err);
        require(result.has_value(), "cli run returned unexpected error");
        return *result;
    }

    mcp::app::MemoryToolCatalog tools_;
    mcp::app::MemoryProfileStore profiles_;
    mcp::app::ToolManagementService management_;
    mcp::app::JsonImportExportService bundles_;
    mcp::cli::CommandApp cli_;
    std::ostringstream out;
    std::ostringstream err;
};

void test_lists_tools() {
    CliHarness harness({tool("tool.echo", "echo", true), tool("tool.sleep", "sleep", false)});

    const auto exit_code = harness.run({"tools", "list"});

    require(exit_code == 0, "tools list exit code mismatch");
    require(harness.out.str() == "tool.echo\techo\tenabled\tapproved\tdefault\n"
                                 "tool.sleep\tsleep\tdisabled\tapproved\tdefault\n",
            "tools list output mismatch");
    require(harness.err.str().empty(), "tools list should not write stderr");
}

void test_enables_and_disables_tool_policy() {
    CliHarness harness({tool("tool.echo", "echo", false)});

    auto exit_code = harness.run({"tools", "enable", "tool.echo"});
    require(exit_code == 0, "enable exit code mismatch");
    require(harness.tools_.list().front().policy.enabled, "tool should be enabled");

    exit_code = harness.run({"tools", "disable", "tool.echo"});
    require(exit_code == 0, "disable exit code mismatch");
    require(!harness.tools_.list().front().policy.enabled, "tool should be disabled");
}

void test_lists_profiles() {
    CliHarness harness({tool("tool.echo", "echo", true)}, {profile("default", "Default"), profile("dev", "Dev")});

    const auto exit_code = harness.run({"profiles", "list"});

    require(exit_code == 0, "profiles list exit code mismatch");
    require(harness.out.str() == "default\tDefault\t1 endpoint(s)\t1 enabled tool(s)\n"
                                 "dev\tDev\t1 endpoint(s)\t1 enabled tool(s)\n",
            "profiles list output mismatch");
}

void test_exports_bundle_through_service() {
    const auto path = std::filesystem::temp_directory_path() / "mcp-cli-export-test.json";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    CliHarness harness({tool("tool.echo", "echo", true), tool("tool.dev", "dev", true, "dev")});

    const auto exit_code = harness.run({"bundle", "export", path.string()});

    require(exit_code == 0, "bundle export exit code mismatch");
    require(harness.out.str() == "Exported bundle default with 1 tool(s)\n", "bundle export output mismatch");

    mcp::app::JsonImportExportService service;
    const auto imported = service.import_bundle(path);
    require(imported.has_value(), "exported bundle should be readable");
    require(imported->profile.id == "default", "exported profile id mismatch");
    require(imported->tools.size() == 1, "exported tool count mismatch");
    require(imported->tools.front().id == "tool.echo", "exported tool id mismatch");

    std::filesystem::remove(path, ec);
}

void test_imports_bundle_through_service() {
    const auto path = std::filesystem::temp_directory_path() / "mcp-cli-import-test.json";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    mcp::app::JsonImportExportService service;
    const auto exported = service.export_bundle(
        mcp::app::ExportBundle{
            .profile = profile("imported", "Imported"),
            .tools = {tool("tool.imported", "imported", true, "imported")},
        },
        path);
    require(exported.has_value(), "test bundle export failed");

    CliHarness harness({}, {});
    const auto exit_code = harness.run({"bundle", "import", path.string()});

    require(exit_code == 0, "bundle import exit code mismatch");
    require(harness.out.str() == "Imported bundle imported with 1 tool(s)\n", "bundle import output mismatch");
    require(harness.profiles_.list_profiles().size() == 1, "imported profile count mismatch");
    require(harness.tools_.list().size() == 1, "imported tool count mismatch");
    require(harness.tools_.list().front().id == "tool.imported", "imported tool id mismatch");

    std::filesystem::remove(path, ec);
}

void test_invalid_command_returns_usage_error() {
    CliHarness harness;

    const auto exit_code = harness.run({"tools"});

    require(exit_code == 2, "invalid command exit code mismatch");
    require(harness.out.str().empty(), "invalid command should not write stdout");
    require(harness.err.str().find("invalid tools command") != std::string::npos, "missing parse error");
    require(harness.err.str().find("mcp tools list") != std::string::npos, "missing usage");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
        {"lists tools", test_lists_tools},
        {"enables and disables tool policy", test_enables_and_disables_tool_policy},
        {"lists profiles", test_lists_profiles},
        {"exports bundle through service", test_exports_bundle_through_service},
        {"imports bundle through service", test_imports_bundle_through_service},
        {"invalid command returns usage error", test_invalid_command_returns_usage_error},
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
