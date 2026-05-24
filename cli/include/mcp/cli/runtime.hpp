#pragma once

#include <cstdlib>
#include <filesystem>

namespace mcp::cli {

struct RuntimeOptions {
    std::filesystem::path state_directory;
    bool json_output = false;
};

inline std::filesystem::path default_state_directory() {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, "MCP_RUNTIME_HOME") == 0 && value != nullptr) {
        std::filesystem::path path(value);
        std::free(value);
        if (!path.empty()) {
            return path;
        }
    }
#else
    if (const char* home = std::getenv("MCP_RUNTIME_HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home);
    }
#endif
    return std::filesystem::current_path() / ".mcp-runtime";
}

} // namespace mcp::cli
