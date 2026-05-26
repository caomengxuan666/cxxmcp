// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/logging.hpp
/// @brief Logging level and notification payloads.
///
/// MCP logging lets a client configure the minimum server log level with
/// `logging/setLevel`, and lets a server emit structured log data through
/// `notifications/message`.

#include <optional>
#include <string>
#include <utility>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief MCP logging severity levels.
enum class LoggingLevel {
  /// Detailed diagnostic messages.
  Debug,
  /// Informational messages.
  Info,
  /// Normal but significant condition.
  Notice,
  /// Warning condition.
  Warning,
  /// Error condition.
  Error,
  /// Critical condition.
  Critical,
  /// Immediate action should be taken.
  Alert,
  /// System is unusable.
  Emergency,
};

/// @brief Parameters for `logging/setLevel`.
struct LoggingSetLevelParams {
  /// Minimum level the receiver should emit after the request.
  LoggingLevel level = LoggingLevel::Info;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
};

/// @brief Parameters for `notifications/message`.
struct LoggingMessageNotificationParams {
  /// Severity of this log message.
  LoggingLevel level = LoggingLevel::Info;
  /// Optional logger name or category.
  std::string logger;
  /// Structured log payload.
  Json data;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
};

/// @brief Builds an InvalidRequest error for logging JSON validation failures.
inline core::Error logging_json_error(std::string message) {
  return core::Error{
      static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

/// @brief Converts a logging level to its MCP string value.
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

/// @brief Parses a logging level string.
/// @return Parsed level, or nullopt for unsupported strings.
inline std::optional<LoggingLevel> logging_level_from_string(
    const std::string& value) {
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

/// @brief Serializes `logging/setLevel` params.
inline Json logging_set_level_params_to_json(
    const LoggingSetLevelParams& params) {
  Json json = Json{{"level", logging_level_to_string(params.level)}};
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  return json;
}

/// @brief Parses `logging/setLevel` params.
/// @return Parsed params or validation error.
inline core::Result<LoggingSetLevelParams> logging_set_level_params_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        logging_json_error("logging/setLevel params must be an object"));
  }
  if (!json.contains("level") || !json.at("level").is_string()) {
    return std::unexpected(
        logging_json_error("logging/setLevel params require a string level"));
  }
  const auto level =
      logging_level_from_string(json.at("level").get<std::string>());
  if (!level.has_value()) {
    return std::unexpected(
        logging_json_error("logging level is not supported"));
  }
  LoggingSetLevelParams params;
  params.level = *level;
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          logging_json_error("logging/setLevel _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  return params;
}

/// @brief Serializes logging message notification params.
inline Json logging_message_notification_params_to_json(
    const LoggingMessageNotificationParams& params) {
  Json json = Json::object();
  json["level"] = logging_level_to_string(params.level);
  if (!params.logger.empty()) {
    json["logger"] = params.logger;
  }
  json["data"] = params.data;
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  return json;
}

/// @brief Parses logging message notification params.
/// @return Parsed params or validation error.
inline core::Result<LoggingMessageNotificationParams>
logging_message_notification_params_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        logging_json_error("logging message params must be an object"));
  }
  if (!json.contains("level") || !json.at("level").is_string()) {
    return std::unexpected(
        logging_json_error("logging message params require a string level"));
  }
  if (!json.contains("data")) {
    return std::unexpected(
        logging_json_error("logging message params require data"));
  }
  const auto level =
      logging_level_from_string(json.at("level").get<std::string>());
  if (!level.has_value()) {
    return std::unexpected(
        logging_json_error("logging level is not supported"));
  }

  LoggingMessageNotificationParams params;
  params.level = *level;
  if (json.contains("logger")) {
    if (!json.at("logger").is_string()) {
      return std::unexpected(
          logging_json_error("logging logger must be a string"));
    }
    params.logger = json.at("logger").get<std::string>();
  }
  params.data = json.at("data");
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          logging_json_error("logging message _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  return params;
}

}  // namespace mcp::protocol
