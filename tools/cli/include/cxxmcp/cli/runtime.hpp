#pragma once

#include "cxxmcp/core/result.hpp"

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

namespace mcp::cli {

    struct RuntimeOptions {
        std::filesystem::path state_directory;
        bool show_version = false;
        bool show_help = false;
        bool json_output = false;
    };

    inline std::filesystem::path default_state_directory() {
#if defined(_MSC_VER)
        char *value = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&value, &size, "MCP_RUNTIME_HOME") == 0 && value != nullptr) {
            std::filesystem::path path(value);
            std::free(value);
            if (!path.empty()) {
                return path;
            }
        }
#else
        if (const char *home = std::getenv("MCP_RUNTIME_HOME"); home != nullptr && *home != '\0') {
            return std::filesystem::path(home);
        }
#endif
        return std::filesystem::current_path() / ".mcp-runtime";
    }

    inline mcp::core::Result<RuntimeOptions> parse_runtime_options(std::vector<std::string_view> &args) {
        RuntimeOptions options{
                .state_directory = default_state_directory(),
        };

        std::vector<std::string_view> filtered;
        filtered.reserve(args.size());
        for (std::size_t index = 0; index < args.size(); ++index) {
            const auto arg = args[index];
            if (arg == "--help" || arg == "-h") {
                if (filtered.empty()) {
                    options.show_help = true;
                } else {
                    filtered.push_back(arg);
                }
                continue;
            }
            if (arg == "--version" || arg == "-V") {
                options.show_version = true;
                continue;
            }
            if (arg == "--json") {
                options.json_output = true;
                continue;
            }
            if (arg == "--state-dir") {
                if (index + 1 >= args.size()) {
                    return std::unexpected(mcp::core::Error{
                            2,
                            "missing state directory",
                            "expected a path after --state-dir.",
                    });
                }
                options.state_directory = std::filesystem::path(args[++index]);
                continue;
            }
            if (core::starts_with(arg, "--state-dir=")) {
                const auto value = arg.substr(std::string_view{"--state-dir="}.size());
                if (value.empty()) {
                    return std::unexpected(mcp::core::Error{
                            2,
                            "missing state directory",
                            "expected a path after --state-dir=.",
                    });
                }
                options.state_directory = std::filesystem::path(value);
                continue;
            }
            filtered.push_back(arg);
        }

        args = std::move(filtered);
        return options;
    }

}// namespace mcp::cli
