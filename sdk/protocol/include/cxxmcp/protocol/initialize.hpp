// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/initialize.hpp
/// @brief Typed initialize request and result payloads.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/capabilities.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Client or server implementation identity.
struct ImplementationInfo {
  /// Human-readable implementation name.
  std::string name;
  /// Optional display title.
  std::optional<std::string> title;
  /// Human-readable implementation version.
  std::string version;
  /// Optional display description.
  std::optional<std::string> description;
  /// Optional icon descriptors.
  std::vector<Icon> icons;
  /// Optional project or documentation website URL.
  std::optional<std::string> website_url;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for the `initialize` request.
struct InitializeParams {
  /// Requested MCP protocol snapshot.
  std::string protocol_version{std::string(McpProtocolVersion)};
  /// Client capabilities advertised during initialization.
  ClientCapabilities capabilities;
  /// Client implementation identity.
  ImplementationInfo client_info;
  /// Optional protocol-level request metadata.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result payload for the `initialize` request.
struct InitializeResult {
  /// Negotiated MCP protocol snapshot.
  std::string protocol_version{std::string(McpProtocolVersion)};
  /// Server capabilities advertised during initialization.
  ServerCapabilities capabilities;
  /// Server implementation identity.
  ImplementationInfo server_info;
  /// Optional human-readable server usage instructions.
  std::optional<std::string> instructions;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

inline core::Error initialize_json_error(std::string message) {
  return core::Error{
      static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline Json implementation_info_to_json(const ImplementationInfo& info) {
  Json json = Json{{"name", info.name}, {"version", info.version}};
  if (info.title.has_value()) {
    json["title"] = *info.title;
  }
  if (info.description.has_value()) {
    json["description"] = *info.description;
  }
  if (!info.icons.empty()) {
    json["icons"] = Json::array();
    for (const auto& icon : info.icons) {
      json["icons"].push_back(icon_to_json(icon));
    }
  }
  if (info.website_url.has_value()) {
    json["websiteUrl"] = *info.website_url;
  }
  if (info.meta.has_value()) {
    json["_meta"] = *info.meta;
  }
  append_json_extensions(json, info.extensions);
  return json;
}

inline core::Result<ImplementationInfo> implementation_info_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        initialize_json_error("implementation info must be an object"));
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return mcp::core::unexpected(
        initialize_json_error("implementation info requires a string name"));
  }
  if (!json.contains("version") || !json.at("version").is_string()) {
    return mcp::core::unexpected(
        initialize_json_error("implementation info requires a string version"));
  }
  ImplementationInfo info;
  info.name = json.at("name").get<std::string>();
  info.version = json.at("version").get<std::string>();
  if (json.contains("title")) {
    if (!json.at("title").is_string()) {
      return mcp::core::unexpected(
          initialize_json_error("implementation info title must be a string"));
    }
    info.title = json.at("title").get<std::string>();
  }
  if (json.contains("description")) {
    if (!json.at("description").is_string()) {
      return mcp::core::unexpected(initialize_json_error(
          "implementation info description must be a string"));
    }
    info.description = json.at("description").get<std::string>();
  }
  if (json.contains("icons")) {
    if (!json.at("icons").is_array()) {
      return mcp::core::unexpected(
          initialize_json_error("implementation info icons must be an array"));
    }
    for (const auto& item : json.at("icons")) {
      const auto icon = icon_from_json(item);
      if (!icon.has_value()) {
        return mcp::core::unexpected(initialize_json_error(
            "implementation info icons must contain valid icon objects"));
      }
      info.icons.push_back(*icon);
    }
  }
  if (json.contains("websiteUrl")) {
    if (!json.at("websiteUrl").is_string()) {
      return mcp::core::unexpected(initialize_json_error(
          "implementation info websiteUrl must be a string"));
    }
    info.website_url = json.at("websiteUrl").get<std::string>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          initialize_json_error("implementation info _meta must be an object"));
    }
    info.meta = json.at("_meta");
  }
  info.extensions =
      collect_json_extensions(json, {"name", "title", "version", "description",
                                     "icons", "websiteUrl", "_meta"});
  return info;
}

inline Json initialize_params_to_json(const InitializeParams& params) {
  Json json = Json{
      {"protocolVersion", params.protocol_version},
      {"capabilities", client_capabilities_to_json(params.capabilities)},
      {"clientInfo", implementation_info_to_json(params.client_info)},
  };
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  append_json_extensions(json, params.extensions);
  return json;
}

inline core::Result<InitializeParams> initialize_params_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        initialize_json_error("initialize params must be an object"));
  }
  if (!json.contains("protocolVersion") ||
      !json.at("protocolVersion").is_string()) {
    return mcp::core::unexpected(initialize_json_error(
        "initialize params require a string protocolVersion"));
  }
  if (!json.contains("capabilities") || !json.at("capabilities").is_object()) {
    return mcp::core::unexpected(
        initialize_json_error("initialize params require object capabilities"));
  }
  if (!json.contains("clientInfo")) {
    return mcp::core::unexpected(
        initialize_json_error("initialize params require clientInfo"));
  }
  const auto capabilities =
      client_capabilities_from_json(json.at("capabilities"));
  if (!capabilities.has_value()) {
    return mcp::core::unexpected(
        initialize_json_error("initialize params capabilities are invalid"));
  }
  const auto client_info = implementation_info_from_json(json.at("clientInfo"));
  if (!client_info) {
    return mcp::core::unexpected(client_info.error());
  }
  InitializeParams params;
  params.protocol_version = json.at("protocolVersion").get<std::string>();
  params.capabilities = *capabilities;
  params.client_info = *client_info;
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          initialize_json_error("initialize params _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  params.extensions = collect_json_extensions(
      json, {"protocolVersion", "capabilities", "clientInfo", "_meta"});
  return params;
}

inline Json initialize_result_to_json(const InitializeResult& result) {
  Json json = Json{
      {"protocolVersion", result.protocol_version},
      {"capabilities", server_capabilities_to_json(result.capabilities)},
      {"serverInfo", implementation_info_to_json(result.server_info)},
  };
  if (result.instructions.has_value()) {
    json["instructions"] = *result.instructions;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

inline core::Result<InitializeResult> initialize_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        initialize_json_error("initialize result must be an object"));
  }
  if (!json.contains("protocolVersion") ||
      !json.at("protocolVersion").is_string()) {
    return mcp::core::unexpected(initialize_json_error(
        "initialize result requires a string protocolVersion"));
  }
  if (!json.contains("capabilities") || !json.at("capabilities").is_object()) {
    return mcp::core::unexpected(initialize_json_error(
        "initialize result requires object capabilities"));
  }
  if (!json.contains("serverInfo")) {
    return mcp::core::unexpected(
        initialize_json_error("initialize result requires serverInfo"));
  }
  const auto capabilities =
      server_capabilities_from_json(json.at("capabilities"));
  if (!capabilities.has_value()) {
    return mcp::core::unexpected(
        initialize_json_error("initialize result capabilities are invalid"));
  }
  const auto server_info = implementation_info_from_json(json.at("serverInfo"));
  if (!server_info) {
    return mcp::core::unexpected(server_info.error());
  }
  InitializeResult result;
  result.protocol_version = json.at("protocolVersion").get<std::string>();
  result.capabilities = *capabilities;
  result.server_info = *server_info;
  if (json.contains("instructions")) {
    if (!json.at("instructions").is_string()) {
      return mcp::core::unexpected(initialize_json_error(
          "initialize result instructions must be a string"));
    }
    result.instructions = json.at("instructions").get<std::string>();
  }
  result.extensions = collect_json_extensions(
      json, {"protocolVersion", "capabilities", "serverInfo", "instructions"});
  return result;
}

}  // namespace mcp::protocol
