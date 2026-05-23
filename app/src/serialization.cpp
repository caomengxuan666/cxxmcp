#include "mcp/app/serialization.hpp"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
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

Json string_map_to_json(const std::unordered_map<std::string, std::string>& values) {
    Json json = Json::object();
    for (const auto& [key, value] : values) {
        json[key] = value;
    }
    return json;
}

core::Result<std::unordered_map<std::string, std::string>> string_map_from_json(const Json& json,
                                                                                std::string_view name) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error(std::string(name) + " must be an object"));
    }

    std::unordered_map<std::string, std::string> values;
    for (const auto& [key, value] : json.items()) {
        if (!value.is_string()) {
            return std::unexpected(make_app_error(std::string(name) + " values must be strings"));
        }
        values[key] = value.get<std::string>();
    }
    return values;
}

Json string_vector_to_json(const std::vector<std::string>& values) {
    Json json = Json::array();
    for (const auto& value : values) {
        json.push_back(value);
    }
    return json;
}

core::Result<std::vector<std::string>> string_vector_from_json(const Json& json, std::string_view name) {
    if (!json.is_array()) {
        return std::unexpected(make_app_error(std::string(name) + " must be an array"));
    }

    std::vector<std::string> values;
    for (const auto& item : json) {
        if (!item.is_string()) {
            return std::unexpected(make_app_error(std::string(name) + " entries must be strings"));
        }
        values.push_back(item.get<std::string>());
    }
    return values;
}

Json capability_bindings_to_json(const std::vector<CapabilityBinding>& bindings) {
    Json json = Json::array();
    for (const auto& binding : bindings) {
        json.push_back(to_json(binding));
    }
    return json;
}

core::Result<std::vector<CapabilityBinding>> capability_bindings_from_json(const Json& json) {
    if (!json.is_array()) {
        return std::unexpected(make_app_error("bindings must be an array"));
    }

    std::vector<CapabilityBinding> bindings;
    for (const auto& item : json) {
        const auto binding = capability_binding_from_json(item);
        if (!binding) {
            return std::unexpected(binding.error());
        }
        bindings.push_back(*binding);
    }
    return bindings;
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

Json to_json(McpServerTransportKind kind) {
    switch (kind) {
    case McpServerTransportKind::stdio:
        return Json("stdio");
    case McpServerTransportKind::streamable_http:
        return Json("streamable_http");
    case McpServerTransportKind::legacy_sse:
        return Json("legacy_sse");
    }
    return Json("stdio");
}

core::Result<McpServerTransportKind> mcp_server_transport_kind_from_json(const Json& json) {
    if (!json.is_string()) {
        return std::unexpected(make_app_error("mcp server transport must be a string"));
    }

    const auto value = json.get<std::string>();
    if (value == "stdio") {
        return McpServerTransportKind::stdio;
    }
    if (value == "streamable_http") {
        return McpServerTransportKind::streamable_http;
    }
    if (value == "legacy_sse") {
        return McpServerTransportKind::legacy_sse;
    }
    return std::unexpected(make_app_error("unknown mcp server transport", value));
}

Json to_json(McpServerTrustState state) {
    switch (state) {
    case McpServerTrustState::untrusted:
        return Json("untrusted");
    case McpServerTrustState::trusted:
        return Json("trusted");
    case McpServerTrustState::blocked:
        return Json("blocked");
    }
    return Json("untrusted");
}

core::Result<McpServerTrustState> mcp_server_trust_state_from_json(const Json& json) {
    if (!json.is_string()) {
        return std::unexpected(make_app_error("mcp server trust state must be a string"));
    }

    const auto value = json.get<std::string>();
    if (value == "untrusted") {
        return McpServerTrustState::untrusted;
    }
    if (value == "trusted") {
        return McpServerTrustState::trusted;
    }
    if (value == "blocked") {
        return McpServerTrustState::blocked;
    }
    return std::unexpected(make_app_error("unknown mcp server trust state", value));
}

Json to_json(McpServerRuntimeState state) {
    switch (state) {
    case McpServerRuntimeState::stopped:
        return Json("stopped");
    case McpServerRuntimeState::starting:
        return Json("starting");
    case McpServerRuntimeState::initializing:
        return Json("initializing");
    case McpServerRuntimeState::running:
        return Json("running");
    case McpServerRuntimeState::degraded:
        return Json("degraded");
    case McpServerRuntimeState::failed:
        return Json("failed");
    }
    return Json("stopped");
}

core::Result<McpServerRuntimeState> mcp_server_runtime_state_from_json(const Json& json) {
    if (!json.is_string()) {
        return std::unexpected(make_app_error("mcp server runtime state must be a string"));
    }

    const auto value = json.get<std::string>();
    if (value == "stopped") {
        return McpServerRuntimeState::stopped;
    }
    if (value == "starting") {
        return McpServerRuntimeState::starting;
    }
    if (value == "initializing") {
        return McpServerRuntimeState::initializing;
    }
    if (value == "running") {
        return McpServerRuntimeState::running;
    }
    if (value == "degraded") {
        return McpServerRuntimeState::degraded;
    }
    if (value == "failed") {
        return McpServerRuntimeState::failed;
    }
    return std::unexpected(make_app_error("unknown mcp server runtime state", value));
}

Json to_json(CapabilityKind kind) {
    switch (kind) {
    case CapabilityKind::tool:
        return Json("tool");
    case CapabilityKind::prompt:
        return Json("prompt");
    case CapabilityKind::resource:
        return Json("resource");
    }
    return Json("tool");
}

core::Result<CapabilityKind> capability_kind_from_json(const Json& json) {
    if (!json.is_string()) {
        return std::unexpected(make_app_error("capability kind must be a string"));
    }

    const auto value = json.get<std::string>();
    if (value == "tool") {
        return CapabilityKind::tool;
    }
    if (value == "prompt") {
        return CapabilityKind::prompt;
    }
    if (value == "resource") {
        return CapabilityKind::resource;
    }
    return std::unexpected(make_app_error("unknown capability kind", value));
}

Json to_json(NamespaceStrategy strategy) {
    switch (strategy) {
    case NamespaceStrategy::none:
        return Json("none");
    case NamespaceStrategy::server_prefix:
        return Json("server_prefix");
    case NamespaceStrategy::custom:
        return Json("custom");
    }
    return Json("server_prefix");
}

core::Result<NamespaceStrategy> namespace_strategy_from_json(const Json& json) {
    if (!json.is_string()) {
        return std::unexpected(make_app_error("namespace strategy must be a string"));
    }

    const auto value = json.get<std::string>();
    if (value == "none") {
        return NamespaceStrategy::none;
    }
    if (value == "server_prefix") {
        return NamespaceStrategy::server_prefix;
    }
    if (value == "custom") {
        return NamespaceStrategy::custom;
    }
    return std::unexpected(make_app_error("unknown namespace strategy", value));
}

Json to_json(const StdioLaunchConfig& config) {
    Json json = Json::object();
    json["command"] = config.command;
    json["args"] = string_vector_to_json(config.args);
    json["cwd"] = config.cwd;
    json["env"] = string_map_to_json(config.env);
    return json;
}

core::Result<StdioLaunchConfig> stdio_launch_config_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("stdio launch config must be an object"));
    }

    StdioLaunchConfig config;
    if (json.contains("command")) {
        if (!json.at("command").is_string()) {
            return std::unexpected(make_app_error("stdio command must be a string"));
        }
        config.command = json.at("command").get<std::string>();
    }
    if (json.contains("args")) {
        const auto args = string_vector_from_json(json.at("args"), "stdio args");
        if (!args) {
            return std::unexpected(args.error());
        }
        config.args = *args;
    }
    if (json.contains("cwd")) {
        if (!json.at("cwd").is_string()) {
            return std::unexpected(make_app_error("stdio cwd must be a string"));
        }
        config.cwd = json.at("cwd").get<std::string>();
    }
    if (json.contains("env")) {
        const auto env = string_map_from_json(json.at("env"), "stdio env");
        if (!env) {
            return std::unexpected(env.error());
        }
        config.env = *env;
    }
    return config;
}

Json to_json(const HttpConnectionConfig& config) {
    Json json = Json::object();
    json["url"] = config.url;
    json["headers"] = string_map_to_json(config.headers);
    return json;
}

core::Result<HttpConnectionConfig> http_connection_config_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("http connection config must be an object"));
    }

    HttpConnectionConfig config;
    if (json.contains("url")) {
        if (!json.at("url").is_string()) {
            return std::unexpected(make_app_error("http url must be a string"));
        }
        config.url = json.at("url").get<std::string>();
    }
    if (json.contains("headers")) {
        const auto headers = string_map_from_json(json.at("headers"), "http headers");
        if (!headers) {
            return std::unexpected(headers.error());
        }
        config.headers = *headers;
    }
    return config;
}

Json to_json(const McpServerDefinition& server) {
    Json json = Json::object();
    json["id"] = server.id;
    json["name"] = server.name;
    json["displayName"] = server.display_name;
    json["description"] = server.description;
    json["transport"] = to_json(server.transport);
    json["stdio"] = to_json(server.stdio);
    json["http"] = to_json(server.http);
    json["enabled"] = server.enabled;
    json["autoStart"] = server.auto_start;
    json["trust"] = to_json(server.trust);
    json["tags"] = string_vector_to_json(server.tags);
    return json;
}

core::Result<McpServerDefinition> mcp_server_definition_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("mcp server definition must be an object"));
    }

    McpServerDefinition server;
    if (!json.contains("id") || !json.at("id").is_string()) {
        return std::unexpected(make_app_error("mcp server definition requires an id"));
    }
    server.id = json.at("id").get<std::string>();

    if (json.contains("name")) {
        if (!json.at("name").is_string()) {
            return std::unexpected(make_app_error("mcp server name must be a string"));
        }
        server.name = json.at("name").get<std::string>();
    }
    if (json.contains("displayName")) {
        if (!json.at("displayName").is_string()) {
            return std::unexpected(make_app_error("mcp server displayName must be a string"));
        }
        server.display_name = json.at("displayName").get<std::string>();
    }
    if (json.contains("description")) {
        if (!json.at("description").is_string()) {
            return std::unexpected(make_app_error("mcp server description must be a string"));
        }
        server.description = json.at("description").get<std::string>();
    }
    if (json.contains("transport")) {
        const auto transport = mcp_server_transport_kind_from_json(json.at("transport"));
        if (!transport) {
            return std::unexpected(transport.error());
        }
        server.transport = *transport;
    }
    if (json.contains("stdio")) {
        const auto stdio = stdio_launch_config_from_json(json.at("stdio"));
        if (!stdio) {
            return std::unexpected(stdio.error());
        }
        server.stdio = *stdio;
    }
    if (json.contains("http")) {
        const auto http = http_connection_config_from_json(json.at("http"));
        if (!http) {
            return std::unexpected(http.error());
        }
        server.http = *http;
    }
    if (json.contains("enabled")) {
        if (!json.at("enabled").is_boolean()) {
            return std::unexpected(make_app_error("mcp server enabled must be a boolean"));
        }
        server.enabled = json.at("enabled").get<bool>();
    }
    if (json.contains("autoStart")) {
        if (!json.at("autoStart").is_boolean()) {
            return std::unexpected(make_app_error("mcp server autoStart must be a boolean"));
        }
        server.auto_start = json.at("autoStart").get<bool>();
    }
    if (json.contains("trust")) {
        const auto trust = mcp_server_trust_state_from_json(json.at("trust"));
        if (!trust) {
            return std::unexpected(trust.error());
        }
        server.trust = *trust;
    }
    if (json.contains("tags")) {
        const auto tags = string_vector_from_json(json.at("tags"), "mcp server tags");
        if (!tags) {
            return std::unexpected(tags.error());
        }
        server.tags = *tags;
    }
    return server;
}

core::Result<std::vector<McpServerDefinition>> mcp_server_definitions_from_client_config_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("client mcp config must be an object"));
    }

    const auto import_group = [](const Json& servers_json,
                                 std::vector<McpServerDefinition>& servers) -> core::Result<core::Unit> {
        if (!servers_json.is_object()) {
            return std::unexpected(make_app_error("client mcp server group must be an object"));
        }

        for (const auto& [server_name, config] : servers_json.items()) {
            if (!config.is_object()) {
                return std::unexpected(make_app_error("client mcp server config must be an object", server_name));
            }

            McpServerDefinition server;
            server.id = server_name;
            server.name = server_name;
            server.display_name = server_name;

            if (config.contains("name")) {
                if (!config.at("name").is_string()) {
                    return std::unexpected(make_app_error("client mcp server name must be a string", server_name));
                }
                server.display_name = config.at("name").get<std::string>();
            }
            if (config.contains("displayName")) {
                if (!config.at("displayName").is_string()) {
                    return std::unexpected(
                            make_app_error("client mcp server displayName must be a string", server_name));
                }
                server.display_name = config.at("displayName").get<std::string>();
            }
            if (config.contains("description")) {
                if (!config.at("description").is_string()) {
                    return std::unexpected(
                            make_app_error("client mcp server description must be a string", server_name));
                }
                server.description = config.at("description").get<std::string>();
            }

            std::string type = "stdio";
            if (config.contains("type")) {
                if (!config.at("type").is_string()) {
                    return std::unexpected(make_app_error("client mcp server type must be a string", server_name));
                }
                type = config.at("type").get<std::string>();
            } else if (config.contains("url")) {
                type = "http";
            }

            if (type == "http" || type == "streamable_http" || type == "streamable-http") {
                server.transport = McpServerTransportKind::streamable_http;
                if (!config.contains("url") || !config.at("url").is_string()) {
                    return std::unexpected(make_app_error("http mcp server requires a url", server_name));
                }
                server.http.url = config.at("url").get<std::string>();
                if (config.contains("headers")) {
                    const auto headers = string_map_from_json(config.at("headers"), "http headers");
                    if (!headers) {
                        return std::unexpected(headers.error());
                    }
                    server.http.headers = *headers;
                }
            } else if (type == "sse") {
                server.transport = McpServerTransportKind::legacy_sse;
                if (!config.contains("url") || !config.at("url").is_string()) {
                    return std::unexpected(make_app_error("sse mcp server requires a url", server_name));
                }
                server.http.url = config.at("url").get<std::string>();
                if (config.contains("headers")) {
                    const auto headers = string_map_from_json(config.at("headers"), "http headers");
                    if (!headers) {
                        return std::unexpected(headers.error());
                    }
                    server.http.headers = *headers;
                }
            } else if (type == "stdio") {
                server.transport = McpServerTransportKind::stdio;
                if (!config.contains("command") || !config.at("command").is_string()) {
                    return std::unexpected(make_app_error("stdio mcp server requires a command", server_name));
                }
                server.stdio.command = config.at("command").get<std::string>();
                if (config.contains("args")) {
                    const auto args = string_vector_from_json(config.at("args"), "stdio args");
                    if (!args) {
                        return std::unexpected(args.error());
                    }
                    server.stdio.args = *args;
                }
                if (config.contains("cwd")) {
                    if (!config.at("cwd").is_string()) {
                        return std::unexpected(make_app_error("stdio cwd must be a string", server_name));
                    }
                    server.stdio.cwd = config.at("cwd").get<std::string>();
                }
                if (config.contains("env")) {
                    const auto env = string_map_from_json(config.at("env"), "stdio env");
                    if (!env) {
                        return std::unexpected(env.error());
                    }
                    server.stdio.env = *env;
                }
            } else {
                return std::unexpected(make_app_error("unknown client mcp server type", type));
            }

            servers.push_back(std::move(server));
        }

        return core::Unit{};
    };

    std::vector<McpServerDefinition> servers;
    if (json.contains("mcpServers")) {
        const auto imported = import_group(json.at("mcpServers"), servers);
        if (!imported) {
            return std::unexpected(imported.error());
        }
    }
    if (json.contains("servers")) {
        const auto imported = import_group(json.at("servers"), servers);
        if (!imported) {
            return std::unexpected(imported.error());
        }
    }
    if (servers.empty()) {
        return std::unexpected(make_app_error("client mcp config requires mcpServers or servers"));
    }

    return servers;
}

Json to_json(const McpServerRuntime& runtime) {
    Json json = Json::object();
    json["serverId"] = runtime.server_id;
    json["state"] = to_json(runtime.state);
    json["processId"] = runtime.process_id;
    json["sessionId"] = runtime.session_id;
    json["protocolVersion"] = runtime.protocol_version;
    json["capabilities"] = runtime.capabilities;
    json["lastError"] = runtime.last_error;
    json["logTail"] = runtime.log_tail;
    return json;
}

core::Result<McpServerRuntime> mcp_server_runtime_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("mcp server runtime must be an object"));
    }

    McpServerRuntime runtime;
    if (json.contains("serverId")) {
        if (!json.at("serverId").is_string()) {
            return std::unexpected(make_app_error("runtime serverId must be a string"));
        }
        runtime.server_id = json.at("serverId").get<std::string>();
    }
    if (json.contains("state")) {
        const auto state = mcp_server_runtime_state_from_json(json.at("state"));
        if (!state) {
            return std::unexpected(state.error());
        }
        runtime.state = *state;
    }
    if (json.contains("processId")) {
        if (!json.at("processId").is_number_integer()) {
            return std::unexpected(make_app_error("runtime processId must be an integer"));
        }
        runtime.process_id = json.at("processId").get<std::int64_t>();
    }
    if (json.contains("sessionId")) {
        if (!json.at("sessionId").is_string()) {
            return std::unexpected(make_app_error("runtime sessionId must be a string"));
        }
        runtime.session_id = json.at("sessionId").get<std::string>();
    }
    if (json.contains("protocolVersion")) {
        if (!json.at("protocolVersion").is_string()) {
            return std::unexpected(make_app_error("runtime protocolVersion must be a string"));
        }
        runtime.protocol_version = json.at("protocolVersion").get<std::string>();
    }
    if (json.contains("capabilities")) {
        runtime.capabilities = json.at("capabilities");
    }
    if (json.contains("lastError")) {
        if (!json.at("lastError").is_string()) {
            return std::unexpected(make_app_error("runtime lastError must be a string"));
        }
        runtime.last_error = json.at("lastError").get<std::string>();
    }
    if (json.contains("logTail")) {
        if (!json.at("logTail").is_string()) {
            return std::unexpected(make_app_error("runtime logTail must be a string"));
        }
        runtime.log_tail = json.at("logTail").get<std::string>();
    }
    return runtime;
}

Json to_json(const DiscoveredCapability& capability) {
    Json json = Json::object();
    json["id"] = capability.id;
    json["kind"] = to_json(capability.kind);
    json["serverId"] = capability.server_id;
    json["upstreamName"] = capability.upstream_name;
    json["exposedName"] = capability.exposed_name;
    json["title"] = capability.title;
    json["description"] = capability.description;
    json["uri"] = capability.uri;
    json["inputSchema"] = capability.input_schema;
    json["outputSchema"] = capability.output_schema;
    json["templateText"] = capability.template_text;
    json["capabilityHash"] = capability.capability_hash;
    return json;
}

core::Result<DiscoveredCapability> discovered_capability_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("discovered capability must be an object"));
    }

    DiscoveredCapability capability;
    if (!json.contains("id") || !json.at("id").is_string()) {
        return std::unexpected(make_app_error("discovered capability requires an id"));
    }
    capability.id = json.at("id").get<std::string>();
    if (json.contains("kind")) {
        const auto kind = capability_kind_from_json(json.at("kind"));
        if (!kind) {
            return std::unexpected(kind.error());
        }
        capability.kind = *kind;
    }
    const auto read_string = [&](std::string_view key, std::string& out) -> core::Result<core::Unit> {
        const auto key_string = std::string(key);
        if (!json.contains(key_string)) {
            return core::Unit{};
        }
        if (!json.at(key_string).is_string()) {
            return std::unexpected(make_app_error("capability " + key_string + " must be a string"));
        }
        out = json.at(key_string).get<std::string>();
        return core::Unit{};
    };
    for (auto [key, out] : std::initializer_list<std::pair<std::string_view, std::string*>>{
             {"serverId", &capability.server_id},
             {"upstreamName", &capability.upstream_name},
             {"exposedName", &capability.exposed_name},
             {"title", &capability.title},
             {"description", &capability.description},
             {"uri", &capability.uri},
             {"templateText", &capability.template_text},
             {"capabilityHash", &capability.capability_hash},
         }) {
        const auto read = read_string(key, *out);
        if (!read) {
            return std::unexpected(read.error());
        }
    }
    if (json.contains("inputSchema")) {
        capability.input_schema = json.at("inputSchema");
    }
    if (json.contains("outputSchema")) {
        capability.output_schema = json.at("outputSchema");
    }
    return capability;
}

Json to_json(const CapabilityBinding& binding) {
    Json json = Json::object();
    json["id"] = binding.id;
    json["serverId"] = binding.server_id;
    json["kind"] = to_json(binding.kind);
    json["upstreamName"] = binding.upstream_name;
    json["exposedName"] = binding.exposed_name;
    json["namespaceStrategy"] = to_json(binding.namespace_strategy);
    json["enabled"] = binding.enabled;
    json["policy"] = to_json(binding.policy);
    return json;
}

core::Result<CapabilityBinding> capability_binding_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("capability binding must be an object"));
    }

    CapabilityBinding binding;
    if (!json.contains("id") || !json.at("id").is_string()) {
        return std::unexpected(make_app_error("capability binding requires an id"));
    }
    binding.id = json.at("id").get<std::string>();
    const auto read_string = [&](std::string_view key, std::string& out) -> core::Result<core::Unit> {
        const auto key_string = std::string(key);
        if (!json.contains(key_string)) {
            return core::Unit{};
        }
        if (!json.at(key_string).is_string()) {
            return std::unexpected(make_app_error("binding " + key_string + " must be a string"));
        }
        out = json.at(key_string).get<std::string>();
        return core::Unit{};
    };
    for (auto [key, out] : std::initializer_list<std::pair<std::string_view, std::string*>>{
             {"serverId", &binding.server_id},
             {"upstreamName", &binding.upstream_name},
             {"exposedName", &binding.exposed_name},
         }) {
        const auto read = read_string(key, *out);
        if (!read) {
            return std::unexpected(read.error());
        }
    }
    if (json.contains("kind")) {
        const auto kind = capability_kind_from_json(json.at("kind"));
        if (!kind) {
            return std::unexpected(kind.error());
        }
        binding.kind = *kind;
    }
    if (json.contains("namespaceStrategy")) {
        const auto strategy = namespace_strategy_from_json(json.at("namespaceStrategy"));
        if (!strategy) {
            return std::unexpected(strategy.error());
        }
        binding.namespace_strategy = *strategy;
    }
    if (json.contains("enabled")) {
        if (!json.at("enabled").is_boolean()) {
            return std::unexpected(make_app_error("binding enabled must be a boolean"));
        }
        binding.enabled = json.at("enabled").get<bool>();
    }
    if (json.contains("policy")) {
        const auto policy = policy_from_json(json.at("policy"));
        if (!policy) {
            return std::unexpected(policy.error());
        }
        binding.policy = *policy;
    }
    return binding;
}

Json to_json(const HostedEndpoint& endpoint) {
    Json json = Json::object();
    json["name"] = endpoint.name;
    json["listenHost"] = endpoint.listen_host;
    json["listenPort"] = endpoint.listen_port;
    json["path"] = endpoint.path;
    json["transport"] = to_json(endpoint.transport);
    return json;
}

core::Result<HostedEndpoint> hosted_endpoint_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("hosted endpoint must be an object"));
    }

    HostedEndpoint endpoint;
    if (json.contains("name")) {
        if (!json.at("name").is_string()) {
            return std::unexpected(make_app_error("endpoint name must be a string"));
        }
        endpoint.name = json.at("name").get<std::string>();
    }
    if (json.contains("listenHost")) {
        if (!json.at("listenHost").is_string()) {
            return std::unexpected(make_app_error("endpoint listenHost must be a string"));
        }
        endpoint.listen_host = json.at("listenHost").get<std::string>();
    }
    if (json.contains("listenPort")) {
        if (!json.at("listenPort").is_number_unsigned()) {
            return std::unexpected(make_app_error("endpoint listenPort must be an unsigned integer"));
        }
        const auto port = json.at("listenPort").get<std::uint64_t>();
        if (port > 65535) {
            return std::unexpected(make_app_error("endpoint listenPort is out of range"));
        }
        endpoint.listen_port = static_cast<std::uint16_t>(port);
    }
    if (json.contains("path")) {
        if (!json.at("path").is_string()) {
            return std::unexpected(make_app_error("endpoint path must be a string"));
        }
        endpoint.path = json.at("path").get<std::string>();
    }
    if (json.contains("transport")) {
        const auto transport = mcp_server_transport_kind_from_json(json.at("transport"));
        if (!transport) {
            return std::unexpected(transport.error());
        }
        endpoint.transport = *transport;
    }
    return endpoint;
}

Json to_json(const ExposureProfile& profile) {
    Json json = Json::object();
    json["id"] = profile.id;
    json["name"] = profile.name;
    json["instructions"] = profile.instructions;
    json["endpoint"] = to_json(profile.endpoint);
    json["bindings"] = capability_bindings_to_json(profile.bindings);
    json["environmentOverrides"] = string_map_to_json(profile.environment_overrides);
    return json;
}

core::Result<ExposureProfile> exposure_profile_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(make_app_error("exposure profile must be an object"));
    }

    ExposureProfile profile;
    if (!json.contains("id") || !json.at("id").is_string()) {
        return std::unexpected(make_app_error("exposure profile requires an id"));
    }
    profile.id = json.at("id").get<std::string>();
    if (json.contains("name")) {
        if (!json.at("name").is_string()) {
            return std::unexpected(make_app_error("exposure profile name must be a string"));
        }
        profile.name = json.at("name").get<std::string>();
    }
    if (json.contains("instructions")) {
        if (!json.at("instructions").is_string()) {
            return std::unexpected(make_app_error("exposure profile instructions must be a string"));
        }
        profile.instructions = json.at("instructions").get<std::string>();
    }
    if (json.contains("endpoint")) {
        const auto endpoint = hosted_endpoint_from_json(json.at("endpoint"));
        if (!endpoint) {
            return std::unexpected(endpoint.error());
        }
        profile.endpoint = *endpoint;
    }
    if (json.contains("bindings")) {
        const auto bindings = capability_bindings_from_json(json.at("bindings"));
        if (!bindings) {
            return std::unexpected(bindings.error());
        }
        profile.bindings = *bindings;
    }
    if (json.contains("environmentOverrides")) {
        const auto environment = string_map_from_json(json.at("environmentOverrides"), "environmentOverrides");
        if (!environment) {
            return std::unexpected(environment.error());
        }
        profile.environment_overrides = *environment;
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
