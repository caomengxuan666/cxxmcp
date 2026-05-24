#include "cxxmcp/app/services.hpp"
#include "cxxmcp/app/tool_management.hpp"

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

    mcp::app::Policy disabled_policy() {
        mcp::app::Policy policy;
        policy.approval = mcp::app::ApprovalState::approved;
        policy.enabled = false;
        policy.permissions.insert(mcp::app::Permission::filesystem_read);
        return policy;
    }

    mcp::app::Policy enabled_policy() {
        auto policy = disabled_policy();
        policy.enabled = true;
        return policy;
    }

    mcp::app::ToolDescriptor tool(std::string id, std::string name) {
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
                .policy = disabled_policy(),
                .profile_id = "default",
        };
    }

    mcp::app::Profile profile(std::vector<std::string> enabled_tools = {}) {
        return mcp::app::Profile{
                .id = "default",
                .name = "Default",
                .endpoints = {},
                .enabled_tool_ids = std::move(enabled_tools),
                .environment = {},
        };
    }

    void test_enable_tool_updates_profile_and_policy() {
        mcp::app::MemoryToolCatalog catalog({tool("tool.echo", "echo")});
        mcp::app::MemoryProfileStore profiles({profile()});
        mcp::app::ToolManagementService service(catalog, profiles);

        const auto enabled = service.enable_tool("default", "tool.echo");
        require(enabled.has_value(), "enable should succeed");

        const auto saved_profile = profiles.list_profiles().front();
        require(saved_profile.enabled_tool_ids.size() == 1, "enabled tool should be stored on profile");
        require(saved_profile.enabled_tool_ids.front() == "tool.echo", "enabled tool id mismatch");
        require(catalog.list().front().policy.enabled, "tool policy should be enabled");
    }

    void test_disable_tool_updates_profile_and_policy() {
        auto existing_tool = tool("tool.echo", "echo");
        existing_tool.policy = enabled_policy();
        mcp::app::MemoryToolCatalog catalog({existing_tool});
        mcp::app::MemoryProfileStore profiles({profile({"tool.echo"})});
        mcp::app::ToolManagementService service(catalog, profiles);

        const auto disabled = service.disable_tool("default", "tool.echo");
        require(disabled.has_value(), "disable should succeed");

        require(profiles.list_profiles().front().enabled_tool_ids.empty(), "disabled tool should be removed from profile");
        require(!catalog.list().front().policy.enabled, "tool policy should be disabled");
    }

    void test_missing_tool_and_profile_fail() {
        mcp::app::MemoryToolCatalog catalog({tool("tool.echo", "echo")});
        mcp::app::MemoryProfileStore profiles({profile()});
        mcp::app::ToolManagementService service(catalog, profiles);

        const auto missing_profile = service.enable_tool("missing", "tool.echo");
        require(!missing_profile.has_value(), "missing profile should fail");
        require(missing_profile.error().message == "profile not found", "missing profile error mismatch");

        const auto missing_tool = service.enable_tool("default", "tool.missing");
        require(!missing_tool.has_value(), "missing tool should fail");
        require(missing_tool.error().message == "tool not found", "missing tool error mismatch");
    }

    void test_profile_bound_listing() {
        auto echo = tool("tool.echo", "echo");
        echo.policy = enabled_policy();
        mcp::app::MemoryToolCatalog catalog({
                echo,
                tool("tool.other", "other"),
        });
        mcp::app::MemoryProfileStore profiles({profile({"tool.echo"})});
        mcp::app::ToolManagementService service(catalog, profiles);

        const auto tools = service.list_profile_tools("default");
        require(tools.has_value(), "profile tool listing should succeed");
        require(tools->size() == 1, "profile tool listing should filter unbound tools");
        require(tools->front().id == "tool.echo", "profile tool listing id mismatch");
    }

    void test_update_policy_state() {
        mcp::app::MemoryToolCatalog catalog({tool("tool.echo", "echo")});
        mcp::app::MemoryProfileStore profiles({profile()});
        mcp::app::ToolManagementService service(catalog, profiles);

        auto policy = enabled_policy();
        policy.approval = mcp::app::ApprovalState::denied;

        const auto updated = service.update_policy("tool.echo", policy);
        require(updated.has_value(), "policy update should succeed");
        require(catalog.list().front().policy.approval == mcp::app::ApprovalState::denied, "policy approval mismatch");
        require(catalog.list().front().policy.enabled, "policy enabled mismatch");
    }

}// namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
            {"enable tool", test_enable_tool_updates_profile_and_policy},
            {"disable tool", test_disable_tool_updates_profile_and_policy},
            {"missing tool and profile", test_missing_tool_and_profile_fail},
            {"profile-bound listing", test_profile_bound_listing},
            {"update policy state", test_update_policy_state},
    };

    std::size_t failures = 0;
    for (const auto &[name, test]: tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception &ex) {
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
