#include "cxxmcp/app.hpp"

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

    mcp::app::McpServerDefinition sample_stdio_server() {
        return mcp::app::McpServerDefinition{
                .id = "server.filesystem",
                .name = "filesystem",
                .display_name = "Filesystem",
                .description = "Local filesystem MCP server",
                .transport = mcp::app::McpServerTransportKind::stdio,
                .stdio = mcp::app::StdioLaunchConfig{
                        .command = "npx",
                        .args = {"-y", "@modelcontextprotocol/server-filesystem", "C:/workspace"},
                        .cwd = "C:/workspace",
                        .env = {{"NODE_ENV", "production"}},
                },
                .http = {},
                .enabled = true,
                .auto_start = true,
                .trust = mcp::app::McpServerTrustState::trusted,
                .tags = {"local", "files"},
        };
    }

    mcp::app::DiscoveredCapability sample_capability() {
        return mcp::app::DiscoveredCapability{
                .id = "server.filesystem:tool:read_file",
                .kind = mcp::app::CapabilityKind::tool,
                .server_id = "server.filesystem",
                .upstream_name = "read_file",
                .exposed_name = "filesystem.read_file",
                .title = "Read File",
                .description = "Read a local file",
                .uri = {},
                .input_schema = Json{{"type", "object"}},
                .output_schema = Json{{"type", "object"}},
                .template_text = {},
                .capability_hash = "hash-1",
        };
    }

    class CheckDiscoverySession final : public mcp::app::McpDiscoverySession {
    public:
        mcp::core::Result<mcp::core::Unit> initialize() override {
            initialized = true;
            return mcp::core::Unit{};
        }

        mcp::core::Result<std::vector<mcp::protocol::ToolDefinition>> discover_tools() override {
            require(initialized, "check discovery session should initialize before listing tools");
            return std::vector<mcp::protocol::ToolDefinition>{mcp::protocol::ToolDefinition{
                    .name = "read_file",
                    .description = "Read a file",
                    .input_schema = Json{{"type", "object"}},
            }};
        }

        mcp::core::Result<std::vector<mcp::protocol::Prompt>> discover_prompts() override {
            require(initialized, "check discovery session should initialize before listing prompts");
            return std::vector<mcp::protocol::Prompt>{};
        }

        mcp::core::Result<std::vector<mcp::protocol::Resource>> discover_resources() override {
            require(initialized, "check discovery session should initialize before listing resources");
            return std::vector<mcp::protocol::Resource>{};
        }

    private:
        bool initialized = false;
    };

    mcp::app::McpDiscoverySessionFactory check_discovery_factory() {
        return [](const mcp::app::McpServerDefinition &) -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
            return std::make_unique<CheckDiscoverySession>();
        };
    }

    mcp::app::ExposureProfile sample_exposure_profile() {
        mcp::app::Policy policy;
        policy.enabled = true;
        policy.approval = mcp::app::ApprovalState::approved;
        policy.permissions.insert(mcp::app::Permission::filesystem_read);

        return mcp::app::ExposureProfile{
                .id = "profile.dev",
                .name = "Dev Gateway",
                .instructions = "Expose only reviewed local development tools.",
                .endpoint = mcp::app::HostedEndpoint{
                        .name = "dev-http",
                        .listen_host = "127.0.0.1",
                        .listen_port = 8765,
                        .path = "/cxxmcp/dev",
                        .transport = mcp::app::McpServerTransportKind::streamable_http,
                },
                .bindings = {
                        mcp::app::CapabilityBinding{
                                .id = "binding.filesystem.read_file",
                                .server_id = "server.filesystem",
                                .kind = mcp::app::CapabilityKind::tool,
                                .upstream_name = "read_file",
                                .exposed_name = "filesystem.read_file",
                                .namespace_strategy = mcp::app::NamespaceStrategy::server_prefix,
                                .enabled = true,
                                .policy = policy,
                        },
                },
                .environment_overrides = {{"MCP_PROFILE", "dev"}},
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

    void test_mcp_server_json_round_trip() {
        const auto json = mcp::app::to_json(sample_stdio_server());
        const auto parsed = mcp::app::mcp_server_definition_from_json(json);
        require(parsed.has_value(), "mcp server json should parse");
        require(parsed->id == "server.filesystem", "mcp server id mismatch");
        require(parsed->transport == mcp::app::McpServerTransportKind::stdio, "mcp server transport mismatch");
        require(parsed->stdio.command == "npx", "mcp server command mismatch");
        require(parsed->stdio.args.size() == 3, "mcp server args mismatch");
        require(parsed->stdio.env.at("NODE_ENV") == "production", "mcp server env mismatch");
        require(parsed->trust == mcp::app::McpServerTrustState::trusted, "mcp server trust mismatch");
    }

    void test_imports_trae_claude_mcp_servers_config() {
        const auto imported = mcp::app::mcp_server_definitions_from_client_config_json(Json{
                {"mcpServers",
                 Json{
                         {"filesystem",
                          Json{
                                  {"command", "npx"},
                                  {"args", Json::array({"-y", "@modelcontextprotocol/server-filesystem", "C:/workspace"})},
                                  {"env", Json{{"NODE_ENV", "production"}, {"MCP_LOG_LEVEL", "debug"}}},
                          }},
                 }},
        });

        require(imported.has_value(), "mcpServers config should import");
        require(imported->size() == 1, "mcpServers import count mismatch");

        const auto &server = imported->front();
        require(server.id == "filesystem", "imported stdio server id mismatch");
        require(server.name == "filesystem", "imported stdio server name mismatch");
        require(server.display_name == "filesystem", "imported stdio server display name mismatch");
        require(server.transport == mcp::app::McpServerTransportKind::stdio, "imported stdio transport mismatch");
        require(server.stdio.command == "npx", "imported stdio command mismatch");
        require(server.stdio.args.size() == 3, "imported stdio args count mismatch");
        require(server.stdio.args.at(1) == "@modelcontextprotocol/server-filesystem", "imported stdio arg mismatch");
        require(server.stdio.env.at("NODE_ENV") == "production", "imported stdio env mismatch");
    }

    void test_imports_vscode_servers_http_config() {
        const auto imported = mcp::app::mcp_server_definitions_from_client_config_json(Json{
                {"servers",
                 Json{
                         {"my-http",
                          Json{
                                  {"type", "http"},
                                  {"url", "http://127.0.0.1:3000/mcp"},
                                  {"headers", Json{{"Authorization", "Bearer token"}, {"X-MCP-Client", "vscode"}}},
                          }},
                 }},
        });

        require(imported.has_value(), "servers http config should import");
        require(imported->size() == 1, "servers http import count mismatch");

        const auto &server = imported->front();
        require(server.id == "my-http", "imported http server id mismatch");
        require(server.name == "my-http", "imported http server name mismatch");
        require(server.transport == mcp::app::McpServerTransportKind::streamable_http, "imported http transport mismatch");
        require(server.http.url == "http://127.0.0.1:3000/mcp", "imported http url mismatch");
        require(server.http.headers.at("Authorization") == "Bearer token", "imported http header mismatch");
    }

    void test_exposure_profile_json_round_trip() {
        const auto json = mcp::app::to_json(sample_exposure_profile());
        const auto parsed = mcp::app::exposure_profile_from_json(json);
        require(parsed.has_value(), "exposure profile json should parse");
        require(parsed->id == "profile.dev", "exposure profile id mismatch");
        require(parsed->endpoint.listen_port == 8765, "exposure profile endpoint port mismatch");
        require(parsed->endpoint.path == "/cxxmcp/dev", "exposure profile path mismatch");
        require(parsed->bindings.size() == 1, "exposure profile binding count mismatch");
        require(parsed->bindings.front().exposed_name == "filesystem.read_file", "binding exposed name mismatch");
        require(parsed->bindings.front().policy.permissions.contains(mcp::app::Permission::filesystem_read),
                "binding policy permission mismatch");
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

        mcp::app::MemoryMcpServerStore servers;
        const auto server_saved = servers.save(sample_stdio_server());
        require(server_saved.has_value(), "mcp server save failed");
        auto listed_servers = servers.list_servers();
        require(listed_servers.size() == 1, "mcp server count mismatch");

        auto updated_server = sample_stdio_server();
        updated_server.display_name = "Filesystem Updated";
        const auto server_updated = servers.save(updated_server);
        require(server_updated.has_value(), "mcp server update failed");
        listed_servers = servers.list_servers();
        require(listed_servers.size() == 1, "mcp server update should replace existing server");
        require(listed_servers.front().display_name == "Filesystem Updated", "mcp server update mismatch");
        const auto server_removed = servers.remove("server.filesystem");
        require(server_removed.has_value(), "mcp server remove failed");
        require(servers.list_servers().empty(), "mcp server should be removed");

        mcp::app::MemoryCapabilityCatalog capabilities;
        const auto capabilities_replaced =
                capabilities.replace_for_server("server.filesystem", {sample_capability()});
        require(capabilities_replaced.has_value(), "capability replace failed");
        require(capabilities.list_capabilities().size() == 1, "capability count mismatch");

        auto second_capability = sample_capability();
        second_capability.id = "server.filesystem:prompt:summarize";
        second_capability.kind = mcp::app::CapabilityKind::prompt;
        second_capability.upstream_name = "summarize";
        const auto capabilities_updated =
                capabilities.replace_for_server("server.filesystem", {second_capability});
        require(capabilities_updated.has_value(), "capability replacement update failed");
        require(capabilities.list_capabilities().size() == 1, "capability replacement should remove stale entries");
        require(capabilities.list_capabilities().front().kind == mcp::app::CapabilityKind::prompt,
                "capability replacement kind mismatch");

        mcp::app::MemoryExposureProfileStore exposure_profiles;
        const auto profile_saved = exposure_profiles.save(sample_exposure_profile());
        require(profile_saved.has_value(), "exposure profile save failed");
        require(exposure_profiles.list_exposure_profiles().size() == 1, "exposure profile count mismatch");
        const auto profile_removed = exposure_profiles.remove("profile.dev");
        require(profile_removed.has_value(), "exposure profile remove failed");
        require(exposure_profiles.list_exposure_profiles().empty(), "exposure profile should be removed");
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

    void test_json_mcp_server_store_persists_to_disk() {
        const auto path = std::filesystem::temp_directory_path() / "mcp-app-servers-store-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        mcp::app::JsonMcpServerStore store(path);
        const auto saved = store.save(sample_stdio_server());
        require(saved.has_value(), "json mcp server save failed");

        mcp::app::JsonMcpServerStore reloaded(path);
        const auto listed = reloaded.list_servers();
        require(listed.size() == 1, "json mcp server persisted count mismatch");
        require(listed.front().id == "server.filesystem", "json mcp server persisted id mismatch");
        require(listed.front().stdio.command == "npx", "json mcp server persisted command mismatch");

        auto updated = sample_stdio_server();
        updated.display_name = "Filesystem Reloaded";
        const auto update_saved = reloaded.save(updated);
        require(update_saved.has_value(), "json mcp server update failed");
        require(store.list_servers().size() == 1, "json mcp server update should replace existing server");
        require(store.list_servers().front().display_name == "Filesystem Reloaded",
                "json mcp server update persisted mismatch");
        const auto removed = store.remove("server.filesystem");
        require(removed.has_value(), "json mcp server remove failed");
        require(store.list_servers().empty(), "json mcp server remove persisted mismatch");

        std::filesystem::remove(path, ec);
    }

    void test_json_capability_catalog_persists_replacements_to_disk() {
        const auto path = std::filesystem::temp_directory_path() / "mcp-app-capability-store-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        mcp::app::JsonCapabilityCatalog catalog(path);
        const auto saved = catalog.replace_for_server("server.filesystem", {sample_capability()});
        require(saved.has_value(), "json capability replace failed");

        mcp::app::JsonCapabilityCatalog reloaded(path);
        auto listed = reloaded.list_capabilities();
        require(listed.size() == 1, "json capability persisted count mismatch");
        require(listed.front().id == "server.filesystem:tool:read_file", "json capability persisted id mismatch");

        auto prompt_capability = sample_capability();
        prompt_capability.id = "server.filesystem:prompt:summarize";
        prompt_capability.kind = mcp::app::CapabilityKind::prompt;
        prompt_capability.upstream_name = "summarize";
        const auto replaced = reloaded.replace_for_server("server.filesystem", {prompt_capability});
        require(replaced.has_value(), "json capability update failed");

        listed = catalog.list_capabilities();
        require(listed.size() == 1, "json capability replacement should remove stale entries");
        require(listed.front().kind == mcp::app::CapabilityKind::prompt, "json capability replacement kind mismatch");

        std::filesystem::remove(path, ec);
    }

    void test_json_exposure_profile_store_persists_to_disk() {
        const auto path = std::filesystem::temp_directory_path() / "mcp-app-exposure-profile-store-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        mcp::app::JsonExposureProfileStore store(path);
        const auto saved = store.save(sample_exposure_profile());
        require(saved.has_value(), "json exposure profile save failed");

        mcp::app::JsonExposureProfileStore reloaded(path);
        const auto listed = reloaded.list_exposure_profiles();
        require(listed.size() == 1, "json exposure profile persisted count mismatch");
        require(listed.front().id == "profile.dev", "json exposure profile persisted id mismatch");
        require(listed.front().bindings.size() == 1, "json exposure profile persisted bindings mismatch");

        auto updated = sample_exposure_profile();
        updated.name = "Dev Gateway Updated";
        const auto update_saved = reloaded.save(updated);
        require(update_saved.has_value(), "json exposure profile update failed");
        require(store.list_exposure_profiles().size() == 1,
                "json exposure profile update should replace existing profile");
        require(store.list_exposure_profiles().front().name == "Dev Gateway Updated",
                "json exposure profile update persisted mismatch");
        const auto removed = store.remove("profile.dev");
        require(removed.has_value(), "json exposure profile remove failed");
        require(store.list_exposure_profiles().empty(), "json exposure profile remove persisted mismatch");

        std::filesystem::remove(path, ec);
    }

    void test_gateway_client_config_builds_http_mcp_servers_config() {
        mcp::app::MemoryExposureProfileStore profiles({sample_exposure_profile()});
        mcp::app::GatewayClientConfigService configs(profiles);

        const auto config = configs.make_http_client_config("profile.dev", "dev-gateway");

        require(config.has_value(), "gateway client config should build");
        require(config->at("mcpServers").contains("dev-gateway"), "gateway client config server missing");
        require(config->at("mcpServers").at("dev-gateway").at("type") == "http",
                "gateway client config type mismatch");
        require(config->at("mcpServers").at("dev-gateway").at("url") == "http://127.0.0.1:8765/cxxmcp/dev",
                "gateway client config url mismatch");
    }

    void test_gateway_client_config_builds_all_http_mcp_servers_config() {
        auto ops_profile = sample_exposure_profile();
        ops_profile.id = "profile.ops";
        ops_profile.name = "Ops Gateway";
        ops_profile.endpoint.listen_port = 8766;
        ops_profile.endpoint.path = "cxxmcp/ops";
        mcp::app::MemoryExposureProfileStore profiles({sample_exposure_profile(), ops_profile});
        mcp::app::GatewayClientConfigService configs(profiles);

        const auto config = configs.make_all_http_client_configs("dev");

        require(config.has_value(), "all gateway client config should build");
        require(config->at("mcpServers").contains("dev.profile.dev"),
                "all gateway client config first server missing");
        require(config->at("mcpServers").contains("dev.profile.ops"),
                "all gateway client config second server missing");
        require(config->at("mcpServers").at("dev.profile.dev").at("url") ==
                        "http://127.0.0.1:8765/cxxmcp/dev",
                "all gateway client config first url mismatch");
        require(config->at("mcpServers").at("dev.profile.ops").at("url") ==
                        "http://127.0.0.1:8766/cxxmcp/ops",
                "all gateway client config second url mismatch");
    }

    void test_gateway_client_config_builds_ready_http_mcp_servers_config() {
        auto unready_profile = sample_exposure_profile();
        unready_profile.id = "profile.unready";
        unready_profile.name = "Unready Gateway";
        unready_profile.endpoint.listen_port = 8767;
        unready_profile.bindings.front().server_id = "missing";

        auto no_endpoint_profile = sample_exposure_profile();
        no_endpoint_profile.id = "profile.no-endpoint";
        no_endpoint_profile.name = "No Endpoint Gateway";
        no_endpoint_profile.endpoint.listen_host = {};
        no_endpoint_profile.endpoint.listen_port = 0;

        mcp::app::MemoryExposureProfileStore profiles(
                {sample_exposure_profile(), unready_profile, no_endpoint_profile});
        mcp::app::MemoryCapabilityCatalog capabilities({sample_capability()});
        mcp::app::MemoryMcpServerStore servers({sample_stdio_server()});
        mcp::app::GatewayReadinessService readiness(profiles, capabilities, servers);
        mcp::app::GatewayClientConfigService configs(profiles);

        const auto config = configs.make_ready_http_client_configs(readiness, "local");

        require(config.has_value(), "ready gateway client config should build");
        require(config->at("mcpServers").size() == 1, "ready gateway client config should skip unready profiles");
        require(config->at("mcpServers").contains("local.profile.dev"),
                "ready gateway client config ready server missing");
        require(!config->at("mcpServers").contains("local.profile.unready"),
                "ready gateway client config should skip unready binding profile");
        require(!config->at("mcpServers").contains("local.profile.no-endpoint"),
                "ready gateway client config should skip endpointless profile");
    }

    void test_gateway_status_service_reports_http_ready_profiles() {
        auto no_endpoint_profile = sample_exposure_profile();
        no_endpoint_profile.id = "profile.no-endpoint";
        no_endpoint_profile.name = "No Endpoint Gateway";
        no_endpoint_profile.endpoint.listen_host = {};
        no_endpoint_profile.endpoint.listen_port = 0;

        auto missing_server_profile = sample_exposure_profile();
        missing_server_profile.id = "profile.missing-server";
        missing_server_profile.name = "Missing Server Gateway";
        missing_server_profile.endpoint.listen_port = 8768;
        missing_server_profile.bindings.front().server_id = "missing";

        mcp::app::MemoryExposureProfileStore profiles(
                {sample_exposure_profile(), no_endpoint_profile, missing_server_profile});
        mcp::app::MemoryCapabilityCatalog capabilities({sample_capability()});
        mcp::app::MemoryMcpServerStore servers({sample_stdio_server()});
        mcp::app::GatewayStatusService status_service(profiles, capabilities, servers);

        const auto status = status_service.check_http_profiles();

        require(!status.ready, "gateway status service should report mixed profile set as not ready");
        require(status.ready_profile_count == 1, "gateway status service ready count mismatch");
        require(status.profiles.size() == 3, "gateway status service profile count mismatch");
        require(status.profiles[0].http_ready, "gateway status service ready profile mismatch");
        require(status.profiles[0].endpoint_configured,
                "gateway status service ready endpoint configured mismatch");
        require(!status.profiles[1].http_ready, "gateway status service endpointless profile should not be ready");
        require(!status.profiles[1].endpoint_configured,
                "gateway status service endpointless configured mismatch");
        require(status.profiles[1].endpoint_issues.front().code == "endpoint_not_configured",
                "gateway status service endpoint issue mismatch");
        require(!status.profiles[2].http_ready, "gateway status service missing server profile should not be ready");
        require(status.profiles[2].readiness.issues.front().code == "capability_not_found",
                "gateway status service readiness issue mismatch");
    }

    void test_gateway_client_config_builds_stdio_mcp_servers_config() {
        mcp::app::MemoryExposureProfileStore profiles({sample_exposure_profile()});
        mcp::app::GatewayClientConfigService configs(profiles);

        const auto config = configs.make_stdio_client_config("profile.dev",
                                                             "C:/bin/mcp.exe",
                                                             {"--state-dir", "C:/state", "gateway", "serve-stdio", "profile.dev"},
                                                             "dev-gateway");

        require(config.has_value(), "gateway stdio client config should build");
        require(config->at("mcpServers").at("dev-gateway").at("command") == "C:/bin/mcp.exe",
                "gateway stdio client config command mismatch");
        require(config->at("mcpServers").at("dev-gateway").at("args").size() == 5,
                "gateway stdio client config args count mismatch");
        require(config->at("mcpServers").at("dev-gateway").at("args").at(4) == "profile.dev",
                "gateway stdio client config profile arg mismatch");
    }

    void test_gateway_onboarding_initializes_all_discovered_servers() {
        auto skipped_server = sample_stdio_server();
        skipped_server.id = "server.empty";
        skipped_server.name = "empty";
        skipped_server.display_name = "Empty";
        auto untrusted_server = sample_stdio_server();
        untrusted_server.id = "server.untrusted";
        untrusted_server.name = "untrusted";
        untrusted_server.display_name = "Untrusted";
        untrusted_server.trust = mcp::app::McpServerTrustState::untrusted;
        auto untrusted_capability = sample_capability();
        untrusted_capability.id = "server.untrusted:tool:read_secret";
        untrusted_capability.server_id = "server.untrusted";
        untrusted_capability.upstream_name = "read_secret";
        untrusted_capability.exposed_name = "untrusted.read_secret";

        mcp::app::MemoryMcpServerStore servers({sample_stdio_server(), skipped_server, untrusted_server});
        mcp::app::MemoryCapabilityCatalog capabilities({sample_capability(), untrusted_capability});
        mcp::app::MemoryExposureProfileStore profiles;
        mcp::app::ExposureManagementService exposure_management(profiles, capabilities);
        mcp::app::GatewayOnboardingService onboarding(servers, capabilities, profiles, exposure_management);

        const auto initialized = onboarding.initialize_all_http_profiles("127.0.0.1",
                                                                         39931,
                                                                         "cxxmcp/imported",
                                                                         "profile.",
                                                                         "Use imported tools only.");

        require(initialized.has_value(), "gateway onboarding init-all should succeed");
        require(initialized->initialized_count == 1, "gateway onboarding initialized count mismatch");
        require(initialized->skipped_count == 2, "gateway onboarding skipped count mismatch");
        require(initialized->reports.size() == 3, "gateway onboarding report count mismatch");
        require(initialized->reports[0].profile_id == "profile.server.filesystem",
                "gateway onboarding profile id mismatch");
        require(initialized->reports[0].url == "http://127.0.0.1:39931/cxxmcp/imported/server.filesystem",
                "gateway onboarding endpoint url mismatch");
        require(initialized->reports[0].bound_capability_count == 1,
                "gateway onboarding binding count mismatch");
        require(initialized->reports[1].skipped_reason == "no capabilities discovered for server",
                "gateway onboarding skip reason mismatch");
        require(initialized->reports[2].skipped_reason == "cxxmcp server is untrusted",
                "gateway onboarding untrusted skip reason mismatch");

        const auto profile = exposure_management.get_profile("profile.server.filesystem");
        require(profile.has_value(), "gateway onboarding profile should be saved");
        require(profile->endpoint.listen_port == 39931, "gateway onboarding endpoint port mismatch");
        require(profile->endpoint.path == "/cxxmcp/imported/server.filesystem",
                "gateway onboarding endpoint path mismatch");
        require(profile->instructions == "Use imported tools only.",
                "gateway onboarding instructions mismatch");
        require(profile->bindings.size() == 1, "gateway onboarding saved binding count mismatch");
    }

    void test_gateway_onboarding_skips_endpoint_port_conflicts() {
        mcp::app::MemoryMcpServerStore servers({sample_stdio_server()});
        mcp::app::MemoryCapabilityCatalog capabilities({sample_capability()});
        mcp::app::MemoryExposureProfileStore profiles;
        mcp::app::ExposureManagementService exposure_management(profiles, capabilities);
        require(exposure_management.create_profile("profile.reserved", "Reserved").has_value(),
                "reserved profile create failed");
        require(exposure_management.configure_endpoint("profile.reserved", "127.0.0.1", 39931, "/cxxmcp/reserved")
                        .has_value(),
                "reserved endpoint configure failed");
        mcp::app::GatewayOnboardingService onboarding(servers, capabilities, profiles, exposure_management);

        const auto initialized = onboarding.initialize_all_http_profiles("127.0.0.1",
                                                                         39931,
                                                                         "cxxmcp/imported",
                                                                         "profile.",
                                                                         {});

        require(initialized.has_value(), "gateway onboarding conflict run should return report");
        require(initialized->initialized_count == 0, "gateway onboarding conflict initialized count mismatch");
        require(initialized->skipped_count == 1, "gateway onboarding conflict skipped count mismatch");
        require(initialized->reports.size() == 1, "gateway onboarding conflict report count mismatch");
        require(initialized->reports[0].skipped_reason ==
                        "gateway endpoint port already used by profile.reserved",
                "gateway onboarding conflict reason mismatch");
        require(!exposure_management.get_profile("profile.server.filesystem").has_value(),
                "gateway onboarding conflict should not create profile");
    }

    void test_gateway_config_import_service_imports_and_initializes() {
        mcp::app::MemoryMcpServerStore servers;
        mcp::app::MemoryCapabilityCatalog capabilities({sample_capability()});
        mcp::app::MemoryExposureProfileStore profiles;
        mcp::app::ExposureManagementService exposure_management(profiles, capabilities);
        mcp::app::ServerManagementService server_management(servers, capabilities, profiles, {});
        mcp::app::GatewayConfigImportService importer(server_management,
                                                      servers,
                                                      capabilities,
                                                      profiles,
                                                      exposure_management);
        const Json config = Json{
                {"mcpServers",
                 Json{
                         {"server.filesystem",
                          Json{
                                  {"command", "node"},
                                  {"args", Json::array({"server.js"})},
                          }},
                 }},
        };

        const auto imported = importer.import_and_initialize(config,
                                                             false,
                                                             true,
                                                             "127.0.0.1",
                                                             39981,
                                                             "/cxxmcp/imported",
                                                             "gateway.",
                                                             "Use imported config tools.");

        require(imported.has_value(), "gateway config import service should succeed");
        require(imported->imported_servers.size() == 1, "gateway config import imported count mismatch");
        require(imported->trusted, "gateway config import trust flag mismatch");
        require(imported->trusted_server_count == 1, "gateway config import trusted count mismatch");
        require(!imported->discovered, "gateway config import discover flag mismatch");
        require(imported->initialization.initialized_count == 1,
                "gateway config import initialized count mismatch");
        require(imported->initialization.reports.front().profile_id == "gateway.server.filesystem",
                "gateway config import profile id mismatch");
        const auto profile = exposure_management.get_profile("gateway.server.filesystem");
        require(profile.has_value(), "gateway config import profile should exist");
        require(profile->endpoint.path == "/cxxmcp/imported/server.filesystem",
                "gateway config import endpoint path mismatch");
        require(profile->instructions == "Use imported config tools.",
                "gateway config import instructions mismatch");
    }

    void test_server_management_checks_servers_without_persisting_capabilities() {
        mcp::app::MemoryMcpServerStore servers({sample_stdio_server()});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore profiles;
        mcp::app::ServerManagementService management(servers, capabilities, profiles, check_discovery_factory());

        const auto health = management.check_server("server.filesystem");
        require(health.has_value(), "server management check should succeed");
        require(health->ready, "server management check readiness mismatch");
        require(health->capability_count == 1, "server management check capability count mismatch");
        require(capabilities.list_capabilities().empty(), "server management check should not persist capabilities");

        const auto discovered = management.discover_server("server.filesystem");
        require(discovered.has_value(), "server management discover should succeed");
        require(discovered->capability_count == 1, "server management discover capability count mismatch");
        require(capabilities.list_capabilities().size() == 1,
                "server management discover should persist capabilities");
    }

    void test_exposure_management_creates_profiles_and_binds_capabilities() {
        mcp::app::MemoryExposureProfileStore profiles;
        mcp::app::MemoryCapabilityCatalog capabilities({sample_capability()});
        mcp::app::ExposureManagementService management(profiles, capabilities);

        const auto created = management.create_profile("profile.dev", "Dev");
        require(created.has_value(), "exposure profile create failed");
        require(profiles.list_exposure_profiles().size() == 1, "created exposure profile count mismatch");

        const auto endpoint = management.configure_endpoint("profile.dev", "127.0.0.1", 39919, "cxxmcp/dev");
        require(endpoint.has_value(), "exposure endpoint configure failed");

        const auto instructions = management.set_instructions("profile.dev", "Use only for reviewed workspace tasks.");
        require(instructions.has_value(), "exposure instructions update failed");
        require(profiles.list_exposure_profiles().front().instructions == "Use only for reviewed workspace tasks.",
                "exposure instructions update mismatch");

        const auto bound = management.bind_capability("profile.dev", "server.filesystem:tool:read_file",
                                                      "dev.read_file");
        require(bound.has_value(), "exposure capability bind failed");
        const auto listed = profiles.list_exposure_profiles();
        require(listed.front().bindings.size() == 1, "bound exposure binding count mismatch");
        require(listed.front().endpoint.listen_port == 39919, "configured exposure endpoint port mismatch");
        require(listed.front().endpoint.path == "/cxxmcp/dev", "configured exposure endpoint path mismatch");
        require(listed.front().bindings.front().exposed_name == "dev.read_file",
                "bound exposure name mismatch");
        require(listed.front().bindings.front().policy.approval == mcp::app::ApprovalState::approved,
                "bound exposure policy approval mismatch");

        const auto disabled_binding =
                management.set_binding_enabled("profile.dev", "server.filesystem:tool:read_file", false);
        require(disabled_binding.has_value(), "disable exposure binding failed");
        require(!profiles.list_exposure_profiles().front().bindings.front().enabled,
                "exposure binding should be disabled");

        const auto enabled_binding =
                management.set_binding_enabled("profile.dev", "server.filesystem:tool:read_file", true);
        require(enabled_binding.has_value(), "enable exposure binding failed");
        require(profiles.list_exposure_profiles().front().bindings.front().enabled,
                "exposure binding should be enabled");

        const auto fetched = management.get_profile("profile.dev");
        require(fetched.has_value(), "get exposure profile failed");
        require(fetched->name == "Dev", "get exposure profile name mismatch");

        const auto unbound = management.unbind_capability("profile.dev", "server.filesystem:tool:read_file");
        require(unbound.has_value(), "exposure capability unbind failed");
        require(profiles.list_exposure_profiles().front().bindings.empty(),
                "unbound exposure binding should be removed");

        const auto server_bound = management.bind_server_capabilities("profile.dev", "server.filesystem");
        require(server_bound.has_value(), "exposure server capability bind failed");
        require(*server_bound == 1, "exposure server capability bind count mismatch");
        require(profiles.list_exposure_profiles().front().bindings.size() == 1,
                "server capability bind should add discovered bindings");
        require(profiles.list_exposure_profiles().front().bindings.front().exposed_name == "filesystem.read_file",
                "server capability bind exposed name mismatch");

        const auto rebound = management.bind_capability("profile.dev", "server.filesystem:tool:read_file",
                                                        "dev.read_file");
        require(rebound.has_value(), "exposure capability rebind failed");

        const auto bindings_removed = management.remove_bindings_for_server("server.filesystem");
        require(bindings_removed.has_value(), "remove exposure bindings failed");
        require(profiles.list_exposure_profiles().front().bindings.empty(),
                "server bindings should be removed from exposure profile");

        const auto profile_removed = management.remove_profile("profile.dev");
        require(profile_removed.has_value(), "remove exposure profile failed");
        require(profiles.list_exposure_profiles().empty(), "removed exposure profile should not remain");
    }

}// namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
            {"bundle json round trip", test_bundle_json_round_trip},
            {"mcp server json round trip", test_mcp_server_json_round_trip},
            {"import Trae/Claude mcpServers config", test_imports_trae_claude_mcp_servers_config},
            {"import VS Code servers http config", test_imports_vscode_servers_http_config},
            {"exposure profile json round trip", test_exposure_profile_json_round_trip},
            {"memory services", test_memory_services},
            {"json import export service", test_json_import_export_service},
            {"json mcp server store persists to disk", test_json_mcp_server_store_persists_to_disk},
            {"json capability catalog persists replacements to disk",
             test_json_capability_catalog_persists_replacements_to_disk},
            {"json exposure profile store persists to disk", test_json_exposure_profile_store_persists_to_disk},
            {"gateway client config builds http mcpServers config",
             test_gateway_client_config_builds_http_mcp_servers_config},
            {"gateway client config builds all http mcpServers config",
             test_gateway_client_config_builds_all_http_mcp_servers_config},
            {"gateway client config builds ready http mcpServers config",
             test_gateway_client_config_builds_ready_http_mcp_servers_config},
            {"gateway status service reports http ready profiles",
             test_gateway_status_service_reports_http_ready_profiles},
            {"gateway client config builds stdio mcpServers config",
             test_gateway_client_config_builds_stdio_mcp_servers_config},
            {"gateway onboarding initializes all discovered servers",
             test_gateway_onboarding_initializes_all_discovered_servers},
            {"gateway onboarding skips endpoint port conflicts",
             test_gateway_onboarding_skips_endpoint_port_conflicts},
            {"gateway config import service imports and initializes",
             test_gateway_config_import_service_imports_and_initializes},
            {"server management checks servers without persisting capabilities",
             test_server_management_checks_servers_without_persisting_capabilities},
            {"exposure management creates and binds", test_exposure_management_creates_profiles_and_binds_capabilities},
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
