#include "cxxmcp/app/server_management.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace mcp::app {
    namespace {

        core::Error make_management_error(std::string message, std::string detail = {}) {
            return core::Error{1, std::move(message), std::move(detail)};
        }

        std::string default_exposed_name(const McpServerDefinition &server, std::string_view upstream_name) {
            if (!server.name.empty()) {
                return server.name + "." + std::string(upstream_name);
            }
            return server.id + "." + std::string(upstream_name);
        }

        DiscoveredCapability capability_from_tool(const McpServerDefinition &server,
                                                  const protocol::ToolDefinition &tool) {
            return DiscoveredCapability{
                    .id = server.id + ":tool:" + tool.name,
                    .kind = CapabilityKind::tool,
                    .server_id = server.id,
                    .upstream_name = tool.name,
                    .exposed_name = default_exposed_name(server, tool.name),
                    .title = tool.name,
                    .description = tool.description,
                    .uri = {},
                    .input_schema = tool.input_schema,
                    .output_schema = protocol::Json::object(),
                    .template_text = {},
                    .capability_hash = {},
            };
        }

        DiscoveredCapability capability_from_prompt(const McpServerDefinition &server,
                                                    const protocol::Prompt &prompt) {
            protocol::Json input_schema = protocol::Json::object();
            input_schema["type"] = "object";
            input_schema["properties"] = protocol::Json::object();
            input_schema["required"] = protocol::Json::array();
            for (const auto &argument: prompt.arguments) {
                input_schema["properties"][argument.name] = protocol::Json{
                        {"type", "string"},
                        {"description", argument.description},
                };
                if (argument.required) {
                    input_schema["required"].push_back(argument.name);
                }
            }

            return DiscoveredCapability{
                    .id = server.id + ":prompt:" + prompt.name,
                    .kind = CapabilityKind::prompt,
                    .server_id = server.id,
                    .upstream_name = prompt.name,
                    .exposed_name = default_exposed_name(server, prompt.name),
                    .title = prompt.name,
                    .description = prompt.description,
                    .uri = {},
                    .input_schema = std::move(input_schema),
                    .output_schema = protocol::Json::object(),
                    .template_text = {},
                    .capability_hash = {},
            };
        }

        DiscoveredCapability capability_from_resource(const McpServerDefinition &server,
                                                      const protocol::Resource &resource) {
            return DiscoveredCapability{
                    .id = server.id + ":resource:" + resource.uri,
                    .kind = CapabilityKind::resource,
                    .server_id = server.id,
                    .upstream_name = resource.name,
                    .exposed_name = default_exposed_name(server, resource.name),
                    .title = resource.name,
                    .description = resource.description,
                    .uri = resource.uri,
                    .input_schema = protocol::Json::object(),
                    .output_schema = protocol::Json{{"mimeType", resource.mime_type}},
                    .template_text = {},
                    .capability_hash = {},
            };
        }

        core::Result<McpServerDefinition> find_server(const McpServerStore &servers, std::string_view server_id) {
            const auto configured_servers = servers.list_servers();
            const auto it = std::find_if(configured_servers.begin(), configured_servers.end(), [&](const auto &server) {
                return server.id == server_id;
            });
            if (it == configured_servers.end()) {
                return std::unexpected(make_management_error("mcp server not found", std::string(server_id)));
            }
            return *it;
        }

        core::Result<core::Unit> require_server_can_execute(const McpServerDefinition &server) {
            if (!server.enabled) {
                return std::unexpected(make_management_error("cxxmcp server is disabled", server.id));
            }
            if (server.trust == McpServerTrustState::untrusted) {
                return std::unexpected(make_management_error("cxxmcp server is untrusted", server.id));
            }
            if (server.trust == McpServerTrustState::blocked) {
                return std::unexpected(make_management_error("cxxmcp server is blocked", server.id));
            }
            return core::Unit{};
        }

        core::Result<core::Unit> require_stdio_server(const McpServerDefinition &server) {
            if (server.transport != McpServerTransportKind::stdio) {
                return std::unexpected(make_management_error("mcp server is not stdio", server.id));
            }
            return core::Unit{};
        }

        core::Result<core::Unit> require_http_server(const McpServerDefinition &server) {
            if (server.transport != McpServerTransportKind::streamable_http &&
                server.transport != McpServerTransportKind::legacy_sse) {
                return std::unexpected(make_management_error("mcp server is not http", server.id));
            }
            return core::Unit{};
        }

        core::Result<core::Unit> remove_profile_bindings_for_server(ExposureProfileStore &profiles,
                                                                    std::string_view server_id) {
            auto exposure_profiles = profiles.list_exposure_profiles();
            for (auto &profile: exposure_profiles) {
                const auto original_size = profile.bindings.size();
                profile.bindings.erase(std::remove_if(profile.bindings.begin(),
                                                      profile.bindings.end(),
                                                      [&](const auto &binding) {
                                                          return binding.server_id == server_id;
                                                      }),
                                       profile.bindings.end());
                if (profile.bindings.size() != original_size) {
                    const auto saved = profiles.save(std::move(profile));
                    if (!saved) {
                        return std::unexpected(saved.error());
                    }
                }
            }
            return core::Unit{};
        }

    }// namespace

    core::Result<std::vector<DiscoveredCapability>> probe_server_capabilities(const McpServerDefinition &server,
                                                                              const McpDiscoverySessionFactory &session_factory) {
        const auto runnable = require_server_can_execute(server);
        if (!runnable) {
            return std::unexpected(runnable.error());
        }
        if (!session_factory) {
            return std::unexpected(make_management_error("mcp discovery session factory is not configured"));
        }

        auto session = session_factory(server);
        if (!session) {
            return std::unexpected(session.error());
        }

        const auto initialized = (*session)->initialize();
        if (!initialized) {
            return std::unexpected(initialized.error());
        }

        std::vector<DiscoveredCapability> discovered;
        const auto tools = (*session)->discover_tools();
        if (!tools) {
            return std::unexpected(tools.error());
        }
        for (const auto &tool: *tools) {
            discovered.push_back(capability_from_tool(server, tool));
        }

        const auto prompts = (*session)->discover_prompts();
        if (!prompts) {
            return std::unexpected(prompts.error());
        }
        for (const auto &prompt: *prompts) {
            discovered.push_back(capability_from_prompt(server, prompt));
        }

        const auto resources = (*session)->discover_resources();
        if (!resources) {
            return std::unexpected(resources.error());
        }
        for (const auto &resource: *resources) {
            discovered.push_back(capability_from_resource(server, resource));
        }

        return discovered;
    }
    ServerManagementService::ServerManagementService(McpServerStore &servers,
                                                     CapabilityCatalog &capabilities,
                                                     ExposureProfileStore &exposure_profiles,
                                                     McpDiscoverySessionFactory session_factory)
        : servers_(servers),
          capabilities_(capabilities),
          exposure_profiles_(exposure_profiles),
          session_factory_(std::move(session_factory)) {}

    core::Result<McpServerDefinition> ServerManagementService::get_server(std::string_view server_id) const {
        return find_server(servers_, server_id);
    }

    core::Result<core::Unit> ServerManagementService::save_server(McpServerDefinition server) {
        return servers_.save(std::move(server));
    }

    core::Result<core::Unit> ServerManagementService::remove_server(std::string_view server_id) {
        const auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }

        const auto removed = servers_.remove(server_id);
        if (!removed) {
            return std::unexpected(removed.error());
        }
        const auto cleared_capabilities = capabilities_.replace_for_server(std::string(server_id), {});
        if (!cleared_capabilities) {
            return std::unexpected(cleared_capabilities.error());
        }
        const auto cleared_bindings = remove_profile_bindings_for_server(exposure_profiles_, server_id);
        if (!cleared_bindings) {
            return std::unexpected(cleared_bindings.error());
        }
        return core::Unit{};
    }

    core::Result<McpServerDefinition> ServerManagementService::set_server_enabled(std::string_view server_id,
                                                                                  bool enabled) {
        auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }

        server->enabled = enabled;
        const auto saved = servers_.save(*server);
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return *server;
    }

    core::Result<McpServerDefinition> ServerManagementService::set_server_trust(std::string_view server_id,
                                                                                McpServerTrustState trust) {
        auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }

        server->trust = trust;
        const auto saved = servers_.save(*server);
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return *server;
    }

    core::Result<McpServerDefinition> ServerManagementService::set_stdio_cwd(std::string_view server_id,
                                                                             std::string_view cwd) {
        if (cwd.empty()) {
            return std::unexpected(make_management_error("stdio cwd must not be empty", std::string(server_id)));
        }

        auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }
        const auto stdio = require_stdio_server(*server);
        if (!stdio) {
            return std::unexpected(stdio.error());
        }

        server->stdio.cwd = std::string(cwd);
        const auto saved = servers_.save(*server);
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return *server;
    }

    core::Result<McpServerDefinition> ServerManagementService::set_stdio_env(std::string_view server_id,
                                                                             std::string_view name,
                                                                             std::string_view value) {
        if (name.empty()) {
            return std::unexpected(make_management_error("stdio env name must not be empty", std::string(server_id)));
        }

        auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }
        const auto stdio = require_stdio_server(*server);
        if (!stdio) {
            return std::unexpected(stdio.error());
        }

        server->stdio.env[std::string(name)] = std::string(value);
        const auto saved = servers_.save(*server);
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return *server;
    }

    core::Result<McpServerDefinition> ServerManagementService::unset_stdio_env(std::string_view server_id,
                                                                               std::string_view name) {
        if (name.empty()) {
            return std::unexpected(make_management_error("stdio env name must not be empty", std::string(server_id)));
        }

        auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }
        const auto stdio = require_stdio_server(*server);
        if (!stdio) {
            return std::unexpected(stdio.error());
        }

        server->stdio.env.erase(std::string(name));
        const auto saved = servers_.save(*server);
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return *server;
    }

    core::Result<McpServerDefinition> ServerManagementService::set_http_header(std::string_view server_id,
                                                                               std::string_view name,
                                                                               std::string_view value) {
        if (name.empty()) {
            return std::unexpected(make_management_error("http header name must not be empty", std::string(server_id)));
        }

        auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }
        const auto http = require_http_server(*server);
        if (!http) {
            return std::unexpected(http.error());
        }

        server->http.headers[std::string(name)] = std::string(value);
        const auto saved = servers_.save(*server);
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return *server;
    }

    core::Result<McpServerDefinition> ServerManagementService::unset_http_header(std::string_view server_id,
                                                                                 std::string_view name) {
        if (name.empty()) {
            return std::unexpected(make_management_error("http header name must not be empty", std::string(server_id)));
        }

        auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }
        const auto http = require_http_server(*server);
        if (!http) {
            return std::unexpected(http.error());
        }

        server->http.headers.erase(std::string(name));
        const auto saved = servers_.save(*server);
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return *server;
    }

    core::Result<DiscoveryResult> ServerManagementService::discover_server(std::string_view server_id) {
        const auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }

        const auto discovered = probe_server_capabilities(*server, session_factory_);
        if (!discovered) {
            return std::unexpected(discovered.error());
        }

        const auto saved = capabilities_.replace_for_server(server->id, *discovered);
        if (!saved) {
            return std::unexpected(saved.error());
        }

        return DiscoveryResult{
                .server_id = server->id,
                .capability_count = discovered->size(),
        };
    }

    core::Result<ServerHealthReport> ServerManagementService::check_server(std::string_view server_id) {
        const auto server = find_server(servers_, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }

        const auto discovered = probe_server_capabilities(*server, session_factory_);
        if (!discovered) {
            return ServerHealthReport{
                    .server_id = server->id,
                    .ready = false,
                    .capability_count = 0,
                    .error_message = discovered.error().message,
                    .error_detail = discovered.error().detail,
            };
        }

        return ServerHealthReport{
                .server_id = server->id,
                .ready = true,
                .capability_count = discovered->size(),
                .error_message = {},
                .error_detail = {},
        };
    }

    std::vector<ServerDiscoveryReport> ServerManagementService::discover_all_servers() {
        std::vector<ServerDiscoveryReport> reports;
        const auto servers = servers_.list_servers();
        reports.reserve(servers.size());

        for (const auto &server: servers) {
            auto discovered = discover_server(server.id);
            if (!discovered) {
                reports.push_back(ServerDiscoveryReport{
                        .server_id = server.id,
                        .discovered = false,
                        .capability_count = 0,
                        .error_message = discovered.error().message,
                        .error_detail = discovered.error().detail,
                });
                continue;
            }

            reports.push_back(ServerDiscoveryReport{
                    .server_id = discovered->server_id,
                    .discovered = true,
                    .capability_count = discovered->capability_count,
                    .error_message = {},
                    .error_detail = {},
            });
        }

        return reports;
    }

    std::vector<ServerHealthReport> ServerManagementService::check_all_servers() {
        std::vector<ServerHealthReport> reports;
        const auto servers = servers_.list_servers();
        reports.reserve(servers.size());

        for (const auto &server: servers) {
            auto health = check_server(server.id);
            if (!health) {
                reports.push_back(ServerHealthReport{
                        .server_id = server.id,
                        .ready = false,
                        .capability_count = 0,
                        .error_message = health.error().message,
                        .error_detail = health.error().detail,
                });
                continue;
            }
            reports.push_back(*health);
        }

        return reports;
    }

}// namespace mcp::app
