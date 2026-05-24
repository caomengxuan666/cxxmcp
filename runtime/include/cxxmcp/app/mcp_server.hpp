#pragma once

#include "cxxmcp/app/policy.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcp::app {

    enum class McpServerTransportKind {
        stdio,
        streamable_http,
        legacy_sse,
    };

    enum class McpServerTrustState {
        untrusted,
        trusted,
        blocked,
    };

    enum class McpServerRuntimeState {
        stopped,
        starting,
        initializing,
        running,
        degraded,
        failed,
    };

    enum class CapabilityKind {
        tool,
        prompt,
        resource,
    };

    enum class NamespaceStrategy {
        none,
        server_prefix,
        custom,
    };

    struct StdioLaunchConfig {
        std::string command;
        std::vector<std::string> args;
        std::string cwd;
        std::unordered_map<std::string, std::string> env;
    };

    struct HttpConnectionConfig {
        std::string url;
        std::unordered_map<std::string, std::string> headers;
    };

    struct McpServerDefinition {
        std::string id;
        std::string name;
        std::string display_name;
        std::string description;
        McpServerTransportKind transport = McpServerTransportKind::stdio;
        StdioLaunchConfig stdio;
        HttpConnectionConfig http;
        bool enabled = true;
        bool auto_start = true;
        McpServerTrustState trust = McpServerTrustState::untrusted;
        std::vector<std::string> tags;
    };

    struct McpServerRuntime {
        std::string server_id;
        McpServerRuntimeState state = McpServerRuntimeState::stopped;
        std::int64_t process_id = 0;
        std::string session_id;
        std::string protocol_version;
        protocol::Json capabilities = protocol::Json::object();
        std::string last_error;
        std::string log_tail;
    };

    struct DiscoveredCapability {
        std::string id;
        CapabilityKind kind = CapabilityKind::tool;
        std::string server_id;
        std::string upstream_name;
        std::string exposed_name;
        std::string title;
        std::string description;
        std::string uri;
        protocol::Json input_schema = protocol::Json::object();
        protocol::Json output_schema = protocol::Json::object();
        std::string template_text;
        std::string capability_hash;
    };

    struct CapabilityBinding {
        std::string id;
        std::string server_id;
        CapabilityKind kind = CapabilityKind::tool;
        std::string upstream_name;
        std::string exposed_name;
        NamespaceStrategy namespace_strategy = NamespaceStrategy::server_prefix;
        bool enabled = true;
        Policy policy;
    };

    struct HostedEndpoint {
        std::string name;
        std::string listen_host = "127.0.0.1";
        std::uint16_t listen_port = 0;
        std::string path = "/mcp";
        McpServerTransportKind transport = McpServerTransportKind::streamable_http;
    };

    struct ExposureProfile {
        std::string id;
        std::string name;
        std::string instructions;
        HostedEndpoint endpoint;
        std::vector<CapabilityBinding> bindings;
        std::unordered_map<std::string, std::string> environment_overrides;
    };

    class McpServerStore {
    public:
        virtual ~McpServerStore() = default;
        virtual std::vector<McpServerDefinition> list_servers() const = 0;
        virtual core::Result<core::Unit> save(McpServerDefinition server) = 0;
        virtual core::Result<core::Unit> remove(std::string_view server_id) = 0;
    };

    class CapabilityCatalog {
    public:
        virtual ~CapabilityCatalog() = default;
        virtual std::vector<DiscoveredCapability> list_capabilities() const = 0;
        virtual core::Result<core::Unit> replace_for_server(std::string server_id,
                                                            std::vector<DiscoveredCapability> capabilities) = 0;
    };

    class ExposureProfileStore {
    public:
        virtual ~ExposureProfileStore() = default;
        virtual std::vector<ExposureProfile> list_exposure_profiles() const = 0;
        virtual core::Result<core::Unit> save(ExposureProfile profile) = 0;
        virtual core::Result<core::Unit> remove(std::string_view profile_id) = 0;
    };

}// namespace mcp::app
