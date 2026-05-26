// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/resource.hpp
/// @brief Resource listing, template, subscription, and read payloads.
///
/// Resources are server-exposed context items addressed by URI. Listing methods
/// advertise resources and templates, read methods return contents, and
/// subscription methods allow clients to receive update notifications when the
/// server advertises resource subscription support.

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Concrete resource advertised by `resources/list`.
struct Resource {
  /// Optional human-readable display title.
  std::string title;
  /// Stable URI used by `resources/read` and subscription methods.
  std::string uri;
  /// Stable resource name.
  std::string name;
  /// Optional human-readable description.
  std::string description;
  /// Optional MIME type for the resource contents.
  std::string mime_type;
  /// Optional size hint in bytes when known.
  std::optional<std::int64_t> size;
  /// Optional icon descriptors for client presentation.
  std::vector<Icon> icons;
  /// Optional annotations for model or client presentation.
  Json annotations = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result object for `resources/list`.
struct ResourcesListResult {
  /// Resources available to the caller.
  std::vector<Resource> resources;
  /// Optional cursor for retrieving the next page.
  std::optional<std::string> next_cursor;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief URI template advertised by `resources/templates/list`.
struct ResourceTemplate {
  /// Optional human-readable display title.
  std::string title;
  /// URI template string that can be expanded into concrete resource URIs.
  std::string uri_template;
  /// Stable template name.
  std::string name;
  /// Optional human-readable description.
  std::string description;
  /// Optional MIME type expected for matching resources.
  std::string mime_type;
  /// Optional size hint in bytes when known.
  std::optional<std::int64_t> size;
  /// Optional icon descriptors for client presentation.
  std::vector<Icon> icons;
  /// Optional annotations for model or client presentation.
  Json annotations = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result object for `resources/templates/list`.
struct ResourceTemplatesListResult {
  /// Resource templates available to the caller.
  std::vector<ResourceTemplate> resource_templates;
  /// Optional cursor for retrieving the next page.
  std::optional<std::string> next_cursor;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for `resources/read`.
struct ResourcesReadParams {
  /// URI of the resource to read.
  std::string uri;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for `resources/subscribe`.
struct ResourcesSubscribeParams {
  /// URI of the resource to subscribe to.
  std::string uri;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for `resources/unsubscribe`.
using ResourcesUnsubscribeParams = ResourcesSubscribeParams;

/// @brief One content part returned by `resources/read`.
struct ResourceContents {
  /// URI of the resource content.
  std::string uri;
  /// Optional MIME type of this content part.
  std::string mime_type;
  /// Text content when the resource is represented as UTF-8 text.
  std::optional<std::string> text;
  /// Base64-encoded binary content when the resource is not text.
  std::optional<std::string> blob;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result object for `resources/read`.
struct ResourcesReadResult {
  /// One or more content parts for the requested URI.
  std::vector<ResourceContents> contents;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for `notifications/resources/updated`.
struct ResourceUpdatedNotificationParams {
  /// URI of the resource that was updated.
  std::string uri;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Builds an InvalidRequest error for resource JSON validation failures.
inline core::Error resource_json_error(std::string message) {
  return core::Error{
      static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline core::Result<std::int64_t> resource_size_from_json(
    const Json& json, std::string_view context) {
  if (!json.is_number_integer()) {
    return std::unexpected(
        resource_json_error(std::string(context) + " size must be an integer"));
  }
  const auto size = json.get<std::int64_t>();
  if (size < 0 || size > static_cast<std::int64_t>(
                             std::numeric_limits<std::uint32_t>::max())) {
    return std::unexpected(resource_json_error(std::string(context) +
                                               " size must be a uint32 value"));
  }
  return size;
}

/// @brief Serializes a resource descriptor.
inline Json resource_to_json(const Resource& resource) {
  Json json = Json::object();
  if (!resource.title.empty()) {
    json["title"] = resource.title;
  }
  json["uri"] = resource.uri;
  json["name"] = resource.name;
  if (!resource.description.empty()) {
    json["description"] = resource.description;
  }
  if (!resource.mime_type.empty()) {
    json["mimeType"] = resource.mime_type;
  }
  if (resource.size.has_value()) {
    json["size"] = *resource.size;
  }
  if (!resource.icons.empty()) {
    json["icons"] = Json::array();
    for (const auto& icon : resource.icons) {
      json["icons"].push_back(icon_to_json(icon));
    }
  }
  if (!resource.annotations.empty()) {
    json["annotations"] = resource.annotations;
  }
  if (resource.meta.has_value()) {
    json["_meta"] = *resource.meta;
  }
  append_json_extensions(json, resource.extensions);
  return json;
}

/// @brief Parses a resource descriptor.
/// @return Parsed resource or validation error.
inline core::Result<Resource> resource_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(resource_json_error("resource must be an object"));
  }
  if (json.contains("title")) {
    if (!json.at("title").is_string()) {
      return std::unexpected(
          resource_json_error("resource title must be a string"));
    }
  }
  if (!json.contains("uri") || !json.at("uri").is_string()) {
    return std::unexpected(
        resource_json_error("resource requires a string uri"));
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return std::unexpected(
        resource_json_error("resource requires a string name"));
  }

  Resource resource;
  if (json.contains("title")) {
    resource.title = json.at("title").get<std::string>();
  }
  resource.uri = json.at("uri").get<std::string>();
  resource.name = json.at("name").get<std::string>();
  if (json.contains("description")) {
    if (!json.at("description").is_string()) {
      return std::unexpected(
          resource_json_error("resource description must be a string"));
    }
    resource.description = json.at("description").get<std::string>();
  }
  if (json.contains("mimeType")) {
    if (!json.at("mimeType").is_string()) {
      return std::unexpected(
          resource_json_error("resource mimeType must be a string"));
    }
    resource.mime_type = json.at("mimeType").get<std::string>();
  }
  if (json.contains("size")) {
    const auto size = resource_size_from_json(json.at("size"), "resource");
    if (!size) {
      return std::unexpected(size.error());
    }
    resource.size = *size;
  }
  if (json.contains("icons")) {
    if (!json.at("icons").is_array()) {
      return std::unexpected(
          resource_json_error("resource icons must be an array"));
    }
    for (const auto& item : json.at("icons")) {
      const auto icon = icon_from_json(item);
      if (!icon.has_value()) {
        return std::unexpected(resource_json_error("resource icon is invalid"));
      }
      resource.icons.push_back(*icon);
    }
  }
  if (json.contains("annotations")) {
    resource.annotations = json.at("annotations");
  }
  if (json.contains("_meta")) {
    resource.meta = json.at("_meta");
  }
  resource.extensions = collect_json_extensions(
      json, {"title", "uri", "name", "description", "mimeType", "size", "icons",
             "annotations", "_meta"});
  return resource;
}

/// @brief Serializes a resource template descriptor.
inline Json resource_template_to_json(
    const ResourceTemplate& resource_template) {
  Json json = Json::object();
  if (!resource_template.title.empty()) {
    json["title"] = resource_template.title;
  }
  json["uriTemplate"] = resource_template.uri_template;
  json["name"] = resource_template.name;
  if (!resource_template.description.empty()) {
    json["description"] = resource_template.description;
  }
  if (!resource_template.mime_type.empty()) {
    json["mimeType"] = resource_template.mime_type;
  }
  if (resource_template.size.has_value()) {
    json["size"] = *resource_template.size;
  }
  if (!resource_template.icons.empty()) {
    json["icons"] = Json::array();
    for (const auto& icon : resource_template.icons) {
      json["icons"].push_back(icon_to_json(icon));
    }
  }
  if (!resource_template.annotations.empty()) {
    json["annotations"] = resource_template.annotations;
  }
  if (resource_template.meta.has_value()) {
    json["_meta"] = *resource_template.meta;
  }
  append_json_extensions(json, resource_template.extensions);
  return json;
}

/// @brief Parses a resource template descriptor.
/// @return Parsed resource template or validation error.
inline core::Result<ResourceTemplate> resource_template_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        resource_json_error("resource template must be an object"));
  }
  if (json.contains("title")) {
    if (!json.at("title").is_string()) {
      return std::unexpected(
          resource_json_error("resource template title must be a string"));
    }
  }
  if (!json.contains("uriTemplate") || !json.at("uriTemplate").is_string()) {
    return std::unexpected(
        resource_json_error("resource template requires a string uriTemplate"));
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return std::unexpected(
        resource_json_error("resource template requires a string name"));
  }

  ResourceTemplate resource_template;
  if (json.contains("title")) {
    resource_template.title = json.at("title").get<std::string>();
  }
  resource_template.uri_template = json.at("uriTemplate").get<std::string>();
  resource_template.name = json.at("name").get<std::string>();
  if (json.contains("description")) {
    if (!json.at("description").is_string()) {
      return std::unexpected(resource_json_error(
          "resource template description must be a string"));
    }
    resource_template.description = json.at("description").get<std::string>();
  }
  if (json.contains("mimeType")) {
    if (!json.at("mimeType").is_string()) {
      return std::unexpected(
          resource_json_error("resource template mimeType must be a string"));
    }
    resource_template.mime_type = json.at("mimeType").get<std::string>();
  }
  if (json.contains("size")) {
    const auto size =
        resource_size_from_json(json.at("size"), "resource template");
    if (!size) {
      return std::unexpected(size.error());
    }
    resource_template.size = *size;
  }
  if (json.contains("icons")) {
    if (!json.at("icons").is_array()) {
      return std::unexpected(
          resource_json_error("resource template icons must be an array"));
    }
    for (const auto& item : json.at("icons")) {
      const auto icon = icon_from_json(item);
      if (!icon.has_value()) {
        return std::unexpected(
            resource_json_error("resource template icon is invalid"));
      }
      resource_template.icons.push_back(*icon);
    }
  }
  if (json.contains("annotations")) {
    resource_template.annotations = json.at("annotations");
  }
  if (json.contains("_meta")) {
    resource_template.meta = json.at("_meta");
  }
  resource_template.extensions = collect_json_extensions(
      json, {"title", "uriTemplate", "name", "description", "mimeType", "size",
             "icons", "annotations", "_meta"});
  return resource_template;
}

/// @brief Serializes a `resources/list` result.
inline Json resources_list_result_to_json(const ResourcesListResult& result) {
  Json json = Json::object();
  json["resources"] = Json::array();
  for (const auto& resource : result.resources) {
    json["resources"].push_back(resource_to_json(resource));
  }
  if (result.next_cursor.has_value()) {
    json["nextCursor"] = *result.next_cursor;
  }
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a `resources/list` result.
/// @return Parsed result or validation error.
inline core::Result<ResourcesListResult> resources_list_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        resource_json_error("resources/list result must be an object"));
  }
  if (!json.contains("resources") || !json.at("resources").is_array()) {
    return std::unexpected(resource_json_error(
        "resources/list result requires a resources array"));
  }

  ResourcesListResult result;
  for (const auto& item : json.at("resources")) {
    const auto resource = resource_from_json(item);
    if (!resource) {
      return std::unexpected(resource.error());
    }
    result.resources.push_back(*resource);
  }
  if (json.contains("nextCursor")) {
    if (!json.at("nextCursor").is_string()) {
      return std::unexpected(
          resource_json_error("resources/list nextCursor must be a string"));
    }
    result.next_cursor = json.at("nextCursor").get<std::string>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          resource_json_error("resources/list result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions =
      collect_json_extensions(json, {"resources", "nextCursor", "_meta"});
  return result;
}

/// @brief Serializes a `resources/templates/list` result.
inline Json resource_templates_list_result_to_json(
    const ResourceTemplatesListResult& result) {
  Json json = Json::object();
  json["resourceTemplates"] = Json::array();
  for (const auto& resource_template : result.resource_templates) {
    json["resourceTemplates"].push_back(
        resource_template_to_json(resource_template));
  }
  if (result.next_cursor.has_value()) {
    json["nextCursor"] = *result.next_cursor;
  }
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a `resources/templates/list` result.
/// @return Parsed result or validation error.
inline core::Result<ResourceTemplatesListResult>
resource_templates_list_result_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(resource_json_error(
        "resources/templates/list result must be an object"));
  }
  if (!json.contains("resourceTemplates") ||
      !json.at("resourceTemplates").is_array()) {
    return std::unexpected(resource_json_error(
        "resources/templates/list result requires a resourceTemplates array"));
  }

  ResourceTemplatesListResult result;
  for (const auto& item : json.at("resourceTemplates")) {
    const auto resource_template = resource_template_from_json(item);
    if (!resource_template) {
      return std::unexpected(resource_template.error());
    }
    result.resource_templates.push_back(*resource_template);
  }
  if (json.contains("nextCursor")) {
    if (!json.at("nextCursor").is_string()) {
      return std::unexpected(resource_json_error(
          "resources/templates/list nextCursor must be a string"));
    }
    result.next_cursor = json.at("nextCursor").get<std::string>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(resource_json_error(
          "resources/templates/list result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions = collect_json_extensions(
      json, {"resourceTemplates", "nextCursor", "_meta"});
  return result;
}

/// @brief Serializes `resources/read` params.
inline Json resources_read_params_to_json(const ResourcesReadParams& params) {
  Json json = Json{{"uri", params.uri}};
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  append_json_extensions(json, params.extensions);
  return json;
}

/// @brief Parses `resources/read` params.
/// @return Parsed params or validation error.
inline core::Result<ResourcesReadParams> resources_read_params_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        resource_json_error("resources/read params must be an object"));
  }
  if (!json.contains("uri") || !json.at("uri").is_string()) {
    return std::unexpected(
        resource_json_error("resources/read params require a string uri"));
  }
  ResourcesReadParams params;
  params.uri = json.at("uri").get<std::string>();
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          resource_json_error("resources/read _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  params.extensions = collect_json_extensions(json, {"uri", "_meta"});
  return params;
}

/// @brief Serializes `resources/subscribe` params.
inline Json resources_subscribe_params_to_json(
    const ResourcesSubscribeParams& params) {
  Json json = Json{{"uri", params.uri}};
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  append_json_extensions(json, params.extensions);
  return json;
}

/// @brief Parses `resources/subscribe` params.
/// @return Parsed params or validation error.
inline core::Result<ResourcesSubscribeParams>
resources_subscribe_params_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        resource_json_error("resources subscribe params must be an object"));
  }
  if (!json.contains("uri") || !json.at("uri").is_string()) {
    return std::unexpected(
        resource_json_error("resources subscribe params require a string uri"));
  }
  ResourcesSubscribeParams params;
  params.uri = json.at("uri").get<std::string>();
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          resource_json_error("resources subscribe _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  params.extensions = collect_json_extensions(json, {"uri", "_meta"});
  return params;
}

/// @brief Serializes `resources/unsubscribe` params.
inline Json resources_unsubscribe_params_to_json(
    const ResourcesUnsubscribeParams& params) {
  return resources_subscribe_params_to_json(params);
}

/// @brief Parses `resources/unsubscribe` params.
/// @return Parsed params or validation error.
inline core::Result<ResourcesUnsubscribeParams>
resources_unsubscribe_params_from_json(const Json& json) {
  return resources_subscribe_params_from_json(json);
}

/// @brief Serializes resource contents.
inline Json resource_contents_to_json(const ResourceContents& contents) {
  Json json = Json::object();
  json["uri"] = contents.uri;
  if (!contents.mime_type.empty()) {
    json["mimeType"] = contents.mime_type;
  }
  if (contents.text.has_value()) {
    json["text"] = *contents.text;
  }
  if (contents.blob.has_value()) {
    json["blob"] = *contents.blob;
  }
  if (contents.meta.has_value()) {
    json["_meta"] = *contents.meta;
  }
  append_json_extensions(json, contents.extensions);
  return json;
}

/// @brief Parses resource contents.
/// @return Parsed contents or validation error.
/// @note A valid contents object must include either `text` or `blob`.
inline core::Result<ResourceContents> resource_contents_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        resource_json_error("resource contents must be an object"));
  }
  if (!json.contains("uri") || !json.at("uri").is_string()) {
    return std::unexpected(
        resource_json_error("resource contents require a string uri"));
  }

  ResourceContents contents;
  contents.uri = json.at("uri").get<std::string>();
  if (json.contains("mimeType")) {
    if (!json.at("mimeType").is_string()) {
      return std::unexpected(
          resource_json_error("resource contents mimeType must be a string"));
    }
    contents.mime_type = json.at("mimeType").get<std::string>();
  }
  if (json.contains("text")) {
    if (!json.at("text").is_string()) {
      return std::unexpected(
          resource_json_error("resource contents text must be a string"));
    }
    contents.text = json.at("text").get<std::string>();
  }
  if (json.contains("blob")) {
    if (!json.at("blob").is_string()) {
      return std::unexpected(
          resource_json_error("resource contents blob must be a string"));
    }
    contents.blob = json.at("blob").get<std::string>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          resource_json_error("resource contents _meta must be an object"));
    }
    contents.meta = json.at("_meta");
  }
  contents.extensions = collect_json_extensions(
      json, {"uri", "mimeType", "text", "blob", "_meta"});
  if (!contents.text.has_value() && !contents.blob.has_value()) {
    return std::unexpected(
        resource_json_error("resource contents require text or blob"));
  }
  return contents;
}

/// @brief Serializes a `resources/read` result.
inline Json resources_read_result_to_json(const ResourcesReadResult& result) {
  Json json = Json::object();
  json["contents"] = Json::array();
  for (const auto& contents : result.contents) {
    json["contents"].push_back(resource_contents_to_json(contents));
  }
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a `resources/read` result.
/// @return Parsed result or validation error.
inline core::Result<ResourcesReadResult> resources_read_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        resource_json_error("resources/read result must be an object"));
  }
  if (!json.contains("contents") || !json.at("contents").is_array()) {
    return std::unexpected(
        resource_json_error("resources/read result requires a contents array"));
  }

  ResourcesReadResult result;
  for (const auto& item : json.at("contents")) {
    const auto contents = resource_contents_from_json(item);
    if (!contents) {
      return std::unexpected(contents.error());
    }
    result.contents.push_back(*contents);
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          resource_json_error("resources/read result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions = collect_json_extensions(json, {"contents", "_meta"});
  return result;
}

/// @brief Serializes `notifications/resources/updated` params.
inline Json resource_updated_notification_params_to_json(
    const ResourceUpdatedNotificationParams& params) {
  Json json = Json{{"uri", params.uri}};
  append_json_extensions(json, params.extensions);
  return json;
}

/// @brief Parses `notifications/resources/updated` params.
inline core::Result<ResourceUpdatedNotificationParams>
resource_updated_notification_params_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        resource_json_error("resource updated params must be an object"));
  }
  if (!json.contains("uri") || !json.at("uri").is_string()) {
    return std::unexpected(
        resource_json_error("resource updated params require a string uri"));
  }
  ResourceUpdatedNotificationParams params;
  params.uri = json.at("uri").get<std::string>();
  params.extensions = collect_json_extensions(json, {"uri"});
  return params;
}

}  // namespace mcp::protocol
