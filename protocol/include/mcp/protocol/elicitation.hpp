#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/task.hpp"
#include "mcp/protocol/types.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace mcp::protocol {

enum class ElicitationAction {
    Accept,
    Decline,
    Cancel,
};

enum class ElicitationMode {
    Form,
    Url,
};

struct StringSchema {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::string> format;
    std::optional<std::string> default_value;
};

struct NumberSchema {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> default_value;
};

struct IntegerSchema {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::int64_t> minimum;
    std::optional<std::int64_t> maximum;
    std::optional<std::int64_t> default_value;
};

struct BooleanSchema {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<bool> default_value;
};

struct EnumSchema {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::vector<std::string> values;
    std::optional<std::string> default_value;
};

using PrimitiveSchema = std::variant<StringSchema, NumberSchema, IntegerSchema, BooleanSchema, EnumSchema>;

struct ElicitationSchema {
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::map<std::string, PrimitiveSchema> properties;
    std::vector<std::string> required;

    class Builder {
    public:
        Builder& title(std::string value) {
            title_ = std::move(value);
            return *this;
        }

        Builder& description(std::string value) {
            description_ = std::move(value);
            return *this;
        }

        Builder& required_string(std::string name,
                                 std::optional<std::string> default_value = std::nullopt,
                                 std::optional<std::string> format = std::nullopt);
        Builder& optional_string(std::string name,
                                 std::optional<std::string> default_value = std::nullopt,
                                 std::optional<std::string> format = std::nullopt);
        Builder& required_number(std::string name,
                                 std::optional<double> minimum = std::nullopt,
                                 std::optional<double> maximum = std::nullopt,
                                 std::optional<double> default_value = std::nullopt);
        Builder& optional_number(std::string name,
                                 std::optional<double> minimum = std::nullopt,
                                 std::optional<double> maximum = std::nullopt,
                                 std::optional<double> default_value = std::nullopt);
        Builder& required_integer(std::string name,
                                  std::optional<std::int64_t> minimum = std::nullopt,
                                  std::optional<std::int64_t> maximum = std::nullopt,
                                  std::optional<std::int64_t> default_value = std::nullopt);
        Builder& optional_integer(std::string name,
                                  std::optional<std::int64_t> minimum = std::nullopt,
                                  std::optional<std::int64_t> maximum = std::nullopt,
                                  std::optional<std::int64_t> default_value = std::nullopt);
        Builder& required_bool(std::string name, std::optional<bool> default_value = std::nullopt);
        Builder& optional_bool(std::string name, std::optional<bool> default_value = std::nullopt);
        Builder& required_email(std::string name, std::optional<std::string> default_value = std::nullopt);
        Builder& optional_email(std::string name, std::optional<std::string> default_value = std::nullopt);
        Builder& required_enum(std::string name,
                               std::vector<std::string> values,
                               std::optional<std::string> default_value = std::nullopt);
        Builder& optional_enum(std::string name,
                               std::vector<std::string> values,
                               std::optional<std::string> default_value = std::nullopt);

        core::Result<ElicitationSchema> build() const;

    private:
        static bool validate_name(const std::string& name);

        Builder& add_required(std::string name, PrimitiveSchema schema);
        Builder& add_optional(std::string name, PrimitiveSchema schema);

        std::optional<std::string> title_;
        std::optional<std::string> description_;
        std::map<std::string, PrimitiveSchema> properties_;
        std::vector<std::string> required_;
    };
};

struct CreateElicitationRequestParam {
    std::string message;
    ElicitationMode mode = ElicitationMode::Form;
    std::optional<std::string> elicitation_id;
    std::optional<std::string> url;
    ElicitationSchema requested_schema;
    std::optional<Json> request_state;
    std::optional<TaskRequestParameters> task;
};

struct CreateElicitationResult {
    ElicitationAction action = ElicitationAction::Cancel;
    std::optional<Json> content;
};

struct ElicitationCompleteNotificationParams {
    std::string elicitation_id;
};

inline std::string elicitation_action_to_string(ElicitationAction action) {
    switch (action) {
    case ElicitationAction::Accept:
        return "accept";
    case ElicitationAction::Decline:
        return "decline";
    case ElicitationAction::Cancel:
        return "cancel";
    }
    return "cancel";
}

inline std::optional<ElicitationAction> elicitation_action_from_string(const std::string& value) {
    if (value == "accept") {
        return ElicitationAction::Accept;
    }
    if (value == "decline") {
        return ElicitationAction::Decline;
    }
    if (value == "cancel") {
        return ElicitationAction::Cancel;
    }
    return std::nullopt;
}

inline std::string elicitation_mode_to_string(ElicitationMode mode) {
    switch (mode) {
    case ElicitationMode::Form:
        return "form";
    case ElicitationMode::Url:
        return "url";
    }
    return "form";
}

inline std::optional<ElicitationMode> elicitation_mode_from_string(const std::string& value) {
    if (value == "form") {
        return ElicitationMode::Form;
    }
    if (value == "url") {
        return ElicitationMode::Url;
    }
    return std::nullopt;
}

inline Json string_schema_to_json(const StringSchema& schema) {
    Json json = Json::object();
    json["type"] = "string";
    if (schema.title.has_value()) {
        json["title"] = *schema.title;
    }
    if (schema.description.has_value()) {
        json["description"] = *schema.description;
    }
    if (schema.format.has_value()) {
        json["format"] = *schema.format;
    }
    if (schema.default_value.has_value()) {
        json["default"] = *schema.default_value;
    }
    return json;
}

inline Json number_schema_to_json(const NumberSchema& schema) {
    Json json = Json::object();
    json["type"] = "number";
    if (schema.title.has_value()) {
        json["title"] = *schema.title;
    }
    if (schema.description.has_value()) {
        json["description"] = *schema.description;
    }
    if (schema.minimum.has_value()) {
        json["minimum"] = *schema.minimum;
    }
    if (schema.maximum.has_value()) {
        json["maximum"] = *schema.maximum;
    }
    if (schema.default_value.has_value()) {
        json["default"] = *schema.default_value;
    }
    return json;
}

inline Json integer_schema_to_json(const IntegerSchema& schema) {
    Json json = Json::object();
    json["type"] = "integer";
    if (schema.title.has_value()) {
        json["title"] = *schema.title;
    }
    if (schema.description.has_value()) {
        json["description"] = *schema.description;
    }
    if (schema.minimum.has_value()) {
        json["minimum"] = *schema.minimum;
    }
    if (schema.maximum.has_value()) {
        json["maximum"] = *schema.maximum;
    }
    if (schema.default_value.has_value()) {
        json["default"] = *schema.default_value;
    }
    return json;
}

inline Json boolean_schema_to_json(const BooleanSchema& schema) {
    Json json = Json::object();
    json["type"] = "boolean";
    if (schema.title.has_value()) {
        json["title"] = *schema.title;
    }
    if (schema.description.has_value()) {
        json["description"] = *schema.description;
    }
    if (schema.default_value.has_value()) {
        json["default"] = *schema.default_value;
    }
    return json;
}

inline Json enum_schema_to_json(const EnumSchema& schema) {
    Json json = Json::object();
    json["type"] = "string";
    json["enum"] = Json::array();
    for (const auto& value : schema.values) {
        json["enum"].push_back(value);
    }
    if (schema.title.has_value()) {
        json["title"] = *schema.title;
    }
    if (schema.description.has_value()) {
        json["description"] = *schema.description;
    }
    if (schema.default_value.has_value()) {
        json["default"] = *schema.default_value;
    }
    return json;
}

inline Json primitive_schema_to_json(const PrimitiveSchema& schema);

inline Json elicitation_schema_to_json(const ElicitationSchema& schema) {
    Json json = Json::object();
    json["type"] = "object";
    if (schema.title.has_value()) {
        json["title"] = *schema.title;
    }
    if (schema.description.has_value()) {
        json["description"] = *schema.description;
    }
    json["properties"] = Json::object();
    for (const auto& [name, property] : schema.properties) {
        json["properties"][name] = primitive_schema_to_json(property);
    }
    if (!schema.required.empty()) {
        json["required"] = schema.required;
    }
    return json;
}

inline core::Error elicitation_json_error(std::string message) {
    return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline core::Result<PrimitiveSchema> primitive_schema_from_json(const Json& json);

inline core::Result<ElicitationSchema> elicitation_schema_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(elicitation_json_error("elicitation schema must be an object"));
    }
    if (!json.contains("type") || !json.at("type").is_string() || json.at("type").get<std::string>() != "object") {
        return std::unexpected(elicitation_json_error("elicitation schema requires type object"));
    }
    if (!json.contains("properties") || !json.at("properties").is_object()) {
        return std::unexpected(elicitation_json_error("elicitation schema requires properties"));
    }

    ElicitationSchema schema;
    if (json.contains("title")) {
        if (!json.at("title").is_string()) {
            return std::unexpected(elicitation_json_error("elicitation schema title must be a string"));
        }
        schema.title = json.at("title").get<std::string>();
    }
    if (json.contains("description")) {
        if (!json.at("description").is_string()) {
            return std::unexpected(elicitation_json_error("elicitation schema description must be a string"));
        }
        schema.description = json.at("description").get<std::string>();
    }
    for (const auto& [name, property_json] : json.at("properties").items()) {
        const auto property = primitive_schema_from_json(property_json);
        if (!property) {
            return std::unexpected(property.error());
        }
        schema.properties.emplace(name, *property);
    }
    if (json.contains("required")) {
        if (!json.at("required").is_array()) {
            return std::unexpected(elicitation_json_error("elicitation schema required must be an array"));
        }
        for (const auto& item : json.at("required")) {
            if (!item.is_string()) {
                return std::unexpected(elicitation_json_error("elicitation schema required entries must be strings"));
            }
            schema.required.push_back(item.get<std::string>());
        }
    }
    return schema;
}

inline Json create_elicitation_request_param_to_json(const CreateElicitationRequestParam& request) {
    Json json = Json::object();
    json["message"] = request.message;
    json["mode"] = elicitation_mode_to_string(request.mode);
    if (request.request_state.has_value()) {
        json["requestState"] = *request.request_state;
    }
    if (request.task.has_value()) {
        json["task"] = task_request_parameters_to_json(*request.task);
    }

    if (request.mode == ElicitationMode::Url) {
        if (request.elicitation_id.has_value()) {
            json["elicitationId"] = *request.elicitation_id;
        }
        if (request.url.has_value()) {
            json["url"] = *request.url;
        }
        return json;
    }

    json["requestedSchema"] = elicitation_schema_to_json(request.requested_schema);
    return json;
}

inline core::Result<CreateElicitationRequestParam> create_elicitation_request_param_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(elicitation_json_error("elicitation request must be an object"));
    }
    if (!json.contains("message") || !json.at("message").is_string()) {
        return std::unexpected(elicitation_json_error("elicitation request requires a string message"));
    }
    ElicitationMode mode = ElicitationMode::Form;
    if (json.contains("mode")) {
        if (!json.at("mode").is_string()) {
            return std::unexpected(elicitation_json_error("elicitation request mode must be a string"));
        }
        const auto parsed_mode = elicitation_mode_from_string(json.at("mode").get<std::string>());
        if (!parsed_mode) {
            return std::unexpected(elicitation_json_error("elicitation request mode is not supported"));
        }
        mode = *parsed_mode;
    }

    CreateElicitationRequestParam request;
    request.message = json.at("message").get<std::string>();
    request.mode = mode;

    if (json.contains("requestState")) {
        request.request_state = json.at("requestState");
    }
    if (json.contains("task")) {
        const auto task = task_request_parameters_from_json(json.at("task"));
        if (!task) {
            return std::unexpected(task.error());
        }
        request.task = *task;
    }

    if (mode == ElicitationMode::Url) {
        if (!json.contains("elicitationId") || !json.at("elicitationId").is_string()) {
            return std::unexpected(elicitation_json_error("elicitation request requires elicitationId"));
        }
        if (!json.contains("url") || !json.at("url").is_string()) {
            return std::unexpected(elicitation_json_error("elicitation request requires url"));
        }
        request.elicitation_id = json.at("elicitationId").get<std::string>();
        request.url = json.at("url").get<std::string>();
        return request;
    }

    const auto schema_key = json.contains("requestedSchema") ? "requestedSchema" : "requested_schema";
    if (!json.contains(schema_key)) {
        return std::unexpected(elicitation_json_error("elicitation request requires requestedSchema"));
    }
    const auto schema = elicitation_schema_from_json(json.at(schema_key));
    if (!schema) {
        return std::unexpected(schema.error());
    }
    request.requested_schema = *schema;
    return request;
}

inline Json create_elicitation_result_to_json(const CreateElicitationResult& result) {
    Json json = Json::object();
    json["action"] = elicitation_action_to_string(result.action);
    if (result.content.has_value()) {
        json["content"] = *result.content;
    }
    return json;
}

inline core::Result<CreateElicitationResult> create_elicitation_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(elicitation_json_error("elicitation result must be an object"));
    }
    if (!json.contains("action") || !json.at("action").is_string()) {
        return std::unexpected(elicitation_json_error("elicitation result requires a string action"));
    }

    const auto action = elicitation_action_from_string(json.at("action").get<std::string>());
    if (!action.has_value()) {
        return std::unexpected(elicitation_json_error("elicitation action is not supported"));
    }

    CreateElicitationResult result;
    result.action = *action;
    if (json.contains("content")) {
        result.content = json.at("content");
    }
    return result;
}

inline Json elicitation_complete_notification_params_to_json(const ElicitationCompleteNotificationParams& params) {
    return Json{{"elicitationId", params.elicitation_id}};
}

inline core::Result<ElicitationCompleteNotificationParams> elicitation_complete_notification_params_from_json(
        const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(elicitation_json_error("elicitation complete notification must be an object"));
    }
    if (!json.contains("elicitationId") || !json.at("elicitationId").is_string()) {
        return std::unexpected(elicitation_json_error("elicitation complete notification requires elicitationId"));
    }
    return ElicitationCompleteNotificationParams{
        .elicitation_id = json.at("elicitationId").get<std::string>(),
    };
}

inline bool ElicitationSchema::Builder::validate_name(const std::string& name) {
    return !name.empty();
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::add_required(std::string name, PrimitiveSchema schema) {
    required_.push_back(name);
    properties_.insert_or_assign(std::move(name), std::move(schema));
    return *this;
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::add_optional(std::string name,
                                                                            PrimitiveSchema schema) {
    properties_.insert_or_assign(std::move(name), std::move(schema));
    return *this;
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_string(std::string name,
                                                                               std::optional<std::string> default_value,
                                                                               std::optional<std::string> format) {
    if (!validate_name(name)) {
        return *this;
    }
    StringSchema schema;
    schema.default_value = std::move(default_value);
    schema.format = std::move(format);
    return add_required(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_string(std::string name,
                                                                               std::optional<std::string> default_value,
                                                                               std::optional<std::string> format) {
    if (!validate_name(name)) {
        return *this;
    }
    StringSchema schema;
    schema.default_value = std::move(default_value);
    schema.format = std::move(format);
    return add_optional(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_number(std::string name,
                                                                               std::optional<double> minimum,
                                                                               std::optional<double> maximum,
                                                                               std::optional<double> default_value) {
    if (!validate_name(name)) {
        return *this;
    }
    NumberSchema schema;
    schema.minimum = minimum;
    schema.maximum = maximum;
    schema.default_value = default_value;
    return add_required(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_number(std::string name,
                                                                               std::optional<double> minimum,
                                                                               std::optional<double> maximum,
                                                                               std::optional<double> default_value) {
    if (!validate_name(name)) {
        return *this;
    }
    NumberSchema schema;
    schema.minimum = minimum;
    schema.maximum = maximum;
    schema.default_value = default_value;
    return add_optional(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_integer(std::string name,
                                                                                std::optional<std::int64_t> minimum,
                                                                                std::optional<std::int64_t> maximum,
                                                                                std::optional<std::int64_t> default_value) {
    if (!validate_name(name)) {
        return *this;
    }
    IntegerSchema schema;
    schema.minimum = minimum;
    schema.maximum = maximum;
    schema.default_value = default_value;
    return add_required(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_integer(std::string name,
                                                                                 std::optional<std::int64_t> minimum,
                                                                                 std::optional<std::int64_t> maximum,
                                                                                 std::optional<std::int64_t> default_value) {
    if (!validate_name(name)) {
        return *this;
    }
    IntegerSchema schema;
    schema.minimum = minimum;
    schema.maximum = maximum;
    schema.default_value = default_value;
    return add_optional(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_bool(std::string name,
                                                                             std::optional<bool> default_value) {
    if (!validate_name(name)) {
        return *this;
    }
    BooleanSchema schema;
    schema.default_value = default_value;
    return add_required(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_bool(std::string name,
                                                                             std::optional<bool> default_value) {
    if (!validate_name(name)) {
        return *this;
    }
    BooleanSchema schema;
    schema.default_value = default_value;
    return add_optional(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_email(std::string name,
                                                                              std::optional<std::string> default_value) {
    return required_string(std::move(name), std::move(default_value), std::string("email"));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_email(std::string name,
                                                                              std::optional<std::string> default_value) {
    return optional_string(std::move(name), std::move(default_value), std::string("email"));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_enum(std::string name,
                                                                             std::vector<std::string> values,
                                                                             std::optional<std::string> default_value) {
    if (!validate_name(name)) {
        return *this;
    }
    EnumSchema schema;
    schema.values = std::move(values);
    schema.default_value = std::move(default_value);
    return add_required(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_enum(std::string name,
                                                                            std::vector<std::string> values,
                                                                            std::optional<std::string> default_value) {
    if (!validate_name(name)) {
        return *this;
    }
    EnumSchema schema;
    schema.values = std::move(values);
    schema.default_value = std::move(default_value);
    return add_optional(std::move(name), std::move(schema));
}

inline core::Result<ElicitationSchema> ElicitationSchema::Builder::build() const {
    if (properties_.empty()) {
        return std::unexpected(elicitation_json_error("elicitation schema requires at least one property"));
    }
    ElicitationSchema schema;
    schema.title = title_;
    schema.description = description_;
    schema.properties = properties_;
    schema.required = required_;
    return schema;
}

inline Json primitive_schema_to_json(const PrimitiveSchema& schema);

inline Json primitive_schema_to_json(const PrimitiveSchema& schema) {
    return std::visit(
        [](const auto& value) -> Json {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, StringSchema>) {
                return string_schema_to_json(value);
            } else if constexpr (std::is_same_v<Value, NumberSchema>) {
                return number_schema_to_json(value);
            } else if constexpr (std::is_same_v<Value, IntegerSchema>) {
                return integer_schema_to_json(value);
            } else if constexpr (std::is_same_v<Value, BooleanSchema>) {
                return boolean_schema_to_json(value);
            } else {
                return enum_schema_to_json(value);
            }
        },
        schema);
}

inline core::Result<PrimitiveSchema> primitive_schema_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(elicitation_json_error("elicitation property schema must be an object"));
    }
    if (!json.contains("type") || !json.at("type").is_string()) {
        return std::unexpected(elicitation_json_error("elicitation property schema requires a string type"));
    }

    const auto type = json.at("type").get<std::string>();
    if (json.contains("enum")) {
        if (!json.at("enum").is_array()) {
            return std::unexpected(elicitation_json_error("elicitation enum must be an array"));
        }
        EnumSchema schema;
        if (json.contains("title")) {
            if (!json.at("title").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation enum title must be a string"));
            }
            schema.title = json.at("title").get<std::string>();
        }
        if (json.contains("description")) {
            if (!json.at("description").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation enum description must be a string"));
            }
            schema.description = json.at("description").get<std::string>();
        }
        for (const auto& item : json.at("enum")) {
            if (!item.is_string()) {
                return std::unexpected(elicitation_json_error("elicitation enum values must be strings"));
            }
            schema.values.push_back(item.get<std::string>());
        }
        if (json.contains("default")) {
            if (!json.at("default").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation enum default must be a string"));
            }
            schema.default_value = json.at("default").get<std::string>();
        }
        return schema;
    }

    if (type == "string") {
        StringSchema schema;
        if (json.contains("title")) {
            if (!json.at("title").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation string title must be a string"));
            }
            schema.title = json.at("title").get<std::string>();
        }
        if (json.contains("description")) {
            if (!json.at("description").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation string description must be a string"));
            }
            schema.description = json.at("description").get<std::string>();
        }
        if (json.contains("format")) {
            if (!json.at("format").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation string format must be a string"));
            }
            schema.format = json.at("format").get<std::string>();
        }
        if (json.contains("default")) {
            if (!json.at("default").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation string default must be a string"));
            }
            schema.default_value = json.at("default").get<std::string>();
        }
        return schema;
    }
    if (type == "number") {
        NumberSchema schema;
        if (json.contains("title")) {
            if (!json.at("title").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation number title must be a string"));
            }
            schema.title = json.at("title").get<std::string>();
        }
        if (json.contains("description")) {
            if (!json.at("description").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation number description must be a string"));
            }
            schema.description = json.at("description").get<std::string>();
        }
        if (json.contains("minimum")) {
            if (!json.at("minimum").is_number()) {
                return std::unexpected(elicitation_json_error("elicitation number minimum must be numeric"));
            }
            schema.minimum = json.at("minimum").get<double>();
        }
        if (json.contains("maximum")) {
            if (!json.at("maximum").is_number()) {
                return std::unexpected(elicitation_json_error("elicitation number maximum must be numeric"));
            }
            schema.maximum = json.at("maximum").get<double>();
        }
        if (json.contains("default")) {
            if (!json.at("default").is_number()) {
                return std::unexpected(elicitation_json_error("elicitation number default must be numeric"));
            }
            schema.default_value = json.at("default").get<double>();
        }
        return schema;
    }
    if (type == "integer") {
        IntegerSchema schema;
        if (json.contains("title")) {
            if (!json.at("title").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation integer title must be a string"));
            }
            schema.title = json.at("title").get<std::string>();
        }
        if (json.contains("description")) {
            if (!json.at("description").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation integer description must be a string"));
            }
            schema.description = json.at("description").get<std::string>();
        }
        if (json.contains("minimum")) {
            if (!json.at("minimum").is_number_integer()) {
                return std::unexpected(elicitation_json_error("elicitation integer minimum must be an integer"));
            }
            schema.minimum = json.at("minimum").get<std::int64_t>();
        }
        if (json.contains("maximum")) {
            if (!json.at("maximum").is_number_integer()) {
                return std::unexpected(elicitation_json_error("elicitation integer maximum must be an integer"));
            }
            schema.maximum = json.at("maximum").get<std::int64_t>();
        }
        if (json.contains("default")) {
            if (!json.at("default").is_number_integer()) {
                return std::unexpected(elicitation_json_error("elicitation integer default must be an integer"));
            }
            schema.default_value = json.at("default").get<std::int64_t>();
        }
        return schema;
    }
    if (type == "boolean") {
        BooleanSchema schema;
        if (json.contains("title")) {
            if (!json.at("title").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation boolean title must be a string"));
            }
            schema.title = json.at("title").get<std::string>();
        }
        if (json.contains("description")) {
            if (!json.at("description").is_string()) {
                return std::unexpected(elicitation_json_error("elicitation boolean description must be a string"));
            }
            schema.description = json.at("description").get<std::string>();
        }
        if (json.contains("default")) {
            if (!json.at("default").is_boolean()) {
                return std::unexpected(elicitation_json_error("elicitation boolean default must be a boolean"));
            }
            schema.default_value = json.at("default").get<bool>();
        }
        return schema;
    }

    return std::unexpected(elicitation_json_error("elicitation property type is not supported"));
}

} // namespace mcp::protocol
