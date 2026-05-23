#include "mcp/app/exposure_management.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace mcp::app {
namespace {

core::Error make_exposure_error(std::string message, std::string detail = {}) {
    return core::Error{1, std::move(message), std::move(detail)};
}

core::Result<ExposureProfile> find_profile(const ExposureProfileStore& profiles, std::string_view profile_id) {
    const auto configured_profiles = profiles.list_exposure_profiles();
    const auto profile_it = std::find_if(configured_profiles.begin(), configured_profiles.end(), [&](const auto& profile) {
        return profile.id == profile_id;
    });
    if (profile_it == configured_profiles.end()) {
        return std::unexpected(make_exposure_error("exposure profile not found", std::string(profile_id)));
    }
    return *profile_it;
}

CapabilityBinding binding_from_capability(const ExposureProfile& profile,
                                          const DiscoveredCapability& capability,
                                          std::string_view exposed_name) {
    Policy policy;
    policy.enabled = true;
    policy.approval = ApprovalState::approved;

    return CapabilityBinding{
        .id = profile.id + ":" + capability.id,
        .server_id = capability.server_id,
        .kind = capability.kind,
        .upstream_name = capability.upstream_name,
        .exposed_name = exposed_name.empty() ? capability.exposed_name : std::string(exposed_name),
        .namespace_strategy = NamespaceStrategy::server_prefix,
        .enabled = true,
        .policy = std::move(policy),
    };
}

void upsert_binding(ExposureProfile& profile, CapabilityBinding binding) {
    const auto binding_it = std::find_if(profile.bindings.begin(), profile.bindings.end(), [&](const auto& existing) {
        return existing.id == binding.id;
    });
    if (binding_it == profile.bindings.end()) {
        profile.bindings.push_back(std::move(binding));
    } else {
        *binding_it = std::move(binding);
    }
}

bool capability_matches_binding(const DiscoveredCapability& capability, const CapabilityBinding& binding) {
    return capability.kind == binding.kind && capability.server_id == binding.server_id &&
           capability.upstream_name == binding.upstream_name;
}

} // namespace

ExposureManagementService::ExposureManagementService(ExposureProfileStore& profiles,
                                                     CapabilityCatalog& capabilities)
    : profiles_(profiles),
      capabilities_(capabilities) {}

core::Result<ExposureProfile> ExposureManagementService::get_profile(std::string_view profile_id) const {
    return find_profile(profiles_, profile_id);
}

core::Result<core::Unit> ExposureManagementService::create_profile(std::string_view profile_id,
                                                                   std::string_view name) {
    const auto profiles = profiles_.list_exposure_profiles();
    const auto duplicate = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return profile.id == profile_id;
    });
    if (duplicate != profiles.end()) {
        return std::unexpected(make_exposure_error("exposure profile already exists", std::string(profile_id)));
    }

    return profiles_.save(ExposureProfile{
        .id = std::string(profile_id),
        .name = std::string(name),
        .instructions = {},
        .endpoint = HostedEndpoint{
            .name = std::string(profile_id),
            .listen_host = "127.0.0.1",
            .listen_port = 0,
            .path = "/mcp",
            .transport = McpServerTransportKind::streamable_http,
        },
        .bindings = {},
        .environment_overrides = {},
    });
}

core::Result<core::Unit> ExposureManagementService::remove_profile(std::string_view profile_id) {
    const auto profile = find_profile(profiles_, profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }
    return profiles_.remove(profile_id);
}

core::Result<core::Unit> ExposureManagementService::configure_endpoint(std::string_view profile_id,
                                                                       std::string_view host,
                                                                       std::uint16_t port,
                                                                       std::string_view path) {
    if (host.empty()) {
        return std::unexpected(make_exposure_error("exposure endpoint host must not be empty",
                                                  std::string(profile_id)));
    }
    if (port == 0) {
        return std::unexpected(make_exposure_error("exposure endpoint port must not be zero",
                                                  std::string(profile_id)));
    }

    std::string normalized_path = path.empty() ? std::string("/mcp") : std::string(path);
    if (!normalized_path.starts_with('/')) {
        normalized_path.insert(normalized_path.begin(), '/');
    }

    auto profiles = profiles_.list_exposure_profiles();
    const auto profile_it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return profile.id == profile_id;
    });
    if (profile_it == profiles.end()) {
        return std::unexpected(make_exposure_error("exposure profile not found", std::string(profile_id)));
    }

    auto profile = *profile_it;
    profile.endpoint.listen_host = std::string(host);
    profile.endpoint.listen_port = port;
    profile.endpoint.path = std::move(normalized_path);
    profile.endpoint.transport = McpServerTransportKind::streamable_http;
    if (profile.endpoint.name.empty()) {
        profile.endpoint.name = profile.id;
    }

    return profiles_.save(std::move(profile));
}

core::Result<core::Unit> ExposureManagementService::set_instructions(std::string_view profile_id,
                                                                     std::string_view instructions) {
    auto profile = find_profile(profiles_, profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }

    profile->instructions = std::string(instructions);
    return profiles_.save(std::move(*profile));
}

core::Result<core::Unit> ExposureManagementService::bind_capability(std::string_view profile_id,
                                                                    std::string_view capability_id,
                                                                    std::string_view exposed_name) {
    auto profiles = profiles_.list_exposure_profiles();
    const auto profile_it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return profile.id == profile_id;
    });
    if (profile_it == profiles.end()) {
        return std::unexpected(make_exposure_error("exposure profile not found", std::string(profile_id)));
    }

    const auto capabilities = capabilities_.list_capabilities();
    const auto capability_it = std::find_if(capabilities.begin(), capabilities.end(), [&](const auto& capability) {
        return capability.id == capability_id;
    });
    if (capability_it == capabilities.end()) {
        return std::unexpected(make_exposure_error("capability not found", std::string(capability_id)));
    }

    auto profile = *profile_it;
    const auto& capability = *capability_it;

    upsert_binding(profile, binding_from_capability(profile, capability, exposed_name));

    return profiles_.save(std::move(profile));
}

core::Result<std::size_t> ExposureManagementService::bind_server_capabilities(std::string_view profile_id,
                                                                              std::string_view server_id) {
    auto profile = find_profile(profiles_, profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }

    const auto capabilities = capabilities_.list_capabilities();
    std::size_t binding_count = 0;
    for (const auto& capability : capabilities) {
        if (capability.server_id != server_id) {
            continue;
        }
        upsert_binding(*profile, binding_from_capability(*profile, capability, {}));
        ++binding_count;
    }

    if (binding_count == 0) {
        return std::unexpected(make_exposure_error("no capabilities discovered for server", std::string(server_id)));
    }

    const auto saved = profiles_.save(std::move(*profile));
    if (!saved) {
        return std::unexpected(saved.error());
    }
    return binding_count;
}

core::Result<core::Unit> ExposureManagementService::set_binding_enabled(std::string_view profile_id,
                                                                        std::string_view capability_id,
                                                                        bool enabled) {
    auto profile = find_profile(profiles_, profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }

    const auto binding_id = std::string(profile_id) + ":" + std::string(capability_id);
    const auto binding_it = std::find_if(profile->bindings.begin(), profile->bindings.end(), [&](const auto& binding) {
        return binding.id == binding_id;
    });
    if (binding_it == profile->bindings.end()) {
        return std::unexpected(make_exposure_error("exposure binding not found", std::string(capability_id)));
    }

    binding_it->enabled = enabled;
    return profiles_.save(std::move(*profile));
}

core::Result<core::Unit> ExposureManagementService::unbind_capability(std::string_view profile_id,
                                                                      std::string_view capability_id) {
    auto profile = find_profile(profiles_, profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }

    const auto binding_id = std::string(profile_id) + ":" + std::string(capability_id);
    const auto original_size = profile->bindings.size();
    profile->bindings.erase(std::remove_if(profile->bindings.begin(),
                                           profile->bindings.end(),
                                           [&](const auto& binding) {
                                               return binding.id == binding_id;
                                           }),
                            profile->bindings.end());
    if (profile->bindings.size() == original_size) {
        return std::unexpected(make_exposure_error("exposure binding not found", std::string(capability_id)));
    }

    return profiles_.save(std::move(*profile));
}

core::Result<std::size_t> ExposureManagementService::prune_stale_bindings(std::string_view profile_id) {
    auto profile = find_profile(profiles_, profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }

    const auto capabilities = capabilities_.list_capabilities();
    const auto original_size = profile->bindings.size();
    profile->bindings.erase(std::remove_if(profile->bindings.begin(),
                                           profile->bindings.end(),
                                           [&](const auto& binding) {
                                               return std::none_of(capabilities.begin(),
                                                                   capabilities.end(),
                                                                   [&](const auto& capability) {
                                                                       return capability_matches_binding(capability,
                                                                                                         binding);
                                                                   });
                                           }),
                            profile->bindings.end());
    const auto pruned = original_size - profile->bindings.size();
    const auto saved = profiles_.save(std::move(*profile));
    if (!saved) {
        return std::unexpected(saved.error());
    }
    return pruned;
}

core::Result<core::Unit> ExposureManagementService::remove_bindings_for_server(std::string_view server_id) {
    auto profiles = profiles_.list_exposure_profiles();
    bool changed = false;
    for (auto& profile : profiles) {
        const auto original_size = profile.bindings.size();
        profile.bindings.erase(std::remove_if(profile.bindings.begin(),
                                              profile.bindings.end(),
                                              [&](const auto& binding) {
                                                  return binding.server_id == server_id;
                                              }),
                               profile.bindings.end());
        if (profile.bindings.size() != original_size) {
            changed = true;
            const auto saved = profiles_.save(profile);
            if (!saved) {
                return std::unexpected(saved.error());
            }
        }
    }

    if (!changed) {
        return core::Unit{};
    }
    return core::Unit{};
}

} // namespace mcp::app
