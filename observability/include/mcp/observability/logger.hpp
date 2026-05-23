#pragma once

#include <string_view>

namespace mcp::observability {

enum class LogLevel {
    trace,
    debug,
    info,
    warn,
    error,
    critical,
};

class Logger {
public:
    virtual ~Logger() = default;
    virtual void log(LogLevel level, std::string_view message) = 0;
};

} // namespace mcp::observability

