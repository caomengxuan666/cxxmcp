#pragma once

#include "mcp/app/import_export.hpp"
#include "mcp/app/policy.hpp"
#include "mcp/app/profile.hpp"
#include "mcp/app/tool_catalog.hpp"
#include "mcp/core/result.hpp"
#include "mcp/protocol/types.hpp"

namespace mcp::app {

using Json = protocol::Json;

Json to_json(Permission permission);
core::Result<Permission> permission_from_json(const Json& json);

Json to_json(ApprovalState state);
core::Result<ApprovalState> approval_state_from_json(const Json& json);

Json to_json(const Policy& policy);
core::Result<Policy> policy_from_json(const Json& json);

Json to_json(ToolSourceKind kind);
core::Result<ToolSourceKind> tool_source_kind_from_json(const Json& json);

Json to_json(const ToolSource& source);
core::Result<ToolSource> tool_source_from_json(const Json& json);

Json to_json(const ToolDescriptor& descriptor);
core::Result<ToolDescriptor> tool_descriptor_from_json(const Json& json);

Json to_json(const Endpoint& endpoint);
core::Result<Endpoint> endpoint_from_json(const Json& json);

Json to_json(const Profile& profile);
core::Result<Profile> profile_from_json(const Json& json);

Json to_json(const ExportBundle& bundle);
core::Result<ExportBundle> export_bundle_from_json(const Json& json);

} // namespace mcp::app
