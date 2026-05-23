#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/tool.hpp"
#include "mcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mcp::protocol {

struct SamplingMessage {
    std::string role;
    ContentBlock content;
};

struct ModelHint {
    std::string name;
};

struct ModelPreferences {
    std::vector<ModelHint> hints;
    std::optional<double> cost_priority;
    std::optional<double> speed_priority;
    std::optional<double> intelligence_priority;
};

struct CreateMessageParams {
    std::vector<SamplingMessage> messages;
    std::optional<ModelPreferences> model_preferences;
    std::optional<std::string> system_prompt;
    std::optional<std::string> include_context;
    double temperature = 0.0;
    int max_tokens = 0;
    std::vector<std::string> stop_sequences;
    Json metadata = Json::object();
};

struct CreateMessageResult {
    std::string role;
    ContentBlock content;
    std::string model;
    std::string stop_reason;
};

inline core::Error sampling_json_error(std::string message) {
    return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline Json sampling_message_to_json(const SamplingMessage& message) {
    return Json{{"role", message.role}, {"content", content_block_to_json(message.content)}};
}

inline core::Result<SamplingMessage> sampling_message_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(sampling_json_error("sampling message must be an object"));
    }
    if (!json.contains("role") || !json.at("role").is_string()) {
        return std::unexpected(sampling_json_error("sampling message requires a string role"));
    }
    if (!json.contains("content")) {
        return std::unexpected(sampling_json_error("sampling message requires content"));
    }
    const auto content = content_block_from_json(json.at("content"));
    if (!content) {
        return std::unexpected(content.error());
    }
    return SamplingMessage{json.at("role").get<std::string>(), *content};
}

inline Json model_hint_to_json(const ModelHint& hint) {
    Json json = Json::object();
    if (!hint.name.empty()) {
        json["name"] = hint.name;
    }
    return json;
}

inline core::Result<ModelHint> model_hint_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(sampling_json_error("model hint must be an object"));
    }
    ModelHint hint;
    if (json.contains("name")) {
        if (!json.at("name").is_string()) {
            return std::unexpected(sampling_json_error("model hint name must be a string"));
        }
        hint.name = json.at("name").get<std::string>();
    }
    return hint;
}

inline Json model_preferences_to_json(const ModelPreferences& preferences) {
    Json json = Json::object();
    if (!preferences.hints.empty()) {
        json["hints"] = Json::array();
        for (const auto& hint : preferences.hints) {
            json["hints"].push_back(model_hint_to_json(hint));
        }
    }
    if (preferences.cost_priority.has_value()) {
        json["costPriority"] = *preferences.cost_priority;
    }
    if (preferences.speed_priority.has_value()) {
        json["speedPriority"] = *preferences.speed_priority;
    }
    if (preferences.intelligence_priority.has_value()) {
        json["intelligencePriority"] = *preferences.intelligence_priority;
    }
    return json;
}

inline core::Result<ModelPreferences> model_preferences_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(sampling_json_error("model preferences must be an object"));
    }
    ModelPreferences preferences;
    if (json.contains("hints")) {
        if (!json.at("hints").is_array()) {
            return std::unexpected(sampling_json_error("model preferences hints must be an array"));
        }
        for (const auto& item : json.at("hints")) {
            const auto hint = model_hint_from_json(item);
            if (!hint) {
                return std::unexpected(hint.error());
            }
            preferences.hints.push_back(*hint);
        }
    }
    const auto read_priority = [&](const char* key, std::optional<double>& target) -> core::Result<core::Unit> {
        if (!json.contains(key)) {
            return core::Unit{};
        }
        if (!json.at(key).is_number()) {
            return std::unexpected(sampling_json_error(std::string("model preferences ") + key + " must be a number"));
        }
        target = json.at(key).get<double>();
        return core::Unit{};
    };
    if (const auto ok = read_priority("costPriority", preferences.cost_priority); !ok) {
        return std::unexpected(ok.error());
    }
    if (const auto ok = read_priority("speedPriority", preferences.speed_priority); !ok) {
        return std::unexpected(ok.error());
    }
    if (const auto ok = read_priority("intelligencePriority", preferences.intelligence_priority); !ok) {
        return std::unexpected(ok.error());
    }
    return preferences;
}

inline Json create_message_params_to_json(const CreateMessageParams& params) {
    Json json = Json::object();
    json["messages"] = Json::array();
    for (const auto& message : params.messages) {
        json["messages"].push_back(sampling_message_to_json(message));
    }
    if (params.model_preferences.has_value()) {
        json["modelPreferences"] = model_preferences_to_json(*params.model_preferences);
    }
    if (params.system_prompt.has_value()) {
        json["systemPrompt"] = *params.system_prompt;
    }
    if (params.include_context.has_value()) {
        json["includeContext"] = *params.include_context;
    }
    if (params.temperature != 0.0) {
        json["temperature"] = params.temperature;
    }
    json["maxTokens"] = params.max_tokens;
    if (!params.stop_sequences.empty()) {
        json["stopSequences"] = params.stop_sequences;
    }
    if (!params.metadata.empty()) {
        json["metadata"] = params.metadata;
    }
    return json;
}

inline core::Result<CreateMessageParams> create_message_params_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(sampling_json_error("sampling/createMessage params must be an object"));
    }
    if (!json.contains("messages") || !json.at("messages").is_array()) {
        return std::unexpected(sampling_json_error("sampling/createMessage params require a messages array"));
    }
    if (!json.contains("maxTokens") || !json.at("maxTokens").is_number_integer()) {
        return std::unexpected(sampling_json_error("sampling/createMessage params require integer maxTokens"));
    }

    CreateMessageParams params;
    for (const auto& item : json.at("messages")) {
        const auto message = sampling_message_from_json(item);
        if (!message) {
            return std::unexpected(message.error());
        }
        params.messages.push_back(*message);
    }
    if (json.contains("modelPreferences")) {
        const auto preferences = model_preferences_from_json(json.at("modelPreferences"));
        if (!preferences) {
            return std::unexpected(preferences.error());
        }
        params.model_preferences = *preferences;
    }
    if (json.contains("systemPrompt")) {
        if (!json.at("systemPrompt").is_string()) {
            return std::unexpected(sampling_json_error("sampling systemPrompt must be a string"));
        }
        params.system_prompt = json.at("systemPrompt").get<std::string>();
    }
    if (json.contains("includeContext")) {
        if (!json.at("includeContext").is_string()) {
            return std::unexpected(sampling_json_error("sampling includeContext must be a string"));
        }
        params.include_context = json.at("includeContext").get<std::string>();
    }
    if (json.contains("temperature")) {
        if (!json.at("temperature").is_number()) {
            return std::unexpected(sampling_json_error("sampling temperature must be a number"));
        }
        params.temperature = json.at("temperature").get<double>();
    }
    params.max_tokens = json.at("maxTokens").get<int>();
    if (json.contains("stopSequences")) {
        if (!json.at("stopSequences").is_array()) {
            return std::unexpected(sampling_json_error("sampling stopSequences must be an array"));
        }
        for (const auto& item : json.at("stopSequences")) {
            if (!item.is_string()) {
                return std::unexpected(sampling_json_error("sampling stopSequences must contain strings"));
            }
            params.stop_sequences.push_back(item.get<std::string>());
        }
    }
    if (json.contains("metadata")) {
        if (!json.at("metadata").is_object()) {
            return std::unexpected(sampling_json_error("sampling metadata must be an object"));
        }
        params.metadata = json.at("metadata");
    }
    return params;
}

inline Json create_message_result_to_json(const CreateMessageResult& result) {
    Json json = Json::object();
    json["role"] = result.role;
    json["content"] = content_block_to_json(result.content);
    json["model"] = result.model;
    if (!result.stop_reason.empty()) {
        json["stopReason"] = result.stop_reason;
    }
    return json;
}

inline core::Result<CreateMessageResult> create_message_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(sampling_json_error("sampling/createMessage result must be an object"));
    }
    if (!json.contains("role") || !json.at("role").is_string()) {
        return std::unexpected(sampling_json_error("sampling/createMessage result requires a string role"));
    }
    if (!json.contains("content")) {
        return std::unexpected(sampling_json_error("sampling/createMessage result requires content"));
    }
    if (!json.contains("model") || !json.at("model").is_string()) {
        return std::unexpected(sampling_json_error("sampling/createMessage result requires a string model"));
    }
    const auto content = content_block_from_json(json.at("content"));
    if (!content) {
        return std::unexpected(content.error());
    }

    CreateMessageResult result;
    result.role = json.at("role").get<std::string>();
    result.content = *content;
    result.model = json.at("model").get<std::string>();
    if (json.contains("stopReason")) {
        if (!json.at("stopReason").is_string()) {
            return std::unexpected(sampling_json_error("sampling stopReason must be a string"));
        }
        result.stop_reason = json.at("stopReason").get<std::string>();
    }
    return result;
}

} // namespace mcp::protocol
