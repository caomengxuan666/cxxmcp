// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/app/client_config.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "cxxmcp/app/gateway.hpp"

namespace mcp::app {
namespace {

core::Error make_client_config_error(std::string message,
                                     std::string detail = {}) {
  return core::Error{1, std::move(message), std::move(detail)};
}

std::string endpoint_url(const HostedEndpoint& endpoint) {
  std::string path =
      endpoint.path.empty() ? std::string("/mcp") : endpoint.path;
  if (!core::starts_with(path, '/')) {
    path.insert(path.begin(), '/');
  }
  return "http://" + endpoint.listen_host + ":" +
         std::to_string(endpoint.listen_port) + path;
}

core::Result<ExposureProfile> find_profile(const ExposureProfileStore& profiles,
                                           std::string_view profile_id) {
  const auto configured_profiles = profiles.list_exposure_profiles();
  const auto profile_it = std::find_if(
      configured_profiles.begin(), configured_profiles.end(),
      [&](const auto& profile) { return profile.id == profile_id; });
  if (profile_it == configured_profiles.end()) {
    return std::unexpected(make_client_config_error(
        "exposure profile not found", std::string(profile_id)));
  }
  return *profile_it;
}

Json client_config_with_server(std::string server_name, Json server_config) {
  return Json{
      {"mcpServers",
       Json{
           {std::move(server_name), std::move(server_config)},
       }},
  };
}

core::Result<Json> http_server_config_for_profile(
    const ExposureProfile& profile) {
  if (profile.endpoint.transport != McpServerTransportKind::streamable_http) {
    return std::unexpected(make_client_config_error(
        "exposure endpoint transport must be streamable_http", profile.id));
  }
  if (profile.endpoint.listen_host.empty()) {
    return std::unexpected(make_client_config_error(
        "exposure endpoint host is not configured", profile.id));
  }
  if (profile.endpoint.listen_port == 0) {
    return std::unexpected(make_client_config_error(
        "exposure endpoint port is not configured", profile.id));
  }

  return Json{
      {"type", "http"},
      {"url", endpoint_url(profile.endpoint)},
  };
}

std::string prefixed_server_name(std::string_view prefix,
                                 std::string_view profile_id) {
  if (prefix.empty()) {
    return std::string(profile_id);
  }
  return std::string(prefix) + "." + std::string(profile_id);
}

}  // namespace

GatewayClientConfigService::GatewayClientConfigService(
    const ExposureProfileStore& profiles)
    : profiles_(profiles) {}

core::Result<Json> GatewayClientConfigService::make_http_client_config(
    std::string_view profile_id, std::string_view server_name) const {
  const auto profile = find_profile(profiles_, profile_id);
  if (!profile) {
    return std::unexpected(profile.error());
  }
  const auto server_config = http_server_config_for_profile(*profile);
  if (!server_config) {
    return std::unexpected(server_config.error());
  }

  const auto name =
      server_name.empty() ? profile->id : std::string(server_name);
  return client_config_with_server(name, *server_config);
}

core::Result<Json> GatewayClientConfigService::make_all_http_client_configs(
    std::string_view server_name_prefix) const {
  const auto configured_profiles = profiles_.list_exposure_profiles();
  if (configured_profiles.empty()) {
    return std::unexpected(
        make_client_config_error("no gateway profiles configured"));
  }

  Json servers = Json::object();
  for (const auto& profile : configured_profiles) {
    const auto server_config = http_server_config_for_profile(profile);
    if (!server_config) {
      return std::unexpected(server_config.error());
    }
    servers[prefixed_server_name(server_name_prefix, profile.id)] =
        *server_config;
  }

  return Json{{"mcpServers", std::move(servers)}};
}

core::Result<Json> GatewayClientConfigService::make_ready_http_client_configs(
    const GatewayReadinessService& readiness,
    std::string_view server_name_prefix) const {
  const auto configured_profiles = profiles_.list_exposure_profiles();
  if (configured_profiles.empty()) {
    return std::unexpected(
        make_client_config_error("no gateway profiles configured"));
  }

  Json servers = Json::object();
  for (const auto& profile : configured_profiles) {
    const auto report = readiness.check_profile(profile.id);
    if (!report.ready) {
      continue;
    }

    const auto server_config = http_server_config_for_profile(profile);
    if (!server_config) {
      continue;
    }
    servers[prefixed_server_name(server_name_prefix, profile.id)] =
        *server_config;
  }

  if (servers.empty()) {
    return std::unexpected(
        make_client_config_error("no ready HTTP gateway profiles configured"));
  }

  return Json{{"mcpServers", std::move(servers)}};
}

core::Result<Json> GatewayClientConfigService::make_stdio_client_config(
    std::string_view profile_id, std::string_view command,
    std::vector<std::string> args, std::string_view server_name) const {
  const auto profile = find_profile(profiles_, profile_id);
  if (!profile) {
    return std::unexpected(profile.error());
  }
  if (command.empty()) {
    return std::unexpected(make_client_config_error(
        "stdio client config command is not configured",
        std::string(profile_id)));
  }

  const auto name =
      server_name.empty() ? profile->id : std::string(server_name);
  return client_config_with_server(name, Json{
                                             {"command", std::string(command)},
                                             {"args", std::move(args)},
                                         });
}

}  // namespace mcp::app
