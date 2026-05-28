// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/annotations.hpp
/// @brief Typed annotations for content blocks, tool definitions, and other
/// protocol objects.
///
/// The MCP protocol uses annotations to carry optional presentation hints
/// (audience, priority, timestamps) on resources, content blocks, and tool
/// definitions.  This header provides a strongly-typed C++ struct that mirrors
/// the reference `rmcp::Annotations` model while preserving unknown JSON fields
/// through a `raw` member for forward compatibility.
///
/// Existing DTOs (ContentBlock, ToolDefinition, ...) keep their opaque
/// `Json annotations` field unchanged.  Users who want typed access can
/// round-trip through the free functions declared here.

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Typed representation of MCP annotations.
///
/// Maps to the `Annotations` struct in the MCP reference model
/// (`rmcp::Annotations`).  All fields are optional; absent members are omitted
/// from the serialized JSON.
struct Annotations {
  /// Target audience roles.  Valid wire values are `"user"` and `"assistant"`.
  std::optional<std::vector<std::string>> audience;

  /// Presentation priority in the closed interval [0.0, 1.0].
  std::optional<float> priority;

  /// ISO 8601 timestamp indicating when the annotated object was last modified.
  std::optional<std::string> last_modified;

  /// Unknown JSON members preserved for forward-compatible round trips.
  Json raw = Json::object();
};

/// @brief Serializes an Annotations struct to JSON.
///
/// Known fields (`audience`, `priority`, `lastModified`) are written with their
/// camelCase wire names.  Members stored in `raw` are merged after the typed
/// fields, without overwriting them.
inline Json annotations_to_json(const Annotations& annotations) {
  Json json = Json::object();

  if (annotations.audience.has_value()) {
    json["audience"] = *annotations.audience;
  }
  if (annotations.priority.has_value()) {
    json["priority"] = *annotations.priority;
  }
  if (annotations.last_modified.has_value()) {
    json["lastModified"] = *annotations.last_modified;
  }

  // Merge unknown fields preserved from a prior deserialization.
  if (annotations.raw.is_object()) {
    for (const auto& item : annotations.raw.items()) {
      if (!json.contains(item.key())) {
        json[item.key()] = item.value();
      }
    }
  }

  return json;
}

/// @brief Parses an Annotations struct from JSON.
///
/// Returns an error when:
/// - `json` is not an object
/// - `audience` is present but is not an array of strings
/// - `priority` is present but is not a number or is outside [0.0, 1.0]
/// - `lastModified` is present but is not a string
inline core::Result<Annotations> annotations_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                    "annotations must be an object",
                    {}});
  }

  Annotations annotations;

  if (json.contains("audience")) {
    const auto& audience_val = json.at("audience");
    if (!audience_val.is_array()) {
      return mcp::core::unexpected(
          core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                      "annotations audience must be an array",
                      {}});
    }
    std::vector<std::string> audience;
    audience.reserve(audience_val.size());
    for (const auto& item : audience_val) {
      if (!item.is_string()) {
        return mcp::core::unexpected(
            core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                        "annotations audience entries must be strings",
                        {}});
      }
      audience.push_back(item.get<std::string>());
    }
    annotations.audience = std::move(audience);
  }

  if (json.contains("priority")) {
    const auto& priority_val = json.at("priority");
    if (!priority_val.is_number()) {
      return mcp::core::unexpected(
          core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                      "annotations priority must be a number",
                      {}});
    }
    const float priority = priority_val.get<float>();
    if (!std::isfinite(priority) || priority < 0.0f || priority > 1.0f) {
      return mcp::core::unexpected(
          core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                      "annotations priority must be between 0.0 and 1.0",
                      {}});
    }
    annotations.priority = priority;
  }

  if (json.contains("lastModified")) {
    const auto& ts_val = json.at("lastModified");
    if (!ts_val.is_string()) {
      return mcp::core::unexpected(
          core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                      "annotations lastModified must be a string",
                      {}});
    }
    annotations.last_modified = ts_val.get<std::string>();
  }

  // Preserve unknown fields for forward-compatible round trips.
  annotations.raw =
      collect_json_extensions(json, {"audience", "priority", "lastModified"});

  return annotations;
}

}  // namespace mcp::protocol
