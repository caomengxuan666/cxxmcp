#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mcp::protocol {

struct Root {
    std::string uri;
    std::string name;
};

struct RootsListResult {
    std::vector<Root> roots;
};

inline core::Error roots_json_error(std::string message) {
    return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline Json root_to_json(const Root& root) {
    Json json = Json::object();
    json["uri"] = root.uri;
    if (!root.name.empty()) {
        json["name"] = root.name;
    }
    return json;
}

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
    return root;
}

inline Json roots_list_result_to_json(const RootsListResult& result) {
    Json json = Json::object();
    json["roots"] = Json::array();
    for (const auto& root : result.roots) {
        json["roots"].push_back(root_to_json(root));
    }
    return json;
}

inline core::Result<RootsListResult> roots_list_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(roots_json_error("roots/list result must be an object"));
    }
    if (!json.contains("roots") || !json.at("roots").is_array()) {
        return std::unexpected(roots_json_error("roots/list result requires a roots array"));
    }

    RootsListResult result;
    for (const auto& item : json.at("roots")) {
        const auto root = root_from_json(item);
        if (!root) {
            return std::unexpected(root.error());
        }
        result.roots.push_back(*root);
    }
    return result;
}

} // namespace mcp::protocol
