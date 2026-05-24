#pragma once

/// @file cxxmcp/protocol/completion.hpp
/// @brief Completion request and result payloads.
///
/// Completion helps clients fill prompt arguments or resource-template
/// variables by sending a `completion/complete` request for a referenced MCP
/// object and one argument value prefix.

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mcp::protocol {

    /// @brief Reference to the prompt or resource template being completed.
    struct CompletionReference {
        /// Reference kind, for example a prompt or resource-template reference.
        std::string type;
        /// Name of the referenced prompt or template.
        std::string name;
    };

    /// @brief Argument value being completed.
    struct CompletionArgument {
        /// Argument name within the referenced object.
        std::string name;
        /// Current argument value or prefix supplied by the caller.
        std::string value;
    };

    /// @brief Parameters for `completion/complete`.
    struct CompleteParams {
        /// Prompt or resource-template reference.
        CompletionReference ref;
        /// Argument being completed.
        CompletionArgument argument;
        /// Optional contextual argument values used to improve completions.
        Json context = Json::object();
    };

    /// @brief Completion candidates returned by the server.
    struct CompletionResult {
        /// Candidate values.
        std::vector<std::string> values;
        /// Optional total number of matches known to the server.
        std::optional<int> total;
        /// Whether additional matches are available beyond the returned values.
        bool has_more = false;
    };

    /// @brief Result object for `completion/complete`.
    struct CompleteResult {
        /// Completion payload nested under the protocol `completion` field.
        CompletionResult completion;
    };

    /// @brief Builds an InvalidRequest error for completion JSON validation failures.
    inline core::Error completion_json_error(std::string message) {
        return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
    }

    /// @brief Serializes a completion reference.
    inline Json completion_reference_to_json(const CompletionReference &ref) {
        Json json = Json::object();
        json["type"] = ref.type;
        json["name"] = ref.name;
        return json;
    }

    /// @brief Parses a completion reference.
    /// @return Parsed reference or validation error.
    inline core::Result<CompletionReference> completion_reference_from_json(const Json &json) {
        if (!json.is_object()) {
            return std::unexpected(completion_json_error("completion ref must be an object"));
        }
        if (!json.contains("type") || !json.at("type").is_string()) {
            return std::unexpected(completion_json_error("completion ref requires a string type"));
        }
        if (!json.contains("name") || !json.at("name").is_string()) {
            return std::unexpected(completion_json_error("completion ref requires a string name"));
        }
        return CompletionReference{json.at("type").get<std::string>(), json.at("name").get<std::string>()};
    }

    /// @brief Serializes a completion argument.
    inline Json completion_argument_to_json(const CompletionArgument &argument) {
        return Json{{"name", argument.name}, {"value", argument.value}};
    }

    /// @brief Parses a completion argument.
    /// @return Parsed argument or validation error.
    inline core::Result<CompletionArgument> completion_argument_from_json(const Json &json) {
        if (!json.is_object()) {
            return std::unexpected(completion_json_error("completion argument must be an object"));
        }
        if (!json.contains("name") || !json.at("name").is_string()) {
            return std::unexpected(completion_json_error("completion argument requires a string name"));
        }
        if (!json.contains("value") || !json.at("value").is_string()) {
            return std::unexpected(completion_json_error("completion argument requires a string value"));
        }
        return CompletionArgument{json.at("name").get<std::string>(), json.at("value").get<std::string>()};
    }

    /// @brief Serializes `completion/complete` params.
    inline Json complete_params_to_json(const CompleteParams &params) {
        Json json = Json::object();
        json["ref"] = completion_reference_to_json(params.ref);
        json["argument"] = completion_argument_to_json(params.argument);
        if (!params.context.empty()) {
            json["context"] = params.context;
        }
        return json;
    }

    /// @brief Parses `completion/complete` params.
    /// @return Parsed params or validation error.
    inline core::Result<CompleteParams> complete_params_from_json(const Json &json) {
        if (!json.is_object()) {
            return std::unexpected(completion_json_error("completion params must be an object"));
        }
        if (!json.contains("ref")) {
            return std::unexpected(completion_json_error("completion params require ref"));
        }
        if (!json.contains("argument")) {
            return std::unexpected(completion_json_error("completion params require argument"));
        }

        const auto ref = completion_reference_from_json(json.at("ref"));
        if (!ref) {
            return std::unexpected(ref.error());
        }
        const auto argument = completion_argument_from_json(json.at("argument"));
        if (!argument) {
            return std::unexpected(argument.error());
        }

        CompleteParams params;
        params.ref = *ref;
        params.argument = *argument;
        if (json.contains("context")) {
            if (!json.at("context").is_object()) {
                return std::unexpected(completion_json_error("completion context must be an object"));
            }
            params.context = json.at("context");
        }
        return params;
    }

    /// @brief Serializes completion candidates.
    inline Json completion_result_to_json(const CompletionResult &completion) {
        Json json = Json::object();
        json["values"] = completion.values;
        if (completion.total.has_value()) {
            json["total"] = *completion.total;
        }
        if (completion.has_more) {
            json["hasMore"] = true;
        }
        return json;
    }

    /// @brief Parses completion candidates.
    /// @return Parsed completion payload or validation error.
    inline core::Result<CompletionResult> completion_result_from_json(const Json &json) {
        if (!json.is_object()) {
            return std::unexpected(completion_json_error("completion result must be an object"));
        }
        if (!json.contains("values") || !json.at("values").is_array()) {
            return std::unexpected(completion_json_error("completion result requires a values array"));
        }

        CompletionResult completion;
        for (const auto &item: json.at("values")) {
            if (!item.is_string()) {
                return std::unexpected(completion_json_error("completion values must be strings"));
            }
            completion.values.push_back(item.get<std::string>());
        }
        if (json.contains("total")) {
            if (!json.at("total").is_number_integer()) {
                return std::unexpected(completion_json_error("completion total must be an integer"));
            }
            completion.total = json.at("total").get<int>();
        }
        if (json.contains("hasMore")) {
            if (!json.at("hasMore").is_boolean()) {
                return std::unexpected(completion_json_error("completion hasMore must be a boolean"));
            }
            completion.has_more = json.at("hasMore").get<bool>();
        }
        return completion;
    }

    /// @brief Serializes a `completion/complete` result.
    inline Json complete_result_to_json(const CompleteResult &result) {
        return Json{{"completion", completion_result_to_json(result.completion)}};
    }

    /// @brief Parses a `completion/complete` result.
    /// @return Parsed result or validation error.
    inline core::Result<CompleteResult> complete_result_from_json(const Json &json) {
        if (!json.is_object()) {
            return std::unexpected(completion_json_error("completion result envelope must be an object"));
        }
        if (!json.contains("completion")) {
            return std::unexpected(completion_json_error("completion result envelope requires completion"));
        }
        const auto completion = completion_result_from_json(json.at("completion"));
        if (!completion) {
            return std::unexpected(completion.error());
        }
        return CompleteResult{*completion};
    }

}// namespace mcp::protocol
