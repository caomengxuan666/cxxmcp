#pragma once

#include "cxxmcp/app/exposure_management.hpp"
#include "cxxmcp/app/import_export.hpp"
#include "cxxmcp/app/mcp_server.hpp"
#include "cxxmcp/app/profile.hpp"
#include "cxxmcp/app/server_management.hpp"
#include "cxxmcp/app/tool_catalog.hpp"
#include "cxxmcp/app/tool_management.hpp"
#include "cxxmcp/core/result.hpp"

#include <filesystem>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <tcb/span.hpp>

namespace mcp::cli {

    void write_usage(std::ostream &out);

    struct CommandServices {
        app::ToolManagementService &management;
        app::ToolCatalog &tools;
        app::ProfileStore &profiles;
        app::ImportExportService &bundles;
        app::McpServerStore &servers;
        app::CapabilityCatalog &capabilities;
        app::ExposureProfileStore &exposure_profiles;
        app::ServerManagementService &server_management;
        app::ExposureManagementService &exposure_management;
        std::string executable_path = "mcp";
        std::filesystem::path state_directory = ".mcp-runtime";
        bool json_output = false;
    };

    class CommandApp {
    public:
        explicit CommandApp(CommandServices services);

        core::Result<int> run(tcb::span<const std::string_view> args, std::ostream &out, std::ostream &err);
        core::Result<int> run(tcb::span<const std::string_view> args,
                              std::istream &in,
                              std::ostream &out,
                              std::ostream &err);

    private:
        CommandServices services_;
    };

}// namespace mcp::cli
