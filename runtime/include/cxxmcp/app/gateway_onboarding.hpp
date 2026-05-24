#pragma once

#include "cxxmcp/app/exposure_management.hpp"
#include "cxxmcp/app/mcp_server.hpp"
#include "cxxmcp/app/serialization.hpp"
#include "cxxmcp/app/server_management.hpp"
#include "cxxmcp/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mcp::app {

    struct GatewayProfileInitReport {
        std::string profile_id;
        std::string server_id;
        std::uint16_t port = 0;
        std::string path;
        std::string url;
        bool created = false;
        bool initialized = false;
        std::size_t bound_capability_count = 0;
        std::string skipped_reason;
    };

    struct GatewayBatchInitResult {
        std::vector<GatewayProfileInitReport> reports;
        std::size_t initialized_count = 0;
        std::size_t skipped_count = 0;
    };

    struct GatewayConfigImportResult {
        std::vector<McpServerDefinition> imported_servers;
        bool trusted = false;
        std::size_t trusted_server_count = 0;
        bool discovered = false;
        std::size_t discovered_capability_count = 0;
        std::vector<ServerDiscoveryReport> discovery_reports;
        GatewayBatchInitResult initialization;
    };

    class GatewayOnboardingService final {
    public:
        GatewayOnboardingService(McpServerStore &servers,
                                 CapabilityCatalog &capabilities,
                                 ExposureProfileStore &profiles,
                                 ExposureManagementService &exposure_management);

        core::Result<GatewayBatchInitResult> initialize_all_http_profiles(std::string_view host,
                                                                          std::uint16_t base_port,
                                                                          std::string_view path_prefix = "/mcp",
                                                                          std::string_view profile_prefix = "profile.",
                                                                          std::string_view instructions = {});

    private:
        McpServerStore &servers_;
        CapabilityCatalog &capabilities_;
        ExposureProfileStore &profiles_;
        ExposureManagementService &exposure_management_;
    };

    class GatewayConfigImportService final {
    public:
        GatewayConfigImportService(ServerManagementService &server_management,
                                   McpServerStore &servers,
                                   CapabilityCatalog &capabilities,
                                   ExposureProfileStore &profiles,
                                   ExposureManagementService &exposure_management);

        core::Result<GatewayConfigImportResult> import_and_initialize(const Json &client_config,
                                                                      bool discover,
                                                                      bool trust,
                                                                      std::string_view host,
                                                                      std::uint16_t base_port,
                                                                      std::string_view path_prefix = "/mcp",
                                                                      std::string_view profile_prefix = "profile.",
                                                                      std::string_view instructions = {});

    private:
        ServerManagementService &server_management_;
        McpServerStore &servers_;
        CapabilityCatalog &capabilities_;
        ExposureProfileStore &profiles_;
        ExposureManagementService &exposure_management_;
    };

}// namespace mcp::app
