#include "mcp/cli/commands.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace mcp::cli {

namespace {

enum class CommandKind {
    help,
    list_tools,
    enable_tool,
    disable_tool,
    list_profiles,
    import_bundle,
    export_bundle,
};

struct ParsedCommand {
    CommandKind kind = CommandKind::help;
    std::vector<std::string_view> values;
};

core::Error make_cli_error(std::string message, std::string detail = {}) {
    return core::Error{2, std::move(message), std::move(detail)};
}

void write_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  mcp tools list\n"
        << "  mcp tools enable <tool-id>\n"
        << "  mcp tools disable <tool-id>\n"
        << "  mcp profiles list\n"
        << "  mcp bundle import <path>\n"
        << "  mcp bundle export <path> [profile-id]\n";
}

class CommandParser {
public:
    core::Result<ParsedCommand> parse(std::span<const std::string_view> args) const {
        if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
            return ParsedCommand{.kind = CommandKind::help};
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

        return std::unexpected(make_cli_error("unknown command", std::string(args[0])));
    }

private:
    core::Result<ParsedCommand> parse_tools(std::span<const std::string_view> args) const {
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
        if (args.size() == 1 && args[0] == "list") {
            return ParsedCommand{.kind = CommandKind::list_profiles};
        }

        return std::unexpected(make_cli_error("invalid profiles command"));
    }

    core::Result<ParsedCommand> parse_bundle(std::span<const std::string_view> args) const {
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

void write_service_error(std::ostream& err, const core::Error& error) {
    err << error.message;
    if (!error.detail.empty()) {
        err << ": " << error.detail;
    }
    err << '\n';
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

int list_tools(app::ToolCatalog& tools, std::ostream& out) {
    const auto descriptors = tools.list();
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

int list_profiles(app::ProfileStore& profiles, std::ostream& out) {
    const auto items = profiles.list_profiles();
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

CommandApp::CommandApp(CommandServices services)
    : services_(services) {}

core::Result<int> CommandApp::run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
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
    case CommandKind::list_tools:
        return list_tools(services_.tools, out);
    case CommandKind::enable_tool:
        return set_tool_enabled(services_.management, services_.profiles, parsed->values[0], true, out, err);
    case CommandKind::disable_tool:
        return set_tool_enabled(services_.management, services_.profiles, parsed->values[0], false, out, err);
    case CommandKind::list_profiles:
        return list_profiles(services_.profiles, out);
    case CommandKind::import_bundle:
        return import_bundle(services_.bundles, services_.profiles, services_.tools, parsed->values[0], out, err);
    case CommandKind::export_bundle:
        return export_bundle(services_.bundles, services_.management, services_.profiles, parsed->values[0],
                             parsed->values.size() > 1 ? parsed->values[1] : std::string_view{}, out, err);
    }

    return std::unexpected(make_cli_error("unhandled command"));
}

} // namespace mcp::cli
