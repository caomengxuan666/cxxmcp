#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <utility>

namespace mcp::protocol {

enum class LoggingLevel {
    Debug,
    Info,
    Notice,
    Warning,
    Error,
    Critical,
    Alert,
    Emergency,
};

struct LoggingSetLevelParams {
    LoggingLevel level = LoggingLevel::Info;
};

struct LoggingMessageNotificationParams {
    LoggingLevel level = LoggingLevel::Info;
    std::string logger;
    Json data;
};

inline core::Error logging_json_error(std::string message) {
    return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline std::string logging_level_to_string(LoggingLevel level) {
    switch (level) {
    case LoggingLevel::Debug:
        return "debug";
    case LoggingLevel::Info:
        return "info";
    case LoggingLevel::Notice:
        return "notice";
    case LoggingLevel::Warning:
        return "warning";
    case LoggingLevel::Error:
        return "error";
    case LoggingLevel::Critical:
        return "critical";
    case LoggingLevel::Alert:
        return "alert";
    case LoggingLevel::Emergency:
        return "emergency";
    }
    return "info";
}

inline std::optional<LoggingLevel> logging_level_from_string(const std::string& value) {
    if (value == "debug") {
        return LoggingLevel::Debug;
    }
    if (value == "info") {
        return LoggingLevel::Info;
    }
    if (value == "notice") {
        return LoggingLevel::Notice;
    }
    if (value == "warning") {
        return LoggingLevel::Warning;
    }
    if (value == "error") {
        return LoggingLevel::Error;
    }
    if (value == "critical") {
        return LoggingLevel::Critical;
    }
    if (value == "alert") {
        return LoggingLevel::Alert;
    }
    if (value == "emergency") {
        return LoggingLevel::Emergency;
    }
    return std::nullopt;
}

inline Json logging_set_level_params_to_json(const LoggingSetLevelParams& params) {
    return Json{{"level", logging_level_to_string(params.level)}};
}

inline core::Result<LoggingSetLevelParams> logging_set_level_params_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(logging_json_error("logging/setLevel params must be an object"));
    }
    if (!json.contains("level") || !json.at("level").is_string()) {
        return std::unexpected(logging_json_error("logging/setLevel params require a string level"));
    }
    const auto level = logging_level_from_string(json.at("level").get<std::string>());
    if (!level.has_value()) {
        return std::unexpected(logging_json_error("logging level is not supported"));
    }
    return LoggingSetLevelParams{*level};
}

inline Json logging_message_notification_params_to_json(const LoggingMessageNotificationParams& params) {
    Json json = Json::object();
    json["level"] = logging_level_to_string(params.level);
    if (!params.logger.empty()) {
        json["logger"] = params.logger;
    }
    json["data"] = params.data;
    return json;
}

inline core::Result<LoggingMessageNotificationParams> logging_message_notification_params_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(logging_json_error("logging message params must be an object"));
    }
    if (!json.contains("level") || !json.at("level").is_string()) {
        return std::unexpected(logging_json_error("logging message params require a string level"));
    }
    if (!json.contains("data")) {
        return std::unexpected(logging_json_error("logging message params require data"));
    }
    const auto level = logging_level_from_string(json.at("level").get<std::string>());
    if (!level.has_value()) {
        return std::unexpected(logging_json_error("logging level is not supported"));
    }

    LoggingMessageNotificationParams params;
    params.level = *level;
    if (json.contains("logger")) {
        if (!json.at("logger").is_string()) {
            return std::unexpected(logging_json_error("logging logger must be a string"));
        }
        params.logger = json.at("logger").get<std::string>();
    }
    params.data = json.at("data");
    return params;
}

} // namespace mcp::protocol
