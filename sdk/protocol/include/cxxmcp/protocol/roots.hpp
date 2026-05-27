// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/roots.hpp
/// @brief Client root discovery payloads.
///
/// Roots describe filesystem or workspace anchors that a client makes available
/// to a server. Servers request them with `roots/list`, and clients can notify
/// changes when the roots capability advertises list change support.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief One client root entry.
struct Root {
  /// Root URI. The interpretation is transport/client specific.
  std::string uri;
  /// Optional human-readable name for display.
  std::string name;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result object for `roots/list`.
struct RootsListResult {
  /// Roots currently available to the server.
  std::vector<Root> roots;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Builds an InvalidRequest error for roots JSON validation failures.
inline core::Error roots_json_error(std::string message) {
  return core::Error{
      static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

/// @brief Serializes a root entry.
inline Json root_to_json(const Root& root) {
  Json json = Json::object();
  json["uri"] = root.uri;
  if (!root.name.empty()) {
    json["name"] = root.name;
  }
  if (root.meta.has_value()) {
    json["_meta"] = *root.meta;
  }
  append_json_extensions(json, root.extensions);
  return json;
}

/// @brief Parses a root entry.
/// @return Parsed root or validation error.
inline core::Result<Root> root_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(roots_json_error("root must be an object"));
  }
  if (!json.contains("uri") || !json.at("uri").is_string()) {
    return std::unexpected(roots_json_error("root requires a string uri"));
  }

  Root root;
  root.uri = json.at("uri").get<std::string>();
  if (json.contains("name")) {
    if (!json.at("name").is_string()) {
      return std::unexpected(roots_json_error("root name must be a string"));
    }
    root.name = json.at("name").get<std::string>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(roots_json_error("root _meta must be an object"));
    }
    root.meta = json.at("_meta");
  }
  root.extensions = collect_json_extensions(json, {"uri", "name", "_meta"});
  return root;
}

/// @brief Serializes a `roots/list` result.
inline Json roots_list_result_to_json(const RootsListResult& result) {
  Json json = Json::object();
  json["roots"] = Json::array();
  for (const auto& root : result.roots) {
    json["roots"].push_back(root_to_json(root));
  }
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a `roots/list` result.
/// @return Parsed result or validation error.
inline core::Result<RootsListResult> roots_list_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        roots_json_error("roots/list result must be an object"));
  }
  if (!json.contains("roots") || !json.at("roots").is_array()) {
    return std::unexpected(
        roots_json_error("roots/list result requires a roots array"));
  }

  RootsListResult result;
  for (const auto& item : json.at("roots")) {
    const auto root = root_from_json(item);
    if (!root) {
      return std::unexpected(root.error());
    }
    result.roots.push_back(*root);
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          roots_json_error("roots/list result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions = collect_json_extensions(json, {"roots", "_meta"});
  return result;
}

}  // namespace mcp::protocol
