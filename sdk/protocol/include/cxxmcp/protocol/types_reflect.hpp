// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/types_reflect.hpp
/// @brief Reflection specializations for DTOs defined in types.hpp.
///
/// types.hpp cannot include reflect.hpp (circular dependency), so Reflect<>
/// specializations for types.hpp DTOs live here.

#include <cmath>

#include "cxxmcp/protocol/reflect.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

// ---------------------------------------------------------------------------
// Finite-number validators for progress/total fields
// ---------------------------------------------------------------------------

inline core::Result<core::Unit> validate_finite_double(const double& value) {
  if (!std::isfinite(value)) {
    return mcp::core::unexpected(
        core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                    "number must be finite",
                    {}});
  }
  return core::Unit{};
}

inline core::Result<core::Unit> validate_optional_finite_double(
    const std::optional<double>& value) {
  if (value.has_value() && !std::isfinite(*value)) {
    return mcp::core::unexpected(
        core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                    "number must be finite",
                    {}});
  }
  return core::Unit{};
}

// ---------------------------------------------------------------------------
// Reflect<Icon>
// ---------------------------------------------------------------------------

template <>
struct Reflect<Icon> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(
        field("src", &Icon::src), field("mimeType", &Icon::mime_type),
        field("sizes", &Icon::sizes), field("theme", &Icon::theme));
  }
  static std::vector<std::string> known_keys() {
    return {"src", "mimeType", "sizes", "theme"};
  }
};

CXXMCP_REFLECT_CHECK(Icon, 4);

// ---------------------------------------------------------------------------
// Reflect<CancelledNotificationParams>
// ---------------------------------------------------------------------------

template <>
struct Reflect<CancelledNotificationParams> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(
        field("requestId", &CancelledNotificationParams::request_id),
        field("reason", &CancelledNotificationParams::reason));
  }
  static std::vector<std::string> known_keys() {
    return {"requestId", "reason"};
  }
};

CXXMCP_REFLECT_CHECK(CancelledNotificationParams, 2);

// ---------------------------------------------------------------------------
// Reflect<ProgressNotificationParams>
// ---------------------------------------------------------------------------

template <>
struct Reflect<ProgressNotificationParams> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(
        field("progressToken", &ProgressNotificationParams::progress_token),
        validated_field("progress", &ProgressNotificationParams::progress,
                        validate_finite_double),
        validated_field("total", &ProgressNotificationParams::total,
                        validate_optional_finite_double),
        field("message", &ProgressNotificationParams::message));
  }
  static std::vector<std::string> known_keys() {
    return {"progressToken", "progress", "total", "message"};
  }
};

CXXMCP_REFLECT_CHECK(ProgressNotificationParams, 4);

// ---------------------------------------------------------------------------
// Serialization wrappers (delegates to reflection)
// ---------------------------------------------------------------------------

/// @brief Serializes a shared icon descriptor.
inline Json icon_to_json(const Icon& icon) { return reflect_to_json(icon); }

/// @brief Parses a shared icon descriptor.
/// @return Parsed icon, or nullopt when required fields or optional fields have
/// invalid wire types.
inline std::optional<Icon> icon_from_json(const Json& json) {
  auto result = reflect_from_json<Icon>(json);
  if (!result) {
    return std::nullopt;
  }
  return std::move(*result);
}

/// @brief Serializes cancellation notification parameters.
inline Json cancelled_notification_params_to_json(
    const CancelledNotificationParams& params) {
  return reflect_to_json(params);
}

/// @brief Parses cancellation notification parameters.
inline std::optional<CancelledNotificationParams>
cancelled_notification_params_from_json(const Json& json) {
  auto result = reflect_from_json<CancelledNotificationParams>(json);
  if (!result) {
    return std::nullopt;
  }
  return std::move(*result);
}

/// @brief Serializes progress notification parameters.
inline Json progress_notification_params_to_json(
    const ProgressNotificationParams& params) {
  return reflect_to_json(params);
}

/// @brief Parses progress notification parameters.
inline std::optional<ProgressNotificationParams>
progress_notification_params_from_json(const Json& json) {
  auto result = reflect_from_json<ProgressNotificationParams>(json);
  if (!result) {
    return std::nullopt;
  }
  return std::move(*result);
}

}  // namespace mcp::protocol
