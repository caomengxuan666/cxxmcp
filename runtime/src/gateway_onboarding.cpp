#include "cxxmcp/app/gateway_onboarding.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace mcp::app {
    namespace {

        core::Error make_onboarding_error(std::string message, std::string detail = {}) {
            return core::Error{1, std::move(message), std::move(detail)};
        }

        std::string profile_id_for_server(std::string_view profile_prefix, std::string_view server_id) {
            return std::string(profile_prefix) + std::string(server_id);
        }

        std::string gateway_profile_name(const McpServerDefinition &server) {
            if (!server.display_name.empty()) {
                return server.display_name + " Gateway";
            }
            if (!server.name.empty()) {
                return server.name + " Gateway";
            }
            return server.id + " Gateway";
        }

        std::string normalize_prefix(std::string_view path_prefix) {
            std::string normalized = path_prefix.empty() ? std::string("/mcp") : std::string(path_prefix);
            if (!core::starts_with(normalized, '/')) {
                normalized.insert(normalized.begin(), '/');
            }
            while (normalized.size() > 1 && core::ends_with(normalized, '/')) {
                normalized.pop_back();
            }
            return normalized;
        }

        std::string endpoint_path(std::string_view path_prefix, std::string_view server_id) {
            return normalize_prefix(path_prefix) + "/" + std::string(server_id);
        }

        std::string endpoint_url(std::string_view host, std::uint16_t port, std::string_view path) {
            return "http://" + std::string(host) + ":" + std::to_string(port) + std::string(path);
        }

        bool profile_exists(const ExposureProfileStore &profiles, std::string_view profile_id) {
            const auto profile_items = profiles.list_exposure_profiles();
            return std::any_of(profile_items.begin(), profile_items.end(), [&](const auto &profile) {
                return profile.id == profile_id;
            });
        }

        std::size_t capability_count_for_server(const CapabilityCatalog &capabilities, std::string_view server_id) {
            const auto capability_items = capabilities.list_capabilities();
            return static_cast<std::size_t>(
                    std::count_if(capability_items.begin(), capability_items.end(), [&](const auto &capability) {
                        return capability.server_id == server_id;
                    }));
        }

        std::string conflicting_profile_for_endpoint(const ExposureProfileStore &profiles,
                                                     std::string_view profile_id,
                                                     std::string_view host,
                                                     std::uint16_t port) {
            const auto profile_items = profiles.list_exposure_profiles();
            const auto conflict = std::find_if(profile_items.begin(), profile_items.end(), [&](const auto &profile) {
                return profile.id != profile_id && profile.endpoint.listen_host == host &&
                       profile.endpoint.listen_port == port;
            });
            if (conflict == profile_items.end()) {
                return {};
            }
            return conflict->id;
        }

        std::string server_skip_reason(const McpServerDefinition &server) {
            if (!server.enabled) {
                return "cxxmcp server is disabled";
            }
            if (server.trust == McpServerTrustState::untrusted) {
                return "cxxmcp server is untrusted";
            }
            if (server.trust == McpServerTrustState::blocked) {
                return "cxxmcp server is blocked";
            }
            return {};
        }

    }// namespace

    GatewayOnboardingService::GatewayOnboardingService(McpServerStore &servers,
                                                       CapabilityCatalog &capabilities,
                                                       ExposureProfileStore &profiles,
                                                       ExposureManagementService &exposure_management)
        : servers_(servers),
          capabilities_(capabilities),
          profiles_(profiles),
          exposure_management_(exposure_management) {}

    core::Result<GatewayBatchInitResult> GatewayOnboardingService::initialize_all_http_profiles(
            std::string_view host,
            std::uint16_t base_port,
            std::string_view path_prefix,
            std::string_view profile_prefix,
            std::string_view instructions) {
        if (host.empty()) {
            return std::unexpected(make_onboarding_error("gateway host must not be empty"));
        }
        if (base_port == 0) {
            return std::unexpected(make_onboarding_error("gateway base port must not be zero"));
        }
        if (profile_prefix.empty()) {
            return std::unexpected(make_onboarding_error("gateway profile prefix must not be empty"));
        }

        const auto server_items = servers_.list_servers();
        if (server_items.empty()) {
            return std::unexpected(make_onboarding_error("no mcp servers configured"));
        }

        GatewayBatchInitResult result;
        result.reports.reserve(server_items.size());
        std::size_t port_offset = 0;

        for (const auto &server: server_items) {
            GatewayProfileInitReport report;
            report.server_id = server.id;
            report.profile_id = profile_id_for_server(profile_prefix, server.id);

            report.skipped_reason = server_skip_reason(server);
            if (!report.skipped_reason.empty()) {
                ++result.skipped_count;
                result.reports.push_back(std::move(report));
                continue;
            }

            const auto capability_count = capability_count_for_server(capabilities_, server.id);
            if (capability_count == 0) {
                report.skipped_reason = "no capabilities discovered for server";
                ++result.skipped_count;
                result.reports.push_back(std::move(report));
                continue;
            }

            const auto assigned_port = static_cast<unsigned int>(base_port) + port_offset;
            if (assigned_port > std::numeric_limits<std::uint16_t>::max()) {
                return std::unexpected(make_onboarding_error("gateway port range overflow",
                                                             std::to_string(assigned_port)));
            }
            report.port = static_cast<std::uint16_t>(assigned_port);
            report.path = endpoint_path(path_prefix, server.id);
            report.url = endpoint_url(host, report.port, report.path);
            const auto conflicting_profile = conflicting_profile_for_endpoint(profiles_,
                                                                              report.profile_id,
                                                                              host,
                                                                              report.port);
            if (!conflicting_profile.empty()) {
                report.skipped_reason = "gateway endpoint port already used by " + conflicting_profile;
                ++result.skipped_count;
                result.reports.push_back(std::move(report));
                continue;
            }

            const bool exists = profile_exists(profiles_, report.profile_id);
            if (!exists) {
                const auto created = exposure_management_.create_profile(report.profile_id,
                                                                         gateway_profile_name(server));
                if (!created) {
                    return std::unexpected(created.error());
                }
                report.created = true;
            }

            const auto configured = exposure_management_.configure_endpoint(report.profile_id,
                                                                            host,
                                                                            report.port,
                                                                            report.path);
            if (!configured) {
                return std::unexpected(configured.error());
            }
            if (!instructions.empty()) {
                const auto instructed = exposure_management_.set_instructions(report.profile_id, instructions);
                if (!instructed) {
                    return std::unexpected(instructed.error());
                }
            }

            const auto bound = exposure_management_.bind_server_capabilities(report.profile_id, server.id);
            if (!bound) {
                return std::unexpected(bound.error());
            }
            report.bound_capability_count = *bound;
            report.initialized = true;
            ++result.initialized_count;
            ++port_offset;
            result.reports.push_back(std::move(report));
        }

        return result;
    }

    GatewayConfigImportService::GatewayConfigImportService(ServerManagementService &server_management,
                                                           McpServerStore &servers,
                                                           CapabilityCatalog &capabilities,
                                                           ExposureProfileStore &profiles,
                                                           ExposureManagementService &exposure_management)
        : server_management_(server_management),
          servers_(servers),
          capabilities_(capabilities),
          profiles_(profiles),
          exposure_management_(exposure_management) {}

    core::Result<GatewayConfigImportResult> GatewayConfigImportService::import_and_initialize(
            const Json &client_config,
            bool discover,
            bool trust,
            std::string_view host,
            std::uint16_t base_port,
            std::string_view path_prefix,
            std::string_view profile_prefix,
            std::string_view instructions) {
        const auto parsed_servers = mcp_server_definitions_from_client_config_json(client_config);
        if (!parsed_servers) {
            return std::unexpected(parsed_servers.error());
        }

        GatewayConfigImportResult result;
        result.trusted = trust;
        result.discovered = discover;
        result.imported_servers.reserve(parsed_servers->size());

        for (auto server: *parsed_servers) {
            if (trust) {
                server.trust = McpServerTrustState::trusted;
            }
            const auto saved = server_management_.save_server(server);
            if (!saved) {
                return std::unexpected(saved.error());
            }
            result.imported_servers.push_back(std::move(server));
        }

        if (trust) {
            result.trusted_server_count = result.imported_servers.size();
        }

        if (discover) {
            result.discovery_reports = server_management_.discover_all_servers();
            for (const auto &report: result.discovery_reports) {
                if (report.discovered) {
                    result.discovered_capability_count += report.capability_count;
                }
            }
        }

        GatewayOnboardingService onboarding(servers_, capabilities_, profiles_, exposure_management_);
        auto initialized = onboarding.initialize_all_http_profiles(host,
                                                                   base_port,
                                                                   path_prefix,
                                                                   profile_prefix,
                                                                   instructions);
        if (!initialized) {
            return std::unexpected(initialized.error());
        }
        result.initialization = std::move(*initialized);
        return result;
    }

}// namespace mcp::app
