// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/app/gateway.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "cxxmcp/protocol/serialization.hpp"

namespace mcp::app {
namespace {

core::Error make_gateway_error(protocol::ErrorCode code, std::string message,
                               std::string detail = {}) {
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail)};
}

GatewayReadinessIssue make_readiness_issue(std::string code,
                                           std::string message,
                                           std::string detail = {}) {
  return GatewayReadinessIssue{
      .code = std::move(code),
      .message = std::move(message),
      .detail = std::move(detail),
  };
}

std::string transport_name(McpServerTransportKind transport) {
  switch (transport) {
    case McpServerTransportKind::stdio:
      return "stdio";
    case McpServerTransportKind::streamable_http:
      return "streamable_http";
    case McpServerTransportKind::legacy_sse:
      return "legacy_sse";
  }
  return "unknown";
}

bool endpoint_configured_for_http(const HostedEndpoint& endpoint) {
  return endpoint.transport == McpServerTransportKind::streamable_http &&
         !endpoint.listen_host.empty() && endpoint.listen_port != 0;
}

std::vector<GatewayReadinessIssue> endpoint_readiness_issues(
    const HostedEndpoint& endpoint) {
  std::vector<GatewayReadinessIssue> issues;
  if (endpoint.transport != McpServerTransportKind::streamable_http) {
    issues.push_back(make_readiness_issue(
        "endpoint_transport_unsupported",
        "gateway endpoint transport is not streamable_http",
        transport_name(endpoint.transport)));
  }
  if (endpoint.listen_host.empty() || endpoint.listen_port == 0) {
    issues.push_back(make_readiness_issue(
        "endpoint_not_configured", "gateway HTTP endpoint is not configured"));
  }
  return issues;
}

std::string binding_exposed_name(const CapabilityBinding& binding) {
  if (!binding.exposed_name.empty()) {
    return binding.exposed_name;
  }
  if (binding.namespace_strategy == NamespaceStrategy::none ||
      binding.server_id.empty()) {
    return binding.upstream_name;
  }
  return binding.server_id + "." + binding.upstream_name;
}

const ExposureProfile* find_profile(
    const std::vector<ExposureProfile>& profiles, std::string_view profile_id) {
  const auto it = std::find_if(
      profiles.begin(), profiles.end(),
      [&](const auto& profile) { return profile.id == profile_id; });
  return it == profiles.end() ? nullptr : &*it;
}

const DiscoveredCapability* find_capability(
    const std::vector<DiscoveredCapability>& capabilities,
    const CapabilityBinding& binding) {
  const auto it = std::find_if(
      capabilities.begin(), capabilities.end(), [&](const auto& capability) {
        return capability.kind == binding.kind &&
               capability.server_id == binding.server_id &&
               capability.upstream_name == binding.upstream_name;
      });
  return it == capabilities.end() ? nullptr : &*it;
}

protocol::JsonRpcResponse gateway_error_response(
    const protocol::JsonRpcRequest& request, protocol::ErrorCode code,
    std::string message, std::string detail = {}) {
  return protocol::make_error_response(
      std::optional<protocol::RequestId>{request.id},
      protocol::make_error(code, std::move(message),
                           detail.empty()
                               ? std::nullopt
                               : std::optional<protocol::Json>{detail}));
}

protocol::JsonRpcResponse gateway_error_response(
    const protocol::JsonRpcRequest& request, const core::Error& error) {
  return protocol::make_error_response(
      std::optional<protocol::RequestId>{request.id},
      protocol::make_error(error.code, error.message,
                           error.detail.empty()
                               ? std::nullopt
                               : std::optional<protocol::Json>{error.detail}));
}

}  // namespace

GatewayReadinessService::GatewayReadinessService(
    const ExposureProfileStore& profiles, const CapabilityCatalog& capabilities,
    const McpServerStore& servers, GatewayServerHealthProvider health_provider)
    : profiles_(profiles),
      capabilities_(capabilities),
      servers_(servers),
      health_provider_(std::move(health_provider)) {}

GatewayReadinessReport GatewayReadinessService::check_profile(
    std::string_view profile_id) const {
  GatewayReadinessReport report{
      .profile_id = std::string(profile_id),
  };

  const auto profiles = profiles_.list_exposure_profiles();
  const auto* profile = find_profile(profiles, profile_id);
  if (!profile) {
    report.issues.push_back(make_readiness_issue("profile_not_found",
                                                 "exposure profile not found",
                                                 std::string(profile_id)));
    return report;
  }

  report.profile_id = profile->id;
  report.binding_count = profile->bindings.size();

  const auto capabilities = capabilities_.list_capabilities();
  const auto servers = servers_.list_servers();
  if (health_provider_ && !health_cache_.has_value()) {
    health_cache_ = health_provider_();
  }
  const auto* health_reports =
      health_cache_.has_value() ? &*health_cache_ : nullptr;
  for (const auto& binding : profile->bindings) {
    if (!binding.enabled || !binding.policy.enabled) {
      continue;
    }
    ++report.enabled_binding_count;

    if (find_capability(capabilities, binding) == nullptr) {
      report.issues.push_back(make_readiness_issue(
          "capability_not_found",
          "capability binding has no discovered capability", binding.id));
    }

    const auto server_it = std::find_if(
        servers.begin(), servers.end(),
        [&](const auto& server) { return server.id == binding.server_id; });
    if (server_it == servers.end()) {
      report.issues.push_back(make_readiness_issue(
          "server_not_found", "mcp server not found", binding.server_id));
      continue;
    }
    if (!server_it->enabled) {
      report.issues.push_back(make_readiness_issue(
          "server_disabled", "cxxmcp server is disabled", server_it->id));
    }
    if (server_it->trust == McpServerTrustState::untrusted) {
      report.issues.push_back(make_readiness_issue(
          "server_untrusted", "cxxmcp server is untrusted", server_it->id));
    }
    if (server_it->trust == McpServerTrustState::blocked) {
      report.issues.push_back(make_readiness_issue(
          "server_blocked", "cxxmcp server is blocked", server_it->id));
    }

    const bool statically_blocked =
        !server_it->enabled || server_it->trust != McpServerTrustState::trusted;
    if (health_reports != nullptr && !statically_blocked) {
      const auto health_it =
          std::find_if(health_reports->begin(), health_reports->end(),
                       [&](const auto& health) {
                         return health.server_id == binding.server_id;
                       });
      if (health_it != health_reports->end() && !health_it->ready) {
        report.issues.push_back(make_readiness_issue(
            "server_unready",
            health_it->error_message.empty() ? "mcp server health check failed"
                                             : health_it->error_message,
            server_it->id));
      }
    }
  }

  if (report.enabled_binding_count == 0) {
    report.issues.push_back(make_readiness_issue(
        "no_enabled_bindings", "no enabled capability bindings configured",
        profile->id));
  }
  report.ready = report.issues.empty();
  return report;
}

GatewayStatusService::GatewayStatusService(
    const ExposureProfileStore& profiles, const CapabilityCatalog& capabilities,
    const McpServerStore& servers, GatewayServerHealthProvider health_provider)
    : profiles_(profiles),
      capabilities_(capabilities),
      servers_(servers),
      health_provider_(std::move(health_provider)) {}

GatewayStatusReport GatewayStatusService::check_http_profiles() const {
  GatewayStatusReport status;
  const auto profile_items = profiles_.list_exposure_profiles();
  status.profiles.reserve(profile_items.size());

  GatewayReadinessService readiness(profiles_, capabilities_, servers_,
                                    health_provider_);
  for (const auto& profile : profile_items) {
    auto profile_status = GatewayProfileStatus{
        .profile = profile,
        .readiness = readiness.check_profile(profile.id),
        .endpoint_issues = endpoint_readiness_issues(profile.endpoint),
        .endpoint_configured = endpoint_configured_for_http(profile.endpoint),
    };
    profile_status.http_ready = profile_status.readiness.ready &&
                                profile_status.endpoint_issues.empty();
    if (profile_status.http_ready) {
      ++status.ready_profile_count;
    }
    status.profiles.push_back(std::move(profile_status));
  }

  status.ready = !status.profiles.empty() &&
                 status.ready_profile_count == status.profiles.size();
  return status;
}

GatewayRoutingService::GatewayRoutingService(
    const ExposureProfileStore& profiles, const CapabilityCatalog& capabilities,
    GatewayToolCaller call_tool, GatewayPromptGetter get_prompt,
    GatewayResourceReader read_resource)
    : profiles_(profiles),
      capabilities_(capabilities),
      call_tool_(std::move(call_tool)),
      get_prompt_(std::move(get_prompt)),
      read_resource_(std::move(read_resource)) {}

core::Result<ExposureProfile> GatewayRoutingService::get_profile(
    std::string_view profile_id) const {
  const auto profiles = profiles_.list_exposure_profiles();
  const auto* profile = find_profile(profiles, profile_id);
  if (!profile) {
    return std::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidRequest, "exposure profile not found",
        std::string(profile_id)));
  }
  return *profile;
}

core::Result<std::vector<protocol::ToolDefinition>>
GatewayRoutingService::list_tools(std::string_view profile_id) const {
  const auto profiles = profiles_.list_exposure_profiles();
  const auto* profile = find_profile(profiles, profile_id);
  if (!profile) {
    return std::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidRequest, "exposure profile not found",
        std::string(profile_id)));
  }

  const auto capabilities = capabilities_.list_capabilities();
  std::vector<protocol::ToolDefinition> tools;
  for (const auto& binding : profile->bindings) {
    if (!binding.enabled || !binding.policy.enabled ||
        binding.kind != CapabilityKind::tool) {
      continue;
    }

    const auto* capability = find_capability(capabilities, binding);
    if (!capability) {
      continue;
    }

    tools.push_back(protocol::ToolDefinition{
        .name = binding_exposed_name(binding),
        .description = capability->description,
        .input_schema = capability->input_schema,
        .streaming = false,
    });
  }

  std::sort(tools.begin(), tools.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.name < rhs.name;
  });
  return tools;
}

core::Result<std::vector<protocol::Prompt>> GatewayRoutingService::list_prompts(
    std::string_view profile_id) const {
  const auto profiles = profiles_.list_exposure_profiles();
  const auto* profile = find_profile(profiles, profile_id);
  if (!profile) {
    return std::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidRequest, "exposure profile not found",
        std::string(profile_id)));
  }

  const auto capabilities = capabilities_.list_capabilities();
  std::vector<protocol::Prompt> prompts;
  for (const auto& binding : profile->bindings) {
    if (!binding.enabled || !binding.policy.enabled ||
        binding.kind != CapabilityKind::prompt) {
      continue;
    }

    const auto* capability = find_capability(capabilities, binding);
    if (!capability) {
      continue;
    }

    prompts.push_back(protocol::Prompt{
        .name = binding_exposed_name(binding),
        .description = capability->description,
        .arguments = {},
    });
  }

  std::sort(
      prompts.begin(), prompts.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
  return prompts;
}

core::Result<std::vector<protocol::Resource>>
GatewayRoutingService::list_resources(std::string_view profile_id) const {
  const auto profiles = profiles_.list_exposure_profiles();
  const auto* profile = find_profile(profiles, profile_id);
  if (!profile) {
    return std::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidRequest, "exposure profile not found",
        std::string(profile_id)));
  }

  const auto capabilities = capabilities_.list_capabilities();
  std::vector<protocol::Resource> resources;
  for (const auto& binding : profile->bindings) {
    if (!binding.enabled || !binding.policy.enabled ||
        binding.kind != CapabilityKind::resource) {
      continue;
    }

    const auto* capability = find_capability(capabilities, binding);
    if (!capability) {
      continue;
    }

    std::string mime_type;
    if (capability->output_schema.is_object() &&
        capability->output_schema.contains("mimeType") &&
        capability->output_schema.at("mimeType").is_string()) {
      mime_type = capability->output_schema.at("mimeType").get<std::string>();
    }

    resources.push_back(protocol::Resource{
        .uri = capability->uri,
        .name = binding_exposed_name(binding),
        .description = capability->description,
        .mime_type = std::move(mime_type),
    });
  }

  std::sort(resources.begin(), resources.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.uri < rhs.uri; });
  return resources;
}

core::Result<protocol::ToolResult> GatewayRoutingService::call_tool(
    std::string_view profile_id, std::string_view exposed_name,
    protocol::Json arguments) const {
  if (!call_tool_) {
    return std::unexpected(
        make_gateway_error(protocol::ErrorCode::InternalError,
                           "gateway tool caller is not configured"));
  }

  const auto profiles = profiles_.list_exposure_profiles();
  const auto* profile = find_profile(profiles, profile_id);
  if (!profile) {
    return std::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidRequest, "exposure profile not found",
        std::string(profile_id)));
  }

  for (const auto& binding : profile->bindings) {
    if (binding.kind != CapabilityKind::tool ||
        binding_exposed_name(binding) != exposed_name) {
      continue;
    }
    if (!binding.enabled || !binding.policy.enabled) {
      return std::unexpected(make_gateway_error(
          protocol::ErrorCode::PermissionDenied, "tool binding is disabled",
          std::string(exposed_name)));
    }

    return call_tool_(binding.server_id, protocol::ToolCall{
                                             .name = binding.upstream_name,
                                             .arguments = std::move(arguments),
                                         });
  }

  return std::unexpected(make_gateway_error(protocol::ErrorCode::ToolNotFound,
                                            "tool binding not found",
                                            std::string(exposed_name)));
}

core::Result<protocol::PromptsGetResult> GatewayRoutingService::get_prompt(
    std::string_view profile_id, std::string_view exposed_name,
    protocol::Json arguments) const {
  if (!get_prompt_) {
    return std::unexpected(
        make_gateway_error(protocol::ErrorCode::InternalError,
                           "gateway prompt getter is not configured"));
  }

  const auto profiles = profiles_.list_exposure_profiles();
  const auto* profile = find_profile(profiles, profile_id);
  if (!profile) {
    return std::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidRequest, "exposure profile not found",
        std::string(profile_id)));
  }

  for (const auto& binding : profile->bindings) {
    if (binding.kind != CapabilityKind::prompt ||
        binding_exposed_name(binding) != exposed_name) {
      continue;
    }
    if (!binding.enabled || !binding.policy.enabled) {
      return std::unexpected(make_gateway_error(
          protocol::ErrorCode::PermissionDenied, "prompt binding is disabled",
          std::string(exposed_name)));
    }

    return get_prompt_(binding.server_id, protocol::PromptsGetParams{
                                              .name = binding.upstream_name,
                                              .arguments = std::move(arguments),
                                          });
  }

  return std::unexpected(make_gateway_error(protocol::ErrorCode::InvalidRequest,
                                            "prompt binding not found",
                                            std::string(exposed_name)));
}

core::Result<protocol::ResourcesReadResult>
GatewayRoutingService::read_resource(std::string_view profile_id,
                                     std::string_view uri) const {
  if (!read_resource_) {
    return std::unexpected(
        make_gateway_error(protocol::ErrorCode::InternalError,
                           "gateway resource reader is not configured"));
  }

  const auto profiles = profiles_.list_exposure_profiles();
  const auto* profile = find_profile(profiles, profile_id);
  if (!profile) {
    return std::unexpected(make_gateway_error(
        protocol::ErrorCode::InvalidRequest, "exposure profile not found",
        std::string(profile_id)));
  }

  const auto capabilities = capabilities_.list_capabilities();
  for (const auto& binding : profile->bindings) {
    if (binding.kind != CapabilityKind::resource) {
      continue;
    }

    const auto* capability = find_capability(capabilities, binding);
    if (!capability || capability->uri != uri) {
      continue;
    }
    if (!binding.enabled || !binding.policy.enabled) {
      return std::unexpected(
          make_gateway_error(protocol::ErrorCode::PermissionDenied,
                             "resource binding is disabled", std::string(uri)));
    }

    return read_resource_(binding.server_id, protocol::ResourcesReadParams{
                                                 .uri = capability->uri});
  }

  return std::unexpected(
      make_gateway_error(protocol::ErrorCode::ResourceNotFound,
                         "resource binding not found", std::string(uri)));
}

GatewayRequestHandler::GatewayRequestHandler(
    const GatewayRoutingService& routing, std::string profile_id)
    : routing_(routing), profile_id_(std::move(profile_id)) {}

core::Result<protocol::JsonRpcResponse> GatewayRequestHandler::handle(
    const protocol::JsonRpcRequest& request) const {
  if (request.method == protocol::InitializeMethod) {
    const auto profile = routing_.get_profile(profile_id_);
    if (!profile) {
      return gateway_error_response(request, profile.error());
    }

    protocol::Json result = protocol::Json::object();
    result["protocolVersion"] = std::string(protocol::McpProtocolVersion);
    result["capabilities"] = protocol::Json{
        {"tools", protocol::Json{{"listChanged", false}}},
        {"prompts", protocol::Json{{"listChanged", false}}},
        {"resources",
         protocol::Json{{"listChanged", false}, {"subscribe", false}}},
    };
    result["serverInfo"] = protocol::Json{
        {"name", "cxxmcp Gateway"},
        {"version", "2.0.0"},
    };
    if (!profile->instructions.empty()) {
      result["instructions"] = profile->instructions;
    }
    return protocol::make_response(request.id, std::move(result));
  }

  if (request.method == protocol::PingMethod) {
    return protocol::make_response(request.id, protocol::Json::object());
  }

  if (request.method == "tools/list") {
    const auto tools = routing_.list_tools(profile_id_);
    if (!tools) {
      return gateway_error_response(request, tools.error());
    }

    protocol::Json result = protocol::Json::object();
    result["tools"] = protocol::Json::array();
    for (const auto& tool : *tools) {
      result["tools"].push_back(protocol::tool_definition_to_json(tool));
    }
    return protocol::make_response(request.id, std::move(result));
  }

  if (request.method == "prompts/list") {
    const auto prompts = routing_.list_prompts(profile_id_);
    if (!prompts) {
      return gateway_error_response(request, prompts.error());
    }

    return protocol::make_response(
        request.id,
        protocol::prompts_list_result_to_json(protocol::PromptsListResult{
            .prompts = *prompts,
            .next_cursor = std::nullopt,
        }));
  }

  if (request.method == "resources/list") {
    const auto resources = routing_.list_resources(profile_id_);
    if (!resources) {
      return gateway_error_response(request, resources.error());
    }

    return protocol::make_response(
        request.id,
        protocol::resources_list_result_to_json(protocol::ResourcesListResult{
            .resources = *resources,
            .next_cursor = std::nullopt,
        }));
  }

  if (request.method == "prompts/get") {
    const auto params = protocol::prompts_get_params_from_json(request.params);
    if (!params) {
      return gateway_error_response(request, params.error());
    }

    const auto result =
        routing_.get_prompt(profile_id_, params->name, params->arguments);
    if (!result) {
      return gateway_error_response(request, result.error());
    }

    return protocol::make_response(
        request.id, protocol::prompts_get_result_to_json(*result));
  }

  if (request.method == "resources/read") {
    const auto params =
        protocol::resources_read_params_from_json(request.params);
    if (!params) {
      return gateway_error_response(request, params.error());
    }

    const auto result = routing_.read_resource(profile_id_, params->uri);
    if (!result) {
      return gateway_error_response(request, result.error());
    }

    return protocol::make_response(
        request.id, protocol::resources_read_result_to_json(*result));
  }

  if (request.method == "tools/call") {
    if (!request.params.is_object()) {
      return gateway_error_response(request,
                                    protocol::ErrorCode::InvalidRequest,
                                    "tools/call params must be an object");
    }
    if (!request.params.contains("name") ||
        !request.params.at("name").is_string()) {
      return gateway_error_response(request,
                                    protocol::ErrorCode::InvalidRequest,
                                    "tools/call requires a string name");
    }

    protocol::Json arguments = protocol::Json::object();
    if (request.params.contains("arguments")) {
      if (!request.params.at("arguments").is_object()) {
        return gateway_error_response(request,
                                      protocol::ErrorCode::InvalidRequest,
                                      "tools/call arguments must be an object");
      }
      arguments = request.params.at("arguments");
    }

    const auto result = routing_.call_tool(
        profile_id_, request.params.at("name").get<std::string>(),
        std::move(arguments));
    if (!result) {
      return gateway_error_response(request, result.error());
    }

    return protocol::make_response(request.id,
                                   protocol::tool_result_to_json(*result));
  }

  return gateway_error_response(request, protocol::ErrorCode::MethodNotFound,
                                "method not found", request.method);
}

}  // namespace mcp::app
