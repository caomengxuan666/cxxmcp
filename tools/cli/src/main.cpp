#include "cxxmcp/app/client_discovery.hpp"
#include "cxxmcp/app/file_stores.hpp"
#include "cxxmcp/app/services.hpp"
#include "cxxmcp/cli/commands.hpp"
#include "cxxmcp/cli/runtime.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

    mcp::app::Profile default_profile() {
        return mcp::app::Profile{
                .id = "default",
                .name = "Default",
                .endpoints = {},
                .enabled_tool_ids = {},
                .environment = {},
        };
    }

    std::string executable_path(int argc, char **argv) {
        if (argc <= 0 || argv == nullptr || argv[0] == nullptr || std::string_view(argv[0]).empty()) {
            return "cxxmcp";
        }

        const std::filesystem::path path(argv[0]);
        if (path.has_parent_path()) {
            return std::filesystem::absolute(path).string();
        }
        return path.string();
    }

}// namespace

int main(int argc, char **argv) {
    std::vector<std::string_view> args;
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }

    const auto runtime_options = mcp::cli::parse_runtime_options(args);
    if (!runtime_options) {
        std::cerr << runtime_options.error().message;
        if (!runtime_options.error().detail.empty()) {
            std::cerr << ": " << runtime_options.error().detail;
        }
        std::cerr << '\n';
        mcp::cli::write_usage(std::cerr);
        return runtime_options.error().code;
    }

    if (runtime_options->show_help) {
        mcp::cli::write_usage(std::cout);
        return 0;
    }
    if (runtime_options->show_version) {
        std::cout << "cxxmcp " << MCP_PROJECT_VERSION << '\n';
        return 0;
    }

    const auto &state = runtime_options->state_directory;

    mcp::app::MemoryToolCatalog tools;
    mcp::app::MemoryProfileStore profiles({default_profile()});
    mcp::app::JsonImportExportService bundles;
    mcp::app::JsonMcpServerStore servers(state / "servers.json");
    mcp::app::JsonCapabilityCatalog capabilities(state / "capabilities.json");
    mcp::app::JsonExposureProfileStore exposure_profiles(state / "exposure_profiles.json");

    mcp::app::ToolManagementService management(tools, profiles);
    mcp::app::ServerManagementService server_management(
            servers,
            capabilities,
            exposure_profiles,
            mcp::app::make_client_discovery_session_for_server);
    mcp::app::ExposureManagementService exposure_management(exposure_profiles, capabilities);

    mcp::cli::CommandApp app(mcp::cli::CommandServices{
            .management = management,
            .tools = tools,
            .profiles = profiles,
            .bundles = bundles,
            .servers = servers,
            .capabilities = capabilities,
            .exposure_profiles = exposure_profiles,
            .server_management = server_management,
            .exposure_management = exposure_management,
            .executable_path = executable_path(argc, argv),
            .state_directory = state,
            .json_output = runtime_options->json_output,
    });

    const auto result = app.run(args, std::cout, std::cerr);
    if (!result) {
        std::cerr << result.error().message;
        if (!result.error().detail.empty()) {
            std::cerr << ": " << result.error().detail;
        }
        std::cerr << '\n';
        return result.error().code;
    }
    return *result;
}
