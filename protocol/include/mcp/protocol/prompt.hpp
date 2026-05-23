#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/tool.hpp"
#include "mcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mcp::protocol {

struct PromptArgument {
    std::string name;
    std::string description;
    bool required = false;
};

struct Prompt {
    std::string name;
    std::string description;
    std::vector<PromptArgument> arguments;
};

struct PromptsListResult {
    std::vector<Prompt> prompts;
    std::optional<std::string> next_cursor;
};

struct PromptsGetParams {
    std::string name;
    Json arguments = Json::object();
};

struct PromptMessage {
    std::string role;
    ContentBlock content;
};

struct PromptsGetResult {
    std::string description;
    std::vector<PromptMessage> messages;
};

inline core::Error prompt_json_error(std::string message) {
    return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline Json prompt_argument_to_json(const PromptArgument& argument) {
    Json json = Json::object();
    json["name"] = argument.name;
    if (!argument.description.empty()) {
        json["description"] = argument.description;
    }
    if (argument.required) {
        json["required"] = true;
    }
    return json;
}

inline core::Result<PromptArgument> prompt_argument_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(prompt_json_error("prompt argument must be an object"));
    }
    if (!json.contains("name") || !json.at("name").is_string()) {
        return std::unexpected(prompt_json_error("prompt argument requires a string name"));
    }

    PromptArgument argument;
    argument.name = json.at("name").get<std::string>();
    if (json.contains("description")) {
        if (!json.at("description").is_string()) {
            return std::unexpected(prompt_json_error("prompt argument description must be a string"));
        }
        argument.description = json.at("description").get<std::string>();
    }
    if (json.contains("required")) {
        if (!json.at("required").is_boolean()) {
            return std::unexpected(prompt_json_error("prompt argument required must be a boolean"));
        }
        argument.required = json.at("required").get<bool>();
    }
    return argument;
}

inline Json prompt_to_json(const Prompt& prompt) {
    Json json = Json::object();
    json["name"] = prompt.name;
    if (!prompt.description.empty()) {
        json["description"] = prompt.description;
    }
    if (!prompt.arguments.empty()) {
        json["arguments"] = Json::array();
        for (const auto& argument : prompt.arguments) {
            json["arguments"].push_back(prompt_argument_to_json(argument));
        }
    }
    return json;
}

inline core::Result<Prompt> prompt_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(prompt_json_error("prompt must be an object"));
    }
    if (!json.contains("name") || !json.at("name").is_string()) {
        return std::unexpected(prompt_json_error("prompt requires a string name"));
    }

    Prompt prompt;
    prompt.name = json.at("name").get<std::string>();
    if (json.contains("description")) {
        if (!json.at("description").is_string()) {
            return std::unexpected(prompt_json_error("prompt description must be a string"));
        }
        prompt.description = json.at("description").get<std::string>();
    }
    if (json.contains("arguments")) {
        if (!json.at("arguments").is_array()) {
            return std::unexpected(prompt_json_error("prompt arguments must be an array"));
        }
        for (const auto& item : json.at("arguments")) {
            const auto argument = prompt_argument_from_json(item);
            if (!argument) {
                return std::unexpected(argument.error());
            }
            prompt.arguments.push_back(*argument);
        }
    }
    return prompt;
}

inline Json prompts_list_result_to_json(const PromptsListResult& result) {
    Json json = Json::object();
    json["prompts"] = Json::array();
    for (const auto& prompt : result.prompts) {
        json["prompts"].push_back(prompt_to_json(prompt));
    }
    if (result.next_cursor.has_value()) {
        json["nextCursor"] = *result.next_cursor;
    }
    return json;
}

inline core::Result<PromptsListResult> prompts_list_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(prompt_json_error("prompts/list result must be an object"));
    }
    if (!json.contains("prompts") || !json.at("prompts").is_array()) {
        return std::unexpected(prompt_json_error("prompts/list result requires a prompts array"));
    }

    PromptsListResult result;
    for (const auto& item : json.at("prompts")) {
        const auto prompt = prompt_from_json(item);
        if (!prompt) {
            return std::unexpected(prompt.error());
        }
        result.prompts.push_back(*prompt);
    }
    if (json.contains("nextCursor")) {
        if (!json.at("nextCursor").is_string()) {
            return std::unexpected(prompt_json_error("prompts/list nextCursor must be a string"));
        }
        result.next_cursor = json.at("nextCursor").get<std::string>();
    }
    return result;
}

inline Json prompts_get_params_to_json(const PromptsGetParams& params) {
    Json json = Json::object();
    json["name"] = params.name;
    if (!params.arguments.empty()) {
        json["arguments"] = params.arguments;
    }
    return json;
}

inline core::Result<PromptsGetParams> prompts_get_params_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(prompt_json_error("prompts/get params must be an object"));
    }
    if (!json.contains("name") || !json.at("name").is_string()) {
        return std::unexpected(prompt_json_error("prompts/get params require a string name"));
    }

    PromptsGetParams params;
    params.name = json.at("name").get<std::string>();
    if (json.contains("arguments")) {
        if (!json.at("arguments").is_object()) {
            return std::unexpected(prompt_json_error("prompts/get arguments must be an object"));
        }
        params.arguments = json.at("arguments");
    }
    return params;
}

inline Json prompt_message_to_json(const PromptMessage& message) {
    Json json = Json::object();
    json["role"] = message.role;
    json["content"] = content_block_to_json(message.content);
    return json;
}

inline core::Result<PromptMessage> prompt_message_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(prompt_json_error("prompt message must be an object"));
    }
    if (!json.contains("role") || !json.at("role").is_string()) {
        return std::unexpected(prompt_json_error("prompt message requires a string role"));
    }
    if (!json.contains("content")) {
        return std::unexpected(prompt_json_error("prompt message requires content"));
    }

    const auto content = content_block_from_json(json.at("content"));
    if (!content) {
        return std::unexpected(content.error());
    }

    PromptMessage message;
    message.role = json.at("role").get<std::string>();
    message.content = *content;
    return message;
}

inline Json prompts_get_result_to_json(const PromptsGetResult& result) {
    Json json = Json::object();
    if (!result.description.empty()) {
        json["description"] = result.description;
    }
    json["messages"] = Json::array();
    for (const auto& message : result.messages) {
        json["messages"].push_back(prompt_message_to_json(message));
    }
    return json;
}

inline core::Result<PromptsGetResult> prompts_get_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(prompt_json_error("prompts/get result must be an object"));
    }
    if (!json.contains("messages") || !json.at("messages").is_array()) {
        return std::unexpected(prompt_json_error("prompts/get result requires a messages array"));
    }

    PromptsGetResult result;
    if (json.contains("description")) {
        if (!json.at("description").is_string()) {
            return std::unexpected(prompt_json_error("prompts/get description must be a string"));
        }
        result.description = json.at("description").get<std::string>();
    }
    for (const auto& item : json.at("messages")) {
        const auto message = prompt_message_from_json(item);
        if (!message) {
            return std::unexpected(message.error());
        }
        result.messages.push_back(*message);
    }
    return result;
}

} // namespace mcp::protocol
