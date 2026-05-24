#pragma once

#include <cstdlib>
#include <memory>
#include <utility>

#include <spdlog/spdlog.h>

namespace mcp::observability {

    using LogLevel = spdlog::level::level_enum;
    using Logger = spdlog::logger;
    using LoggerPtr = std::shared_ptr<Logger>;

    inline void set_level(LogLevel level) {
        spdlog::set_level(level);
    }

    inline void flush_on(LogLevel level) {
        spdlog::flush_on(level);
    }

    inline void set_default_logger(LoggerPtr logger) {
        spdlog::set_default_logger(std::move(logger));
    }

    template<typename... Args>
    inline void trace(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        spdlog::trace(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void debug(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        spdlog::debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void info(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        spdlog::info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void warn(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        spdlog::warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void error(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        spdlog::error(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    [[noreturn]] inline void fatal(spdlog::format_string_t<Args...> fmt, Args &&...args) {
        spdlog::critical(fmt, std::forward<Args>(args)...);
        spdlog::default_logger_raw()->flush();
        std::abort();
    }

}// namespace mcp::observability

#ifdef TRACE
#undef TRACE
#endif
#define TRACE(...) ::mcp::observability::trace(__VA_ARGS__)

#ifdef DEBUG
#undef DEBUG
#endif
#define DEBUG(...) ::mcp::observability::debug(__VA_ARGS__)

#ifdef INFO
#undef INFO
#endif
#define INFO(...) ::mcp::observability::info(__VA_ARGS__)

#ifdef WARN
#undef WARN
#endif
#define WARN(...) ::mcp::observability::warn(__VA_ARGS__)

#ifdef ERROR
#undef ERROR
#endif
#define ERROR(...) ::mcp::observability::error(__VA_ARGS__)

#ifdef FATAL
#undef FATAL
#endif
#define FATAL(...) ::mcp::observability::fatal(__VA_ARGS__)
