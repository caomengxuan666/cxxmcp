#include "cxxmcp/app.hpp"

#include <iostream>
#include <memory>
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

    mcp::app::McpServerDefinition filesystem_server() {
        return mcp::app::McpServerDefinition{
                .id = "server.filesystem",
                .name = "filesystem",
                .display_name = "Filesystem",
                .description = "Filesystem MCP server",
                .transport = mcp::app::McpServerTransportKind::stdio,
                .stdio = mcp::app::StdioLaunchConfig{
                        .command = "npx",
                        .args = {"-y", "@modelcontextprotocol/server-filesystem", "C:/workspace"},
                        .cwd = "C:/workspace",
                        .env = {},
                },
                .http = {},
                .enabled = true,
                .auto_start = true,
                .trust = mcp::app::McpServerTrustState::trusted,
                .tags = {"local"},
        };
    }

    class FakeDiscoverySession final : public mcp::app::McpDiscoverySession {
    public:
        explicit FakeDiscoverySession(bool *initialized_observer = nullptr)
            : initialized_observer_(initialized_observer) {}

        mcp::core::Result<mcp::core::Unit> initialize() override {
            initialized = true;
            if (initialized_observer_ != nullptr) {
                *initialized_observer_ = true;
            }
            return mcp::core::Unit{};
        }

        mcp::core::Result<std::vector<mcp::protocol::ToolDefinition>> discover_tools() override {
            return std::vector<mcp::protocol::ToolDefinition>{
                    mcp::protocol::ToolDefinition{
                            .name = "read_file",
                            .description = "Read a file",
                            .input_schema = Json{{"type", "object"}},
                            .streaming = false,
                    },
            };
        }

        mcp::core::Result<std::vector<mcp::protocol::Prompt>> discover_prompts() override {
            return std::vector<mcp::protocol::Prompt>{
                    mcp::protocol::Prompt{
                            .name = "summarize",
                            .description = "Summarize input",
                            .arguments = {
                                    mcp::protocol::PromptArgument{
                                            .name = "text",
                                            .description = "Input text",
                                            .required = true,
                                    },
                            },
                    },
            };
        }

        mcp::core::Result<std::vector<mcp::protocol::Resource>> discover_resources() override {
            return std::vector<mcp::protocol::Resource>{
                    mcp::protocol::Resource{
                            .uri = "file:///workspace/README.md",
                            .name = "Readme",
                            .description = "Project readme",
                            .mime_type = "text/markdown",
                    },
            };
        }

        bool initialized = false;

    private:
        bool *initialized_observer_ = nullptr;
    };

    void test_discover_server_writes_normalized_capabilities() {
        mcp::app::MemoryMcpServerStore servers({filesystem_server()});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        bool initialized = false;

        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [&](const mcp::app::McpServerDefinition &server)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    require(server.id == "server.filesystem", "factory server id mismatch");
                    return std::make_unique<FakeDiscoverySession>(&initialized);
                });

        const auto discovered = management.discover_server("server.filesystem");
        require(discovered.has_value(), "discover_server should succeed");
        require(initialized, "discovery should initialize session first");
        require(discovered->server_id == "server.filesystem", "discovery result server id mismatch");
        require(discovered->capability_count == 3, "discovery result capability count mismatch");

        const auto listed = capabilities.list_capabilities();
        require(listed.size() == 3, "capability catalog count mismatch");
        require(listed[0].server_id == "server.filesystem", "capability server id mismatch");
        require(listed[0].kind == mcp::app::CapabilityKind::tool, "tool capability kind mismatch");
        require(listed[0].upstream_name == "read_file", "tool capability name mismatch");
        require(listed[0].exposed_name == "filesystem.read_file", "tool exposed name mismatch");
        require(listed[1].kind == mcp::app::CapabilityKind::prompt, "prompt capability kind mismatch");
        require(listed[1].upstream_name == "summarize", "prompt capability name mismatch");
        require(listed[2].kind == mcp::app::CapabilityKind::resource, "resource capability kind mismatch");
        require(listed[2].uri == "file:///workspace/README.md", "resource capability uri mismatch");
    }

    void test_discover_all_servers_reports_successes_and_skips() {
        auto trusted = filesystem_server();
        auto untrusted = filesystem_server();
        untrusted.id = "server.untrusted";
        untrusted.name = "untrusted";
        untrusted.trust = mcp::app::McpServerTrustState::untrusted;
        mcp::app::MemoryMcpServerStore servers({trusted, untrusted});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;

        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::make_unique<FakeDiscoverySession>();
                });

        const auto reports = management.discover_all_servers();
        require(reports.size() == 2, "discover all report count mismatch");
        require(reports[0].server_id == "server.filesystem", "discover all first server mismatch");
        require(reports[0].discovered, "trusted server should be discovered");
        require(reports[0].capability_count == 3, "trusted server discovery count mismatch");
        require(reports[1].server_id == "server.untrusted", "discover all second server mismatch");
        require(!reports[1].discovered, "untrusted server should be skipped");
        require(reports[1].error_message == "cxxmcp server is untrusted", "untrusted report error mismatch");
        require(capabilities.list_capabilities().size() == 3, "discover all should persist trusted capabilities only");
    }

    void test_missing_server_fails_discovery() {
        mcp::app::MemoryMcpServerStore servers;
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles({
                mcp::app::ExposureProfile{
                        .id = "profile.dev",
                        .name = "Dev",
                        .instructions = {},
                        .endpoint = {},
                        .bindings = {mcp::app::CapabilityBinding{
                                .id = "binding.filesystem.read_file",
                                .server_id = "server.filesystem",
                                .kind = mcp::app::CapabilityKind::tool,
                                .upstream_name = "read_file",
                                .exposed_name = "filesystem.read_file",
                                .namespace_strategy = mcp::app::NamespaceStrategy::server_prefix,
                                .enabled = true,
                                .policy = {},
                        }},
                        .environment_overrides = {},
                },
        });
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "not used"});
                });

        const auto discovered = management.discover_server("missing");
        require(!discovered.has_value(), "missing server discovery should fail");
        require(discovered.error().message == "mcp server not found", "missing server error mismatch");
    }

    void test_updates_server_enabled_state() {
        mcp::app::MemoryMcpServerStore servers({filesystem_server()});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "not used"});
                });

        const auto disabled = management.set_server_enabled("server.filesystem", false);
        require(disabled.has_value(), "disable server should succeed");
        require(!disabled->enabled, "returned server should be disabled");
        require(!servers.list_servers().front().enabled, "stored server should be disabled");

        const auto enabled = management.set_server_enabled("server.filesystem", true);
        require(enabled.has_value(), "enable server should succeed");
        require(enabled->enabled, "returned server should be enabled");
        require(servers.list_servers().front().enabled, "stored server should be enabled");
    }

    void test_updates_server_trust_state() {
        auto server = filesystem_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        mcp::app::MemoryMcpServerStore servers({server});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "not used"});
                });

        const auto trusted = management.set_server_trust("server.filesystem", mcp::app::McpServerTrustState::trusted);
        require(trusted.has_value(), "trust server should succeed");
        require(trusted->trust == mcp::app::McpServerTrustState::trusted, "returned server trust mismatch");
        require(servers.list_servers().front().trust == mcp::app::McpServerTrustState::trusted,
                "stored server trust mismatch");

        const auto blocked = management.set_server_trust("server.filesystem", mcp::app::McpServerTrustState::blocked);
        require(blocked.has_value(), "block server should succeed");
        require(servers.list_servers().front().trust == mcp::app::McpServerTrustState::blocked,
                "stored blocked server mismatch");
    }

    void test_updates_stdio_launch_environment() {
        mcp::app::MemoryMcpServerStore servers({filesystem_server()});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "not used"});
                });

        const auto cwd = management.set_stdio_cwd("server.filesystem", "D:/workspace");
        require(cwd.has_value(), "set stdio cwd should succeed");
        require(cwd->stdio.cwd == "D:/workspace", "returned stdio cwd mismatch");
        require(servers.list_servers().front().stdio.cwd == "D:/workspace", "stored stdio cwd mismatch");

        const auto env = management.set_stdio_env("server.filesystem", "API_TOKEN", "secret");
        require(env.has_value(), "set stdio env should succeed");
        require(env->stdio.env.at("API_TOKEN") == "secret", "returned stdio env mismatch");
        require(servers.list_servers().front().stdio.env.at("API_TOKEN") == "secret", "stored stdio env mismatch");

        const auto removed = management.unset_stdio_env("server.filesystem", "API_TOKEN");
        require(removed.has_value(), "unset stdio env should succeed");
        require(!removed->stdio.env.contains("API_TOKEN"), "returned stdio env should be removed");
        require(!servers.list_servers().front().stdio.env.contains("API_TOKEN"), "stored stdio env should be removed");
    }

    void test_rejects_stdio_options_for_http_server() {
        auto server = filesystem_server();
        server.id = "server.remote";
        server.transport = mcp::app::McpServerTransportKind::streamable_http;
        server.stdio = {};
        server.http.url = "http://127.0.0.1:3000/mcp";
        mcp::app::MemoryMcpServerStore servers({server});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "not used"});
                });

        const auto cwd = management.set_stdio_cwd("server.remote", "D:/workspace");
        require(!cwd.has_value(), "set stdio cwd should reject http server");
        require(cwd.error().message == "mcp server is not stdio", "set stdio cwd error mismatch");
    }

    void test_updates_http_headers() {
        auto server = filesystem_server();
        server.id = "server.remote";
        server.transport = mcp::app::McpServerTransportKind::streamable_http;
        server.stdio = {};
        server.http.url = "http://127.0.0.1:3000/mcp";
        mcp::app::MemoryMcpServerStore servers({server});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "not used"});
                });

        const auto header = management.set_http_header("server.remote", "Authorization", "Bearer token");
        require(header.has_value(), "set http header should succeed");
        require(header->http.headers.at("Authorization") == "Bearer token", "returned http header mismatch");
        require(servers.list_servers().front().http.headers.at("Authorization") == "Bearer token",
                "stored http header mismatch");

        const auto removed = management.unset_http_header("server.remote", "Authorization");
        require(removed.has_value(), "unset http header should succeed");
        require(!removed->http.headers.contains("Authorization"), "returned http header should be removed");
        require(!servers.list_servers().front().http.headers.contains("Authorization"),
                "stored http header should be removed");
    }

    void test_rejects_http_headers_for_stdio_server() {
        mcp::app::MemoryMcpServerStore servers({filesystem_server()});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "not used"});
                });

        const auto header = management.set_http_header("server.filesystem", "Authorization", "Bearer token");
        require(!header.has_value(), "set http header should reject stdio server");
        require(header.error().message == "mcp server is not http", "set http header error mismatch");
    }

    void test_removes_server_and_capabilities() {
        mcp::app::MemoryMcpServerStore servers({filesystem_server()});
        mcp::app::MemoryCapabilityCatalog capabilities({
                mcp::app::DiscoveredCapability{
                        .id = "server.filesystem:tool:read_file",
                        .kind = mcp::app::CapabilityKind::tool,
                        .server_id = "server.filesystem",
                        .upstream_name = "read_file",
                        .exposed_name = "filesystem.read_file",
                        .title = "Read File",
                        .description = "Read a file",
                        .uri = {},
                        .input_schema = Json::object(),
                        .output_schema = Json::object(),
                        .template_text = {},
                        .capability_hash = {},
                },
        });
        mcp::app::MemoryExposureProfileStore exposure_profiles({
                mcp::app::ExposureProfile{
                        .id = "profile.dev",
                        .name = "Dev",
                        .instructions = {},
                        .endpoint = {},
                        .bindings = {mcp::app::CapabilityBinding{
                                .id = "binding.filesystem.read_file",
                                .server_id = "server.filesystem",
                                .kind = mcp::app::CapabilityKind::tool,
                                .upstream_name = "read_file",
                                .exposed_name = "filesystem.read_file",
                                .namespace_strategy = mcp::app::NamespaceStrategy::server_prefix,
                                .enabled = true,
                                .policy = {},
                        }},
                        .environment_overrides = {},
                },
        });
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "not used"});
                });

        const auto removed = management.remove_server("server.filesystem");
        require(removed.has_value(), "remove server should succeed");
        require(servers.list_servers().empty(), "removed server should not remain");
        require(capabilities.list_capabilities().empty(), "removed server capabilities should be cleared");
        require(exposure_profiles.list_exposure_profiles().front().bindings.empty(),
                "removed server exposure bindings should be cleared");
    }

    void test_untrusted_server_fails_discovery() {
        auto server = filesystem_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        mcp::app::MemoryMcpServerStore servers({server});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "discovery should not start"});
                });

        const auto discovered = management.discover_server("server.filesystem");
        require(!discovered.has_value(), "untrusted server discovery should fail");
        require(discovered.error().message == "cxxmcp server is untrusted", "untrusted discovery error mismatch");
    }

    void test_disabled_server_fails_discovery() {
        auto server = filesystem_server();
        server.enabled = false;
        mcp::app::MemoryMcpServerStore servers({server});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::unexpected(mcp::core::Error{1, "discovery should not start"});
                });

        const auto discovered = management.discover_server("server.filesystem");
        require(!discovered.has_value(), "disabled server discovery should fail");
        require(discovered.error().message == "cxxmcp server is disabled", "disabled discovery error mismatch");
    }

}// namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
            {"discover server writes normalized capabilities", test_discover_server_writes_normalized_capabilities},
            {"discover all servers reports successes and skips", test_discover_all_servers_reports_successes_and_skips},
            {"missing server fails discovery", test_missing_server_fails_discovery},
            {"updates server enabled state", test_updates_server_enabled_state},
            {"updates server trust state", test_updates_server_trust_state},
            {"updates stdio launch environment", test_updates_stdio_launch_environment},
            {"rejects stdio options for http server", test_rejects_stdio_options_for_http_server},
            {"updates http headers", test_updates_http_headers},
            {"rejects http headers for stdio server", test_rejects_http_headers_for_stdio_server},
            {"removes server and capabilities", test_removes_server_and_capabilities},
            {"untrusted server fails discovery", test_untrusted_server_fails_discovery},
            {"disabled server fails discovery", test_disabled_server_fails_discovery},
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
