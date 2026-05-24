#pragma once

#include "cxxmcp/app/mcp_server.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/tool.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mcp::app {

    class McpDiscoverySession {
    public:
        virtual ~McpDiscoverySession() = default;
        virtual core::Result<core::Unit> initialize() = 0;
        virtual core::Result<std::vector<protocol::ToolDefinition>> discover_tools() = 0;
        virtual core::Result<std::vector<protocol::Prompt>> discover_prompts() = 0;
        virtual core::Result<std::vector<protocol::Resource>> discover_resources() = 0;
    };

    using McpDiscoverySessionFactory = std::function<core::Result<std::unique_ptr<McpDiscoverySession>>(
            const McpServerDefinition &server)>;

    struct DiscoveryResult {
        std::string server_id;
        std::size_t capability_count = 0;
    };

    struct ServerDiscoveryReport {
        std::string server_id;
        bool discovered = false;
        std::size_t capability_count = 0;
        std::string error_message;
        std::string error_detail;
    };

    struct ServerHealthReport {
        std::string server_id;
        bool ready = false;
        std::size_t capability_count = 0;
        std::string error_message;
        std::string error_detail;
    };

    class ServerManagementService final {
    public:
        ServerManagementService(McpServerStore &servers,
                                CapabilityCatalog &capabilities,
                                ExposureProfileStore &exposure_profiles,
                                McpDiscoverySessionFactory session_factory);

        core::Result<McpServerDefinition> get_server(std::string_view server_id) const;
        core::Result<core::Unit> save_server(McpServerDefinition server);
        core::Result<core::Unit> remove_server(std::string_view server_id);
        core::Result<McpServerDefinition> set_server_enabled(std::string_view server_id, bool enabled);
        core::Result<McpServerDefinition> set_server_trust(std::string_view server_id, McpServerTrustState trust);
        core::Result<McpServerDefinition> set_stdio_cwd(std::string_view server_id, std::string_view cwd);
        core::Result<McpServerDefinition> set_stdio_env(std::string_view server_id,
                                                        std::string_view name,
                                                        std::string_view value);
        core::Result<McpServerDefinition> unset_stdio_env(std::string_view server_id, std::string_view name);
        core::Result<McpServerDefinition> set_http_header(std::string_view server_id,
                                                          std::string_view name,
                                                          std::string_view value);
        core::Result<McpServerDefinition> unset_http_header(std::string_view server_id, std::string_view name);
        core::Result<DiscoveryResult> discover_server(std::string_view server_id);
        core::Result<ServerHealthReport> check_server(std::string_view server_id);
        std::vector<ServerDiscoveryReport> discover_all_servers();
        std::vector<ServerHealthReport> check_all_servers();

    private:
        McpServerStore &servers_;
        CapabilityCatalog &capabilities_;
        ExposureProfileStore &exposure_profiles_;
        McpDiscoverySessionFactory session_factory_;
    };

}// namespace mcp::app
