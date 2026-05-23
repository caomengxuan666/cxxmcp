#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/types.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mcp::protocol {

struct CompletionReference {
    std::string type;
    std::string name;
};

struct CompletionArgument {
    std::string name;
    std::string value;
};

struct CompleteParams {
    CompletionReference ref;
    CompletionArgument argument;
    Json context = Json::object();
};

struct CompletionResult {
    std::vector<std::string> values;
    std::optional<int> total;
    bool has_more = false;
};

struct CompleteResult {
    CompletionResult completion;
};

inline core::Error completion_json_error(std::string message) {
    return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline Json completion_reference_to_json(const CompletionReference& ref) {
    Json json = Json::object();
    json["type"] = ref.type;
    json["name"] = ref.name;
    return json;
}

inline core::Result<CompletionReference> completion_reference_from_json(const Json& json) {
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

inline Json completion_argument_to_json(const CompletionArgument& argument) {
    return Json{{"name", argument.name}, {"value", argument.value}};
}

inline core::Result<CompletionArgument> completion_argument_from_json(const Json& json) {
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

inline Json complete_params_to_json(const CompleteParams& params) {
    Json json = Json::object();
    json["ref"] = completion_reference_to_json(params.ref);
    json["argument"] = completion_argument_to_json(params.argument);
    if (!params.context.empty()) {
        json["context"] = params.context;
    }
    return json;
}

inline core::Result<CompleteParams> complete_params_from_json(const Json& json) {
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

inline Json completion_result_to_json(const CompletionResult& completion) {
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

inline core::Result<CompletionResult> completion_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(completion_json_error("completion result must be an object"));
    }
    if (!json.contains("values") || !json.at("values").is_array()) {
        return std::unexpected(completion_json_error("completion result requires a values array"));
    }

    CompletionResult completion;
    for (const auto& item : json.at("values")) {
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

inline Json complete_result_to_json(const CompleteResult& result) {
    return Json{{"completion", completion_result_to_json(result.completion)}};
}

inline core::Result<CompleteResult> complete_result_from_json(const Json& json) {
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

} // namespace mcp::protocol
