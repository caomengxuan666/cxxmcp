#pragma once

/// @file cxxmcp/protocol/sampling.hpp
/// @brief Client-side model sampling request and response payloads.
///
/// Sampling lets a server ask a capable client to create a model message
/// through `sampling/createMessage`. The request carries chat-like messages,
/// optional model preferences, and generation controls; the response carries
/// one generated message and the model that produced it.

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mcp::protocol {

    /// @brief Input message supplied to a sampling request.
    struct SamplingMessage {
        /// Message role understood by the sampling client.
        std::string role;
        /// Message content block.
        ContentBlock content;
    };

    /// @brief Soft model name hint for sampling.
    struct ModelHint {
        /// Model family, name, or alias preferred by the requester.
        std::string name;
    };

    /// @brief Preferences used by the client when choosing a model.
    struct ModelPreferences {
        /// Ordered or unordered model hints supplied by the requester.
        std::vector<ModelHint> hints;
        /// Optional priority for low cost, typically normalized by the peer.
        std::optional<double> cost_priority;
        /// Optional priority for low latency, typically normalized by the peer.
        std::optional<double> speed_priority;
        /// Optional priority for model capability, typically normalized by the peer.
        std::optional<double> intelligence_priority;
    };

    /// @brief Parameters for `sampling/createMessage`.
    struct CreateMessageParams {
        /// Conversation messages to sample from.
        std::vector<SamplingMessage> messages;
        /// Optional model selection preferences.
        std::optional<ModelPreferences> model_preferences;
        /// Optional system prompt supplied outside the message array.
        std::optional<std::string> system_prompt;
        /// Optional instruction for whether client context should be included.
        std::optional<std::string> include_context;
        /// Optional sampling temperature. Zero means omitted during serialization.
        double temperature = 0.0;
        /// Maximum tokens the client may generate.
        int max_tokens = 0;
        /// Optional stop sequences.
        std::vector<std::string> stop_sequences;
        /// Optional metadata object carried through the sampling request.
        Json metadata = Json::object();
        /// Optional task request parameters for asynchronous sampling.
        std::optional<TaskRequestParameters> task;
    };

    /// @brief Result object for `sampling/createMessage`.
    struct CreateMessageResult {
        /// Role assigned to the generated message.
        std::string role;
        /// Generated content.
        ContentBlock content;
        /// Model identifier selected by the client.
        std::string model;
        /// Optional reason generation stopped.
        std::string stop_reason;
    };

    /// @brief Builds an InvalidRequest error for sampling JSON validation failures.
    inline core::Error sampling_json_error(std::string message) {
        return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
    }

    /// @brief Serializes a sampling message.
    inline Json sampling_message_to_json(const SamplingMessage &message) {
        return Json{{"role", message.role}, {"content", content_block_to_json(message.content)}};
    }

    /// @brief Parses a sampling message.
    /// @return Parsed message or validation error.
    inline core::Result<SamplingMessage> sampling_message_from_json(const Json &json) {
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

    /// @brief Serializes a model hint.
    inline Json model_hint_to_json(const ModelHint &hint) {
        Json json = Json::object();
        if (!hint.name.empty()) {
            json["name"] = hint.name;
        }
        return json;
    }

    /// @brief Parses a model hint.
    /// @return Parsed hint or validation error.
    inline core::Result<ModelHint> model_hint_from_json(const Json &json) {
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

    /// @brief Serializes model preferences.
    inline Json model_preferences_to_json(const ModelPreferences &preferences) {
        Json json = Json::object();
        if (!preferences.hints.empty()) {
            json["hints"] = Json::array();
            for (const auto &hint: preferences.hints) {
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

    /// @brief Parses model preferences.
    /// @return Parsed preferences or validation error.
    inline core::Result<ModelPreferences> model_preferences_from_json(const Json &json) {
        if (!json.is_object()) {
            return std::unexpected(sampling_json_error("model preferences must be an object"));
        }
        ModelPreferences preferences;
        if (json.contains("hints")) {
            if (!json.at("hints").is_array()) {
                return std::unexpected(sampling_json_error("model preferences hints must be an array"));
            }
            for (const auto &item: json.at("hints")) {
                const auto hint = model_hint_from_json(item);
                if (!hint) {
                    return std::unexpected(hint.error());
                }
                preferences.hints.push_back(*hint);
            }
        }
        const auto read_priority = [&](const char *key, std::optional<double> &target) -> core::Result<core::Unit> {
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

    /// @brief Serializes `sampling/createMessage` params.
    inline Json create_message_params_to_json(const CreateMessageParams &params) {
        Json json = Json::object();
        json["messages"] = Json::array();
        for (const auto &message: params.messages) {
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
        if (params.task.has_value()) {
            json["task"] = task_request_parameters_to_json(*params.task);
        }
        return json;
    }

    /// @brief Parses `sampling/createMessage` params.
    /// @return Parsed params or validation error.
    inline core::Result<CreateMessageParams> create_message_params_from_json(const Json &json) {
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
        for (const auto &item: json.at("messages")) {
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
            for (const auto &item: json.at("stopSequences")) {
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
        if (json.contains("task")) {
            const auto task = task_request_parameters_from_json(json.at("task"));
            if (!task) {
                return std::unexpected(task.error());
            }
            params.task = *task;
        }
        return params;
    }

    /// @brief Serializes a `sampling/createMessage` result.
    inline Json create_message_result_to_json(const CreateMessageResult &result) {
        Json json = Json::object();
        json["role"] = result.role;
        json["content"] = content_block_to_json(result.content);
        json["model"] = result.model;
        if (!result.stop_reason.empty()) {
            json["stopReason"] = result.stop_reason;
        }
        return json;
    }

    /// @brief Parses a `sampling/createMessage` result.
    /// @return Parsed result or validation error.
    inline core::Result<CreateMessageResult> create_message_result_from_json(const Json &json) {
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

}// namespace mcp::protocol
