#include "mcp/cli/commands.hpp"

#include "mcp/app/client_config.hpp"
#include "mcp/app/gateway_onboarding.hpp"
#include "mcp/app/gateway_runtime.hpp"
#include "mcp/app/serialization.hpp"
#include "mcp/protocol/prompt.hpp"
#include "mcp/protocol/resource.hpp"
#include "mcp/protocol/tool.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mcp::cli {

namespace {

enum class CommandKind {
    help,
    tools_help,
    profiles_help,
    bundle_help,
    servers_help,
    capabilities_help,
    exposures_help,
    gateway_help,
    doctor,
    list_tools,
    enable_tool,
    disable_tool,
    list_profiles,
    import_bundle,
    export_bundle,
    list_servers,
    inspect_server,
    add_stdio_server,
    add_http_server,
    import_servers,
    discover_server,
    discover_all_servers,
    check_server,
    check_all_servers,
    remove_server,
    set_server_enabled,
    set_server_trust,
    set_server_cwd,
    set_server_env,
    unset_server_env,
    set_server_header,
    unset_server_header,
    list_capabilities,
    inspect_capability,
    list_exposure_profiles,
    inspect_exposure_profile,
    create_exposure_profile,
    remove_exposure_profile,
    configure_exposure_endpoint,
    set_exposure_instructions,
    clear_exposure_instructions,
    bind_exposure_capability,
    bind_exposure_server,
    set_exposure_binding_enabled,
    unbind_exposure_capability,
    prune_exposure_bindings,
    init_gateway,
    init_stdio_gateway,
    init_http_gateway,
    init_all_gateways,
    import_gateway_config,
    list_gateway_profiles,
    inspect_gateway_profile,
    gateway_status,
    gateway_client_config,
    gateway_all_client_configs,
    gateway_client_config_stdio,
    check_gateway,
    check_all_gateways,
    preview_gateway,
    serve_gateway_stdio,
    serve_gateway_http,
    serve_all_gateways_http,
};

struct ParsedCommand {
    CommandKind kind = CommandKind::help;
    std::vector<std::string_view> values;
    std::vector<std::string_view> options;
};

core::Error make_cli_error(std::string message, std::string detail = {}) {
    return core::Error{2, std::move(message), std::move(detail)};
}

app::GatewayServerHealthProvider gateway_health_provider(app::ServerManagementService& management);

void write_command_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  cxxmcp [--help] [--json] [--state-dir <path>] [--version] <command>\n"
        << "  cxxmcp doctor\n"
        << "  cxxmcp tools list\n"
        << "  cxxmcp tools enable <tool-id>\n"
        << "  cxxmcp tools disable <tool-id>\n"
        << "  cxxmcp profiles list\n"
        << "  cxxmcp servers list\n"
        << "  cxxmcp servers inspect <server-id>\n"
        << "  cxxmcp servers add-stdio [--trust] [--discover] [--cwd <cwd>] [--env <name> <value>]... <server-id> <command> [args...]\n"
        << "  cxxmcp servers add-http [--trust] [--discover] [--header <name> <value>]... <server-id> <url>\n"
        << "  cxxmcp servers import [--trust] [--discover] <path>\n"
        << "  cxxmcp servers discover <server-id>\n"
        << "  cxxmcp servers discover-all\n"
        << "  cxxmcp servers check <server-id>\n"
        << "  cxxmcp servers check-all\n"
        << "  cxxmcp servers remove <server-id>\n"
        << "  cxxmcp servers enable <server-id>\n"
        << "  cxxmcp servers disable <server-id>\n"
        << "  cxxmcp servers trust <server-id>\n"
        << "  cxxmcp servers untrust <server-id>\n"
        << "  cxxmcp servers block <server-id>\n"
        << "  cxxmcp servers set-cwd <server-id> <cwd>\n"
        << "  cxxmcp servers set-env <server-id> <name> <value>\n"
        << "  cxxmcp servers unset-env <server-id> <name>\n"
        << "  cxxmcp servers set-header <server-id> <name> <value>\n"
        << "  cxxmcp servers unset-header <server-id> <name>\n"
        << "  cxxmcp capabilities list\n"
        << "  cxxmcp capabilities inspect <capability-id>\n"
        << "  cxxmcp exposures list\n"
        << "  cxxmcp exposures inspect <profile-id>\n"
        << "  cxxmcp exposures create <profile-id> <name>\n"
        << "  cxxmcp exposures remove <profile-id>\n"
        << "  cxxmcp exposures endpoint <profile-id> <host> <port> [path]\n"
        << "  cxxmcp exposures set-instructions <profile-id> <text>\n"
        << "  cxxmcp exposures clear-instructions <profile-id>\n"
        << "  cxxmcp exposures bind <profile-id> <capability-id> [exposed-name]\n"
        << "  cxxmcp exposures bind-server <profile-id> <server-id>\n"
        << "  cxxmcp exposures enable <profile-id> <capability-id>\n"
        << "  cxxmcp exposures disable <profile-id> <capability-id>\n"
        << "  cxxmcp exposures unbind <profile-id> <capability-id>\n"
        << "  cxxmcp exposures prune <profile-id>\n"
        << "  cxxmcp gateway init [--trust] [--discover] [--instructions <text>] <profile-id> <server-id> <host> <port> [path]\n"
        << "  cxxmcp gateway init-stdio [--trust] [--discover] [--path <path>] [--instructions <text>] <profile-id> <server-id> <host> <port> <command> [args...]\n"
        << "  cxxmcp gateway init-http [--trust] [--discover] [--path <path>] [--instructions <text>] [--header <name> <value>]... <profile-id> <server-id> <host> <port> <url>\n"
        << "  cxxmcp gateway init-all [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <host> <base-port> [path-prefix]\n"
        << "  cxxmcp gateway import-config [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <config-path> <host> <base-port> [path-prefix]\n"
        << "  cxxmcp gateway list\n"
        << "  cxxmcp gateway inspect <profile-id>\n"
        << "  cxxmcp gateway status\n"
        << "  cxxmcp gateway client-config <profile-id> [server-name]\n"
        << "  cxxmcp gateway client-config-all [--ready-only] [server-name-prefix]\n"
        << "  cxxmcp gateway client-config-stdio <profile-id> [server-name]\n"
        << "  cxxmcp gateway check <profile-id>\n"
        << "  cxxmcp gateway check-all\n"
        << "  cxxmcp gateway preview <profile-id>\n"
        << "  cxxmcp gateway serve-stdio <profile-id>\n"
        << "  cxxmcp gateway serve-http <profile-id>\n"
        << "  cxxmcp gateway serve-all [--ready-only]\n"
        << "  cxxmcp bundle import <path>\n"
        << "  cxxmcp bundle export <path> [profile-id]\n"
        << "\n"
        << "Global options:\n"
        << "  --json                 Write structured JSON for automation-friendly commands.\n"
        << "  --state-dir <path>     Use a specific runtime state directory.\n"
        << "  --version, -V          Print the CLI version.\n"
        << "\n"
        << "Common workflows:\n"
        << "  cxxmcp gateway init-stdio --trust --discover --path /mcp/files --instructions \"Use reviewed file tools only.\" profile.fs filesystem 127.0.0.1 39931 node server.js\n"
        << "  cxxmcp gateway init-http --trust --discover --path /mcp/remote --instructions \"Use reviewed remote tools only.\" --header Authorization \"Bearer token\" profile.remote remote 127.0.0.1 39932 http://127.0.0.1:3000/mcp\n"
        << "  cxxmcp gateway init-all --trust --discover --instructions \"Use imported tools only.\" 127.0.0.1 39940 /mcp/imported\n"
        << "  cxxmcp gateway import-config --trust --discover --instructions \"Use imported tools only.\" ./client-mcp-config.json 127.0.0.1 39940 /mcp/imported\n"
        << "  cxxmcp gateway check profile.fs\n"
        << "  cxxmcp gateway client-config profile.fs fs-gateway\n"
        << "  cxxmcp gateway serve-http profile.fs\n"
        << "  cxxmcp gateway status\n"
        << "  cxxmcp gateway client-config-all --ready-only local\n"
        << "  cxxmcp gateway serve-all --ready-only\n"
        << "\n"
        << "Use scoped help for details: cxxmcp gateway help, cxxmcp servers help, cxxmcp exposures help.\n";
}

void write_tools_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  cxxmcp tools list\n"
        << "  cxxmcp tools enable <tool-id>\n"
        << "  cxxmcp tools disable <tool-id>\n";
}

void write_profiles_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  cxxmcp profiles list\n";
}

void write_bundle_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  cxxmcp bundle import <path>\n"
        << "  cxxmcp bundle export <path> [profile-id]\n";
}

void write_servers_usage(std::ostream& out) {
    out << "Manage upstream MCP servers. Servers must be trusted before discovery or gateway routing.\n"
        << "\n"
        << "Usage:\n"
        << "  cxxmcp servers list\n"
        << "  cxxmcp servers inspect <server-id>\n"
        << "  cxxmcp servers add-stdio [--trust] [--discover] [--cwd <cwd>] [--env <name> <value>]... <server-id> <command> [args...]\n"
        << "  cxxmcp servers add-http [--trust] [--discover] [--header <name> <value>]... <server-id> <url>\n"
        << "  cxxmcp servers import [--trust] [--discover] <path>\n"
        << "  cxxmcp servers discover <server-id>\n"
        << "  cxxmcp servers discover-all\n"
        << "  cxxmcp servers check <server-id>\n"
        << "  cxxmcp servers check-all\n"
        << "  cxxmcp servers remove <server-id>\n"
        << "  cxxmcp servers enable <server-id>\n"
        << "  cxxmcp servers disable <server-id>\n"
        << "  cxxmcp servers trust <server-id>\n"
        << "  cxxmcp servers untrust <server-id>\n"
        << "  cxxmcp servers block <server-id>\n"
        << "  cxxmcp servers set-cwd <server-id> <cwd>\n"
        << "  cxxmcp servers set-env <server-id> <name> <value>\n"
        << "  cxxmcp servers unset-env <server-id> <name>\n"
        << "  cxxmcp servers set-header <server-id> <name> <value>\n"
        << "  cxxmcp servers unset-header <server-id> <name>\n"
        << "\n"
        << "Examples:\n"
        << "  cxxmcp servers import --trust --discover ./client-mcp-config.json\n"
        << "  cxxmcp servers add-stdio --trust --discover --cwd C:\\workspace --env API_TOKEN secret filesystem node server.js --root C:\\workspace\n"
        << "  cxxmcp servers add-http --trust --discover --header Authorization \"Bearer token\" remote http://127.0.0.1:3000/mcp\n"
        << "  cxxmcp servers set-header remote X-Request-ID request-id\n"
        << "  cxxmcp servers trust filesystem\n"
        << "  cxxmcp servers discover filesystem\n";
}

void write_capabilities_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  cxxmcp capabilities list\n"
        << "  cxxmcp capabilities inspect <capability-id>\n";
}

void write_exposures_usage(std::ostream& out) {
    out << "Manage gateway exposure profiles. A profile chooses endpoint, instructions, and exposed upstream capabilities.\n"
        << "\n"
        << "Usage:\n"
        << "  cxxmcp exposures list\n"
        << "  cxxmcp exposures inspect <profile-id>\n"
        << "  cxxmcp exposures create <profile-id> <name>\n"
        << "  cxxmcp exposures remove <profile-id>\n"
        << "  cxxmcp exposures endpoint <profile-id> <host> <port> [path]\n"
        << "  cxxmcp exposures set-instructions <profile-id> <text>\n"
        << "  cxxmcp exposures clear-instructions <profile-id>\n"
        << "  cxxmcp exposures bind <profile-id> <capability-id> [exposed-name]\n"
        << "  cxxmcp exposures bind-server <profile-id> <server-id>\n"
        << "  cxxmcp exposures enable <profile-id> <capability-id>\n"
        << "  cxxmcp exposures disable <profile-id> <capability-id>\n"
        << "  cxxmcp exposures unbind <profile-id> <capability-id>\n"
        << "  cxxmcp exposures prune <profile-id>\n"
        << "\n"
        << "Examples:\n"
        << "  cxxmcp exposures create profile.dev Dev\n"
        << "  cxxmcp exposures endpoint profile.dev 127.0.0.1 39931 /mcp/dev\n"
        << "  cxxmcp exposures bind-server profile.dev filesystem\n"
        << "  cxxmcp exposures set-instructions profile.dev \"Use reviewed workspace tools only.\"\n"
        << "  cxxmcp gateway check profile.dev\n";
}

void write_gateway_usage(std::ostream& out) {
    out << "Run and package MCP gateway profiles for downstream MCP clients.\n"
        << "\n"
        << "Usage:\n"
        << "  cxxmcp gateway init [--trust] [--discover] [--instructions <text>] <profile-id> <server-id> <host> <port> [path]\n"
        << "  cxxmcp gateway init-stdio [--trust] [--discover] [--path <path>] [--instructions <text>] <profile-id> <server-id> <host> <port> <command> [args...]\n"
        << "  cxxmcp gateway init-http [--trust] [--discover] [--path <path>] [--instructions <text>] [--header <name> <value>]... <profile-id> <server-id> <host> <port> <url>\n"
        << "  cxxmcp gateway init-all [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <host> <base-port> [path-prefix]\n"
        << "  cxxmcp gateway import-config [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <config-path> <host> <base-port> [path-prefix]\n"
        << "  cxxmcp gateway list\n"
        << "  cxxmcp gateway inspect <profile-id>\n"
        << "  cxxmcp gateway status\n"
        << "  cxxmcp gateway client-config <profile-id> [server-name]\n"
        << "  cxxmcp gateway client-config-all [--ready-only] [server-name-prefix]\n"
        << "  cxxmcp gateway client-config-stdio <profile-id> [server-name]\n"
        << "  cxxmcp gateway check <profile-id>\n"
        << "  cxxmcp gateway check-all\n"
        << "  cxxmcp gateway preview <profile-id>\n"
        << "  cxxmcp gateway serve-stdio <profile-id>\n"
        << "  cxxmcp gateway serve-http <profile-id>\n"
        << "  cxxmcp gateway serve-all [--ready-only]\n"
        << "\n"
        << "Fast path:\n"
        << "  cxxmcp gateway init-stdio --trust --discover --path /mcp/files --instructions \"Use reviewed file tools only.\" profile.fs filesystem 127.0.0.1 39931 node server.js\n"
        << "  cxxmcp gateway init-http --trust --discover --path /mcp/remote --instructions \"Use reviewed remote tools only.\" --header Authorization \"Bearer token\" profile.remote remote 127.0.0.1 39932 http://127.0.0.1:3000/mcp\n"
        << "  cxxmcp gateway init-all --trust --discover --instructions \"Use imported tools only.\" 127.0.0.1 39940 /mcp/imported\n"
        << "  cxxmcp gateway import-config --trust --discover --instructions \"Use imported tools only.\" ./client-mcp-config.json 127.0.0.1 39940 /mcp/imported\n"
        << "  cxxmcp gateway check profile.fs\n"
        << "  cxxmcp gateway client-config profile.fs fs-gateway\n"
        << "  cxxmcp gateway serve-http profile.fs\n"
        << "  cxxmcp gateway status\n"
        << "  cxxmcp gateway client-config-all --ready-only local\n"
        << "  cxxmcp gateway serve-all --ready-only\n"
        << "\n"
        << "Use client-config-stdio when the downstream client should start this gateway process itself.\n";
}

bool is_help(std::string_view value) {
    return value == "help" || value == "--help" || value == "-h";
}

class CommandParser {
public:
    core::Result<ParsedCommand> parse(std::span<const std::string_view> args) const {
        if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
            return ParsedCommand{.kind = CommandKind::help};
        }

        if (args[0] == "doctor") {
            if (args.size() == 1) {
                return ParsedCommand{.kind = CommandKind::doctor};
            }
            return std::unexpected(make_cli_error("invalid doctor command"));
        }
        if (args[0] == "tools") {
            return parse_tools(args.subspan(1));
        }
        if (args[0] == "profiles") {
            return parse_profiles(args.subspan(1));
        }
        if (args[0] == "bundle") {
            return parse_bundle(args.subspan(1));
        }
        if (args[0] == "servers" || args[0] == "server") {
            return parse_servers(args.subspan(1));
        }
        if (args[0] == "capabilities" || args[0] == "capability") {
            return parse_capabilities(args.subspan(1));
        }
        if (args[0] == "exposures" || args[0] == "exposure") {
            return parse_exposures(args.subspan(1));
        }
        if (args[0] == "gateway") {
            return parse_gateway(args.subspan(1));
        }

        return std::unexpected(make_cli_error("unknown command", std::string(args[0])));
    }

private:
    core::Result<ParsedCommand> parse_tools(std::span<const std::string_view> args) const {
        if (args.size() == 1 && is_help(args[0])) {
            return ParsedCommand{.kind = CommandKind::tools_help};
        }
        if (args.size() == 1 && args[0] == "list") {
            return ParsedCommand{.kind = CommandKind::list_tools};
        }
        if (args.size() == 2 && args[0] == "enable") {
            return ParsedCommand{.kind = CommandKind::enable_tool, .values = {args[1]}};
        }
        if (args.size() == 2 && args[0] == "disable") {
            return ParsedCommand{.kind = CommandKind::disable_tool, .values = {args[1]}};
        }

        return std::unexpected(make_cli_error("invalid tools command"));
    }

    core::Result<ParsedCommand> parse_profiles(std::span<const std::string_view> args) const {
        if (args.size() == 1 && is_help(args[0])) {
            return ParsedCommand{.kind = CommandKind::profiles_help};
        }
        if (args.size() == 1 && args[0] == "list") {
            return ParsedCommand{.kind = CommandKind::list_profiles};
        }

        return std::unexpected(make_cli_error("invalid profiles command"));
    }

    core::Result<ParsedCommand> parse_bundle(std::span<const std::string_view> args) const {
        if (args.size() == 1 && is_help(args[0])) {
            return ParsedCommand{.kind = CommandKind::bundle_help};
        }
        if (args.size() == 2 && args[0] == "import") {
            return ParsedCommand{.kind = CommandKind::import_bundle, .values = {args[1]}};
        }
        if ((args.size() == 2 || args.size() == 3) && args[0] == "export") {
            ParsedCommand command{.kind = CommandKind::export_bundle, .values = {args[1]}};
            if (args.size() == 3) {
                command.values.push_back(args[2]);
            }
            return command;
        }

        return std::unexpected(make_cli_error("invalid bundle command"));
    }

    core::Result<ParsedCommand> parse_servers(std::span<const std::string_view> args) const {
        if (args.size() == 1 && is_help(args[0])) {
            return ParsedCommand{.kind = CommandKind::servers_help};
        }
        if (args.size() == 1 && args[0] == "list") {
            return ParsedCommand{.kind = CommandKind::list_servers};
        }
        if (args.size() == 2 && args[0] == "inspect") {
            return ParsedCommand{.kind = CommandKind::inspect_server, .values = {args[1]}};
        }
        if (args.size() >= 3 && args[0] == "add-stdio") {
            ParsedCommand command{.kind = CommandKind::add_stdio_server};
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--trust") {
                    command.options.push_back("trust");
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--discover") {
                    command.options.push_back("discover");
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--cwd") {
                    if (values.size() < 2) {
                        return std::unexpected(make_cli_error("invalid servers command", "missing value after --cwd"));
                    }
                    command.options.push_back("cwd");
                    command.options.push_back(values[1]);
                    values = values.subspan(2);
                    continue;
                }
                if (values[0] == "--env") {
                    if (values.size() < 3) {
                        return std::unexpected(
                            make_cli_error("invalid servers command", "expected <name> <value> after --env"));
                    }
                    command.options.push_back("env");
                    command.options.push_back(values[1]);
                    command.options.push_back(values[2]);
                    values = values.subspan(3);
                    continue;
                }
                return std::unexpected(make_cli_error("invalid servers command", std::string(values[0])));
            }
            if (values.size() < 2) {
                return std::unexpected(make_cli_error("invalid servers command"));
            }
            command.values.reserve(values.size());
            for (const auto value : values) {
                command.values.push_back(value);
            }
            return command;
        }
        if (args.size() >= 3 && args[0] == "add-http") {
            ParsedCommand command{.kind = CommandKind::add_http_server};
            std::vector<std::string_view> headers;
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--trust") {
                    command.options.push_back("trust");
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--discover") {
                    command.options.push_back("discover");
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] != "--header") {
                    return std::unexpected(make_cli_error("invalid servers command", std::string(values[0])));
                }
                if (values.size() < 3) {
                    return std::unexpected(
                        make_cli_error("invalid servers command", "expected <name> <value> after --header"));
                }
                headers.push_back(values[1]);
                headers.push_back(values[2]);
                values = values.subspan(3);
            }
            if (values.size() != 2) {
                return std::unexpected(make_cli_error("invalid servers command"));
            }
            command.values = {values[0], values[1]};
            for (const auto header : headers) {
                command.values.push_back(header);
            }
            return command;
        }
        if (args.size() >= 2 && args[0] == "import") {
            ParsedCommand command{.kind = CommandKind::import_servers};
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--trust") {
                    command.options.push_back("trust");
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--discover") {
                    command.options.push_back("discover");
                    values = values.subspan(1);
                    continue;
                }
                return std::unexpected(make_cli_error("invalid servers command", std::string(values[0])));
            }
            if (values.size() != 1) {
                return std::unexpected(make_cli_error("invalid servers command"));
            }
            command.values.push_back(values[0]);
            return command;
        }
        if (args.size() == 2 && args[0] == "discover") {
            return ParsedCommand{.kind = CommandKind::discover_server, .values = {args[1]}};
        }
        if (args.size() == 1 && args[0] == "discover-all") {
            return ParsedCommand{.kind = CommandKind::discover_all_servers};
        }
        if (args.size() == 2 && args[0] == "check") {
            return ParsedCommand{.kind = CommandKind::check_server, .values = {args[1]}};
        }
        if (args.size() == 1 && args[0] == "check-all") {
            return ParsedCommand{.kind = CommandKind::check_all_servers};
        }
        if (args.size() == 2 && args[0] == "remove") {
            return ParsedCommand{.kind = CommandKind::remove_server, .values = {args[1]}};
        }
        if (args.size() == 2 && args[0] == "enable") {
            return ParsedCommand{.kind = CommandKind::set_server_enabled, .values = {args[1], "true"}};
        }
        if (args.size() == 2 && args[0] == "disable") {
            return ParsedCommand{.kind = CommandKind::set_server_enabled, .values = {args[1], "false"}};
        }
        if (args.size() == 2 && args[0] == "trust") {
            return ParsedCommand{.kind = CommandKind::set_server_trust, .values = {args[1], "trusted"}};
        }
        if (args.size() == 2 && args[0] == "untrust") {
            return ParsedCommand{.kind = CommandKind::set_server_trust, .values = {args[1], "untrusted"}};
        }
        if (args.size() == 2 && args[0] == "block") {
            return ParsedCommand{.kind = CommandKind::set_server_trust, .values = {args[1], "blocked"}};
        }
        if (args.size() == 3 && args[0] == "set-cwd") {
            return ParsedCommand{.kind = CommandKind::set_server_cwd, .values = {args[1], args[2]}};
        }
        if (args.size() == 4 && args[0] == "set-env") {
            return ParsedCommand{.kind = CommandKind::set_server_env, .values = {args[1], args[2], args[3]}};
        }
        if (args.size() == 3 && args[0] == "unset-env") {
            return ParsedCommand{.kind = CommandKind::unset_server_env, .values = {args[1], args[2]}};
        }
        if (args.size() == 4 && args[0] == "set-header") {
            return ParsedCommand{.kind = CommandKind::set_server_header, .values = {args[1], args[2], args[3]}};
        }
        if (args.size() == 3 && args[0] == "unset-header") {
            return ParsedCommand{.kind = CommandKind::unset_server_header, .values = {args[1], args[2]}};
        }

        return std::unexpected(make_cli_error("invalid servers command"));
    }

    core::Result<ParsedCommand> parse_capabilities(std::span<const std::string_view> args) const {
        if (args.size() == 1 && is_help(args[0])) {
            return ParsedCommand{.kind = CommandKind::capabilities_help};
        }
        if (args.size() == 1 && args[0] == "list") {
            return ParsedCommand{.kind = CommandKind::list_capabilities};
        }
        if (args.size() == 2 && args[0] == "inspect") {
            return ParsedCommand{.kind = CommandKind::inspect_capability, .values = {args[1]}};
        }

        return std::unexpected(make_cli_error("invalid capabilities command"));
    }

    core::Result<ParsedCommand> parse_exposures(std::span<const std::string_view> args) const {
        if (args.size() == 1 && is_help(args[0])) {
            return ParsedCommand{.kind = CommandKind::exposures_help};
        }
        if (args.size() == 1 && args[0] == "list") {
            return ParsedCommand{.kind = CommandKind::list_exposure_profiles};
        }
        if (args.size() == 2 && args[0] == "inspect") {
            return ParsedCommand{.kind = CommandKind::inspect_exposure_profile, .values = {args[1]}};
        }
        if (args.size() == 3 && args[0] == "create") {
            return ParsedCommand{.kind = CommandKind::create_exposure_profile, .values = {args[1], args[2]}};
        }
        if (args.size() == 2 && args[0] == "remove") {
            return ParsedCommand{.kind = CommandKind::remove_exposure_profile, .values = {args[1]}};
        }
        if ((args.size() == 4 || args.size() == 5) && args[0] == "endpoint") {
            ParsedCommand command{
                .kind = CommandKind::configure_exposure_endpoint,
                .values = {args[1], args[2], args[3]},
            };
            if (args.size() == 5) {
                command.values.push_back(args[4]);
            }
            return command;
        }
        if (args.size() == 3 && args[0] == "set-instructions") {
            return ParsedCommand{.kind = CommandKind::set_exposure_instructions, .values = {args[1], args[2]}};
        }
        if (args.size() == 2 && args[0] == "clear-instructions") {
            return ParsedCommand{.kind = CommandKind::clear_exposure_instructions, .values = {args[1]}};
        }
        if ((args.size() == 3 || args.size() == 4) && args[0] == "bind") {
            ParsedCommand command{.kind = CommandKind::bind_exposure_capability, .values = {args[1], args[2]}};
            if (args.size() == 4) {
                command.values.push_back(args[3]);
            }
            return command;
        }
        if (args.size() == 3 && args[0] == "bind-server") {
            return ParsedCommand{.kind = CommandKind::bind_exposure_server, .values = {args[1], args[2]}};
        }
        if (args.size() == 3 && args[0] == "enable") {
            return ParsedCommand{
                .kind = CommandKind::set_exposure_binding_enabled,
                .values = {args[1], args[2], "true"},
            };
        }
        if (args.size() == 3 && args[0] == "disable") {
            return ParsedCommand{
                .kind = CommandKind::set_exposure_binding_enabled,
                .values = {args[1], args[2], "false"},
            };
        }
        if (args.size() == 3 && args[0] == "unbind") {
            return ParsedCommand{.kind = CommandKind::unbind_exposure_capability, .values = {args[1], args[2]}};
        }
        if (args.size() == 2 && args[0] == "prune") {
            return ParsedCommand{.kind = CommandKind::prune_exposure_bindings, .values = {args[1]}};
        }

        return std::unexpected(make_cli_error("invalid exposures command"));
    }

    core::Result<ParsedCommand> parse_gateway(std::span<const std::string_view> args) const {
        if (args.size() == 1 && is_help(args[0])) {
            return ParsedCommand{.kind = CommandKind::gateway_help};
        }
        if (args.size() >= 1 && args[0] == "init") {
            bool discover = false;
            bool trust = false;
            std::string_view instructions;
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--discover") {
                    discover = true;
                } else if (values[0] == "--trust") {
                    trust = true;
                } else if (values[0] == "--instructions") {
                    if (values.size() < 2) {
                        return std::unexpected(
                            make_cli_error("invalid gateway command", "missing value after --instructions"));
                    }
                    instructions = values[1];
                    values = values.subspan(2);
                    continue;
                } else {
                    return std::unexpected(make_cli_error("invalid gateway command", std::string(values[0])));
                }
                values = values.subspan(1);
            }
            if (values.size() != 4 && values.size() != 5) {
                return std::unexpected(make_cli_error("invalid gateway command"));
            }
            ParsedCommand command{
                .kind = CommandKind::init_gateway,
                .values = {discover ? std::string_view{"true"} : std::string_view{"false"},
                           trust ? std::string_view{"true"} : std::string_view{"false"},
                           values[0],
                           values[1],
                           values[2],
                           values[3]},
            };
            if (values.size() == 5) {
                command.values.push_back(values[4]);
            }
            if (!instructions.empty()) {
                command.options.push_back("instructions");
                command.options.push_back(instructions);
            }
            return command;
        }
        if (args.size() >= 1 && args[0] == "init-stdio") {
            bool discover = false;
            bool trust = false;
            std::string_view path;
            std::string_view instructions;
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--discover") {
                    discover = true;
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--trust") {
                    trust = true;
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--path") {
                    if (values.size() < 2) {
                        return std::unexpected(make_cli_error("invalid gateway command", "missing value after --path"));
                    }
                    path = values[1];
                    values = values.subspan(2);
                    continue;
                }
                if (values[0] == "--instructions") {
                    if (values.size() < 2) {
                        return std::unexpected(
                            make_cli_error("invalid gateway command", "missing value after --instructions"));
                    }
                    instructions = values[1];
                    values = values.subspan(2);
                    continue;
                }
                return std::unexpected(make_cli_error("invalid gateway command", std::string(values[0])));
            }
            if (values.size() < 5) {
                return std::unexpected(make_cli_error("invalid gateway command"));
            }
            ParsedCommand command{
                .kind = CommandKind::init_stdio_gateway,
                .values = {discover ? std::string_view{"true"} : std::string_view{"false"},
                           trust ? std::string_view{"true"} : std::string_view{"false"},
                           path,
                           values[0],
                           values[1],
                           values[2],
                           values[3],
                           values[4]},
            };
            for (const auto value : values.subspan(5)) {
                command.values.push_back(value);
            }
            if (!instructions.empty()) {
                command.options.push_back("instructions");
                command.options.push_back(instructions);
            }
            return command;
        }
        if (args.size() >= 1 && args[0] == "init-http") {
            bool discover = false;
            bool trust = false;
            std::string_view path;
            std::string_view instructions;
            std::vector<std::string_view> headers;
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--discover") {
                    discover = true;
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--trust") {
                    trust = true;
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--path") {
                    if (values.size() < 2) {
                        return std::unexpected(make_cli_error("invalid gateway command", "missing value after --path"));
                    }
                    path = values[1];
                    values = values.subspan(2);
                    continue;
                }
                if (values[0] == "--instructions") {
                    if (values.size() < 2) {
                        return std::unexpected(
                            make_cli_error("invalid gateway command", "missing value after --instructions"));
                    }
                    instructions = values[1];
                    values = values.subspan(2);
                    continue;
                }
                if (values[0] == "--header") {
                    if (values.size() < 3) {
                        return std::unexpected(
                            make_cli_error("invalid gateway command", "expected <name> <value> after --header"));
                    }
                    headers.push_back(values[1]);
                    headers.push_back(values[2]);
                    values = values.subspan(3);
                    continue;
                }
                return std::unexpected(make_cli_error("invalid gateway command", std::string(values[0])));
            }
            if (values.size() != 5) {
                return std::unexpected(make_cli_error("invalid gateway command"));
            }
            ParsedCommand command{
                .kind = CommandKind::init_http_gateway,
                .values = {discover ? std::string_view{"true"} : std::string_view{"false"},
                           trust ? std::string_view{"true"} : std::string_view{"false"},
                           path,
                           values[0],
                           values[1],
                           values[2],
                           values[3],
                           values[4]},
            };
            for (const auto header : headers) {
                command.values.push_back(header);
            }
            if (!instructions.empty()) {
                command.options.push_back("instructions");
                command.options.push_back(instructions);
            }
            return command;
        }
        if (args.size() >= 1 && args[0] == "init-all") {
            bool discover = false;
            bool trust = false;
            std::string_view instructions;
            std::string_view profile_prefix;
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--discover") {
                    discover = true;
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--trust") {
                    trust = true;
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--instructions") {
                    if (values.size() < 2) {
                        return std::unexpected(
                            make_cli_error("invalid gateway command", "missing value after --instructions"));
                    }
                    instructions = values[1];
                    values = values.subspan(2);
                    continue;
                }
                if (values[0] == "--profile-prefix") {
                    if (values.size() < 2) {
                        return std::unexpected(
                            make_cli_error("invalid gateway command", "missing value after --profile-prefix"));
                    }
                    profile_prefix = values[1];
                    values = values.subspan(2);
                    continue;
                }
                return std::unexpected(make_cli_error("invalid gateway command", std::string(values[0])));
            }
            if (values.size() != 2 && values.size() != 3) {
                return std::unexpected(make_cli_error("invalid gateway command"));
            }
            ParsedCommand command{
                .kind = CommandKind::init_all_gateways,
                .values = {discover ? std::string_view{"true"} : std::string_view{"false"},
                           trust ? std::string_view{"true"} : std::string_view{"false"},
                           values[0],
                           values[1]},
            };
            if (values.size() == 3) {
                command.values.push_back(values[2]);
            }
            if (!instructions.empty()) {
                command.options.push_back("instructions");
                command.options.push_back(instructions);
            }
            if (!profile_prefix.empty()) {
                command.options.push_back("profile-prefix");
                command.options.push_back(profile_prefix);
            }
            return command;
        }
        if (args.size() >= 1 && args[0] == "import-config") {
            bool discover = false;
            bool trust = false;
            std::string_view instructions;
            std::string_view profile_prefix;
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--discover") {
                    discover = true;
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--trust") {
                    trust = true;
                    values = values.subspan(1);
                    continue;
                }
                if (values[0] == "--instructions") {
                    if (values.size() < 2) {
                        return std::unexpected(
                            make_cli_error("invalid gateway command", "missing value after --instructions"));
                    }
                    instructions = values[1];
                    values = values.subspan(2);
                    continue;
                }
                if (values[0] == "--profile-prefix") {
                    if (values.size() < 2) {
                        return std::unexpected(
                            make_cli_error("invalid gateway command", "missing value after --profile-prefix"));
                    }
                    profile_prefix = values[1];
                    values = values.subspan(2);
                    continue;
                }
                return std::unexpected(make_cli_error("invalid gateway command", std::string(values[0])));
            }
            if (values.size() != 3 && values.size() != 4) {
                return std::unexpected(make_cli_error("invalid gateway command"));
            }
            ParsedCommand command{
                .kind = CommandKind::import_gateway_config,
                .values = {discover ? std::string_view{"true"} : std::string_view{"false"},
                           trust ? std::string_view{"true"} : std::string_view{"false"},
                           values[0],
                           values[1],
                           values[2]},
            };
            if (values.size() == 4) {
                command.values.push_back(values[3]);
            }
            if (!instructions.empty()) {
                command.options.push_back("instructions");
                command.options.push_back(instructions);
            }
            if (!profile_prefix.empty()) {
                command.options.push_back("profile-prefix");
                command.options.push_back(profile_prefix);
            }
            return command;
        }
        if (args.size() == 1 && args[0] == "list") {
            return ParsedCommand{.kind = CommandKind::list_gateway_profiles};
        }
        if (args.size() == 2 && args[0] == "inspect") {
            return ParsedCommand{.kind = CommandKind::inspect_gateway_profile, .values = {args[1]}};
        }
        if (args.size() == 1 && args[0] == "status") {
            return ParsedCommand{.kind = CommandKind::gateway_status};
        }
        if ((args.size() == 2 || args.size() == 3) && args[0] == "client-config") {
            ParsedCommand command{.kind = CommandKind::gateway_client_config, .values = {args[1]}};
            if (args.size() == 3) {
                command.values.push_back(args[2]);
            }
            return command;
        }
        if (!args.empty() && args[0] == "client-config-all") {
            ParsedCommand command{.kind = CommandKind::gateway_all_client_configs};
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--ready-only") {
                    command.options.push_back("ready-only");
                    values = values.subspan(1);
                    continue;
                }
                return std::unexpected(make_cli_error("invalid gateway command", std::string(values[0])));
            }
            if (values.size() > 1) {
                return std::unexpected(make_cli_error("invalid gateway command"));
            }
            if (values.size() == 1) {
                command.values.push_back(values[0]);
            }
            return command;
        }
        if ((args.size() == 2 || args.size() == 3) && args[0] == "client-config-stdio") {
            ParsedCommand command{.kind = CommandKind::gateway_client_config_stdio, .values = {args[1]}};
            if (args.size() == 3) {
                command.values.push_back(args[2]);
            }
            return command;
        }
        if (args.size() == 2 && args[0] == "check") {
            return ParsedCommand{.kind = CommandKind::check_gateway, .values = {args[1]}};
        }
        if (args.size() == 1 && args[0] == "check-all") {
            return ParsedCommand{.kind = CommandKind::check_all_gateways};
        }
        if (args.size() == 2 && args[0] == "preview") {
            return ParsedCommand{.kind = CommandKind::preview_gateway, .values = {args[1]}};
        }
        if (args.size() == 2 && args[0] == "serve-stdio") {
            return ParsedCommand{.kind = CommandKind::serve_gateway_stdio, .values = {args[1]}};
        }
        if (args.size() == 2 && args[0] == "serve-http") {
            return ParsedCommand{.kind = CommandKind::serve_gateway_http, .values = {args[1]}};
        }
        if (!args.empty() && args[0] == "serve-all") {
            ParsedCommand command{.kind = CommandKind::serve_all_gateways_http};
            auto values = args.subspan(1);
            while (!values.empty() && values[0].starts_with("--")) {
                if (values[0] == "--ready-only") {
                    command.options.push_back("ready-only");
                    values = values.subspan(1);
                    continue;
                }
                return std::unexpected(make_cli_error("invalid gateway command", std::string(values[0])));
            }
            if (!values.empty()) {
                return std::unexpected(make_cli_error("invalid gateway command"));
            }
            return command;
        }

        return std::unexpected(make_cli_error("invalid gateway command"));
    }
};

std::string_view to_string(app::ApprovalState approval) {
    switch (approval) {
    case app::ApprovalState::pending:
        return "pending";
    case app::ApprovalState::approved:
        return "approved";
    case app::ApprovalState::denied:
        return "denied";
    }
    return "pending";
}

std::string_view enabled_label(bool enabled) {
    return enabled ? "enabled" : "disabled";
}

std::string_view to_string(app::McpServerTransportKind transport) {
    switch (transport) {
    case app::McpServerTransportKind::stdio:
        return "stdio";
    case app::McpServerTransportKind::streamable_http:
        return "streamable_http";
    case app::McpServerTransportKind::legacy_sse:
        return "legacy_sse";
    }
    return "stdio";
}

std::string_view to_string(app::McpServerTrustState trust) {
    switch (trust) {
    case app::McpServerTrustState::untrusted:
        return "untrusted";
    case app::McpServerTrustState::trusted:
        return "trusted";
    case app::McpServerTrustState::blocked:
        return "blocked";
    }
    return "untrusted";
}

std::string_view to_string(app::CapabilityKind kind) {
    switch (kind) {
    case app::CapabilityKind::tool:
        return "tool";
    case app::CapabilityKind::prompt:
        return "prompt";
    case app::CapabilityKind::resource:
        return "resource";
    }
    return "tool";
}

void write_service_error(std::ostream& err, const core::Error& error) {
    err << error.message;
    if (!error.detail.empty()) {
        err << ": " << error.detail;
    }
    err << '\n';
}

std::string service_error_suggestion(const core::Error& error) {
    if (error.message == "cxxmcp server is untrusted") {
        return "trust the server with: cxxmcp servers trust " + error.detail;
    }
    if (error.message == "mcp server is disabled") {
        return "enable the server with: cxxmcp servers enable " + error.detail;
    }
    if (error.message == "mcp server is blocked") {
        return "review and trust the server with: cxxmcp servers trust " + error.detail;
    }
    if (error.message == "no capabilities discovered for server") {
        return "discover capabilities with: cxxmcp servers discover " + error.detail;
    }
    return {};
}

void write_service_error_with_hint(std::ostream& err, const core::Error& error) {
    write_service_error(err, error);
    const auto suggestion = service_error_suggestion(error);
    if (!suggestion.empty()) {
        err << "hint: " << suggestion << '\n';
    }
}

template <typename Range, typename ToJson>
void write_json_array(const Range& items, ToJson to_json, std::ostream& out) {
    app::Json json = app::Json::array();
    for (const auto& item : items) {
        json.push_back(to_json(item));
    }
    out << json.dump(2) << '\n';
}

core::Result<app::Profile> select_profile(const std::vector<app::Profile>& profiles, std::string_view profile_id) {
    if (!profile_id.empty()) {
        const auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
            return profile.id == profile_id;
        });
        if (it == profiles.end()) {
            return std::unexpected(make_cli_error("profile not found", std::string(profile_id)));
        }
        return *it;
    }

    if (profiles.empty()) {
        return std::unexpected(make_cli_error("no profiles configured"));
    }
    if (profiles.size() != 1) {
        return std::unexpected(make_cli_error("profile id required when multiple profiles exist"));
    }

    return profiles.front();
}

core::Result<app::ExposureProfile> select_exposure_profile(const std::vector<app::ExposureProfile>& profiles,
                                                           std::string_view profile_id) {
    const auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return profile.id == profile_id;
    });
    if (it == profiles.end()) {
        return std::unexpected(make_cli_error("exposure profile not found", std::string(profile_id)));
    }
    return *it;
}

core::Result<std::uint16_t> parse_port(std::string_view value) {
    unsigned int port = 0;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(first, last, port);
    if (ec != std::errc{} || ptr != last || port == 0 || port > 65535) {
        return std::unexpected(make_cli_error("port must be an integer from 1 to 65535", std::string(value)));
    }
    return static_cast<std::uint16_t>(port);
}

int list_tools(app::ToolCatalog& tools, bool json_output, std::ostream& out) {
    const auto descriptors = tools.list();
    if (json_output) {
        write_json_array(descriptors, [](const auto& tool) { return app::to_json(tool); }, out);
        return 0;
    }

    if (descriptors.empty()) {
        out << "No tools configured\n";
        return 0;
    }

    for (const auto& tool : descriptors) {
        out << tool.id << '\t' << tool.definition.name << '\t' << enabled_label(tool.policy.enabled) << '\t'
            << to_string(tool.policy.approval) << '\t' << tool.profile_id << '\n';
    }
    return 0;
}

core::Result<std::string> select_profile_id(const std::vector<app::Profile>& profiles) {
    if (profiles.empty()) {
        return std::unexpected(make_cli_error("no profiles configured"));
    }

    if (profiles.size() == 1) {
        return profiles.front().id;
    }

    const auto default_profile = std::find_if(profiles.begin(), profiles.end(), [](const auto& profile) {
        return profile.id == "default";
    });
    if (default_profile != profiles.end()) {
        return default_profile->id;
    }

    return std::unexpected(make_cli_error("profile id required when multiple profiles exist"));
}

int set_tool_enabled(app::ToolManagementService& management, app::ProfileStore& profiles, std::string_view tool_id,
                     bool enabled, std::ostream& out, std::ostream& err) {
    const auto profile_id = select_profile_id(profiles.list_profiles());
    if (!profile_id) {
        write_service_error(err, profile_id.error());
        return 1;
    }

    const auto updated = enabled ? management.enable_tool(*profile_id, std::string(tool_id))
                                 : management.disable_tool(*profile_id, std::string(tool_id));
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    out << (enabled ? "Enabled" : "Disabled") << " tool " << tool_id << '\n';
    return 0;
}

int list_profiles(app::ProfileStore& profiles, bool json_output, std::ostream& out) {
    const auto items = profiles.list_profiles();
    if (json_output) {
        write_json_array(items, [](const auto& profile) { return app::to_json(profile); }, out);
        return 0;
    }

    if (items.empty()) {
        out << "No profiles configured\n";
        return 0;
    }

    for (const auto& profile : items) {
        out << profile.id << '\t' << profile.name << '\t' << profile.endpoints.size() << " endpoint(s)\t"
            << profile.enabled_tool_ids.size() << " enabled tool(s)\n";
    }
    return 0;
}

int list_servers(app::McpServerStore& servers, bool json_output, std::ostream& out) {
    const auto items = servers.list_servers();
    if (json_output) {
        write_json_array(items, [](const auto& server) { return app::to_json(server); }, out);
        return 0;
    }

    if (items.empty()) {
        out << "No MCP servers configured\n";
        return 0;
    }

    for (const auto& server : items) {
        out << server.id << '\t' << server.name << '\t' << to_string(server.transport) << '\t'
            << enabled_label(server.enabled) << '\t' << to_string(server.trust) << '\n';
    }
    return 0;
}

int inspect_server(app::ServerManagementService& management,
                   app::CapabilityCatalog& capabilities,
                   app::ExposureProfileStore& exposure_profiles,
                   app::McpServerStore& servers,
                   std::string_view server_id,
                   bool json_output,
                   std::ostream& out,
                   std::ostream& err) {
    const auto server = management.get_server(server_id);
    if (!server) {
        write_service_error(err, server.error());
        return 1;
    }

    const auto capability_items = capabilities.list_capabilities();
    const auto capability_count = static_cast<std::size_t>(
        std::count_if(capability_items.begin(), capability_items.end(), [&](const auto& capability) {
            return capability.server_id == server_id;
        }));

    struct ExposureUsage {
        std::string profile_id;
        bool ready = false;
        std::size_t binding_count = 0;
        std::size_t enabled_binding_count = 0;
    };

    app::GatewayReadinessService readiness(exposure_profiles, capabilities, servers, gateway_health_provider(management));
    std::vector<ExposureUsage> exposure_usages;
    std::size_t exposure_binding_count = 0;
    for (const auto& profile : exposure_profiles.list_exposure_profiles()) {
        std::size_t binding_count = 0;
        std::size_t enabled_binding_count = 0;
        for (const auto& binding : profile.bindings) {
            if (binding.server_id != server_id) {
                continue;
            }
            ++binding_count;
            if (binding.enabled && binding.policy.enabled) {
                ++enabled_binding_count;
            }
        }
        if (binding_count == 0) {
            continue;
        }

        const auto report = readiness.check_profile(profile.id);
        exposure_binding_count += binding_count;
        exposure_usages.push_back(ExposureUsage{
            .profile_id = profile.id,
            .ready = report.ready,
            .binding_count = binding_count,
            .enabled_binding_count = enabled_binding_count,
        });
    }

    if (json_output) {
        auto json = app::to_json(*server);
        json["capabilityCount"] = capability_count;
        json["exposureBindingCount"] = exposure_binding_count;
        json["exposureProfiles"] = app::Json::array();
        for (const auto& usage : exposure_usages) {
            json["exposureProfiles"].push_back(app::Json{
                {"profileId", usage.profile_id},
                {"ready", usage.ready},
                {"bindingCount", usage.binding_count},
                {"enabledBindingCount", usage.enabled_binding_count},
            });
        }
        out << json.dump(2) << '\n';
        return 0;
    }

    out << "id: " << server->id << '\n'
        << "name: " << server->name << '\n'
        << "display_name: " << server->display_name << '\n'
        << "transport: " << to_string(server->transport) << '\n'
        << "enabled: " << enabled_label(server->enabled) << '\n'
        << "trust: " << to_string(server->trust) << '\n';
    if (!server->description.empty()) {
        out << "description: " << server->description << '\n';
    }
    if (!server->stdio.command.empty()) {
        out << "stdio.command: " << server->stdio.command << '\n';
    }
    if (!server->stdio.args.empty()) {
        out << "stdio.args:";
        for (const auto& arg : server->stdio.args) {
            out << ' ' << arg;
        }
        out << '\n';
    }
    if (!server->stdio.cwd.empty()) {
        out << "stdio.cwd: " << server->stdio.cwd << '\n';
    }
    if (!server->stdio.env.empty()) {
        out << "stdio.env:\n";
        for (const auto& [name, value] : server->stdio.env) {
            out << "  " << name << '=' << value << '\n';
        }
    }
    if (!server->http.url.empty()) {
        out << "http.url: " << server->http.url << '\n';
    }
    if (!server->http.headers.empty()) {
        out << "http.headers:\n";
        for (const auto& [name, value] : server->http.headers) {
            out << "  " << name << '=' << value << '\n';
        }
    }
    if (!server->tags.empty()) {
        out << "tags:";
        for (const auto& tag : server->tags) {
            out << ' ' << tag;
        }
        out << '\n';
    }
    out << "capabilities: " << capability_count << " discovered\n"
        << "exposure_bindings: " << exposure_binding_count << '\n';
    for (const auto& usage : exposure_usages) {
        out << "exposure: " << usage.profile_id << '\t' << (usage.ready ? "ready" : "not-ready") << '\t'
            << usage.binding_count << " binding(s)\t" << usage.enabled_binding_count << " enabled binding(s)\n";
    }
    return 0;
}

int write_server_json(const app::McpServerDefinition& server, std::ostream& out) {
    out << app::to_json(server).dump(2) << '\n';
    return 0;
}

bool has_option(std::span<const std::string_view> options, std::string_view name) {
    return std::find(options.begin(), options.end(), name) != options.end();
}

std::string_view option_value(std::span<const std::string_view> options, std::string_view name) {
    for (std::size_t index = 0; index + 1 < options.size(); ++index) {
        if (options[index] == name) {
            return options[index + 1];
        }
    }
    return {};
}

int add_stdio_server(app::ServerManagementService& management,
                     std::span<const std::string_view> values,
                     std::span<const std::string_view> options,
                     bool json_output,
                     std::ostream& out,
                     std::ostream& err) {
    app::StdioLaunchConfig stdio{
        .command = std::string(values[1]),
    };
    std::vector<std::string> args;
    args.reserve(values.size() > 2 ? values.size() - 2 : 0);
    for (const auto arg : values.subspan(2)) {
        args.emplace_back(arg);
    }
    stdio.args = std::move(args);

    for (std::size_t index = 0; index < options.size();) {
        const auto option = options[index++];
        if (option == "cwd") {
            stdio.cwd = std::string(options[index++]);
            continue;
        }
        if (option == "env") {
            const auto name = std::string(options[index++]);
            const auto value = std::string(options[index++]);
            stdio.env[name] = value;
            continue;
        }
    }

    app::McpServerDefinition server{
        .id = std::string(values[0]),
        .name = std::string(values[0]),
        .display_name = std::string(values[0]),
        .description = {},
        .transport = app::McpServerTransportKind::stdio,
        .stdio = std::move(stdio),
        .enabled = true,
        .auto_start = true,
        .trust = has_option(options, "trust") ? app::McpServerTrustState::trusted
                                              : app::McpServerTrustState::untrusted,
    };

    const auto saved = management.save_server(server);
    if (!saved) {
        write_service_error(err, saved.error());
        return 1;
    }

    std::size_t discovered_capability_count = 0;
    const bool discover = has_option(options, "discover");
    if (discover) {
        const auto discovered = management.discover_server(server.id);
        if (!discovered) {
            write_service_error_with_hint(err, discovered.error());
            return 1;
        }
        discovered_capability_count = discovered->capability_count;
    }

    if (json_output) {
        auto json = app::to_json(server);
        json["discovered"] = discover;
        json["discoveredCapabilityCount"] = discovered_capability_count;
        out << json.dump(2) << '\n';
        return 0;
    }

    out << "Added stdio MCP server " << values[0] << '\n';
    if (discover) {
        out << "Discovered " << discovered_capability_count << " capability(s)\n";
    }
    return 0;
}

int add_http_server(app::ServerManagementService& management,
                    std::span<const std::string_view> values,
                    std::span<const std::string_view> options,
                    bool json_output,
                    std::ostream& out,
                    std::ostream& err) {
    app::HttpConnectionConfig http{
        .url = std::string(values[1]),
    };
    for (std::size_t index = 2; index + 1 < values.size(); index += 2) {
        http.headers[std::string(values[index])] = std::string(values[index + 1]);
    }

    app::McpServerDefinition server{
        .id = std::string(values[0]),
        .name = std::string(values[0]),
        .display_name = std::string(values[0]),
        .description = {},
        .transport = app::McpServerTransportKind::streamable_http,
        .http = std::move(http),
        .enabled = true,
        .auto_start = true,
        .trust = has_option(options, "trust") ? app::McpServerTrustState::trusted
                                              : app::McpServerTrustState::untrusted,
    };

    const auto saved = management.save_server(server);
    if (!saved) {
        write_service_error(err, saved.error());
        return 1;
    }

    std::size_t discovered_capability_count = 0;
    const bool discover = has_option(options, "discover");
    if (discover) {
        const auto discovered = management.discover_server(server.id);
        if (!discovered) {
            write_service_error_with_hint(err, discovered.error());
            return 1;
        }
        discovered_capability_count = discovered->capability_count;
    }

    if (json_output) {
        auto json = app::to_json(server);
        json["discovered"] = discover;
        json["discoveredCapabilityCount"] = discovered_capability_count;
        out << json.dump(2) << '\n';
        return 0;
    }

    out << "Added HTTP MCP server " << values[0] << '\n';
    if (discover) {
        out << "Discovered " << discovered_capability_count << " capability(s)\n";
    }
    return 0;
}

int import_servers(app::ServerManagementService& management,
                   const std::filesystem::path& path,
                   std::span<const std::string_view> options,
                   bool json_output,
                   std::ostream& out,
                   std::ostream& err) {
    std::ifstream input(path);
    if (!input.is_open()) {
        write_service_error(err, make_cli_error("failed to open MCP client config", path.string()));
        return 1;
    }

    app::Json json;
    try {
        input >> json;
    } catch (const std::exception& exception) {
        write_service_error(err, make_cli_error("failed to parse MCP client config", exception.what()));
        return 1;
    }

    const auto servers = app::mcp_server_definitions_from_client_config_json(json);
    if (!servers) {
        write_service_error(err, servers.error());
        return 1;
    }

    const bool trust = has_option(options, "trust");
    const bool discover = has_option(options, "discover");
    std::size_t imported_count = 0;
    std::size_t discovered_capability_count = 0;
    app::Json discovery_reports = app::Json::array();
    std::vector<app::McpServerDefinition> imported_servers;
    imported_servers.reserve(servers->size());
    for (const auto& server : *servers) {
        auto imported_server = server;
        if (trust) {
            imported_server.trust = app::McpServerTrustState::trusted;
        }
        const auto saved = management.save_server(imported_server);
        if (!saved) {
            write_service_error(err, saved.error());
            return 1;
        }
        imported_servers.push_back(std::move(imported_server));
        ++imported_count;
    }

    if (discover) {
        for (const auto& server : imported_servers) {
            const auto discovered = management.discover_server(server.id);
            if (!discovered) {
                write_service_error_with_hint(err, discovered.error());
                return 1;
            }
            discovered_capability_count += discovered->capability_count;
            discovery_reports.push_back(app::Json{
                {"serverId", discovered->server_id},
                {"discovered", true},
                {"capabilityCount", discovered->capability_count},
            });
        }
    }

    if (json_output) {
        app::Json result = app::Json::object();
        result["importedCount"] = imported_count;
        result["trusted"] = trust;
        result["discovered"] = discover;
        result["discoveredCapabilityCount"] = discovered_capability_count;
        result["discoveryReports"] = std::move(discovery_reports);
        result["servers"] = app::Json::array();
        for (const auto& server : imported_servers) {
            result["servers"].push_back(app::to_json(server));
        }
        out << result.dump(2) << '\n';
        return 0;
    }

    out << "Imported " << imported_count << " cxxmcp server(s)\n";
    if (trust) {
        out << "Trust: trusted\n";
    }
    if (discover) {
        out << "Discovered " << discovered_capability_count << " capability(s)\n";
    }
    return 0;
}

int discover_server(app::ServerManagementService& management,
                    std::string_view server_id,
                    bool json_output,
                    std::ostream& out,
                    std::ostream& err) {
    const auto discovered = management.discover_server(server_id);
    if (!discovered) {
        write_service_error_with_hint(err, discovered.error());
        return 1;
    }

    if (json_output) {
        app::Json json = app::Json::object();
        json["serverId"] = discovered->server_id;
        json["discovered"] = true;
        json["capabilityCount"] = discovered->capability_count;
        out << json.dump(2) << '\n';
        return 0;
    }

    out << "Discovered " << discovered->capability_count << " capability(s) from " << discovered->server_id << '\n';
    return 0;
}

app::Json discovery_report_to_json(const app::ServerDiscoveryReport& report) {
    app::Json json = app::Json::object();
    json["serverId"] = report.server_id;
    json["discovered"] = report.discovered;
    json["capabilityCount"] = report.capability_count;
    if (!report.error_message.empty()) {
        json["error"] = report.error_message;
    }
    if (!report.error_detail.empty()) {
        json["detail"] = report.error_detail;
    }
    return json;
}

app::Json server_health_report_to_json(const app::ServerHealthReport& report) {
    app::Json json = app::Json::object();
    json["serverId"] = report.server_id;
    json["ready"] = report.ready;
    json["capabilityCount"] = report.capability_count;
    if (!report.error_message.empty()) {
        json["error"] = report.error_message;
    }
    if (!report.error_detail.empty()) {
        json["detail"] = report.error_detail;
    }
    return json;
}

std::size_t count_ready_servers(const std::vector<app::ServerHealthReport>& reports) {
    return static_cast<std::size_t>(std::count_if(reports.begin(), reports.end(), [](const auto& report) {
        return report.ready;
    }));
}

app::GatewayServerHealthProvider gateway_health_provider(app::ServerManagementService& management) {
    return [&management] {
        const auto reports = management.check_all_servers();
        std::vector<app::GatewayServerHealth> health;
        health.reserve(reports.size());
        for (const auto& report : reports) {
            health.push_back(app::GatewayServerHealth{
                .server_id = report.server_id,
                .ready = report.ready,
                .capability_count = report.capability_count,
                .error_message = report.error_message,
                .error_detail = report.error_detail,
            });
        }
        return health;
    };
}

std::string readiness_issue_suggestion(const app::GatewayReadinessIssue& issue,
                                       std::string_view profile_id = {}) {
    const std::string profile = profile_id.empty() ? std::string("<profile-id>") : std::string(profile_id);
    if (issue.code == "no_servers") {
        return "add one with: cxxmcp servers import <path>; or cxxmcp servers add-stdio [--trust] [--discover] [--cwd <cwd>] [--env <name> <value>]... <server-id> <command> [args...]";
    }
    if (issue.code == "no_capabilities") {
        return "discover capabilities with: cxxmcp servers discover-all";
    }
    if (issue.code == "no_exposure_profiles") {
        return "create one with: cxxmcp exposures create <profile-id> <name>";
    }
    if (issue.code == "endpoint_not_configured") {
        return "configure it with: cxxmcp exposures endpoint " + profile + " 127.0.0.1 <port> /mcp/" +
               profile;
    }
    if (issue.code == "endpoint_transport_unsupported") {
        return "configure an HTTP gateway endpoint with: cxxmcp exposures endpoint " + profile +
               " 127.0.0.1 <port> /mcp/" + profile;
    }
    if (issue.code == "profile_not_found") {
        return "create the profile with: cxxmcp exposures create " + issue.detail + " <name>";
    }
    if (issue.code == "no_enabled_bindings") {
        return "bind capabilities with: cxxmcp exposures bind-server " + issue.detail +
               " <server-id>; or re-enable a binding with: cxxmcp exposures enable " + issue.detail +
               " <capability-id>";
    }
    if (issue.code == "capability_not_found") {
        return "refresh discovery for the owning server with: cxxmcp servers discover <server-id>; or prune stale bindings with: cxxmcp exposures prune " +
               profile;
    }
    if (issue.code == "server_not_found") {
        return "import the server with: cxxmcp servers import <path>; or prune stale bindings with: cxxmcp exposures prune " +
               profile;
    }
    if (issue.code == "server_disabled") {
        return "enable the server with: cxxmcp servers enable " + issue.detail;
    }
    if (issue.code == "server_untrusted") {
        return "trust the server with: cxxmcp servers trust " + issue.detail;
    }
    if (issue.code == "server_blocked") {
        return "review and trust the server with: cxxmcp servers trust " + issue.detail;
    }
    if (issue.code == "server_unready") {
        return "check upstream health with: cxxmcp servers check " + issue.detail;
    }
    return {};
}

void write_readiness_issue(std::ostream& out,
                           const app::GatewayReadinessIssue& issue,
                           std::string_view profile_id = {}) {
    out << "- " << issue.message;
    if (!issue.detail.empty()) {
        out << ": " << issue.detail;
    }
    out << '\n';

    const auto suggestion = readiness_issue_suggestion(issue, profile_id);
    if (!suggestion.empty()) {
        out << "  hint: " << suggestion << '\n';
    }
}

app::Json readiness_report_to_json(const app::GatewayReadinessReport& report) {
    app::Json json = app::Json::object();
    json["profileId"] = report.profile_id;
    json["ready"] = report.ready;
    json["bindingCount"] = report.binding_count;
    json["enabledBindingCount"] = report.enabled_binding_count;
    json["issues"] = app::Json::array();
    for (const auto& issue : report.issues) {
        app::Json issue_json = app::Json::object();
        issue_json["code"] = issue.code;
        issue_json["message"] = issue.message;
        if (!issue.detail.empty()) {
            issue_json["detail"] = issue.detail;
        }
        const auto suggestion = readiness_issue_suggestion(issue, report.profile_id);
        if (!suggestion.empty()) {
            issue_json["suggestion"] = suggestion;
        }
        json["issues"].push_back(std::move(issue_json));
    }
    return json;
}

std::string endpoint_url(const app::HostedEndpoint& endpoint) {
    return "http://" + endpoint.listen_host + ':' + std::to_string(endpoint.listen_port) + endpoint.path;
}

app::Json endpoint_to_json(const app::HostedEndpoint& endpoint) {
    app::Json json = app::Json::object();
    json["host"] = endpoint.listen_host;
    json["port"] = endpoint.listen_port;
    json["path"] = endpoint.path;
    json["url"] = endpoint_url(endpoint);
    return json;
}

app::Json gateway_profile_status_to_json(const app::GatewayProfileStatus& status) {
    auto json = readiness_report_to_json(status.readiness);
    json["httpReady"] = status.http_ready;
    json["endpointConfigured"] = status.endpoint_configured;
    json["endpoint"] = endpoint_to_json(status.profile.endpoint);
    for (const auto& issue : status.endpoint_issues) {
        app::Json issue_json = app::Json::object();
        issue_json["code"] = issue.code;
        issue_json["message"] = issue.message;
        if (!issue.detail.empty()) {
            issue_json["detail"] = issue.detail;
        }
        const auto suggestion = readiness_issue_suggestion(issue, status.profile.id);
        if (!suggestion.empty()) {
            issue_json["suggestion"] = suggestion;
        }
        json["issues"].push_back(std::move(issue_json));
    }
    return json;
}

int discover_all_servers(app::ServerManagementService& management, bool json_output, std::ostream& out) {
    const auto reports = management.discover_all_servers();
    if (json_output) {
        write_json_array(reports, [](const auto& report) { return discovery_report_to_json(report); }, out);
        return 0;
    }

    if (reports.empty()) {
        out << "No MCP servers configured\n";
        return 0;
    }

    for (const auto& report : reports) {
        if (report.discovered) {
            out << report.server_id << "\tdiscovered\t" << report.capability_count << " capability(s)\n";
            continue;
        }

        out << report.server_id << "\tskipped\t" << report.error_message;
        if (!report.error_detail.empty()) {
            out << ": " << report.error_detail;
        }
        out << '\n';
    }
    return 0;
}

int check_server(app::ServerManagementService& management,
                 std::string_view server_id,
                 bool json_output,
                 std::ostream& out,
                 std::ostream& err) {
    const auto health = management.check_server(server_id);
    if (!health) {
        write_service_error_with_hint(err, health.error());
        return 1;
    }

    if (json_output) {
        out << server_health_report_to_json(*health).dump(2) << '\n';
        return health->ready ? 0 : 1;
    }

    out << health->server_id << '\t' << (health->ready ? "ready" : "not-ready") << '\t'
        << health->capability_count << " capability(s)\n";
    if (!health->ready) {
        write_service_error_with_hint(err,
                                      core::Error{1, health->error_message, health->error_detail});
    }
    return health->ready ? 0 : 1;
}

int check_all_servers(app::ServerManagementService& management,
                      bool json_output,
                      std::ostream& out,
                      std::ostream& err) {
    const auto reports = management.check_all_servers();
    bool ready = true;

    if (json_output) {
        app::Json json = app::Json::array();
        for (const auto& report : reports) {
            json.push_back(server_health_report_to_json(report));
            ready = ready && report.ready;
        }
        out << json.dump(2) << '\n';
        return ready ? 0 : 1;
    }

    if (reports.empty()) {
        out << "No MCP servers configured\n";
        return 1;
    }

    for (const auto& report : reports) {
        out << report.server_id << '\t' << (report.ready ? "ready" : "not-ready") << '\t'
            << report.capability_count << " capability(s)\n";
        if (!report.ready) {
            ready = false;
            write_service_error_with_hint(err,
                                          core::Error{1, report.error_message, report.error_detail});
        }
    }
    return ready ? 0 : 1;
}
int check_gateway(app::ExposureProfileStore& profiles,
                  app::CapabilityCatalog& capabilities,
                  app::McpServerStore& servers,
                  app::ServerManagementService& server_management,
                  std::string_view profile_id,
                  bool json_output,
                  std::ostream& out) {
    app::GatewayStatusService status_service(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto status = status_service.check_http_profiles();
    const auto profile_status = std::find_if(status.profiles.begin(), status.profiles.end(), [&](const auto& item) {
        return item.profile.id == profile_id;
    });
    if (profile_status == status.profiles.end()) {
        app::GatewayReadinessService readiness(profiles, capabilities, servers, gateway_health_provider(server_management));
        const auto report = readiness.check_profile(profile_id);
        if (json_output) {
            out << readiness_report_to_json(report).dump(2) << '\n';
            return 1;
        }

        out << report.profile_id << "\tnot-ready\t" << report.enabled_binding_count
            << " enabled binding(s)\n";
        for (const auto& issue : report.issues) {
            write_readiness_issue(out, issue, report.profile_id);
        }
        return 1;
    }

    if (json_output) {
        out << gateway_profile_status_to_json(*profile_status).dump(2) << '\n';
        return profile_status->http_ready ? 0 : 1;
    }

    out << profile_status->profile.id << '\t' << (profile_status->http_ready ? "ready" : "not-ready")
        << '\t' << profile_status->readiness.enabled_binding_count << " enabled binding(s)\n";
    for (const auto& issue : profile_status->endpoint_issues) {
        write_readiness_issue(out, issue, profile_status->profile.id);
    }
    for (const auto& issue : profile_status->readiness.issues) {
        write_readiness_issue(out, issue, profile_status->profile.id);
    }
    return profile_status->http_ready ? 0 : 1;
}

int check_all_gateways(app::ExposureProfileStore& profiles,
                       app::CapabilityCatalog& capabilities,
                       app::McpServerStore& servers,
                       app::ServerManagementService& server_management,
                       bool json_output,
                       std::ostream& out) {
    app::GatewayStatusService status_service(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto status = status_service.check_http_profiles();

    if (json_output) {
        app::Json json = app::Json::object();
        json["ready"] = status.ready;
        json["profileCount"] = status.profiles.size();
        json["readyProfileCount"] = status.ready_profile_count;
        json["profiles"] = app::Json::array();
        for (const auto& profile_status : status.profiles) {
            json["profiles"].push_back(gateway_profile_status_to_json(profile_status));
        }
        out << json.dump(2) << '\n';
        return status.ready ? 0 : 1;
    }

    if (status.profiles.empty()) {
        out << "No gateway profiles configured\n";
        return 1;
    }

    for (const auto& profile_status : status.profiles) {
        out << profile_status.profile.id << '\t' << (profile_status.http_ready ? "ready" : "not-ready")
            << '\t' << profile_status.readiness.enabled_binding_count << " enabled binding(s)\n";
        for (const auto& issue : profile_status.endpoint_issues) {
            write_readiness_issue(out, issue, profile_status.profile.id);
        }
        for (const auto& issue : profile_status.readiness.issues) {
            write_readiness_issue(out, issue, profile_status.profile.id);
        }
    }
    return status.ready ? 0 : 1;
}

int gateway_status(app::ExposureProfileStore& profiles,
                   app::CapabilityCatalog& capabilities,
                   app::McpServerStore& servers,
                   app::ServerManagementService& server_management,
                   bool json_output,
                   std::ostream& out) {
    app::GatewayStatusService status_service(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto status = status_service.check_http_profiles();
    if (json_output) {
        app::Json json = app::Json::object();
        json["ready"] = status.ready;
        json["profileCount"] = status.profiles.size();
        json["readyProfileCount"] = status.ready_profile_count;
        json["profiles"] = app::Json::array();
        json["issues"] = app::Json::array();
        if (status.profiles.empty()) {
            app::GatewayReadinessIssue issue{
                .code = "no_exposure_profiles",
                .message = "no gateway profiles configured",
            };
            app::Json issue_json = app::Json::object();
            issue_json["code"] = issue.code;
            issue_json["message"] = issue.message;
            issue_json["suggestion"] = readiness_issue_suggestion(issue);
            json["issues"].push_back(std::move(issue_json));
        }
        for (const auto& profile_status : status.profiles) {
            json["profiles"].push_back(gateway_profile_status_to_json(profile_status));
        }
        out << json.dump(2) << '\n';
        return status.ready ? 0 : 1;
    }

    if (status.profiles.empty()) {
        out << "No gateway profiles configured\n"
            << "Next: cxxmcp gateway import-config --trust --discover <config-path> 127.0.0.1 <base-port> /mcp/imported\n";
        return 1;
    }

    out << "gateway\t" << (status.ready ? "ready" : "not-ready") << '\n'
        << "profiles\t" << status.ready_profile_count << '/' << status.profiles.size()
        << " ready for HTTP clients\n";
    for (const auto& profile_status : status.profiles) {
        const auto endpoint = profile_status.endpoint_configured ? endpoint_url(profile_status.profile.endpoint)
                                                                 : std::string("<endpoint not configured>");
        out << profile_status.profile.id << '\t' << (profile_status.http_ready ? "ready" : "not-ready")
            << '\t' << endpoint << '\t' << profile_status.readiness.enabled_binding_count
            << " enabled binding(s)\n";
        for (const auto& issue : profile_status.endpoint_issues) {
            write_readiness_issue(out, issue, profile_status.profile.id);
        }
        for (const auto& issue : profile_status.readiness.issues) {
            write_readiness_issue(out, issue, profile_status.profile.id);
        }
    }
    if (status.ready_profile_count != 0) {
        out << "Next: cxxmcp gateway client-config-all --ready-only\n"
            << "Next: cxxmcp gateway serve-all --ready-only\n";
    }
    return status.ready ? 0 : 1;
}

int preview_gateway(app::ExposureProfileStore& profiles,
                    app::CapabilityCatalog& capabilities,
                    app::McpServerStore& servers,
                    app::ServerManagementService& server_management,
                    std::string_view profile_id,
                    bool json_output,
                    std::ostream& out,
                    std::ostream& err) {
    const auto profile = select_exposure_profile(profiles.list_exposure_profiles(), profile_id);
    if (!profile) {
        write_service_error(err, profile.error());
        return 1;
    }

    app::GatewayReadinessService readiness(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto report = readiness.check_profile(profile_id);
    app::GatewayRoutingService routing(profiles, capabilities, {});
    const auto tools = routing.list_tools(profile_id);
    if (!tools) {
        write_service_error(err, tools.error());
        return 1;
    }
    const auto prompts = routing.list_prompts(profile_id);
    if (!prompts) {
        write_service_error(err, prompts.error());
        return 1;
    }
    const auto resources = routing.list_resources(profile_id);
    if (!resources) {
        write_service_error(err, resources.error());
        return 1;
    }

    if (json_output) {
        auto json = readiness_report_to_json(report);
        json["endpoint"] = endpoint_to_json(profile->endpoint);
        json["tools"] = app::Json::array();
        for (const auto& tool : *tools) {
            json["tools"].push_back(protocol::tool_definition_to_json(tool));
        }
        json["prompts"] = app::Json::array();
        for (const auto& prompt : *prompts) {
            json["prompts"].push_back(protocol::prompt_to_json(prompt));
        }
        json["resources"] = app::Json::array();
        for (const auto& resource : *resources) {
            json["resources"].push_back(protocol::resource_to_json(resource));
        }
        out << json.dump(2) << '\n';
        return report.ready ? 0 : 1;
    }

    out << report.profile_id << '\t' << (report.ready ? "ready" : "not-ready") << '\t'
        << endpoint_url(profile->endpoint) << '\n';
    for (const auto& issue : report.issues) {
        write_readiness_issue(out, issue, report.profile_id);
    }

    out << "tools:\n";
    for (const auto& tool : *tools) {
        out << "- " << tool.name << '\t' << tool.description << '\n';
    }
    out << "prompts:\n";
    for (const auto& prompt : *prompts) {
        out << "- " << prompt.name << '\t' << prompt.description << '\n';
    }
    out << "resources:\n";
    for (const auto& resource : *resources) {
        out << "- " << resource.uri << '\t' << resource.name << '\t' << resource.description << '\n';
    }
    return report.ready ? 0 : 1;
}

int doctor(app::McpServerStore& servers,
           app::ServerManagementService& server_management,
           app::CapabilityCatalog& capabilities,
           app::ExposureProfileStore& profiles,
           bool json_output,
           std::ostream& out) {
    const auto server_items = servers.list_servers();
    const auto capability_items = capabilities.list_capabilities();
    const auto profile_items = profiles.list_exposure_profiles();
    const auto upstream_health_reports = server_management.check_all_servers();
    const auto upstream_ready_count = count_ready_servers(upstream_health_reports);
    const bool upstreams_ready = !upstream_health_reports.empty() &&
                                 upstream_ready_count == upstream_health_reports.size();

    std::vector<app::GatewayReadinessIssue> issues;
    if (server_items.empty()) {
        issues.push_back(app::GatewayReadinessIssue{
            .code = "no_servers",
            .message = "No MCP servers configured",
        });
    }
    if (!server_items.empty() && capability_items.empty()) {
        issues.push_back(app::GatewayReadinessIssue{
            .code = "no_capabilities",
            .message = "No capabilities discovered",
        });
    }
    if (profile_items.empty()) {
        issues.push_back(app::GatewayReadinessIssue{
            .code = "no_exposure_profiles",
            .message = "No exposure profiles configured",
        });
    }

    app::GatewayStatusService status_service(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto gateway_status_report = status_service.check_http_profiles();

    const bool ready = issues.empty() && upstreams_ready && gateway_status_report.ready;
    if (json_output) {
        app::Json json = app::Json::object();
        json["ready"] = ready;
        json["serverCount"] = server_items.size();
        json["capabilityCount"] = capability_items.size();
        json["exposureProfileCount"] = profile_items.size();
        json["upstreams"] = app::Json::object();
        json["upstreams"]["ready"] = upstreams_ready;
        json["upstreams"]["serverCount"] = upstream_health_reports.size();
        json["upstreams"]["readyCount"] = upstream_ready_count;
        json["upstreams"]["servers"] = app::Json::array();
        for (const auto& report : upstream_health_reports) {
            json["upstreams"]["servers"].push_back(server_health_report_to_json(report));
        }
        json["issues"] = app::Json::array();
        for (const auto& issue : issues) {
            app::Json issue_json = app::Json::object();
            issue_json["code"] = issue.code;
            issue_json["message"] = issue.message;
            const auto suggestion = readiness_issue_suggestion(issue);
            if (!suggestion.empty()) {
                issue_json["suggestion"] = suggestion;
            }
            json["issues"].push_back(std::move(issue_json));
        }
        json["profiles"] = app::Json::array();
        for (const auto& status : gateway_status_report.profiles) {
            json["profiles"].push_back(gateway_profile_status_to_json(status));
        }
        out << json.dump(2) << '\n';
        return ready ? 0 : 1;
    }

    out << "runtime\t" << (ready ? "ready" : "not-ready") << '\n'
        << "servers\t" << server_items.size() << " configured\n"
        << "upstreams\t" << upstream_ready_count << '/' << upstream_health_reports.size() << " ready\n"
        << "capabilities\t" << capability_items.size() << " discovered\n"
        << "exposures\t" << profile_items.size() << " configured\n";

    for (const auto& issue : issues) {
        write_readiness_issue(out, issue);
    }
    for (const auto& report : upstream_health_reports) {
        out << "upstream\t" << report.server_id << '\t' << (report.ready ? "ready" : "not-ready") << '\t'
            << report.capability_count << " capability(s)\n";
        if (!report.ready) {
            const core::Error error{1, report.error_message, report.error_detail};
            out << "- " << error.message;
            if (!error.detail.empty()) {
                out << ": " << error.detail;
            }
            out << '\n';
            const auto suggestion = service_error_suggestion(error);
            if (!suggestion.empty()) {
                out << "  hint: " << suggestion << '\n';
            }
        }
    }
    for (const auto& status : gateway_status_report.profiles) {
        out << status.profile.id << '\t' << (status.http_ready ? "ready" : "not-ready") << '\t'
            << status.readiness.enabled_binding_count << " enabled binding(s)\n";
        for (const auto& issue : status.endpoint_issues) {
            write_readiness_issue(out, issue, status.profile.id);
        }
        for (const auto& issue : status.readiness.issues) {
            write_readiness_issue(out, issue, status.profile.id);
        }
    }
    return ready ? 0 : 1;
}

bool has_blocking_readiness_issue(const app::GatewayReadinessReport& report) {
    return !report.issues.empty();
}

int reject_unready_gateway_bindings(app::ExposureProfileStore& profiles,
                                    app::CapabilityCatalog& capabilities,
                                    app::McpServerStore& servers,
                                    app::ServerManagementService& server_management,
                                    std::string_view profile_id,
                                    std::ostream& err) {
    app::GatewayReadinessService readiness(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto report = readiness.check_profile(profile_id);
    if (!has_blocking_readiness_issue(report)) {
        return 0;
    }

    err << "gateway profile is not ready: " << report.profile_id << '\n';
    for (const auto& issue : report.issues) {
        write_readiness_issue(err, issue, report.profile_id);
    }
    return 1;
}

int remove_server(app::ServerManagementService& management,
                  std::string_view server_id,
                  bool json_output,
                  std::ostream& out,
                  std::ostream& err) {
    const auto removed = management.remove_server(server_id);
    if (!removed) {
        write_service_error(err, removed.error());
        return 1;
    }

    if (json_output) {
        app::Json json = app::Json::object();
        json["serverId"] = server_id;
        json["removed"] = true;
        out << json.dump(2) << '\n';
        return 0;
    }

    out << "Removed cxxmcp server " << server_id << '\n';
    return 0;
}

core::Result<app::McpServerTrustState> parse_server_trust(std::string_view value) {
    if (value == "untrusted") {
        return app::McpServerTrustState::untrusted;
    }
    if (value == "trusted") {
        return app::McpServerTrustState::trusted;
    }
    if (value == "blocked") {
        return app::McpServerTrustState::blocked;
    }
    return std::unexpected(make_cli_error("invalid server trust state", std::string(value)));
}

int set_server_enabled(app::ServerManagementService& management,
                       std::string_view server_id,
                       bool enabled,
                       bool json_output,
                       std::ostream& out,
                       std::ostream& err) {
    const auto updated = management.set_server_enabled(server_id, enabled);
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_server_json(*updated, out);
    }

    out << (enabled ? "Enabled" : "Disabled") << " cxxmcp server " << server_id << '\n';
    return 0;
}

int set_server_trust(app::ServerManagementService& management,
                     std::string_view server_id,
                     std::string_view trust_text,
                     bool json_output,
                     std::ostream& out,
                     std::ostream& err) {
    const auto trust = parse_server_trust(trust_text);
    if (!trust) {
        write_service_error(err, trust.error());
        return 1;
    }

    const auto updated = management.set_server_trust(server_id, *trust);
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_server_json(*updated, out);
    }

    out << "Set cxxmcp server " << server_id << " trust to " << to_string(*trust) << '\n';
    return 0;
}

int set_server_cwd(app::ServerManagementService& management,
                   std::string_view server_id,
                   std::string_view cwd,
                   bool json_output,
                   std::ostream& out,
                   std::ostream& err) {
    const auto updated = management.set_stdio_cwd(server_id, cwd);
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_server_json(*updated, out);
    }

    out << "Set cxxmcp server " << server_id << " cwd to " << cwd << '\n';
    return 0;
}

int set_server_env(app::ServerManagementService& management,
                   std::string_view server_id,
                   std::string_view name,
                   std::string_view value,
                   bool json_output,
                   std::ostream& out,
                   std::ostream& err) {
    const auto updated = management.set_stdio_env(server_id, name, value);
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_server_json(*updated, out);
    }

    out << "Set cxxmcp server " << server_id << " env " << name << '\n';
    return 0;
}

int unset_server_env(app::ServerManagementService& management,
                     std::string_view server_id,
                     std::string_view name,
                     bool json_output,
                     std::ostream& out,
                     std::ostream& err) {
    const auto updated = management.unset_stdio_env(server_id, name);
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_server_json(*updated, out);
    }

    out << "Unset cxxmcp server " << server_id << " env " << name << '\n';
    return 0;
}

int set_server_header(app::ServerManagementService& management,
                      std::string_view server_id,
                      std::string_view name,
                      std::string_view value,
                      bool json_output,
                      std::ostream& out,
                      std::ostream& err) {
    const auto updated = management.set_http_header(server_id, name, value);
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_server_json(*updated, out);
    }

    out << "Set cxxmcp server " << server_id << " header " << name << '\n';
    return 0;
}

int unset_server_header(app::ServerManagementService& management,
                        std::string_view server_id,
                        std::string_view name,
                        bool json_output,
                        std::ostream& out,
                        std::ostream& err) {
    const auto updated = management.unset_http_header(server_id, name);
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_server_json(*updated, out);
    }

    out << "Unset cxxmcp server " << server_id << " header " << name << '\n';
    return 0;
}

int list_capabilities(app::CapabilityCatalog& capabilities, bool json_output, std::ostream& out) {
    const auto items = capabilities.list_capabilities();
    if (json_output) {
        write_json_array(items, [](const auto& capability) { return app::to_json(capability); }, out);
        return 0;
    }

    if (items.empty()) {
        out << "No capabilities discovered\n";
        return 0;
    }

    for (const auto& capability : items) {
        out << capability.id << '\t' << to_string(capability.kind) << '\t' << capability.server_id << '\t'
            << capability.upstream_name << '\t' << capability.exposed_name << '\n';
    }
    return 0;
}

int inspect_capability(app::CapabilityCatalog& capabilities,
                       std::string_view capability_id,
                       bool json_output,
                       std::ostream& out,
                       std::ostream& err) {
    const auto items = capabilities.list_capabilities();
    const auto capability_it = std::find_if(items.begin(), items.end(), [&](const auto& capability) {
        return capability.id == capability_id;
    });
    if (capability_it == items.end()) {
        write_service_error(err, make_cli_error("capability not found", std::string(capability_id)));
        return 1;
    }

    if (json_output) {
        out << app::to_json(*capability_it).dump(2) << '\n';
        return 0;
    }

    out << "id: " << capability_it->id << '\n'
        << "kind: " << to_string(capability_it->kind) << '\n'
        << "server_id: " << capability_it->server_id << '\n'
        << "upstream_name: " << capability_it->upstream_name << '\n'
        << "exposed_name: " << capability_it->exposed_name << '\n';
    if (!capability_it->title.empty()) {
        out << "title: " << capability_it->title << '\n';
    }
    if (!capability_it->description.empty()) {
        out << "description: " << capability_it->description << '\n';
    }
    if (!capability_it->uri.empty()) {
        out << "uri: " << capability_it->uri << '\n';
    }
    if (!capability_it->template_text.empty()) {
        out << "template_text: " << capability_it->template_text << '\n';
    }
    if (!capability_it->capability_hash.empty()) {
        out << "capability_hash: " << capability_it->capability_hash << '\n';
    }
    if (!capability_it->input_schema.empty()) {
        out << "input_schema:\n" << capability_it->input_schema.dump(2) << '\n';
    }
    if (!capability_it->output_schema.empty()) {
        out << "output_schema:\n" << capability_it->output_schema.dump(2) << '\n';
    }
    return 0;
}

int list_exposure_profiles(app::ExposureProfileStore& profiles, bool json_output, std::ostream& out) {
    const auto items = profiles.list_exposure_profiles();
    if (json_output) {
        write_json_array(items, [](const auto& profile) { return app::to_json(profile); }, out);
        return 0;
    }

    if (items.empty()) {
        out << "No exposure profiles configured\n";
        return 0;
    }

    for (const auto& profile : items) {
        out << profile.id << '\t' << profile.name << '\t' << profile.endpoint.listen_host << ':'
            << profile.endpoint.listen_port << '\t' << profile.endpoint.path << '\t' << profile.bindings.size()
            << " binding(s)\n";
    }
    return 0;
}

int inspect_exposure_profile(app::ExposureManagementService& management,
                             app::ExposureProfileStore& profiles,
                             app::CapabilityCatalog& capabilities,
                             app::McpServerStore& servers,
                             app::ServerManagementService& server_management,
                             std::string_view profile_id,
                             bool json_output,
                             std::ostream& out,
                             std::ostream& err) {
    const auto profile = management.get_profile(profile_id);
    if (!profile) {
        write_service_error(err, profile.error());
        return 1;
    }

    app::GatewayStatusService status_service(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto status = status_service.check_http_profiles();
    const auto profile_status = std::find_if(status.profiles.begin(), status.profiles.end(), [&](const auto& item) {
        return item.profile.id == profile_id;
    });

    app::GatewayReadinessService readiness(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto report = profile_status != status.profiles.end()
                            ? profile_status->readiness
                            : readiness.check_profile(profile_id);

    if (json_output) {
        auto json = app::to_json(*profile);
        json["readiness"] = readiness_report_to_json(report);
        if (profile_status != status.profiles.end()) {
            json["gatewayStatus"] = gateway_profile_status_to_json(*profile_status);
        }
        out << json.dump(2) << '\n';
        return 0;
    }

    out << "id: " << profile->id << '\n'
        << "name: " << profile->name << '\n'
        << "endpoint: http://" << profile->endpoint.listen_host << ':' << profile->endpoint.listen_port
        << profile->endpoint.path << '\n'
        << "bindings: " << profile->bindings.size() << '\n'
        << "readiness: " << (report.ready ? "ready" : "not-ready") << '\n';
    if (profile_status != status.profiles.end()) {
        out << "http-readiness: " << (profile_status->http_ready ? "ready" : "not-ready") << '\n';
        for (const auto& issue : profile_status->endpoint_issues) {
            write_readiness_issue(out, issue, report.profile_id);
        }
    }
    for (const auto& issue : report.issues) {
        write_readiness_issue(out, issue, report.profile_id);
    }
    if (!profile->instructions.empty()) {
        out << "instructions: " << profile->instructions << '\n';
    }
    for (const auto& binding : profile->bindings) {
        out << "binding: " << binding.id << '\t' << to_string(binding.kind) << '\t' << binding.server_id << '\t'
            << binding.upstream_name << '\t' << binding.exposed_name << '\t' << enabled_label(binding.enabled)
            << '\n';
    }
    return 0;
}

core::Result<app::Json> exposure_profile_json(app::ExposureManagementService& management,
                                              std::string_view profile_id) {
    const auto profile = management.get_profile(profile_id);
    if (!profile) {
        return std::unexpected(profile.error());
    }
    return app::to_json(*profile);
}

int write_exposure_profile_json(app::ExposureManagementService& management,
                                std::string_view profile_id,
                                std::ostream& out,
                                std::ostream& err) {
    const auto json = exposure_profile_json(management, profile_id);
    if (!json) {
        write_service_error(err, json.error());
        return 1;
    }

    out << json->dump(2) << '\n';
    return 0;
}

int create_exposure_profile(app::ExposureManagementService& management,
                            std::string_view profile_id,
                            std::string_view name,
                            bool json_output,
                            std::ostream& out,
                            std::ostream& err) {
    const auto created = management.create_profile(profile_id, name);
    if (!created) {
        write_service_error(err, created.error());
        return 1;
    }

    if (json_output) {
        return write_exposure_profile_json(management, profile_id, out, err);
    }

    out << "Created exposure profile " << profile_id << '\n';
    return 0;
}

int remove_exposure_profile(app::ExposureManagementService& management,
                            std::string_view profile_id,
                            bool json_output,
                            std::ostream& out,
                            std::ostream& err) {
    const auto removed = management.remove_profile(profile_id);
    if (!removed) {
        write_service_error(err, removed.error());
        return 1;
    }

    if (json_output) {
        app::Json json = app::Json::object();
        json["profileId"] = profile_id;
        json["removed"] = true;
        out << json.dump(2) << '\n';
        return 0;
    }

    out << "Removed exposure profile " << profile_id << '\n';
    return 0;
}

int configure_exposure_endpoint(app::ExposureManagementService& management,
                                std::string_view profile_id,
                                std::string_view host,
                                std::string_view port_text,
                                std::string_view path,
                                bool json_output,
                                std::ostream& out,
                                std::ostream& err) {
    const auto port = parse_port(port_text);
    if (!port) {
        write_service_error(err, port.error());
        return 1;
    }

    std::string normalized_path = path.empty() ? std::string("/mcp") : std::string(path);
    if (!normalized_path.starts_with('/')) {
        normalized_path.insert(normalized_path.begin(), '/');
    }

    const auto configured = management.configure_endpoint(profile_id, host, *port, normalized_path);
    if (!configured) {
        write_service_error(err, configured.error());
        return 1;
    }

    if (json_output) {
        return write_exposure_profile_json(management, profile_id, out, err);
    }

    out << "Configured exposure endpoint " << profile_id << " at http://" << host << ':' << *port
        << normalized_path << '\n';
    return 0;
}

int set_exposure_instructions(app::ExposureManagementService& management,
                              std::string_view profile_id,
                              std::string_view instructions,
                              bool json_output,
                              std::ostream& out,
                              std::ostream& err) {
    const auto updated = management.set_instructions(profile_id, instructions);
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_exposure_profile_json(management, profile_id, out, err);
    }

    out << "Set exposure profile " << profile_id << " instructions\n";
    return 0;
}

int clear_exposure_instructions(app::ExposureManagementService& management,
                                std::string_view profile_id,
                                bool json_output,
                                std::ostream& out,
                                std::ostream& err) {
    const auto updated = management.set_instructions(profile_id, {});
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_exposure_profile_json(management, profile_id, out, err);
    }

    out << "Cleared exposure profile " << profile_id << " instructions\n";
    return 0;
}

int bind_exposure_capability(app::ExposureManagementService& management,
                             std::string_view profile_id,
                             std::string_view capability_id,
                             std::string_view exposed_name,
                             bool json_output,
                             std::ostream& out,
                             std::ostream& err) {
    const auto bound = management.bind_capability(profile_id, capability_id, exposed_name);
    if (!bound) {
        write_service_error(err, bound.error());
        return 1;
    }

    if (json_output) {
        return write_exposure_profile_json(management, profile_id, out, err);
    }

    out << "Bound capability " << capability_id << " to " << profile_id;
    if (!exposed_name.empty()) {
        out << " as " << exposed_name;
    }
    out << '\n';
    return 0;
}

int bind_exposure_server(app::ExposureManagementService& management,
                         std::string_view profile_id,
                         std::string_view server_id,
                         bool json_output,
                         std::ostream& out,
                         std::ostream& err) {
    const auto bound = management.bind_server_capabilities(profile_id, server_id);
    if (!bound) {
        write_service_error(err, bound.error());
        return 1;
    }

    if (json_output) {
        auto json = exposure_profile_json(management, profile_id);
        if (!json) {
            write_service_error(err, json.error());
            return 1;
        }
        (*json)["boundCapabilityCount"] = *bound;
        out << json->dump(2) << '\n';
        return 0;
    }

    out << "Bound " << *bound << " capability(s) from " << server_id << " to " << profile_id << '\n';
    return 0;
}

int set_exposure_binding_enabled(app::ExposureManagementService& management,
                                 std::string_view profile_id,
                                 std::string_view capability_id,
                                 bool enabled,
                                 bool json_output,
                                 std::ostream& out,
                                 std::ostream& err) {
    const auto updated = management.set_binding_enabled(profile_id, capability_id, enabled);
    if (!updated) {
        write_service_error(err, updated.error());
        return 1;
    }

    if (json_output) {
        return write_exposure_profile_json(management, profile_id, out, err);
    }

    out << (enabled ? "Enabled" : "Disabled") << " exposure binding " << capability_id << " in "
        << profile_id << '\n';
    return 0;
}

int unbind_exposure_capability(app::ExposureManagementService& management,
                               std::string_view profile_id,
                               std::string_view capability_id,
                               bool json_output,
                               std::ostream& out,
                               std::ostream& err) {
    const auto unbound = management.unbind_capability(profile_id, capability_id);
    if (!unbound) {
        write_service_error(err, unbound.error());
        return 1;
    }

    if (json_output) {
        return write_exposure_profile_json(management, profile_id, out, err);
    }

    out << "Unbound capability " << capability_id << " from " << profile_id << '\n';
    return 0;
}

int prune_exposure_bindings(app::ExposureManagementService& management,
                            std::string_view profile_id,
                            bool json_output,
                            std::ostream& out,
                            std::ostream& err) {
    const auto pruned = management.prune_stale_bindings(profile_id);
    if (!pruned) {
        write_service_error(err, pruned.error());
        return 1;
    }

    if (json_output) {
        app::Json json = app::Json::object();
        json["profileId"] = profile_id;
        json["prunedBindingCount"] = *pruned;
        out << json.dump(2) << '\n';
        return 0;
    }

    out << "Pruned " << *pruned << " stale exposure binding(s) from " << profile_id << '\n';
    return 0;
}

int serve_gateway_stdio(app::ExposureProfileStore& profiles,
                        app::CapabilityCatalog& capabilities,
                        app::McpServerStore& servers,
                        app::ServerManagementService& server_management,
                        std::string_view profile_id,
                        std::istream& in,
                        std::ostream& out,
                        std::ostream& err) {
    const auto ready = reject_unready_gateway_bindings(profiles, capabilities, servers, server_management, profile_id, err);
    if (ready != 0) {
        return ready;
    }

    app::GatewayRoutingService routing(
        profiles,
        capabilities,
        app::make_upstream_gateway_tool_caller(servers),
        app::make_upstream_gateway_prompt_getter(servers),
        app::make_upstream_gateway_resource_reader(servers));
    const auto served = app::run_stdio_gateway(routing, profile_id, in, out);
    if (!served) {
        write_service_error(err, served.error());
        return 1;
    }
    return 0;
}

int write_gateway_client_config(app::ExposureProfileStore& profiles,
                                app::CapabilityCatalog& capabilities,
                                app::McpServerStore& servers,
                                app::ServerManagementService& server_management,
                                std::string_view profile_id,
                                std::string_view server_name,
                                std::ostream& out,
                                std::ostream& err) {
    app::GatewayStatusService status_service(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto status = status_service.check_http_profiles();
    const auto profile_status = std::find_if(status.profiles.begin(), status.profiles.end(), [&](const auto& item) {
        return item.profile.id == profile_id;
    });
    if (profile_status != status.profiles.end() && !profile_status->http_ready) {
        err << "gateway profile is not ready: " << profile_status->profile.id << '\n';
        for (const auto& issue : profile_status->endpoint_issues) {
            write_readiness_issue(err, issue, profile_status->profile.id);
        }
        for (const auto& issue : profile_status->readiness.issues) {
            write_readiness_issue(err, issue, profile_status->profile.id);
        }
        return 1;
    }

    app::GatewayClientConfigService configs(profiles);
    const auto config = configs.make_http_client_config(profile_id, server_name);
    if (!config) {
        write_service_error(err, config.error());
        return 1;
    }

    out << config->dump(2) << '\n';
    return 0;
}

int write_gateway_all_client_configs(app::ExposureProfileStore& profiles,
                                     app::CapabilityCatalog& capabilities,
                                     app::McpServerStore& servers,
                                     app::ServerManagementService& server_management,
                                     std::string_view server_name_prefix,
                                     bool ready_only,
                                     std::ostream& out,
                                     std::ostream& err) {
    app::GatewayClientConfigService configs(profiles);
    const auto config = [&]() -> core::Result<app::Json> {
        if (!ready_only) {
            app::GatewayStatusService status_service(profiles, capabilities, servers, gateway_health_provider(server_management));
            const auto status = status_service.check_http_profiles();
            if (!status.profiles.empty() && !status.ready) {
                for (const auto& profile_status : status.profiles) {
                    if (profile_status.http_ready) {
                        continue;
                    }
                    err << "gateway profile is not ready: " << profile_status.profile.id << '\n';
                    for (const auto& issue : profile_status.endpoint_issues) {
                        write_readiness_issue(err, issue, profile_status.profile.id);
                    }
                    for (const auto& issue : profile_status.readiness.issues) {
                        write_readiness_issue(err, issue, profile_status.profile.id);
                    }
                }
                err << "hint: use cxxmcp gateway client-config-all --ready-only to skip unready profiles\n";
                return std::unexpected(make_cli_error("one or more gateway profiles are not ready"));
            }
            return configs.make_all_http_client_configs(server_name_prefix);
        }
        app::GatewayReadinessService readiness(profiles, capabilities, servers, gateway_health_provider(server_management));
        return configs.make_ready_http_client_configs(readiness, server_name_prefix);
    }();
    if (!config) {
        write_service_error(err, config.error());
        return 1;
    }

    out << config->dump(2) << '\n';
    return 0;
}

int write_gateway_stdio_client_config(app::ExposureProfileStore& profiles,
                                      app::CapabilityCatalog& capabilities,
                                      app::McpServerStore& servers,
                                      app::ServerManagementService& server_management,
                                      std::string_view profile_id,
                                      std::string_view server_name,
                                      std::string_view executable_path,
                                      const std::filesystem::path& state_directory,
                                      std::ostream& out,
                                      std::ostream& err) {
    app::GatewayReadinessService readiness(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto report = readiness.check_profile(profile_id);
    if (!report.ready) {
        err << "gateway profile is not ready: " << report.profile_id << '\n';
        for (const auto& issue : report.issues) {
            write_readiness_issue(err, issue, report.profile_id);
        }
        return 1;
    }

    app::GatewayClientConfigService configs(profiles);
    const auto config = configs.make_stdio_client_config(profile_id,
                                                         executable_path,
                                                         {"--state-dir",
                                                          state_directory.string(),
                                                          "gateway",
                                                          "serve-stdio",
                                                          std::string(profile_id)},
                                                         server_name);
    if (!config) {
        write_service_error(err, config.error());
        return 1;
    }

    out << config->dump(2) << '\n';
    return 0;
}

int init_gateway_profile(app::ServerManagementService& server_management,
                         app::ExposureManagementService& exposure_management,
                         app::ExposureProfileStore& exposure_profiles,
                         app::CapabilityCatalog& capabilities,
                         app::McpServerStore& servers,
                         bool discover,
                         bool trust,
                         std::string_view profile_id,
                         std::string_view server_id,
                         std::string_view host,
                         std::string_view port_text,
                         std::string_view path,
                         std::string_view instructions,
                         bool json_output,
                         std::ostream& out,
                         std::ostream& err) {
    const auto port = parse_port(port_text);
    if (!port) {
        write_service_error(err, port.error());
        return 1;
    }

    const auto server = server_management.get_server(server_id);
    if (!server) {
        write_service_error(err, server.error());
        return 1;
    }

    if (trust) {
        const auto trusted = server_management.set_server_trust(server_id, app::McpServerTrustState::trusted);
        if (!trusted) {
            write_service_error(err, trusted.error());
            return 1;
        }
    }

    std::size_t discovered_capability_count = 0;
    if (discover) {
        const auto discovered = server_management.discover_server(server_id);
        if (!discovered) {
            write_service_error_with_hint(err, discovered.error());
            return 1;
        }
        discovered_capability_count = discovered->capability_count;
    }

    const auto capability_items = capabilities.list_capabilities();
    const auto capability_count = static_cast<std::size_t>(
        std::count_if(capability_items.begin(), capability_items.end(), [&](const auto& capability) {
            return capability.server_id == server_id;
        }));
    if (capability_count == 0) {
        write_service_error_with_hint(err,
                                      make_cli_error("no capabilities discovered for server",
                                                     std::string(server_id)));
        return 1;
    }

    const auto existing_profiles = exposure_profiles.list_exposure_profiles();
    const bool exists = std::any_of(existing_profiles.begin(), existing_profiles.end(), [&](const auto& profile) {
        return profile.id == profile_id;
    });
    bool created = false;
    if (!exists) {
        const auto created_profile = exposure_management.create_profile(profile_id, profile_id);
        if (!created_profile) {
            write_service_error(err, created_profile.error());
            return 1;
        }
        created = true;
    }

    std::string normalized_path = path.empty() ? std::string("/mcp") : std::string(path);
    if (!normalized_path.starts_with('/')) {
        normalized_path.insert(normalized_path.begin(), '/');
    }

    const auto configured = exposure_management.configure_endpoint(profile_id, host, *port, normalized_path);
    if (!configured) {
        write_service_error(err, configured.error());
        return 1;
    }
    if (!instructions.empty()) {
        const auto instructed = exposure_management.set_instructions(profile_id, instructions);
        if (!instructed) {
            write_service_error(err, instructed.error());
            return 1;
        }
    }

    const auto profile_before_bind = exposure_management.get_profile(profile_id);
    if (!profile_before_bind) {
        write_service_error(err, profile_before_bind.error());
        return 1;
    }
    std::unordered_set<std::string> existing_binding_ids;
    for (const auto& binding : profile_before_bind->bindings) {
        if (binding.server_id == server_id) {
            existing_binding_ids.insert(binding.id);
        }
    }

    const auto bound = exposure_management.bind_server_capabilities(profile_id, server_id);
    if (!bound) {
        write_service_error(err, bound.error());
        return 1;
    }
    std::size_t refreshed_binding_count = 0;
    for (const auto& capability : capability_items) {
        if (capability.server_id != server_id) {
            continue;
        }
        const auto binding_id = std::string(profile_id) + ":" + capability.id;
        if (existing_binding_ids.contains(binding_id)) {
            ++refreshed_binding_count;
        }
    }
    const auto added_binding_count = *bound >= refreshed_binding_count ? *bound - refreshed_binding_count : 0;

    app::GatewayReadinessService readiness(exposure_profiles,
                                           capabilities,
                                           servers,
                                           gateway_health_provider(server_management));
    const auto report = readiness.check_profile(profile_id);
    app::HostedEndpoint endpoint{
        .name = std::string(profile_id),
        .listen_host = std::string(host),
        .listen_port = *port,
        .path = normalized_path,
        .transport = app::McpServerTransportKind::streamable_http,
    };

    if (json_output) {
        const auto final_server = server_management.get_server(server_id);
        if (!final_server) {
            write_service_error(err, final_server.error());
            return 1;
        }
        const auto final_profile = exposure_management.get_profile(profile_id);
        if (!final_profile) {
            write_service_error(err, final_profile.error());
            return 1;
        }

        app::Json json = app::Json::object();
        json["profileId"] = profile_id;
        json["serverId"] = server_id;
        json["server"] = app::to_json(*final_server);
        json["profile"] = app::to_json(*final_profile);
        json["created"] = created;
        json["trusted"] = trust;
        json["discovered"] = discover;
        json["discoveredCapabilityCount"] = discovered_capability_count;
        json["boundCapabilityCount"] = *bound;
        json["addedBindingCount"] = added_binding_count;
        json["refreshedBindingCount"] = refreshed_binding_count;
        json["endpoint"] = endpoint_to_json(endpoint);
        json["readiness"] = readiness_report_to_json(report);
        out << json.dump(2) << '\n';
        return report.ready ? 0 : 1;
    }

    out << "Initialized gateway profile " << profile_id << " for " << server_id << '\n'
        << "Profile: " << (created ? "created" : "reused") << '\n'
        << (trust ? "Trust: trusted\n" : "")
        << (discover ? "Discovered " + std::to_string(discovered_capability_count) + " capability(s)\n" : "")
        << (!instructions.empty() ? "Instructions: set\n" : "")
        << "Bound " << *bound << " capability(s)\n"
        << "Bindings: " << added_binding_count << " added, " << refreshed_binding_count << " refreshed\n"
        << "Endpoint: " << endpoint_url(endpoint) << '\n'
        << "Readiness: " << (report.ready ? "ready" : "not-ready") << '\n';
    for (const auto& issue : report.issues) {
        write_readiness_issue(out, issue, report.profile_id);
    }
    if (report.ready) {
        out << "Next: cxxmcp gateway check " << profile_id << '\n'
            << "Next: cxxmcp gateway client-config " << profile_id << '\n'
            << "Next: cxxmcp gateway serve-http " << profile_id << '\n'
            << "Next: cxxmcp gateway client-config-stdio " << profile_id << '\n';
    }
    return report.ready ? 0 : 1;
}

app::Json gateway_batch_report_to_json(const app::GatewayProfileInitReport& report) {
    app::Json json = app::Json::object();
    json["serverId"] = report.server_id;
    json["profileId"] = report.profile_id;
    json["initialized"] = report.initialized;
    json["created"] = report.created;
    json["port"] = report.port;
    json["path"] = report.path;
    json["url"] = report.url;
    json["boundCapabilityCount"] = report.bound_capability_count;
    if (!report.skipped_reason.empty()) {
        json["skippedReason"] = report.skipped_reason;
    }
    return json;
}

void write_gateway_batch_next_steps(std::ostream& out) {
    out << "Next: cxxmcp gateway status\n"
        << "Next: cxxmcp gateway client-config-all --ready-only\n"
        << "Next: cxxmcp gateway serve-all --ready-only\n";
}

int init_all_gateway_profiles(app::McpServerStore& servers,
                              app::ServerManagementService& server_management,
                              app::CapabilityCatalog& capabilities,
                              app::ExposureProfileStore& profiles,
                              app::ExposureManagementService& exposure_management,
                              bool discover,
                              bool trust,
                              std::string_view host,
                              std::string_view base_port_text,
                              std::string_view path_prefix,
                              std::string_view profile_prefix,
                              std::string_view instructions,
                              bool json_output,
                              std::ostream& out,
                              std::ostream& err) {
    const auto base_port = parse_port(base_port_text);
    if (!base_port) {
        write_service_error(err, base_port.error());
        return 1;
    }

    std::size_t trusted_count = 0;
    if (trust) {
        const auto server_items = servers.list_servers();
        for (const auto& server : server_items) {
            const auto trusted = server_management.set_server_trust(server.id,
                                                                    app::McpServerTrustState::trusted);
            if (!trusted) {
                write_service_error(err, trusted.error());
                return 1;
            }
            ++trusted_count;
        }
    }

    std::vector<app::ServerDiscoveryReport> discovery_reports;
    std::size_t discovered_capability_count = 0;
    if (discover) {
        discovery_reports = server_management.discover_all_servers();
        for (const auto& report : discovery_reports) {
            if (report.discovered) {
                discovered_capability_count += report.capability_count;
            }
        }
    }

    app::GatewayOnboardingService onboarding(servers, capabilities, profiles, exposure_management);
    const auto initialized = onboarding.initialize_all_http_profiles(host,
                                                                     *base_port,
                                                                     path_prefix.empty() ? "/mcp" : path_prefix,
                                                                     profile_prefix.empty() ? "profile." : profile_prefix,
                                                                     instructions);
    if (!initialized) {
        write_service_error(err, initialized.error());
        return 1;
    }

    if (json_output) {
        app::Json json = app::Json::object();
        json["trusted"] = trust;
        json["trustedServerCount"] = trusted_count;
        json["discovered"] = discover;
        json["discoveredCapabilityCount"] = discovered_capability_count;
        json["discoveryReports"] = app::Json::array();
        for (const auto& report : discovery_reports) {
            json["discoveryReports"].push_back(discovery_report_to_json(report));
        }
        json["initializedCount"] = initialized->initialized_count;
        json["skippedCount"] = initialized->skipped_count;
        json["profiles"] = app::Json::array();
        for (const auto& report : initialized->reports) {
            json["profiles"].push_back(gateway_batch_report_to_json(report));
        }
        out << json.dump(2) << '\n';
        return initialized->initialized_count == 0 ? 1 : 0;
    }

    if (trust) {
        out << "Trust: " << trusted_count << " server(s) trusted\n";
    }
    if (discover) {
        out << "Discovered " << discovered_capability_count << " capability(s)\n";
        for (const auto& report : discovery_reports) {
            if (report.discovered) {
                continue;
            }
            out << "- " << report.server_id << "\tskipped\t" << report.error_message;
            if (!report.error_detail.empty()) {
                out << ": " << report.error_detail;
            }
            out << '\n';
        }
    }
    out << "Initialized " << initialized->initialized_count << " gateway profile(s)\n";
    for (const auto& report : initialized->reports) {
        if (!report.initialized) {
            continue;
        }
        out << "- " << report.profile_id << '\t' << report.server_id << '\t' << report.url << '\t'
            << report.bound_capability_count << " capability(s)\t"
            << (report.created ? "created" : "reused") << '\n';
    }
    if (initialized->skipped_count != 0) {
        out << "Skipped " << initialized->skipped_count << " cxxmcp server(s)\n";
        for (const auto& report : initialized->reports) {
            if (report.initialized) {
                continue;
            }
            out << "- " << report.server_id << '\t' << report.skipped_reason << '\n';
        }
    }
    if (initialized->initialized_count != 0) {
        write_gateway_batch_next_steps(out);
    }
    return initialized->initialized_count == 0 ? 1 : 0;
}

int import_gateway_config(app::ServerManagementService& server_management,
                          app::McpServerStore& servers,
                          app::CapabilityCatalog& capabilities,
                          app::ExposureProfileStore& profiles,
                          app::ExposureManagementService& exposure_management,
                          bool discover,
                          bool trust,
                          const std::filesystem::path& config_path,
                          std::string_view host,
                          std::string_view base_port_text,
                          std::string_view path_prefix,
                          std::string_view profile_prefix,
                          std::string_view instructions,
                          bool json_output,
                          std::ostream& out,
                          std::ostream& err) {
    const auto base_port = parse_port(base_port_text);
    if (!base_port) {
        write_service_error(err, base_port.error());
        return 1;
    }

    std::ifstream input(config_path);
    if (!input.is_open()) {
        write_service_error(err, make_cli_error("failed to open MCP client config", config_path.string()));
        return 1;
    }

    app::Json config_json;
    try {
        input >> config_json;
    } catch (const std::exception& exception) {
        write_service_error(err, make_cli_error("failed to parse MCP client config", exception.what()));
        return 1;
    }

    app::GatewayConfigImportService importer(server_management,
                                             servers,
                                             capabilities,
                                             profiles,
                                             exposure_management);
    const auto imported = importer.import_and_initialize(config_json,
                                                         discover,
                                                         trust,
                                                         host,
                                                         *base_port,
                                                         path_prefix.empty() ? "/mcp" : path_prefix,
                                                         profile_prefix.empty() ? "profile." : profile_prefix,
                                                         instructions);
    if (!imported) {
        write_service_error(err, imported.error());
        return 1;
    }

    if (json_output) {
        app::Json json = app::Json::object();
        json["importedCount"] = imported->imported_servers.size();
        json["servers"] = app::Json::array();
        for (const auto& server : imported->imported_servers) {
            json["servers"].push_back(app::to_json(server));
        }
        json["trusted"] = imported->trusted;
        json["trustedServerCount"] = imported->trusted_server_count;
        json["discovered"] = imported->discovered;
        json["discoveredCapabilityCount"] = imported->discovered_capability_count;
        json["discoveryReports"] = app::Json::array();
        for (const auto& report : imported->discovery_reports) {
            json["discoveryReports"].push_back(discovery_report_to_json(report));
        }
        json["initializedCount"] = imported->initialization.initialized_count;
        json["skippedCount"] = imported->initialization.skipped_count;
        json["profiles"] = app::Json::array();
        for (const auto& report : imported->initialization.reports) {
            json["profiles"].push_back(gateway_batch_report_to_json(report));
        }
        out << json.dump(2) << '\n';
        return imported->initialization.initialized_count == 0 ? 1 : 0;
    }

    out << "Imported " << imported->imported_servers.size() << " cxxmcp server(s)\n";
    if (imported->trusted) {
        out << "Trust: " << imported->trusted_server_count << " server(s) trusted\n";
    }
    if (imported->discovered) {
        out << "Discovered " << imported->discovered_capability_count << " capability(s)\n";
        for (const auto& report : imported->discovery_reports) {
            if (report.discovered) {
                continue;
            }
            out << "- " << report.server_id << "\tskipped\t" << report.error_message;
            if (!report.error_detail.empty()) {
                out << ": " << report.error_detail;
            }
            out << '\n';
        }
    }
    out << "Initialized " << imported->initialization.initialized_count << " gateway profile(s)\n";
    for (const auto& report : imported->initialization.reports) {
        if (!report.initialized) {
            continue;
        }
        out << "- " << report.profile_id << '\t' << report.server_id << '\t' << report.url << '\t'
            << report.bound_capability_count << " capability(s)\t"
            << (report.created ? "created" : "reused") << '\n';
    }
    if (imported->initialization.skipped_count != 0) {
        out << "Skipped " << imported->initialization.skipped_count << " cxxmcp server(s)\n";
        for (const auto& report : imported->initialization.reports) {
            if (report.initialized) {
                continue;
            }
            out << "- " << report.server_id << '\t' << report.skipped_reason << '\n';
        }
    }
    if (imported->initialization.initialized_count != 0) {
        write_gateway_batch_next_steps(out);
    }
    return imported->initialization.initialized_count == 0 ? 1 : 0;
}

int init_stdio_gateway_profile(app::ServerManagementService& server_management,
                               app::ExposureManagementService& exposure_management,
                               app::ExposureProfileStore& exposure_profiles,
                               app::CapabilityCatalog& capabilities,
                               app::McpServerStore& servers,
                               bool discover,
                               bool trust,
                               std::string_view path,
                               std::string_view profile_id,
                               std::string_view server_id,
                               std::string_view host,
                               std::string_view port_text,
                               std::string_view command,
                               std::span<const std::string_view> command_args,
                               std::string_view instructions,
                               bool json_output,
                               std::ostream& out,
                               std::ostream& err) {
    std::vector<std::string> args;
    args.reserve(command_args.size());
    for (const auto arg : command_args) {
        args.emplace_back(arg);
    }

    app::McpServerDefinition server{
        .id = std::string(server_id),
        .name = std::string(server_id),
        .display_name = std::string(server_id),
        .description = {},
        .transport = app::McpServerTransportKind::stdio,
        .stdio = app::StdioLaunchConfig{
            .command = std::string(command),
            .args = std::move(args),
        },
        .enabled = true,
        .auto_start = true,
        .trust = app::McpServerTrustState::untrusted,
    };

    const auto saved = server_management.save_server(std::move(server));
    if (!saved) {
        write_service_error(err, saved.error());
        return 1;
    }

    return init_gateway_profile(server_management,
                                exposure_management,
                                exposure_profiles,
                                capabilities,
                                servers,
                                discover,
                                trust,
                                profile_id,
                                server_id,
                                host,
                                port_text,
                                path,
                                instructions,
                                json_output,
                                out,
                                err);
}

int init_http_gateway_profile(app::ServerManagementService& server_management,
                              app::ExposureManagementService& exposure_management,
                              app::ExposureProfileStore& exposure_profiles,
                              app::CapabilityCatalog& capabilities,
                              app::McpServerStore& servers,
                              bool discover,
                              bool trust,
                              std::string_view path,
                              std::string_view profile_id,
                              std::string_view server_id,
                              std::string_view host,
                              std::string_view port_text,
                              std::string_view url,
                              std::span<const std::string_view> header_values,
                              std::string_view instructions,
                              bool json_output,
                              std::ostream& out,
                              std::ostream& err) {
    std::unordered_map<std::string, std::string> headers;
    for (std::size_t index = 0; index + 1 < header_values.size(); index += 2) {
        headers[std::string(header_values[index])] = std::string(header_values[index + 1]);
    }

    app::McpServerDefinition server{
        .id = std::string(server_id),
        .name = std::string(server_id),
        .display_name = std::string(server_id),
        .description = {},
        .transport = app::McpServerTransportKind::streamable_http,
        .http = app::HttpConnectionConfig{
            .url = std::string(url),
            .headers = std::move(headers),
        },
        .enabled = true,
        .auto_start = true,
        .trust = app::McpServerTrustState::untrusted,
    };

    const auto saved = server_management.save_server(std::move(server));
    if (!saved) {
        write_service_error(err, saved.error());
        return 1;
    }

    return init_gateway_profile(server_management,
                                exposure_management,
                                exposure_profiles,
                                capabilities,
                                servers,
                                discover,
                                trust,
                                profile_id,
                                server_id,
                                host,
                                port_text,
                                path,
                                instructions,
                                json_output,
                                out,
                                err);
}

int serve_gateway_http(app::ExposureProfileStore& profiles,
                       app::CapabilityCatalog& capabilities,
                       app::McpServerStore& servers,
                       app::ServerManagementService& server_management,
                       std::string_view profile_id,
                       std::ostream& out,
                       std::ostream& err) {
    const auto profile = select_exposure_profile(profiles.list_exposure_profiles(), profile_id);
    if (!profile) {
        write_service_error(err, profile.error());
        return 1;
    }
    if (profile->endpoint.listen_port == 0) {
        write_service_error(err, make_cli_error("exposure endpoint port is not configured", std::string(profile_id)));
        return 1;
    }

    const auto ready = reject_unready_gateway_bindings(profiles, capabilities, servers, server_management, profile_id, err);
    if (ready != 0) {
        return ready;
    }

    app::GatewayRoutingService routing(
        profiles,
        capabilities,
        app::make_upstream_gateway_tool_caller(servers),
        app::make_upstream_gateway_prompt_getter(servers),
        app::make_upstream_gateway_resource_reader(servers));

    out << "Serving gateway " << profile_id << " at http://" << profile->endpoint.listen_host << ':'
        << profile->endpoint.listen_port << profile->endpoint.path << '\n';
    out.flush();

    const auto served = app::run_http_gateway(routing, profile_id, profile->endpoint);
    if (!served) {
        write_service_error(err, served.error());
        return 1;
    }
    return 0;
}

int serve_all_gateways_http(app::ExposureProfileStore& profiles,
                            app::CapabilityCatalog& capabilities,
                            app::McpServerStore& servers,
                            app::ServerManagementService& server_management,
                            bool ready_only,
                            std::ostream& out,
                            std::ostream& err) {
    const auto profile_items = profiles.list_exposure_profiles();
    if (profile_items.empty()) {
        write_service_error(err, make_cli_error("no gateway profiles configured"));
        return 1;
    }

    std::vector<app::ExposureProfile> startable_profiles;
    startable_profiles.reserve(profile_items.size());
    app::GatewayStatusService status_service(profiles, capabilities, servers, gateway_health_provider(server_management));
    const auto status = status_service.check_http_profiles();
    for (const auto& profile_status : status.profiles) {
        const auto& profile = profile_status.profile;
        if (ready_only) {
            if (!profile_status.http_ready) {
                err << "Skipping gateway profile " << profile.id << '\n';
                for (const auto& issue : profile_status.endpoint_issues) {
                    write_readiness_issue(err, issue, profile.id);
                }
                for (const auto& issue : profile_status.readiness.issues) {
                    write_readiness_issue(err, issue, profile.id);
                }
                continue;
            }
        } else {
            if (profile.endpoint.listen_port == 0) {
                write_service_error(err,
                                    make_cli_error("exposure endpoint port is not configured", profile.id));
                return 1;
            }
            if (!profile_status.endpoint_issues.empty()) {
                for (const auto& issue : profile_status.endpoint_issues) {
                    write_readiness_issue(err, issue, profile.id);
                }
                return 1;
            }
            if (has_blocking_readiness_issue(profile_status.readiness)) {
                err << "gateway profile is not ready: " << profile_status.readiness.profile_id << '\n';
                for (const auto& issue : profile_status.readiness.issues) {
                    write_readiness_issue(err, issue, profile_status.readiness.profile_id);
                }
                return 1;
            }
        }
        startable_profiles.push_back(profile);
    }
    if (startable_profiles.empty()) {
        write_service_error(err,
                            make_cli_error(ready_only ? "no ready gateway profiles configured"
                                                      : "no gateway profiles configured"));
        return 1;
    }

    app::GatewayRuntimeManager manager(
        profiles,
        capabilities,
        app::make_upstream_gateway_tool_caller(servers),
        app::make_upstream_gateway_prompt_getter(servers),
        app::make_upstream_gateway_resource_reader(servers));

    for (const auto& profile : startable_profiles) {
        const auto started = manager.start_http_gateway(profile.id);
        if (!started) {
            write_service_error(err, started.error());
            return 1;
        }
    }

    out << "Serving " << startable_profiles.size() << " gateway profile(s)\n";
    for (const auto& profile : startable_profiles) {
        out << "- " << profile.id << "\thttp://" << profile.endpoint.listen_host << ':'
            << profile.endpoint.listen_port << profile.endpoint.path << '\n';
    }
    out.flush();

    while (true) {
        std::this_thread::sleep_for(std::chrono::hours(24));
    }
}

int import_bundle(app::ImportExportService& bundles, app::ProfileStore& profiles, app::ToolCatalog& tools,
                  const std::filesystem::path& path, std::ostream& out, std::ostream& err) {
    const auto imported = bundles.import_bundle(path);
    if (!imported) {
        write_service_error(err, imported.error());
        return 1;
    }

    const auto saved = profiles.save(imported->profile);
    if (!saved) {
        write_service_error(err, saved.error());
        return 1;
    }

    for (const auto& tool : imported->tools) {
        const auto added = tools.add(tool);
        if (!added) {
            write_service_error(err, added.error());
            return 1;
        }
    }

    out << "Imported bundle " << imported->profile.id << " with " << imported->tools.size() << " tool(s)\n";
    return 0;
}

int export_bundle(app::ImportExportService& bundles, app::ToolManagementService& management, app::ProfileStore& profiles,
                  const std::filesystem::path& path, std::string_view profile_id, std::ostream& out,
                  std::ostream& err) {
    const auto profile = select_profile(profiles.list_profiles(), profile_id);
    if (!profile) {
        write_service_error(err, profile.error());
        return 1;
    }

    app::ExportBundle bundle{
        .profile = *profile,
        .tools = {},
    };
    const auto tools = management.list_profile_tools(profile->id);
    if (!tools) {
        write_service_error(err, tools.error());
        return 1;
    }
    bundle.tools = *tools;

    const auto exported = bundles.export_bundle(bundle, path);
    if (!exported) {
        write_service_error(err, exported.error());
        return 1;
    }

    out << "Exported bundle " << profile->id << " with " << bundle.tools.size() << " tool(s)\n";
    return 0;
}

} // namespace

void write_usage(std::ostream& out) {
    write_command_usage(out);
}

CommandApp::CommandApp(CommandServices services)
    : services_(services) {}

core::Result<int> CommandApp::run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    return run(args, std::cin, out, err);
}

core::Result<int> CommandApp::run(std::span<const std::string_view> args,
                                  std::istream& in,
                                  std::ostream& out,
                                  std::ostream& err) {
    const CommandParser parser;
    const auto parsed = parser.parse(args);
    if (!parsed) {
        write_service_error(err, parsed.error());
        write_usage(err);
        return 2;
    }

    switch (parsed->kind) {
    case CommandKind::help:
        write_usage(out);
        return 0;
    case CommandKind::tools_help:
        write_tools_usage(out);
        return 0;
    case CommandKind::profiles_help:
        write_profiles_usage(out);
        return 0;
    case CommandKind::bundle_help:
        write_bundle_usage(out);
        return 0;
    case CommandKind::servers_help:
        write_servers_usage(out);
        return 0;
    case CommandKind::capabilities_help:
        write_capabilities_usage(out);
        return 0;
    case CommandKind::exposures_help:
        write_exposures_usage(out);
        return 0;
    case CommandKind::gateway_help:
        write_gateway_usage(out);
        return 0;
    case CommandKind::doctor:
        return doctor(services_.servers,
                      services_.server_management,
                      services_.capabilities,
                      services_.exposure_profiles,
                      services_.json_output,
                      out);
    case CommandKind::list_tools:
        return list_tools(services_.tools, services_.json_output, out);
    case CommandKind::enable_tool:
        return set_tool_enabled(services_.management, services_.profiles, parsed->values[0], true, out, err);
    case CommandKind::disable_tool:
        return set_tool_enabled(services_.management, services_.profiles, parsed->values[0], false, out, err);
    case CommandKind::list_profiles:
        return list_profiles(services_.profiles, services_.json_output, out);
    case CommandKind::list_servers:
        return list_servers(services_.servers, services_.json_output, out);
    case CommandKind::inspect_server:
        return inspect_server(services_.server_management,
                              services_.capabilities,
                              services_.exposure_profiles,
                              services_.servers,
                              parsed->values[0],
                              services_.json_output,
                              out,
                              err);
    case CommandKind::add_stdio_server:
        return add_stdio_server(services_.server_management,
                                parsed->values,
                                parsed->options,
                                services_.json_output,
                                out,
                                err);
    case CommandKind::add_http_server:
        return add_http_server(services_.server_management,
                               parsed->values,
                               parsed->options,
                               services_.json_output,
                               out,
                               err);
    case CommandKind::import_servers:
        return import_servers(services_.server_management,
                              parsed->values[0],
                              parsed->options,
                              services_.json_output,
                              out,
                              err);
    case CommandKind::discover_server:
        return discover_server(services_.server_management, parsed->values[0], services_.json_output, out, err);
    case CommandKind::discover_all_servers:
        return discover_all_servers(services_.server_management, services_.json_output, out);
    case CommandKind::check_server:
        return check_server(services_.server_management, parsed->values[0], services_.json_output, out, err);
    case CommandKind::check_all_servers:
        return check_all_servers(services_.server_management, services_.json_output, out, err);
    case CommandKind::remove_server:
        return remove_server(services_.server_management, parsed->values[0], services_.json_output, out, err);
    case CommandKind::set_server_enabled:
        return set_server_enabled(services_.server_management,
                                  parsed->values[0],
                                  parsed->values[1] == "true",
                                  services_.json_output,
                                  out,
                                  err);
    case CommandKind::set_server_trust:
        return set_server_trust(services_.server_management,
                                parsed->values[0],
                                parsed->values[1],
                                services_.json_output,
                                out,
                                err);
    case CommandKind::set_server_cwd:
        return set_server_cwd(services_.server_management,
                              parsed->values[0],
                              parsed->values[1],
                              services_.json_output,
                              out,
                              err);
    case CommandKind::set_server_env:
        return set_server_env(services_.server_management,
                              parsed->values[0],
                              parsed->values[1],
                              parsed->values[2],
                              services_.json_output,
                              out,
                              err);
    case CommandKind::unset_server_env:
        return unset_server_env(services_.server_management,
                                parsed->values[0],
                                parsed->values[1],
                                services_.json_output,
                                out,
                                err);
    case CommandKind::set_server_header:
        return set_server_header(services_.server_management,
                                 parsed->values[0],
                                 parsed->values[1],
                                 parsed->values[2],
                                 services_.json_output,
                                 out,
                                 err);
    case CommandKind::unset_server_header:
        return unset_server_header(services_.server_management,
                                   parsed->values[0],
                                   parsed->values[1],
                                   services_.json_output,
                                   out,
                                   err);
    case CommandKind::list_capabilities:
        return list_capabilities(services_.capabilities, services_.json_output, out);
    case CommandKind::inspect_capability:
        return inspect_capability(services_.capabilities, parsed->values[0], services_.json_output, out, err);
    case CommandKind::list_exposure_profiles:
        return list_exposure_profiles(services_.exposure_profiles, services_.json_output, out);
    case CommandKind::inspect_exposure_profile:
        return inspect_exposure_profile(services_.exposure_management,
                                        services_.exposure_profiles,
                                        services_.capabilities,
                                        services_.servers,
                                        services_.server_management,
                                        parsed->values[0],
                                        services_.json_output,
                                        out,
                                        err);
    case CommandKind::create_exposure_profile:
        return create_exposure_profile(services_.exposure_management,
                                       parsed->values[0],
                                       parsed->values[1],
                                       services_.json_output,
                                       out,
                                       err);
    case CommandKind::remove_exposure_profile:
        return remove_exposure_profile(services_.exposure_management,
                                       parsed->values[0],
                                       services_.json_output,
                                       out,
                                       err);
    case CommandKind::configure_exposure_endpoint:
        return configure_exposure_endpoint(services_.exposure_management,
                                           parsed->values[0],
                                           parsed->values[1],
                                           parsed->values[2],
                                           parsed->values.size() > 3 ? parsed->values[3] : std::string_view{},
                                           services_.json_output,
                                           out,
                                           err);
    case CommandKind::set_exposure_instructions:
        return set_exposure_instructions(services_.exposure_management,
                                         parsed->values[0],
                                         parsed->values[1],
                                         services_.json_output,
                                         out,
                                         err);
    case CommandKind::clear_exposure_instructions:
        return clear_exposure_instructions(services_.exposure_management,
                                           parsed->values[0],
                                           services_.json_output,
                                           out,
                                           err);
    case CommandKind::bind_exposure_capability:
        return bind_exposure_capability(services_.exposure_management,
                                        parsed->values[0],
                                        parsed->values[1],
                                        parsed->values.size() > 2 ? parsed->values[2] : std::string_view{},
                                        services_.json_output,
                                        out,
                                        err);
    case CommandKind::bind_exposure_server:
        return bind_exposure_server(services_.exposure_management,
                                    parsed->values[0],
                                    parsed->values[1],
                                    services_.json_output,
                                    out,
                                    err);
    case CommandKind::set_exposure_binding_enabled:
        return set_exposure_binding_enabled(services_.exposure_management,
                                            parsed->values[0],
                                            parsed->values[1],
                                            parsed->values[2] == "true",
                                            services_.json_output,
                                            out,
                                            err);
    case CommandKind::unbind_exposure_capability:
        return unbind_exposure_capability(services_.exposure_management,
                                          parsed->values[0],
                                          parsed->values[1],
                                          services_.json_output,
                                          out,
                                          err);
    case CommandKind::prune_exposure_bindings:
        return prune_exposure_bindings(services_.exposure_management,
                                       parsed->values[0],
                                       services_.json_output,
                                       out,
                                       err);
    case CommandKind::init_gateway:
        return init_gateway_profile(services_.server_management,
                                    services_.exposure_management,
                                    services_.exposure_profiles,
                                    services_.capabilities,
                                    services_.servers,
                                    parsed->values[0] == "true",
                                    parsed->values[1] == "true",
                                    parsed->values[2],
                                    parsed->values[3],
                                    parsed->values[4],
                                    parsed->values[5],
                                    parsed->values.size() > 6 ? parsed->values[6] : std::string_view{},
                                    option_value(parsed->options, "instructions"),
                                    services_.json_output,
                                    out,
                                    err);
    case CommandKind::init_stdio_gateway:
        return init_stdio_gateway_profile(services_.server_management,
                                          services_.exposure_management,
                                          services_.exposure_profiles,
                                          services_.capabilities,
                                          services_.servers,
                                          parsed->values[0] == "true",
                                          parsed->values[1] == "true",
                                          parsed->values[2],
                                          parsed->values[3],
                                          parsed->values[4],
                                          parsed->values[5],
                                          parsed->values[6],
                                          parsed->values[7],
                                          std::span<const std::string_view>(parsed->values).subspan(8),
                                          option_value(parsed->options, "instructions"),
                                          services_.json_output,
                                          out,
                                          err);
    case CommandKind::init_http_gateway:
        return init_http_gateway_profile(services_.server_management,
                                         services_.exposure_management,
                                         services_.exposure_profiles,
                                         services_.capabilities,
                                         services_.servers,
                                         parsed->values[0] == "true",
                                         parsed->values[1] == "true",
                                         parsed->values[2],
                                         parsed->values[3],
                                         parsed->values[4],
                                         parsed->values[5],
                                         parsed->values[6],
                                         parsed->values[7],
                                         std::span<const std::string_view>(parsed->values).subspan(8),
                                         option_value(parsed->options, "instructions"),
                                         services_.json_output,
                                         out,
                                         err);
    case CommandKind::init_all_gateways:
        return init_all_gateway_profiles(services_.servers,
                                         services_.server_management,
                                         services_.capabilities,
                                         services_.exposure_profiles,
                                         services_.exposure_management,
                                         parsed->values[0] == "true",
                                         parsed->values[1] == "true",
                                         parsed->values[2],
                                         parsed->values[3],
                                         parsed->values.size() > 4 ? parsed->values[4] : std::string_view{},
                                         option_value(parsed->options, "profile-prefix"),
                                         option_value(parsed->options, "instructions"),
                                         services_.json_output,
                                         out,
                                         err);
    case CommandKind::import_gateway_config:
        return import_gateway_config(services_.server_management,
                                     services_.servers,
                                     services_.capabilities,
                                     services_.exposure_profiles,
                                     services_.exposure_management,
                                     parsed->values[0] == "true",
                                     parsed->values[1] == "true",
                                     std::filesystem::path(std::string(parsed->values[2])),
                                     parsed->values[3],
                                     parsed->values[4],
                                     parsed->values.size() > 5 ? parsed->values[5] : std::string_view{},
                                     option_value(parsed->options, "profile-prefix"),
                                     option_value(parsed->options, "instructions"),
                                     services_.json_output,
                                     out,
                                     err);
    case CommandKind::list_gateway_profiles:
        return list_exposure_profiles(services_.exposure_profiles, services_.json_output, out);
    case CommandKind::inspect_gateway_profile:
        return inspect_exposure_profile(services_.exposure_management,
                                        services_.exposure_profiles,
                                        services_.capabilities,
                                        services_.servers,
                                        services_.server_management,
                                        parsed->values[0],
                                        services_.json_output,
                                        out,
                                        err);
    case CommandKind::gateway_status:
        return gateway_status(services_.exposure_profiles,
                              services_.capabilities,
                              services_.servers,
                              services_.server_management,
                              services_.json_output,
                              out);
    case CommandKind::gateway_client_config:
        return write_gateway_client_config(services_.exposure_profiles,
                                           services_.capabilities,
                                           services_.servers,
                                           services_.server_management,
                                           parsed->values[0],
                                           parsed->values.size() > 1 ? parsed->values[1] : std::string_view{},
                                           out,
                                           err);
    case CommandKind::gateway_all_client_configs:
        return write_gateway_all_client_configs(
            services_.exposure_profiles,
            services_.capabilities,
            services_.servers,
            services_.server_management,
            parsed->values.empty() ? std::string_view{} : parsed->values[0],
            has_option(parsed->options, "ready-only"),
            out,
            err);
    case CommandKind::gateway_client_config_stdio:
        return write_gateway_stdio_client_config(services_.exposure_profiles,
                                                services_.capabilities,
                                                services_.servers,
                                                services_.server_management,
                                                parsed->values[0],
                                                parsed->values.size() > 1 ? parsed->values[1] : std::string_view{},
                                                services_.executable_path,
                                                services_.state_directory,
                                                out,
                                                err);
    case CommandKind::check_gateway:
        return check_gateway(services_.exposure_profiles,
                             services_.capabilities,
                             services_.servers,
                             services_.server_management,
                             parsed->values[0],
                             services_.json_output,
                             out);
    case CommandKind::check_all_gateways:
        return check_all_gateways(services_.exposure_profiles,
                                  services_.capabilities,
                                  services_.servers,
                                  services_.server_management,
                                  services_.json_output,
                                  out);
    case CommandKind::preview_gateway:
        return preview_gateway(services_.exposure_profiles,
                               services_.capabilities,
                               services_.servers,
                               services_.server_management,
                               parsed->values[0],
                               services_.json_output,
                               out,
                               err);
    case CommandKind::serve_gateway_stdio:
        return serve_gateway_stdio(services_.exposure_profiles,
                                   services_.capabilities,
                                   services_.servers,
                                   services_.server_management,
                                   parsed->values[0],
                                   in,
                                   out,
                                   err);
    case CommandKind::serve_gateway_http:
        return serve_gateway_http(services_.exposure_profiles,
                                  services_.capabilities,
                                  services_.servers,
                                  services_.server_management,
                                  parsed->values[0],
                                  out,
                                  err);
    case CommandKind::serve_all_gateways_http:
        return serve_all_gateways_http(services_.exposure_profiles,
                                       services_.capabilities,
                                       services_.servers,
                                       services_.server_management,
                                       has_option(parsed->options, "ready-only"),
                                       out,
                                       err);
    case CommandKind::import_bundle:
        return import_bundle(services_.bundles, services_.profiles, services_.tools, parsed->values[0], out, err);
    case CommandKind::export_bundle:
        return export_bundle(services_.bundles, services_.management, services_.profiles, parsed->values[0],
                             parsed->values.size() > 1 ? parsed->values[1] : std::string_view{}, out, err);
    }

    return std::unexpected(make_cli_error("unhandled command"));
}

} // namespace mcp::cli


