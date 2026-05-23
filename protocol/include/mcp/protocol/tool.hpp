#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcp::protocol {

struct ContentBlock {
    std::string type = "text";
    std::string text;
    Json data = Json::object();
};

struct ToolDefinition {
    std::string name;
    std::string description;
    Json input_schema = Json::object();
    bool streaming = false;
};

struct ToolCall {
    std::string name;
    Json arguments = Json::object();
};

struct ToolResult {
    std::vector<ContentBlock> content;
    std::optional<Json> structured_content;
    bool is_error = false;
};

inline Json content_block_to_json(const ContentBlock& block) {
    Json json = Json::object();
    json["type"] = block.type;
    json["text"] = block.text;
    if (!block.data.empty()) {
        json["data"] = block.data;
    }
    return json;
}

inline core::Result<ContentBlock> content_block_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(core::Error{
            static_cast<int>(ErrorCode::InvalidRequest),
            "content block must be an object",
            {},
        });
    }

    ContentBlock block;
    if (json.contains("type")) {
        if (!json.at("type").is_string()) {
            return std::unexpected(core::Error{
                static_cast<int>(ErrorCode::InvalidRequest),
                "content block type must be a string",
                {},
            });
        }
        block.type = json.at("type").get<std::string>();
    }

    if (json.contains("text")) {
        if (!json.at("text").is_string()) {
            return std::unexpected(core::Error{
                static_cast<int>(ErrorCode::InvalidRequest),
                "content block text must be a string",
                {},
            });
        }
        block.text = json.at("text").get<std::string>();
    }

    if (json.contains("data")) {
        block.data = json.at("data");
    }

    return block;
}

inline Json tool_definition_to_json(const ToolDefinition& definition) {
    Json json = Json::object();
    json["name"] = definition.name;
    json["description"] = definition.description;
    json["inputSchema"] = definition.input_schema;
    json["streaming"] = definition.streaming;
    return json;
}

inline core::Result<ToolDefinition> tool_definition_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(core::Error{
            static_cast<int>(ErrorCode::InvalidRequest),
            "tool definition must be an object",
            {},
        });
    }

    ToolDefinition definition;
    if (!json.contains("name") || !json.at("name").is_string()) {
        return std::unexpected(core::Error{
            static_cast<int>(ErrorCode::InvalidRequest),
            "tool definition requires a string name",
            {},
        });
    }
    definition.name = json.at("name").get<std::string>();

    if (json.contains("description")) {
        if (!json.at("description").is_string()) {
            return std::unexpected(core::Error{
                static_cast<int>(ErrorCode::InvalidRequest),
                "tool definition description must be a string",
                {},
            });
        }
        definition.description = json.at("description").get<std::string>();
    }

    if (json.contains("inputSchema")) {
        definition.input_schema = json.at("inputSchema");
    }

    if (json.contains("streaming")) {
        if (!json.at("streaming").is_boolean()) {
            return std::unexpected(core::Error{
                static_cast<int>(ErrorCode::InvalidRequest),
                "tool definition streaming must be a boolean",
                {},
            });
        }
        definition.streaming = json.at("streaming").get<bool>();
    }

    return definition;
}

inline Json tool_result_to_json(const ToolResult& result) {
    Json json = Json::object();
    json["content"] = Json::array();
    for (const auto& block : result.content) {
        json["content"].push_back(content_block_to_json(block));
    }
    if (result.structured_content.has_value()) {
        json["structuredContent"] = *result.structured_content;
    }
    if (result.is_error) {
        json["isError"] = true;
    }
    return json;
}

inline core::Result<ToolResult> tool_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(core::Error{
            static_cast<int>(ErrorCode::InvalidRequest),
            "tool result must be an object",
            {},
        });
    }

    ToolResult result;
    if (json.contains("content")) {
        if (!json.at("content").is_array()) {
            return std::unexpected(core::Error{
                static_cast<int>(ErrorCode::InvalidRequest),
                "tool result content must be an array",
                {},
            });
        }
        for (const auto& item : json.at("content")) {
            const auto block = content_block_from_json(item);
            if (!block) {
                return std::unexpected(block.error());
            }
            result.content.push_back(*block);
        }
    }

    if (json.contains("structuredContent")) {
        result.structured_content = json.at("structuredContent");
    }

    if (json.contains("isError")) {
        if (!json.at("isError").is_boolean()) {
            return std::unexpected(core::Error{
                static_cast<int>(ErrorCode::InvalidRequest),
                "tool result isError must be a boolean",
                {},
            });
        }
        result.is_error = json.at("isError").get<bool>();
    }

    return result;
}

} // namespace mcp::protocol
