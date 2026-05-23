#include "mcp/app/serialization.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mcp::app {

namespace {

core::Error make_app_error(std::string message, std::string detail = {}) {
    return core::Error{1, std::move(message), std::move(detail)};
}

Json permissions_to_json(const std::unordered_set<Permission>& permissions) {
    std::vector<std::string> values;
    values.reserve(permissions.size());
    for (const auto permission : permissions) {
        values.push_back(to_json(permission).get<std::string>());
    }
    std::sort(values.begin(), values.end());

    Json json = Json::array();
    for (const auto& value : values) {
        json.push_back(value);
    }
    return json;
}

core::Result<std::unordered_set<Permission>> permissions_from_json(const Json& json) {
    if (!json.is_array()) {
        return std::unexpected(make_app_error("permissions must be an array"));
    }

    std::unordered_set<Permission> permissions;
    for (const auto& item : json) {
        const auto permission = permission_from_json(item);
        if (!permission) {
            return std::unexpected(permission.error());
        }
        permissions.insert(*permission);
    }
    return permissions;
}

Json endpoints_to_json(const std::vector<Endpoint>& endpoints) {
    Json json = Json::array();
    for (const auto& endpoint : endpoints) {
        json.push_back(to_json(endpoint));
    }
    return json;
}

core::Result<std::vector<Endpoint>> endpoints_from_json(const Json& json) {
    if (!json.is_array()) {
        return std::unexpected(make_app_error("endpoints must be an array"));
    }

    std::vector<Endpoint> endpoints;
    for (const auto& item : json) {
        const auto endpoint = endpoint_from_json(item);
        if (!endpoint) {
            return std::unexpected(endpoint.error());
        }
        endpoints.push_back(*endpoint);
    }
    return endpoints;
}

Json tool_descriptors_to_json(const std::vector<ToolDescriptor>& tools) {
    Json json = Json::array();
    for (const auto& tool : tools) {
        json.push_back(to_json(tool));
    }
    return json;
}

core::Result<std::vector<ToolDescriptor>> tool_descriptors_from_json(const Json& json) {
    if (!json.is_array()) {
        return std::unexpected(make_app_error("tools must be an array"));
    }

    std::vector<ToolDescriptor> tools;
    for (const auto& item : json) {
        const auto descriptor = tool_descriptor_from_json(item);
        if (!descriptor) {
            return std::unexpected(descriptor.error());
        }
        tools.push_back(*descriptor);
    }
    return tools;
}

} // namespace

Json to_json(Permission permission) {
    switch (permission) {
    case Permission::network_access:
        return Json("network_access");
    case Permission::filesystem_read:
        return Json("filesystem_read");
    case Permission::filesystem_write:
        return Json("filesystem_write");
    case Permission::command_execution:
        return Json("command_execution");
    }
    return Json("unknown");
}

core::Result<Permission> permission_from_json(const Json& json) {
    if (!json.is_string()) {
        return std::unexpected(make_app_error("permission must be a string"));
    }

    const auto value = json.get<std::string>();
    if (value == "network_access") {
        return Permission::network_access;
    }
    if (value == "filesystem_read") {
        return Permission::filesystem_read;
    }
    if (value == "filesystem_write") {
        return Permission::filesystem_write;
    }
    if (value == "command_execution") {
        return Permission::command_execution;
    }

    return std::unexpected(make_app_error("unknown permission", value));
}

Json to_json(ApprovalState state) {
    switch (state) {
    case ApprovalState::pending:
        return Json("pending");
    case ApprovalState::approved:
        return Json("approved");
    case ApprovalState::denied:
        return Json("denied");
    }
    return Json("pending");
}

core::Result<ApprovalState> approval_state_from_json(const Json& json) {
    if (!json.is_string()) {
        return std::unexpected(make_app_error("approval state must be a string"));
    }

    const auto value = json.get<std::string>();
    if (value == "pending") {
        return ApprovalState::pending;
    }
    if (value == "approved") {
        return ApprovalState::approved;
    }
    if (value == "denied") {
        return ApprovalState::denied;
    }

    return std::unexpected(make_app_error("unknown approval state", value));
}

Json to_json(const Policy& policy) {
    Json json = Json::object();
    json["approval"] = to_json(policy.approval);
    json["permissions"] = permissions_to_json(policy.permissions);
    json["enabled"] = policy.enabled;
    return json;
}

core::Result<Policy> policy_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("policy must be an object"));
    }

    Policy policy;
    if (json.contains("approval")) {
        const auto approval = approval_state_from_json(json.at("approval"));
        if (!approval) {
            return std::unexpected(approval.error());
        }
        policy.approval = *approval;
    }

    if (json.contains("permissions")) {
        const auto permissions = permissions_from_json(json.at("permissions"));
        if (!permissions) {
            return std::unexpected(permissions.error());
        }
        policy.permissions = *permissions;
    }

    if (json.contains("enabled")) {
        if (!json.at("enabled").is_boolean()) {
            return std::unexpected(make_app_error("policy enabled must be a boolean"));
        }
        policy.enabled = json.at("enabled").get<bool>();
    }

    return policy;
}

Json to_json(ToolSourceKind kind) {
    switch (kind) {
    case ToolSourceKind::local_manifest:
        return Json("local_manifest");
    case ToolSourceKind::local_plugin:
        return Json("local_plugin");
    case ToolSourceKind::remote_mcp_server:
        return Json("remote_mcp_server");
    case ToolSourceKind::generated_adapter:
        return Json("generated_adapter");
    }
    return Json("local_manifest");
}

core::Result<ToolSourceKind> tool_source_kind_from_json(const Json& json) {
    if (!json.is_string()) {
        return std::unexpected(make_app_error("tool source kind must be a string"));
    }

    const auto value = json.get<std::string>();
    if (value == "local_manifest") {
        return ToolSourceKind::local_manifest;
    }
    if (value == "local_plugin") {
        return ToolSourceKind::local_plugin;
    }
    if (value == "remote_mcp_server") {
        return ToolSourceKind::remote_mcp_server;
    }
    if (value == "generated_adapter") {
        return ToolSourceKind::generated_adapter;
    }

    return std::unexpected(make_app_error("unknown tool source kind", value));
}

Json to_json(const ToolSource& source) {
    Json json = Json::object();
    json["kind"] = to_json(source.kind);
    json["location"] = source.location;
    return json;
}

core::Result<ToolSource> tool_source_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("tool source must be an object"));
    }

    ToolSource source;
    if (json.contains("kind")) {
        const auto kind = tool_source_kind_from_json(json.at("kind"));
        if (!kind) {
            return std::unexpected(kind.error());
        }
        source.kind = *kind;
    }

    if (json.contains("location")) {
        if (!json.at("location").is_string()) {
            return std::unexpected(make_app_error("tool source location must be a string"));
        }
        source.location = json.at("location").get<std::string>();
    }

    return source;
}

Json to_json(const ToolDescriptor& descriptor) {
    Json json = Json::object();
    json["id"] = descriptor.id;
    json["definition"] = protocol::tool_definition_to_json(descriptor.definition);
    json["source"] = to_json(descriptor.source);
    json["policy"] = to_json(descriptor.policy);
    json["profileId"] = descriptor.profile_id;
    return json;
}

core::Result<ToolDescriptor> tool_descriptor_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("tool descriptor must be an object"));
    }

    ToolDescriptor descriptor;
    if (!json.contains("id") || !json.at("id").is_string()) {
        return std::unexpected(make_app_error("tool descriptor requires an id"));
    }
    descriptor.id = json.at("id").get<std::string>();

    if (json.contains("definition")) {
        const auto definition = protocol::tool_definition_from_json(json.at("definition"));
        if (!definition) {
            return std::unexpected(definition.error());
        }
        descriptor.definition = *definition;
    } else {
        return std::unexpected(make_app_error("tool descriptor requires a definition"));
    }

    if (json.contains("source")) {
        const auto source = tool_source_from_json(json.at("source"));
        if (!source) {
            return std::unexpected(source.error());
        }
        descriptor.source = *source;
    }

    if (json.contains("policy")) {
        const auto policy = policy_from_json(json.at("policy"));
        if (!policy) {
            return std::unexpected(policy.error());
        }
        descriptor.policy = *policy;
    }

    if (json.contains("profileId")) {
        if (!json.at("profileId").is_string()) {
            return std::unexpected(make_app_error("tool descriptor profileId must be a string"));
        }
        descriptor.profile_id = json.at("profileId").get<std::string>();
    }

    return descriptor;
}

Json to_json(const Endpoint& endpoint) {
    Json json = Json::object();
    json["name"] = endpoint.name;
    json["url"] = endpoint.url;
    return json;
}

core::Result<Endpoint> endpoint_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("endpoint must be an object"));
    }

    Endpoint endpoint;
    if (!json.contains("name") || !json.at("name").is_string()) {
        return std::unexpected(make_app_error("endpoint requires a name"));
    }
    endpoint.name = json.at("name").get<std::string>();

    if (!json.contains("url") || !json.at("url").is_string()) {
        return std::unexpected(make_app_error("endpoint requires a url"));
    }
    endpoint.url = json.at("url").get<std::string>();

    return endpoint;
}

Json to_json(const Profile& profile) {
    Json json = Json::object();
    json["id"] = profile.id;
    json["name"] = profile.name;
    json["endpoints"] = endpoints_to_json(profile.endpoints);
    json["enabledToolIds"] = profile.enabled_tool_ids;

    Json environment = Json::object();
    for (const auto& [key, value] : profile.environment) {
        environment[key] = value;
    }
    json["environment"] = std::move(environment);
    return json;
}

core::Result<Profile> profile_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("profile must be an object"));
    }

    Profile profile;
    if (!json.contains("id") || !json.at("id").is_string()) {
        return std::unexpected(make_app_error("profile requires an id"));
    }
    profile.id = json.at("id").get<std::string>();

    if (json.contains("name")) {
        if (!json.at("name").is_string()) {
            return std::unexpected(make_app_error("profile name must be a string"));
        }
        profile.name = json.at("name").get<std::string>();
    }

    if (json.contains("endpoints")) {
        const auto endpoints = endpoints_from_json(json.at("endpoints"));
        if (!endpoints) {
            return std::unexpected(endpoints.error());
        }
        profile.endpoints = *endpoints;
    }

    if (json.contains("enabledToolIds")) {
        if (!json.at("enabledToolIds").is_array()) {
            return std::unexpected(make_app_error("enabledToolIds must be an array"));
        }
        for (const auto& item : json.at("enabledToolIds")) {
            if (!item.is_string()) {
                return std::unexpected(make_app_error("enabledToolIds entries must be strings"));
            }
            profile.enabled_tool_ids.push_back(item.get<std::string>());
        }
    }

    if (json.contains("environment")) {
        if (!json.at("environment").is_object()) {
            return std::unexpected(make_app_error("environment must be an object"));
        }
        for (const auto& [key, value] : json.at("environment").items()) {
            if (!value.is_string()) {
                return std::unexpected(make_app_error("environment values must be strings"));
            }
            profile.environment[key] = value.get<std::string>();
        }
    }

    return profile;
}

Json to_json(const ExportBundle& bundle) {
    Json json = Json::object();
    json["profile"] = to_json(bundle.profile);
    json["tools"] = tool_descriptors_to_json(bundle.tools);
    return json;
}

core::Result<ExportBundle> export_bundle_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("export bundle must be an object"));
    }

    if (!json.contains("profile")) {
        return std::unexpected(make_app_error("export bundle requires a profile"));
    }
    if (!json.contains("tools")) {
        return std::unexpected(make_app_error("export bundle requires tools"));
    }

    const auto profile = profile_from_json(json.at("profile"));
    if (!profile) {
        return std::unexpected(profile.error());
    }

    const auto tools = tool_descriptors_from_json(json.at("tools"));
    if (!tools) {
        return std::unexpected(tools.error());
    }

    return ExportBundle{
        .profile = *profile,
        .tools = *tools,
    };
}

} // namespace mcp::app
