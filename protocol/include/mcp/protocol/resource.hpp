#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mcp::protocol {

struct Resource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
};

struct ResourcesListResult {
    std::vector<Resource> resources;
    std::optional<std::string> next_cursor;
};

struct ResourceTemplate {
    std::string uri_template;
    std::string name;
    std::string description;
    std::string mime_type;
};

struct ResourceTemplatesListResult {
    std::vector<ResourceTemplate> resource_templates;
    std::optional<std::string> next_cursor;
};

struct ResourcesReadParams {
    std::string uri;
};

struct ResourcesSubscribeParams {
    std::string uri;
};

using ResourcesUnsubscribeParams = ResourcesSubscribeParams;

struct ResourceContents {
    std::string uri;
    std::string mime_type;
    std::optional<std::string> text;
    std::optional<std::string> blob;
};

struct ResourcesReadResult {
    std::vector<ResourceContents> contents;
};

inline core::Error resource_json_error(std::string message) {
    return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline Json resource_to_json(const Resource& resource) {
    Json json = Json::object();
    json["uri"] = resource.uri;
    json["name"] = resource.name;
    if (!resource.description.empty()) {
        json["description"] = resource.description;
    }
    if (!resource.mime_type.empty()) {
        json["mimeType"] = resource.mime_type;
    }
    return json;
}

inline core::Result<Resource> resource_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(resource_json_error("resource must be an object"));
    }
    if (!json.contains("uri") || !json.at("uri").is_string()) {
        return std::unexpected(resource_json_error("resource requires a string uri"));
    }
    if (!json.contains("name") || !json.at("name").is_string()) {
        return std::unexpected(resource_json_error("resource requires a string name"));
    }

    Resource resource;
    resource.uri = json.at("uri").get<std::string>();
    resource.name = json.at("name").get<std::string>();
    if (json.contains("description")) {
        if (!json.at("description").is_string()) {
            return std::unexpected(resource_json_error("resource description must be a string"));
        }
        resource.description = json.at("description").get<std::string>();
    }
    if (json.contains("mimeType")) {
        if (!json.at("mimeType").is_string()) {
            return std::unexpected(resource_json_error("resource mimeType must be a string"));
        }
        resource.mime_type = json.at("mimeType").get<std::string>();
    }
    return resource;
}

inline Json resource_template_to_json(const ResourceTemplate& resource_template) {
    Json json = Json::object();
    json["uriTemplate"] = resource_template.uri_template;
    json["name"] = resource_template.name;
    if (!resource_template.description.empty()) {
        json["description"] = resource_template.description;
    }
    if (!resource_template.mime_type.empty()) {
        json["mimeType"] = resource_template.mime_type;
    }
    return json;
}

inline core::Result<ResourceTemplate> resource_template_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(resource_json_error("resource template must be an object"));
    }
    if (!json.contains("uriTemplate") || !json.at("uriTemplate").is_string()) {
        return std::unexpected(resource_json_error("resource template requires a string uriTemplate"));
    }
    if (!json.contains("name") || !json.at("name").is_string()) {
        return std::unexpected(resource_json_error("resource template requires a string name"));
    }

    ResourceTemplate resource_template;
    resource_template.uri_template = json.at("uriTemplate").get<std::string>();
    resource_template.name = json.at("name").get<std::string>();
    if (json.contains("description")) {
        if (!json.at("description").is_string()) {
            return std::unexpected(resource_json_error("resource template description must be a string"));
        }
        resource_template.description = json.at("description").get<std::string>();
    }
    if (json.contains("mimeType")) {
        if (!json.at("mimeType").is_string()) {
            return std::unexpected(resource_json_error("resource template mimeType must be a string"));
        }
        resource_template.mime_type = json.at("mimeType").get<std::string>();
    }
    return resource_template;
}

inline Json resources_list_result_to_json(const ResourcesListResult& result) {
    Json json = Json::object();
    json["resources"] = Json::array();
    for (const auto& resource : result.resources) {
        json["resources"].push_back(resource_to_json(resource));
    }
    if (result.next_cursor.has_value()) {
        json["nextCursor"] = *result.next_cursor;
    }
    return json;
}

inline core::Result<ResourcesListResult> resources_list_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(resource_json_error("resources/list result must be an object"));
    }
    if (!json.contains("resources") || !json.at("resources").is_array()) {
        return std::unexpected(resource_json_error("resources/list result requires a resources array"));
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
            return std::unexpected(resource_json_error("resources/list nextCursor must be a string"));
        }
        result.next_cursor = json.at("nextCursor").get<std::string>();
    }
    return result;
}

inline Json resource_templates_list_result_to_json(const ResourceTemplatesListResult& result) {
    Json json = Json::object();
    json["resourceTemplates"] = Json::array();
    for (const auto& resource_template : result.resource_templates) {
        json["resourceTemplates"].push_back(resource_template_to_json(resource_template));
    }
    if (result.next_cursor.has_value()) {
        json["nextCursor"] = *result.next_cursor;
    }
    return json;
}

inline core::Result<ResourceTemplatesListResult> resource_templates_list_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(resource_json_error("resources/templates/list result must be an object"));
    }
    if (!json.contains("resourceTemplates") || !json.at("resourceTemplates").is_array()) {
        return std::unexpected(
            resource_json_error("resources/templates/list result requires a resourceTemplates array"));
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
            return std::unexpected(resource_json_error("resources/templates/list nextCursor must be a string"));
        }
        result.next_cursor = json.at("nextCursor").get<std::string>();
    }
    return result;
}

inline Json resources_read_params_to_json(const ResourcesReadParams& params) {
    return Json{{"uri", params.uri}};
}

inline core::Result<ResourcesReadParams> resources_read_params_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(resource_json_error("resources/read params must be an object"));
    }
    if (!json.contains("uri") || !json.at("uri").is_string()) {
        return std::unexpected(resource_json_error("resources/read params require a string uri"));
    }
    return ResourcesReadParams{json.at("uri").get<std::string>()};
}

inline Json resources_subscribe_params_to_json(const ResourcesSubscribeParams& params) {
    return Json{{"uri", params.uri}};
}

inline core::Result<ResourcesSubscribeParams> resources_subscribe_params_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(resource_json_error("resources subscribe params must be an object"));
    }
    if (!json.contains("uri") || !json.at("uri").is_string()) {
        return std::unexpected(resource_json_error("resources subscribe params require a string uri"));
    }
    return ResourcesSubscribeParams{json.at("uri").get<std::string>()};
}

inline Json resources_unsubscribe_params_to_json(const ResourcesUnsubscribeParams& params) {
    return resources_subscribe_params_to_json(params);
}

inline core::Result<ResourcesUnsubscribeParams> resources_unsubscribe_params_from_json(const Json& json) {
    return resources_subscribe_params_from_json(json);
}

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
    return json;
}

inline core::Result<ResourceContents> resource_contents_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(resource_json_error("resource contents must be an object"));
    }
    if (!json.contains("uri") || !json.at("uri").is_string()) {
        return std::unexpected(resource_json_error("resource contents require a string uri"));
    }

    ResourceContents contents;
    contents.uri = json.at("uri").get<std::string>();
    if (json.contains("mimeType")) {
        if (!json.at("mimeType").is_string()) {
            return std::unexpected(resource_json_error("resource contents mimeType must be a string"));
        }
        contents.mime_type = json.at("mimeType").get<std::string>();
    }
    if (json.contains("text")) {
        if (!json.at("text").is_string()) {
            return std::unexpected(resource_json_error("resource contents text must be a string"));
        }
        contents.text = json.at("text").get<std::string>();
    }
    if (json.contains("blob")) {
        if (!json.at("blob").is_string()) {
            return std::unexpected(resource_json_error("resource contents blob must be a string"));
        }
        contents.blob = json.at("blob").get<std::string>();
    }
    if (!contents.text.has_value() && !contents.blob.has_value()) {
        return std::unexpected(resource_json_error("resource contents require text or blob"));
    }
    return contents;
}

inline Json resources_read_result_to_json(const ResourcesReadResult& result) {
    Json json = Json::object();
    json["contents"] = Json::array();
    for (const auto& contents : result.contents) {
        json["contents"].push_back(resource_contents_to_json(contents));
    }
    return json;
}

inline core::Result<ResourcesReadResult> resources_read_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(resource_json_error("resources/read result must be an object"));
    }
    if (!json.contains("contents") || !json.at("contents").is_array()) {
        return std::unexpected(resource_json_error("resources/read result requires a contents array"));
    }

    ResourcesReadResult result;
    for (const auto& item : json.at("contents")) {
        const auto contents = resource_contents_from_json(item);
        if (!contents) {
            return std::unexpected(contents.error());
        }
        result.contents.push_back(*contents);
    }
    return result;
}

} // namespace mcp::protocol
