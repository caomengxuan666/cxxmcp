// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/app/mcp_server.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::app {

using GatewayToolCaller = std::function<core::Result<protocol::ToolResult>(
    std::string_view server_id, const protocol::ToolCall& call)>;
using GatewayPromptGetter =
    std::function<core::Result<protocol::PromptsGetResult>(
        std::string_view server_id, const protocol::PromptsGetParams& params)>;
using GatewayResourceReader =
    std::function<core::Result<protocol::ResourcesReadResult>(
        std::string_view server_id,
        const protocol::ResourcesReadParams& params)>;

struct GatewayReadinessIssue {
  std::string code;
  std::string message;
  std::string detail;
};

struct GatewayServerHealth {
  std::string server_id;
  bool ready = false;
  std::size_t capability_count = 0;
  std::string error_message;
  std::string error_detail;
};

using GatewayServerHealthProvider =
    std::function<std::vector<GatewayServerHealth>()>;

struct GatewayReadinessReport {
  std::string profile_id;
  bool ready = false;
  std::size_t binding_count = 0;
  std::size_t enabled_binding_count = 0;
  std::vector<GatewayReadinessIssue> issues;
};

struct GatewayProfileStatus {
  ExposureProfile profile;
  GatewayReadinessReport readiness;
  std::vector<GatewayReadinessIssue> endpoint_issues;
  bool endpoint_configured = false;
  bool http_ready = false;
};

struct GatewayStatusReport {
  std::vector<GatewayProfileStatus> profiles;
  std::size_t ready_profile_count = 0;
  bool ready = false;
};

class GatewayReadinessService final {
 public:
  GatewayReadinessService(const ExposureProfileStore& profiles,
                          const CapabilityCatalog& capabilities,
                          const McpServerStore& servers,
                          GatewayServerHealthProvider health_provider = {});

  GatewayReadinessReport check_profile(std::string_view profile_id) const;

 private:
  const ExposureProfileStore& profiles_;
  const CapabilityCatalog& capabilities_;
  const McpServerStore& servers_;
  GatewayServerHealthProvider health_provider_;
  mutable std::optional<std::vector<GatewayServerHealth>> health_cache_;
};

class GatewayStatusService final {
 public:
  GatewayStatusService(const ExposureProfileStore& profiles,
                       const CapabilityCatalog& capabilities,
                       const McpServerStore& servers,
                       GatewayServerHealthProvider health_provider = {});

  GatewayStatusReport check_http_profiles() const;

 private:
  const ExposureProfileStore& profiles_;
  const CapabilityCatalog& capabilities_;
  const McpServerStore& servers_;
  GatewayServerHealthProvider health_provider_;
};

class GatewayRoutingService final {
 public:
  GatewayRoutingService(const ExposureProfileStore& profiles,
                        const CapabilityCatalog& capabilities,
                        GatewayToolCaller call_tool,
                        GatewayPromptGetter get_prompt = {},
                        GatewayResourceReader read_resource = {});

  core::Result<ExposureProfile> get_profile(std::string_view profile_id) const;
  core::Result<std::vector<protocol::ToolDefinition>> list_tools(
      std::string_view profile_id) const;
  core::Result<std::vector<protocol::Prompt>> list_prompts(
      std::string_view profile_id) const;
  core::Result<std::vector<protocol::Resource>> list_resources(
      std::string_view profile_id) const;
  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view profile_id, std::string_view exposed_name,
      protocol::Json arguments) const;
  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view profile_id, std::string_view uri) const;
  core::Result<protocol::ToolResult> call_tool(std::string_view profile_id,
                                               std::string_view exposed_name,
                                               protocol::Json arguments) const;

 private:
  const ExposureProfileStore& profiles_;
  const CapabilityCatalog& capabilities_;
  GatewayToolCaller call_tool_;
  GatewayPromptGetter get_prompt_;
  GatewayResourceReader read_resource_;
};

class GatewayRequestHandler final {
 public:
  GatewayRequestHandler(const GatewayRoutingService& routing,
                        std::string profile_id);

  core::Result<protocol::JsonRpcResponse> handle(
      const protocol::JsonRpcRequest& request) const;

 private:
  const GatewayRoutingService& routing_;
  std::string profile_id_;
};

}  // namespace mcp::app
