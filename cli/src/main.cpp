#include "mcp/app/client_discovery.hpp"
#include "mcp/app/file_stores.hpp"
#include "mcp/app/services.hpp"
#include "mcp/cli/commands.hpp"
#include "mcp/cli/runtime.hpp"

#include <CLI/CLI.hpp>
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

std::string executable_path(int argc, char** argv) {
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr || std::string_view(argv[0]).empty()) {
        return "cxxmcp";
    }

    const std::filesystem::path path(argv[0]);
    if (path.has_parent_path()) {
        return std::filesystem::absolute(path).string();
    }
    return path.string();
}

} // namespace

int main(int argc, char** argv) {
    mcp::cli::RuntimeOptions runtime_options{
        .state_directory = mcp::cli::default_state_directory(),
    };

    if (argc > 1 && argv != nullptr && argv[1] != nullptr) {
        const std::string_view first_arg(argv[1]);
        if (first_arg == "--help" || first_arg == "-h") {
            mcp::cli::write_usage(std::cout);
            return 0;
        }
        if (first_arg == "--version" || first_arg == "-V") {
            std::cout << "cxxmcp " << MCP_PROJECT_VERSION << '\n';
            return 0;
        }
    }

    std::string state_directory_value = runtime_options.state_directory.string();
    CLI::App cli_app{"cxxmcp command-line interface"};
    cli_app.allow_extras();
    cli_app.set_help_flag("");
    cli_app.set_help_all_flag("");
    cli_app.set_version_flag("");
    cli_app.add_option("--state-dir", state_directory_value, "Use a specific runtime state directory.");
    cli_app.add_flag("--json", runtime_options.json_output, "Write structured JSON for automation-friendly commands.");

    try {
        cli_app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return cli_app.exit(error);
    }

    runtime_options.state_directory = std::filesystem::path(state_directory_value);

    std::vector<std::string_view> args;
    const auto extra_args = cli_app.remaining(true);
    args.reserve(extra_args.size());
    for (const auto& value : extra_args) {
        args.emplace_back(value);
    }

    const auto& state = runtime_options.state_directory;

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
        .json_output = runtime_options.json_output,
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
