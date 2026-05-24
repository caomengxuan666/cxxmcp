#pragma once

#include "cxxmcp/app/import_export.hpp"
#include "cxxmcp/app/mcp_server.hpp"
#include "cxxmcp/app/policy.hpp"
#include "cxxmcp/app/profile.hpp"
#include "cxxmcp/app/tool_catalog.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::app {

    using Json = protocol::Json;

    Json to_json(Permission permission);
    core::Result<Permission> permission_from_json(const Json &json);

    Json to_json(ApprovalState state);
    core::Result<ApprovalState> approval_state_from_json(const Json &json);

    Json to_json(const Policy &policy);
    core::Result<Policy> policy_from_json(const Json &json);

    Json to_json(ToolSourceKind kind);
    core::Result<ToolSourceKind> tool_source_kind_from_json(const Json &json);

    Json to_json(const ToolSource &source);
    core::Result<ToolSource> tool_source_from_json(const Json &json);

    Json to_json(const ToolDescriptor &descriptor);
    core::Result<ToolDescriptor> tool_descriptor_from_json(const Json &json);

    Json to_json(const Endpoint &endpoint);
    core::Result<Endpoint> endpoint_from_json(const Json &json);

    Json to_json(const Profile &profile);
    core::Result<Profile> profile_from_json(const Json &json);

    Json to_json(McpServerTransportKind kind);
    core::Result<McpServerTransportKind> mcp_server_transport_kind_from_json(const Json &json);

    Json to_json(McpServerTrustState state);
    core::Result<McpServerTrustState> mcp_server_trust_state_from_json(const Json &json);

    Json to_json(McpServerRuntimeState state);
    core::Result<McpServerRuntimeState> mcp_server_runtime_state_from_json(const Json &json);

    Json to_json(CapabilityKind kind);
    core::Result<CapabilityKind> capability_kind_from_json(const Json &json);

    Json to_json(NamespaceStrategy strategy);
    core::Result<NamespaceStrategy> namespace_strategy_from_json(const Json &json);

    Json to_json(const StdioLaunchConfig &config);
    core::Result<StdioLaunchConfig> stdio_launch_config_from_json(const Json &json);

    Json to_json(const HttpConnectionConfig &config);
    core::Result<HttpConnectionConfig> http_connection_config_from_json(const Json &json);

    Json to_json(const McpServerDefinition &server);
    core::Result<McpServerDefinition> mcp_server_definition_from_json(const Json &json);
    core::Result<std::vector<McpServerDefinition>> mcp_server_definitions_from_client_config_json(const Json &json);

    Json to_json(const McpServerRuntime &runtime);
    core::Result<McpServerRuntime> mcp_server_runtime_from_json(const Json &json);

    Json to_json(const DiscoveredCapability &capability);
    core::Result<DiscoveredCapability> discovered_capability_from_json(const Json &json);

    Json to_json(const CapabilityBinding &binding);
    core::Result<CapabilityBinding> capability_binding_from_json(const Json &json);

    Json to_json(const HostedEndpoint &endpoint);
    core::Result<HostedEndpoint> hosted_endpoint_from_json(const Json &json);

    Json to_json(const ExposureProfile &profile);
    core::Result<ExposureProfile> exposure_profile_from_json(const Json &json);

    Json to_json(const ExportBundle &bundle);
    core::Result<ExportBundle> export_bundle_from_json(const Json &json);

}// namespace mcp::app
