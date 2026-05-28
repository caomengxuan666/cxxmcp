// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/elicitation.hpp
/// @brief Elicitation request, result, and schema payloads.
///
/// Elicitation lets a peer ask the user for additional input during MCP request
/// handling. Form mode embeds a constrained JSON schema for values to collect;
/// URL mode sends the user to an external flow and reports completion by
/// notification. Task request parameters may be included when supported.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief User action returned by an elicitation request.
enum class ElicitationAction {
  /// The user accepted and may have supplied content.
  Accept,
  /// The user declined the request.
  Decline,
  /// The elicitation was cancelled.
  Cancel,
};

/// @brief Elicitation interaction mode.
enum class ElicitationMode {
  /// Inline form described by an ElicitationSchema.
  Form,
  /// External URL flow completed by notification.
  Url,
};

/// @brief Primitive string property schema for form elicitation.
struct StringSchema {
  /// Optional display title.
  std::optional<std::string> title;
  /// Optional display description.
  std::optional<std::string> description;
  /// Optional JSON Schema string format, such as `email`.
  std::optional<std::string> format;
  /// Optional minimum string length.
  std::optional<std::int64_t> min_length;
  /// Optional maximum string length.
  std::optional<std::int64_t> max_length;
  /// Optional default string value.
  std::optional<std::string> default_value;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Primitive number property schema for form elicitation.
struct NumberSchema {
  /// Optional display title.
  std::optional<std::string> title;
  /// Optional display description.
  std::optional<std::string> description;
  /// Optional inclusive minimum.
  std::optional<double> minimum;
  /// Optional inclusive maximum.
  std::optional<double> maximum;
  /// Optional default numeric value.
  std::optional<double> default_value;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Primitive integer property schema for form elicitation.
struct IntegerSchema {
  /// Optional display title.
  std::optional<std::string> title;
  /// Optional display description.
  std::optional<std::string> description;
  /// Optional inclusive minimum.
  std::optional<std::int64_t> minimum;
  /// Optional inclusive maximum.
  std::optional<std::int64_t> maximum;
  /// Optional default integer value.
  std::optional<std::int64_t> default_value;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Primitive boolean property schema for form elicitation.
struct BooleanSchema {
  /// Optional display title.
  std::optional<std::string> title;
  /// Optional display description.
  std::optional<std::string> description;
  /// Optional default boolean value.
  std::optional<bool> default_value;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Primitive string enum property schema for form elicitation.
struct EnumSchema {
  /// Optional display title.
  std::optional<std::string> title;
  /// Optional display description.
  std::optional<std::string> description;
  /// Allowed string values.
  std::vector<std::string> values;
  /// Optional display names for legacy `enumNames`.
  std::vector<std::string> enum_names;
  /// Optional titles for RMCP/spec `oneOf`/`anyOf` enum entries.
  std::vector<std::string> value_titles;
  /// Whether to serialize a single-select enum as titled `oneOf`.
  bool titled_single_select = false;
  /// Whether this enum is represented as an array multi-select.
  bool multi_select = false;
  /// Optional minimum number of selected items for multi-select.
  std::optional<std::int64_t> min_items;
  /// Optional maximum number of selected items for multi-select.
  std::optional<std::int64_t> max_items;
  /// Optional default enum value.
  std::optional<std::string> default_value;
  /// Optional default enum values for multi-select.
  std::vector<std::string> default_values;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Variant over primitive schema shapes supported by elicitation forms.
using PrimitiveSchema = std::variant<StringSchema, NumberSchema, IntegerSchema,
                                     BooleanSchema, EnumSchema>;

/// @brief Object schema requested from a user in form elicitation.
struct ElicitationSchema {
  /// Optional form title.
  std::optional<std::string> title;
  /// Optional form description.
  std::optional<std::string> description;
  /// Properties keyed by field name.
  std::map<std::string, PrimitiveSchema> properties;
  /// Names of required properties.
  std::vector<std::string> required;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();

  /// @brief Creates a new string primitive schema.
  static StringSchema new_string(
      std::optional<std::string> title = std::nullopt,
      std::optional<std::string> description = std::nullopt,
      std::optional<std::string> format = std::nullopt,
      std::optional<std::int64_t> min_length = std::nullopt,
      std::optional<std::int64_t> max_length = std::nullopt,
      std::optional<std::string> default_value = std::nullopt) {
    StringSchema schema;
    schema.title = std::move(title);
    schema.description = std::move(description);
    schema.format = std::move(format);
    schema.min_length = min_length;
    schema.max_length = max_length;
    schema.default_value = std::move(default_value);
    return schema;
  }

  /// @brief Creates a new number primitive schema.
  static NumberSchema new_number(
      std::optional<std::string> title = std::nullopt,
      std::optional<std::string> description = std::nullopt,
      std::optional<double> minimum = std::nullopt,
      std::optional<double> maximum = std::nullopt,
      std::optional<double> default_value = std::nullopt) {
    NumberSchema schema;
    schema.title = std::move(title);
    schema.description = std::move(description);
    schema.minimum = minimum;
    schema.maximum = maximum;
    schema.default_value = default_value;
    return schema;
  }

  /// @brief Creates a new integer primitive schema.
  static IntegerSchema new_integer(
      std::optional<std::string> title = std::nullopt,
      std::optional<std::string> description = std::nullopt,
      std::optional<std::int64_t> minimum = std::nullopt,
      std::optional<std::int64_t> maximum = std::nullopt,
      std::optional<std::int64_t> default_value = std::nullopt) {
    IntegerSchema schema;
    schema.title = std::move(title);
    schema.description = std::move(description);
    schema.minimum = minimum;
    schema.maximum = maximum;
    schema.default_value = default_value;
    return schema;
  }

  /// @brief Creates a new boolean primitive schema.
  static BooleanSchema new_boolean(
      std::optional<std::string> title = std::nullopt,
      std::optional<std::string> description = std::nullopt,
      std::optional<bool> default_value = std::nullopt) {
    BooleanSchema schema;
    schema.title = std::move(title);
    schema.description = std::move(description);
    schema.default_value = default_value;
    return schema;
  }

  /// @brief Creates a new enum primitive schema.
  static EnumSchema new_enum(
      std::vector<std::string> values,
      std::optional<std::string> title = std::nullopt,
      std::optional<std::string> description = std::nullopt,
      std::optional<std::string> default_value = std::nullopt) {
    EnumSchema schema;
    schema.values = std::move(values);
    schema.title = std::move(title);
    schema.description = std::move(description);
    schema.default_value = std::move(default_value);
    return schema;
  }

  /// @brief Fluent builder for valid elicitation object schemas.
  ///
  /// The builder adds primitive fields and tracks required field names. The
  /// final build step rejects empty schemas because form elicitation requires
  /// at least one requested property.
  class Builder {
   public:
    /// @brief Sets the optional form title.
    Builder& title(std::string value) {
      title_ = std::move(value);
      return *this;
    }

    /// @brief Sets the optional form description.
    Builder& description(std::string value) {
      description_ = std::move(value);
      return *this;
    }

    /// @brief Adds a required string field.
    Builder& required_string(
        std::string name,
        std::optional<std::string> default_value = std::nullopt,
        std::optional<std::string> format = std::nullopt);
    /// @brief Adds an optional string field.
    Builder& optional_string(
        std::string name,
        std::optional<std::string> default_value = std::nullopt,
        std::optional<std::string> format = std::nullopt);
    /// @brief Adds a required number field.
    Builder& required_number(
        std::string name, std::optional<double> minimum = std::nullopt,
        std::optional<double> maximum = std::nullopt,
        std::optional<double> default_value = std::nullopt);
    /// @brief Adds an optional number field.
    Builder& optional_number(
        std::string name, std::optional<double> minimum = std::nullopt,
        std::optional<double> maximum = std::nullopt,
        std::optional<double> default_value = std::nullopt);
    /// @brief Adds a required integer field.
    Builder& required_integer(
        std::string name, std::optional<std::int64_t> minimum = std::nullopt,
        std::optional<std::int64_t> maximum = std::nullopt,
        std::optional<std::int64_t> default_value = std::nullopt);
    /// @brief Adds an optional integer field.
    Builder& optional_integer(
        std::string name, std::optional<std::int64_t> minimum = std::nullopt,
        std::optional<std::int64_t> maximum = std::nullopt,
        std::optional<std::int64_t> default_value = std::nullopt);
    /// @brief Adds a required boolean field.
    Builder& required_bool(std::string name,
                           std::optional<bool> default_value = std::nullopt);
    /// @brief Adds an optional boolean field.
    Builder& optional_bool(std::string name,
                           std::optional<bool> default_value = std::nullopt);
    /// @brief Adds a required string field with email format.
    Builder& required_email(
        std::string name,
        std::optional<std::string> default_value = std::nullopt);
    /// @brief Adds an optional string field with email format.
    Builder& optional_email(
        std::string name,
        std::optional<std::string> default_value = std::nullopt);
    /// @brief Adds a required string enum field.
    Builder& required_enum(
        std::string name, std::vector<std::string> values,
        std::optional<std::string> default_value = std::nullopt);
    /// @brief Adds an optional string enum field.
    Builder& optional_enum(
        std::string name, std::vector<std::string> values,
        std::optional<std::string> default_value = std::nullopt);

    /// @brief Adds a required property with a pre-built primitive schema.
    Builder& required_property(std::string name, PrimitiveSchema schema) {
      return add_required(std::move(name), std::move(schema));
    }

    /// @brief Adds an optional property with a pre-built primitive schema.
    Builder& optional_property(std::string name, PrimitiveSchema schema) {
      return add_optional(std::move(name), std::move(schema));
    }

    /// @brief Builds the schema after validation.
    /// @return Schema or an error when no properties were added.
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

  /// @brief Creates a builder for an elicitation object schema.
  static Builder builder() { return Builder{}; }
};

/// @brief Parameters for `elicitation/create`.
struct CreateElicitationRequestParam {
  /// User-facing message explaining what input is requested.
  std::string message;
  /// Interaction mode; form mode serializes `requested_schema`, URL mode
  /// serializes `elicitation_id` and `url`.
  ElicitationMode mode = ElicitationMode::Form;
  /// Required in URL mode to correlate the later completion notification.
  std::optional<std::string> elicitation_id;
  /// Required in URL mode; target URL for the external interaction.
  std::optional<std::string> url;
  /// Form-mode schema describing requested values.
  ElicitationSchema requested_schema;
  /// Optional opaque state echoed through the elicitation request lifecycle.
  std::optional<Json> request_state;
  /// Optional task request parameters for asynchronous elicitation.
  std::optional<TaskRequestParameters> task;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result object for `elicitation/create`.
struct CreateElicitationResult {
  /// User action taken in response to the request.
  ElicitationAction action = ElicitationAction::Cancel;
  /// Optional content object supplied when the action is Accept.
  std::optional<Json> content;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for `notifications/elicitation/complete`.
struct ElicitationCompleteNotificationParams {
  /// URL-mode elicitation id that has completed.
  std::string elicitation_id;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Converts an elicitation action to its MCP string value.
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

/// @brief Parses an elicitation action string.
/// @return Parsed action, or nullopt for unsupported strings.
inline std::optional<ElicitationAction> elicitation_action_from_string(
    const std::string& value) {
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

/// @brief Converts an elicitation mode to its MCP string value.
inline std::string elicitation_mode_to_string(ElicitationMode mode) {
  switch (mode) {
    case ElicitationMode::Form:
      return "form";
    case ElicitationMode::Url:
      return "url";
  }
  return "form";
}

/// @brief Parses an elicitation mode string.
/// @return Parsed mode, or nullopt for unsupported strings.
inline std::optional<ElicitationMode> elicitation_mode_from_string(
    const std::string& value) {
  if (value == "form") {
    return ElicitationMode::Form;
  }
  if (value == "url") {
    return ElicitationMode::Url;
  }
  return std::nullopt;
}

/// @brief Returns true for string formats allowed by the MCP elicitation
/// schema.
inline bool elicitation_string_format_is_supported(
    const std::string& value) noexcept {
  return value == "email" || value == "uri" || value == "date" ||
         value == "date-time";
}

/// @brief Serializes a string property schema.
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
  if (schema.min_length.has_value()) {
    json["minLength"] = *schema.min_length;
  }
  if (schema.max_length.has_value()) {
    json["maxLength"] = *schema.max_length;
  }
  if (schema.default_value.has_value()) {
    json["default"] = *schema.default_value;
  }
  append_json_extensions(json, schema.extensions);
  return json;
}

/// @brief Serializes a number property schema.
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
  append_json_extensions(json, schema.extensions);
  return json;
}

/// @brief Serializes an integer property schema.
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
  append_json_extensions(json, schema.extensions);
  return json;
}

/// @brief Serializes a boolean property schema.
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
  append_json_extensions(json, schema.extensions);
  return json;
}

/// @brief Serializes a string enum property schema.
inline Json enum_schema_to_json(const EnumSchema& schema) {
  Json json = Json::object();
  json["type"] = schema.multi_select ? "array" : "string";
  if (schema.multi_select) {
    if (!schema.value_titles.empty() &&
        schema.value_titles.size() == schema.values.size()) {
      json["items"] = Json::object();
      json["items"]["anyOf"] = Json::array();
      for (std::size_t index = 0; index < schema.values.size(); ++index) {
        json["items"]["anyOf"].push_back(
            Json{{"const", schema.values[index]},
                 {"title", schema.value_titles[index]}});
      }
    } else {
      json["items"] = Json{{"type", "string"}, {"enum", Json::array()}};
      for (const auto& value : schema.values) {
        json["items"]["enum"].push_back(value);
      }
    }
    if (schema.min_items.has_value()) {
      json["minItems"] = *schema.min_items;
    }
    if (schema.max_items.has_value()) {
      json["maxItems"] = *schema.max_items;
    }
    if (!schema.default_values.empty()) {
      json["default"] = schema.default_values;
    }
  } else if (schema.titled_single_select && !schema.value_titles.empty() &&
             schema.value_titles.size() == schema.values.size()) {
    json["oneOf"] = Json::array();
    for (std::size_t index = 0; index < schema.values.size(); ++index) {
      json["oneOf"].push_back(Json{{"const", schema.values[index]},
                                   {"title", schema.value_titles[index]}});
    }
  } else {
    json["enum"] = Json::array();
    for (const auto& value : schema.values) {
      json["enum"].push_back(value);
    }
    if (!schema.enum_names.empty()) {
      json["enumNames"] = schema.enum_names;
    }
  }
  if (schema.title.has_value()) {
    json["title"] = *schema.title;
  }
  if (schema.description.has_value()) {
    json["description"] = *schema.description;
  }
  if (!schema.multi_select && schema.default_value.has_value()) {
    json["default"] = *schema.default_value;
  }
  append_json_extensions(json, schema.extensions);
  return json;
}

/// @brief Serializes any primitive elicitation property schema.
inline Json primitive_schema_to_json(const PrimitiveSchema& schema);

/// @brief Serializes a form elicitation object schema.
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
  append_json_extensions(json, schema.extensions);
  return json;
}

/// @brief Builds an InvalidRequest error for elicitation JSON validation
/// failures.
inline core::Error elicitation_json_error(std::string message) {
  return core::Error{
      static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

/// @brief Parses `{"const": "...", "title": "..."}` enum choices.
inline core::Result<core::Unit> parse_titled_enum_choices(
    const Json& choices, std::vector<std::string>& values,
    std::vector<std::string>& titles, std::string_view context) {
  if (!choices.is_array()) {
    return mcp::core::unexpected(
        elicitation_json_error(std::string(context) + " must be an array"));
  }
  for (const auto& choice : choices) {
    if (!choice.is_object() || !choice.contains("const") ||
        !choice.at("const").is_string() || !choice.contains("title") ||
        !choice.at("title").is_string()) {
      return mcp::core::unexpected(elicitation_json_error(
          std::string(context) + " entries require string const and title"));
    }
    values.push_back(choice.at("const").get<std::string>());
    titles.push_back(choice.at("title").get<std::string>());
  }
  return core::Unit{};
}

/// @brief Validates all selected enum values are allowed.
inline bool enum_values_are_allowed(const std::vector<std::string>& allowed,
                                    const std::vector<std::string>& values) {
  for (const auto& value : values) {
    if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
      return false;
    }
  }
  return true;
}

/// @brief Parses any primitive elicitation property schema.
/// @return Parsed primitive schema or validation error.
inline core::Result<PrimitiveSchema> primitive_schema_from_json(
    const Json& json);

/// @brief Parses a form elicitation object schema.
/// @return Parsed schema or validation error.
inline core::Result<ElicitationSchema> elicitation_schema_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        elicitation_json_error("elicitation schema must be an object"));
  }
  if (!json.contains("type") || !json.at("type").is_string() ||
      json.at("type").get<std::string>() != "object") {
    return mcp::core::unexpected(
        elicitation_json_error("elicitation schema requires type object"));
  }
  if (!json.contains("properties") || !json.at("properties").is_object()) {
    return mcp::core::unexpected(
        elicitation_json_error("elicitation schema requires properties"));
  }

  ElicitationSchema schema;
  if (json.contains("title")) {
    if (!json.at("title").is_string()) {
      return mcp::core::unexpected(
          elicitation_json_error("elicitation schema title must be a string"));
    }
    schema.title = json.at("title").get<std::string>();
  }
  if (json.contains("description")) {
    if (!json.at("description").is_string()) {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation schema description must be a string"));
    }
    schema.description = json.at("description").get<std::string>();
  }
  for (const auto& [name, property_json] : json.at("properties").items()) {
    const auto property = primitive_schema_from_json(property_json);
    if (!property) {
      return mcp::core::unexpected(property.error());
    }
    schema.properties.emplace(name, *property);
  }
  if (json.contains("required")) {
    if (!json.at("required").is_array()) {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation schema required must be an array"));
    }
    for (const auto& item : json.at("required")) {
      if (!item.is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation schema required entries must be strings"));
      }
      schema.required.push_back(item.get<std::string>());
    }
  }
  schema.extensions = collect_json_extensions(
      json, {"type", "title", "description", "properties", "required"});
  return schema;
}

/// @brief Serializes `elicitation/create` params.
/// @note URL mode omits `requestedSchema`; form mode omits `elicitationId` and
/// `url`.
inline Json create_elicitation_request_param_to_json(
    const CreateElicitationRequestParam& request) {
  Json json = Json::object();
  json["message"] = request.message;
  json["mode"] = elicitation_mode_to_string(request.mode);
  if (request.request_state.has_value()) {
    json["requestState"] = *request.request_state;
  }
  if (request.task.has_value()) {
    json["task"] = task_request_parameters_to_json(*request.task);
  }
  if (request.meta.has_value()) {
    json["_meta"] = *request.meta;
  }
  append_json_extensions(json, request.extensions);

  if (request.mode == ElicitationMode::Url) {
    if (request.elicitation_id.has_value()) {
      json["elicitationId"] = *request.elicitation_id;
    }
    if (request.url.has_value()) {
      json["url"] = *request.url;
    }
    return json;
  }

  json["requestedSchema"] =
      elicitation_schema_to_json(request.requested_schema);
  return json;
}

/// @brief Parses `elicitation/create` params.
/// @return Parsed params or validation error.
inline core::Result<CreateElicitationRequestParam>
create_elicitation_request_param_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        elicitation_json_error("elicitation request must be an object"));
  }
  if (!json.contains("message") || !json.at("message").is_string()) {
    return mcp::core::unexpected(elicitation_json_error(
        "elicitation request requires a string message"));
  }
  ElicitationMode mode = ElicitationMode::Form;
  if (json.contains("mode")) {
    if (!json.at("mode").is_string()) {
      return mcp::core::unexpected(
          elicitation_json_error("elicitation request mode must be a string"));
    }
    const auto parsed_mode =
        elicitation_mode_from_string(json.at("mode").get<std::string>());
    if (!parsed_mode) {
      return mcp::core::unexpected(
          elicitation_json_error("elicitation request mode is not supported"));
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
      return mcp::core::unexpected(task.error());
    }
    request.task = *task;
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation request _meta must be an object"));
    }
    request.meta = json.at("_meta");
  }
  request.extensions = collect_json_extensions(
      json, {"message", "mode", "elicitationId", "url", "requestedSchema",
             "requested_schema", "requestState", "task", "_meta"});

  if (mode == ElicitationMode::Url) {
    if (!json.contains("elicitationId") ||
        !json.at("elicitationId").is_string()) {
      return mcp::core::unexpected(
          elicitation_json_error("elicitation request requires elicitationId"));
    }
    if (!json.contains("url") || !json.at("url").is_string()) {
      return mcp::core::unexpected(
          elicitation_json_error("elicitation request requires url"));
    }
    request.elicitation_id = json.at("elicitationId").get<std::string>();
    request.url = json.at("url").get<std::string>();
    return request;
  }

  const auto schema_key =
      json.contains("requestedSchema") ? "requestedSchema" : "requested_schema";
  if (!json.contains(schema_key)) {
    return mcp::core::unexpected(
        elicitation_json_error("elicitation request requires requestedSchema"));
  }
  const auto schema = elicitation_schema_from_json(json.at(schema_key));
  if (!schema) {
    return mcp::core::unexpected(schema.error());
  }
  request.requested_schema = *schema;
  return request;
}

/// @brief Serializes an `elicitation/create` result.
inline Json create_elicitation_result_to_json(
    const CreateElicitationResult& result) {
  Json json = Json::object();
  json["action"] = elicitation_action_to_string(result.action);
  if (result.content.has_value()) {
    json["content"] = *result.content;
  }
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses an `elicitation/create` result.
/// @return Parsed result or validation error.
inline core::Result<CreateElicitationResult>
create_elicitation_result_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        elicitation_json_error("elicitation result must be an object"));
  }
  if (!json.contains("action") || !json.at("action").is_string()) {
    return mcp::core::unexpected(
        elicitation_json_error("elicitation result requires a string action"));
  }

  const auto action =
      elicitation_action_from_string(json.at("action").get<std::string>());
  if (!action.has_value()) {
    return mcp::core::unexpected(
        elicitation_json_error("elicitation action is not supported"));
  }

  CreateElicitationResult result;
  result.action = *action;
  if (json.contains("content")) {
    result.content = json.at("content");
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          elicitation_json_error("elicitation result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions =
      collect_json_extensions(json, {"action", "content", "_meta"});
  return result;
}

/// @brief Validates a form elicitation content object against one primitive
/// property schema.
inline core::Result<core::Unit> validate_elicitation_content_property(
    const std::string& name, const PrimitiveSchema& schema, const Json& value) {
  return std::visit(
      [&](const auto& property) -> core::Result<core::Unit> {
        using Property = std::decay_t<decltype(property)>;
        if constexpr (std::is_same_v<Property, StringSchema>) {
          if (!value.is_string()) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' must be a string"));
          }
          const auto string_value = value.get<std::string>();
          if (property.min_length.has_value() &&
              string_value.size() <
                  static_cast<std::size_t>(*property.min_length)) {
            return mcp::core::unexpected(
                elicitation_json_error("elicitation content field '" + name +
                                       "' is shorter than minLength"));
          }
          if (property.max_length.has_value() &&
              string_value.size() >
                  static_cast<std::size_t>(*property.max_length)) {
            return mcp::core::unexpected(
                elicitation_json_error("elicitation content field '" + name +
                                       "' is longer than maxLength"));
          }
        } else if constexpr (std::is_same_v<Property, NumberSchema>) {
          if (!value.is_number()) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' must be numeric"));
          }
          const auto number = value.get<double>();
          if (!protocol_number_is_finite(number)) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' must be finite"));
          }
          if (property.minimum.has_value() && number < *property.minimum) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' is below minimum"));
          }
          if (property.maximum.has_value() && number > *property.maximum) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' is above maximum"));
          }
        } else if constexpr (std::is_same_v<Property, IntegerSchema>) {
          if (!value.is_number_integer()) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' must be an integer"));
          }
          const auto integer = value.get<std::int64_t>();
          if (property.minimum.has_value() && integer < *property.minimum) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' is below minimum"));
          }
          if (property.maximum.has_value() && integer > *property.maximum) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' is above maximum"));
          }
        } else if constexpr (std::is_same_v<Property, BooleanSchema>) {
          if (!value.is_boolean()) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' must be a boolean"));
          }
        } else {
          if (property.multi_select) {
            if (!value.is_array()) {
              return mcp::core::unexpected(elicitation_json_error(
                  "elicitation content field '" + name + "' must be an array"));
            }
            if (property.min_items.has_value() &&
                value.size() < static_cast<std::size_t>(*property.min_items)) {
              return mcp::core::unexpected(
                  elicitation_json_error("elicitation content field '" + name +
                                         "' has too few selections"));
            }
            if (property.max_items.has_value() &&
                value.size() > static_cast<std::size_t>(*property.max_items)) {
              return mcp::core::unexpected(
                  elicitation_json_error("elicitation content field '" + name +
                                         "' has too many selections"));
            }
            for (const auto& item : value) {
              if (!item.is_string()) {
                return mcp::core::unexpected(elicitation_json_error(
                    "elicitation content field '" + name +
                    "' selections must be strings"));
              }
              const auto enum_value = item.get<std::string>();
              if (std::find(property.values.begin(), property.values.end(),
                            enum_value) == property.values.end()) {
                return mcp::core::unexpected(
                    elicitation_json_error("elicitation content field '" +
                                           name + "' must match enum values"));
              }
            }
          } else if (!value.is_string()) {
            return mcp::core::unexpected(elicitation_json_error(
                "elicitation content field '" + name + "' must be a string"));
          } else {
            const auto enum_value = value.get<std::string>();
            if (std::find(property.values.begin(), property.values.end(),
                          enum_value) == property.values.end()) {
              return mcp::core::unexpected(
                  elicitation_json_error("elicitation content field '" + name +
                                         "' must match an enum value"));
            }
          }
        }
        return core::Unit{};
      },
      schema);
}

/// @brief Validates a form elicitation content object against the SDK's
/// constrained ElicitationSchema model.
///
/// The SDK intentionally validates only the primitive schema subset it models:
/// required fields, primitive JSON types, numeric bounds, and enum values.
/// Unknown content members are allowed because the schema model does not expose
/// JSON Schema `additionalProperties`.
inline core::Result<core::Unit> validate_elicitation_content(
    const ElicitationSchema& schema, const Json& content) {
  if (!content.is_object()) {
    return mcp::core::unexpected(
        elicitation_json_error("elicitation content must be an object"));
  }

  for (const auto& required : schema.required) {
    if (!content.contains(required)) {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation content requires field '" + required + "'"));
    }
  }

  for (const auto& [name, property] : schema.properties) {
    if (!content.contains(name)) {
      continue;
    }
    const auto valid =
        validate_elicitation_content_property(name, property, content.at(name));
    if (!valid) {
      return valid;
    }
  }

  return core::Unit{};
}

/// @brief Validates an accepted elicitation result's content against the
/// requested form schema.
///
/// Non-accept results do not require content and are treated as valid. Accept
/// results without content are valid only when the schema has no required
/// fields.
inline core::Result<core::Unit> validate_elicitation_result_content(
    const ElicitationSchema& schema, const CreateElicitationResult& result) {
  if (result.action != ElicitationAction::Accept) {
    return core::Unit{};
  }
  if (!result.content.has_value()) {
    if (schema.required.empty()) {
      return core::Unit{};
    }
    return mcp::core::unexpected(
        elicitation_json_error("accepted elicitation result requires content"));
  }
  return validate_elicitation_content(schema, *result.content);
}

/// @brief Serializes URL-mode completion notification params.
inline Json elicitation_complete_notification_params_to_json(
    const ElicitationCompleteNotificationParams& params) {
  Json json = Json{{"elicitationId", params.elicitation_id}};
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  append_json_extensions(json, params.extensions);
  return json;
}

/// @brief Parses URL-mode completion notification params.
/// @return Parsed params or validation error.
inline core::Result<ElicitationCompleteNotificationParams>
elicitation_complete_notification_params_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(elicitation_json_error(
        "elicitation complete notification must be an object"));
  }
  if (!json.contains("elicitationId") ||
      !json.at("elicitationId").is_string()) {
    return mcp::core::unexpected(elicitation_json_error(
        "elicitation complete notification requires elicitationId"));
  }
  ElicitationCompleteNotificationParams params;
  params.elicitation_id = json.at("elicitationId").get<std::string>();
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation complete notification _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  params.extensions = collect_json_extensions(json, {"elicitationId", "_meta"});
  return params;
}

/// @brief Validates a property name accepted by the schema builder.
inline bool ElicitationSchema::Builder::validate_name(const std::string& name) {
  return !name.empty();
}

/// @brief Adds a required primitive property to the builder.
inline ElicitationSchema::Builder& ElicitationSchema::Builder::add_required(
    std::string name, PrimitiveSchema schema) {
  required_.push_back(name);
  properties_.insert_or_assign(std::move(name), std::move(schema));
  return *this;
}

/// @brief Adds an optional primitive property to the builder.
inline ElicitationSchema::Builder& ElicitationSchema::Builder::add_optional(
    std::string name, PrimitiveSchema schema) {
  properties_.insert_or_assign(std::move(name), std::move(schema));
  return *this;
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_string(
    std::string name, std::optional<std::string> default_value,
    std::optional<std::string> format) {
  if (!validate_name(name)) {
    return *this;
  }
  StringSchema schema;
  schema.default_value = std::move(default_value);
  schema.format = std::move(format);
  return add_required(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_string(
    std::string name, std::optional<std::string> default_value,
    std::optional<std::string> format) {
  if (!validate_name(name)) {
    return *this;
  }
  StringSchema schema;
  schema.default_value = std::move(default_value);
  schema.format = std::move(format);
  return add_optional(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_number(
    std::string name, std::optional<double> minimum,
    std::optional<double> maximum, std::optional<double> default_value) {
  if (!validate_name(name)) {
    return *this;
  }
  NumberSchema schema;
  schema.minimum = minimum;
  schema.maximum = maximum;
  schema.default_value = default_value;
  return add_required(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_number(
    std::string name, std::optional<double> minimum,
    std::optional<double> maximum, std::optional<double> default_value) {
  if (!validate_name(name)) {
    return *this;
  }
  NumberSchema schema;
  schema.minimum = minimum;
  schema.maximum = maximum;
  schema.default_value = default_value;
  return add_optional(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_integer(
    std::string name, std::optional<std::int64_t> minimum,
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

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_integer(
    std::string name, std::optional<std::int64_t> minimum,
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

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_bool(
    std::string name, std::optional<bool> default_value) {
  if (!validate_name(name)) {
    return *this;
  }
  BooleanSchema schema;
  schema.default_value = default_value;
  return add_required(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_bool(
    std::string name, std::optional<bool> default_value) {
  if (!validate_name(name)) {
    return *this;
  }
  BooleanSchema schema;
  schema.default_value = default_value;
  return add_optional(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_email(
    std::string name, std::optional<std::string> default_value) {
  return required_string(std::move(name), std::move(default_value),
                         std::string("email"));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_email(
    std::string name, std::optional<std::string> default_value) {
  return optional_string(std::move(name), std::move(default_value),
                         std::string("email"));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::required_enum(
    std::string name, std::vector<std::string> values,
    std::optional<std::string> default_value) {
  if (!validate_name(name)) {
    return *this;
  }
  EnumSchema schema;
  schema.values = std::move(values);
  schema.default_value = std::move(default_value);
  return add_required(std::move(name), std::move(schema));
}

inline ElicitationSchema::Builder& ElicitationSchema::Builder::optional_enum(
    std::string name, std::vector<std::string> values,
    std::optional<std::string> default_value) {
  if (!validate_name(name)) {
    return *this;
  }
  EnumSchema schema;
  schema.values = std::move(values);
  schema.default_value = std::move(default_value);
  return add_optional(std::move(name), std::move(schema));
}

/// @brief Builds the final elicitation schema.
inline core::Result<ElicitationSchema> ElicitationSchema::Builder::build()
    const {
  if (properties_.empty()) {
    return mcp::core::unexpected(elicitation_json_error(
        "elicitation schema requires at least one property"));
  }
  ElicitationSchema schema;
  schema.title = title_;
  schema.description = description_;
  schema.properties = properties_;
  schema.required = required_;
  return schema;
}

/// @brief Serializes any primitive elicitation property schema.
inline Json primitive_schema_to_json(const PrimitiveSchema& schema);

/// @brief Serializes any primitive elicitation property schema.
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

/// @brief Parses any primitive elicitation property schema.
inline core::Result<PrimitiveSchema> primitive_schema_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(elicitation_json_error(
        "elicitation property schema must be an object"));
  }
  if (!json.contains("type") || !json.at("type").is_string()) {
    return mcp::core::unexpected(elicitation_json_error(
        "elicitation property schema requires a string type"));
  }

  const auto type = json.at("type").get<std::string>();
  if (type == "array") {
    EnumSchema schema;
    schema.multi_select = true;
    if (json.contains("title")) {
      if (!json.at("title").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation multi-select title must be a string"));
      }
      schema.title = json.at("title").get<std::string>();
    }
    if (json.contains("description")) {
      if (!json.at("description").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation multi-select description must be a string"));
      }
      schema.description = json.at("description").get<std::string>();
    }
    if (!json.contains("items") || !json.at("items").is_object()) {
      return mcp::core::unexpected(
          elicitation_json_error("elicitation multi-select requires items"));
    }
    const auto& items = json.at("items");
    if (items.contains("type") &&
        (!items.at("type").is_string() ||
         items.at("type").get<std::string>() != "string")) {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation multi-select items type must be string"));
    }
    if (items.contains("enum")) {
      if (!items.at("enum").is_array()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation multi-select enum must be an array"));
      }
      for (const auto& item : items.at("enum")) {
        if (!item.is_string()) {
          return mcp::core::unexpected(elicitation_json_error(
              "elicitation multi-select enum values must be strings"));
        }
        schema.values.push_back(item.get<std::string>());
      }
    } else if (items.contains("anyOf") || items.contains("oneOf")) {
      const auto choices_key = items.contains("anyOf") ? "anyOf" : "oneOf";
      const auto parsed_choices = parse_titled_enum_choices(
          items.at(choices_key), schema.values, schema.value_titles,
          "elicitation multi-select choices");
      if (!parsed_choices) {
        return mcp::core::unexpected(parsed_choices.error());
      }
    } else {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation multi-select requires enum, anyOf, or oneOf items"));
    }
    if (json.contains("minItems")) {
      if (!json.at("minItems").is_number_integer()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation multi-select minItems must be an integer"));
      }
      schema.min_items = json.at("minItems").get<std::int64_t>();
      if (*schema.min_items < 0) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation multi-select minItems must be non-negative"));
      }
    }
    if (json.contains("maxItems")) {
      if (!json.at("maxItems").is_number_integer()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation multi-select maxItems must be an integer"));
      }
      schema.max_items = json.at("maxItems").get<std::int64_t>();
      if (*schema.max_items < 0) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation multi-select maxItems must be non-negative"));
      }
    }
    if (schema.min_items.has_value() && schema.max_items.has_value() &&
        *schema.min_items > *schema.max_items) {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation multi-select minItems must be <= maxItems"));
    }
    if (json.contains("default")) {
      if (!json.at("default").is_array()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation multi-select default must be an array"));
      }
      for (const auto& item : json.at("default")) {
        if (!item.is_string()) {
          return mcp::core::unexpected(elicitation_json_error(
              "elicitation multi-select default values must be strings"));
        }
        schema.default_values.push_back(item.get<std::string>());
      }
      if (!enum_values_are_allowed(schema.values, schema.default_values)) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation multi-select default must match enum values"));
      }
    }
    schema.extensions = collect_json_extensions(
        json, {"type", "title", "description", "minItems", "maxItems", "items",
               "default"});
    return schema;
  }
  if (json.contains("oneOf")) {
    if (type != "string") {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation titled enum type must be string"));
    }
    EnumSchema schema;
    schema.titled_single_select = true;
    if (json.contains("title")) {
      if (!json.at("title").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation titled enum title must be a string"));
      }
      schema.title = json.at("title").get<std::string>();
    }
    if (json.contains("description")) {
      if (!json.at("description").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation titled enum description must be a string"));
      }
      schema.description = json.at("description").get<std::string>();
    }
    const auto parsed_choices = parse_titled_enum_choices(
        json.at("oneOf"), schema.values, schema.value_titles,
        "elicitation titled enum oneOf");
    if (!parsed_choices) {
      return mcp::core::unexpected(parsed_choices.error());
    }
    if (json.contains("default")) {
      if (!json.at("default").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation titled enum default must be a string"));
      }
      schema.default_value = json.at("default").get<std::string>();
      if (std::find(schema.values.begin(), schema.values.end(),
                    *schema.default_value) == schema.values.end()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation titled enum default must match an enum value"));
      }
    }
    schema.extensions = collect_json_extensions(
        json, {"type", "title", "description", "oneOf", "default"});
    return schema;
  }
  if (json.contains("enum")) {
    if (type != "string") {
      return mcp::core::unexpected(
          elicitation_json_error("elicitation enum type must be string"));
    }
    if (!json.at("enum").is_array()) {
      return mcp::core::unexpected(
          elicitation_json_error("elicitation enum must be an array"));
    }
    EnumSchema schema;
    if (json.contains("title")) {
      if (!json.at("title").is_string()) {
        return mcp::core::unexpected(
            elicitation_json_error("elicitation enum title must be a string"));
      }
      schema.title = json.at("title").get<std::string>();
    }
    if (json.contains("description")) {
      if (!json.at("description").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation enum description must be a string"));
      }
      schema.description = json.at("description").get<std::string>();
    }
    for (const auto& item : json.at("enum")) {
      if (!item.is_string()) {
        return mcp::core::unexpected(
            elicitation_json_error("elicitation enum values must be strings"));
      }
      schema.values.push_back(item.get<std::string>());
    }
    if (json.contains("enumNames")) {
      if (!json.at("enumNames").is_array()) {
        return mcp::core::unexpected(
            elicitation_json_error("elicitation enumNames must be an array"));
      }
      for (const auto& item : json.at("enumNames")) {
        if (!item.is_string()) {
          return mcp::core::unexpected(elicitation_json_error(
              "elicitation enumNames entries must be strings"));
        }
        schema.enum_names.push_back(item.get<std::string>());
      }
      if (schema.enum_names.size() != schema.values.size()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation enumNames size must match enum values"));
      }
    }
    if (json.contains("default")) {
      if (!json.at("default").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation enum default must be a string"));
      }
      schema.default_value = json.at("default").get<std::string>();
      if (std::find(schema.values.begin(), schema.values.end(),
                    *schema.default_value) == schema.values.end()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation enum default must match an enum value"));
      }
    }
    schema.extensions = collect_json_extensions(
        json, {"type", "title", "description", "enum", "enumNames", "default"});
    return schema;
  }

  if (type == "string") {
    StringSchema schema;
    if (json.contains("title")) {
      if (!json.at("title").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation string title must be a string"));
      }
      schema.title = json.at("title").get<std::string>();
    }
    if (json.contains("description")) {
      if (!json.at("description").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation string description must be a string"));
      }
      schema.description = json.at("description").get<std::string>();
    }
    if (json.contains("format")) {
      if (!json.at("format").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation string format must be a string"));
      }
      schema.format = json.at("format").get<std::string>();
      if (!elicitation_string_format_is_supported(*schema.format)) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation string format is not supported"));
      }
    }
    if (json.contains("minLength")) {
      if (!json.at("minLength").is_number_integer()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation string minLength must be an integer"));
      }
      schema.min_length = json.at("minLength").get<std::int64_t>();
      if (*schema.min_length < 0) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation string minLength must be non-negative"));
      }
    }
    if (json.contains("maxLength")) {
      if (!json.at("maxLength").is_number_integer()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation string maxLength must be an integer"));
      }
      schema.max_length = json.at("maxLength").get<std::int64_t>();
      if (*schema.max_length < 0) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation string maxLength must be non-negative"));
      }
    }
    if (schema.min_length.has_value() && schema.max_length.has_value() &&
        *schema.min_length > *schema.max_length) {
      return mcp::core::unexpected(elicitation_json_error(
          "elicitation string minLength must be <= maxLength"));
    }
    if (json.contains("default")) {
      if (!json.at("default").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation string default must be a string"));
      }
      schema.default_value = json.at("default").get<std::string>();
    }
    schema.extensions =
        collect_json_extensions(json, {"type", "title", "description", "format",
                                       "minLength", "maxLength", "default"});
    return schema;
  }
  if (type == "number") {
    NumberSchema schema;
    if (json.contains("title")) {
      if (!json.at("title").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation number title must be a string"));
      }
      schema.title = json.at("title").get<std::string>();
    }
    if (json.contains("description")) {
      if (!json.at("description").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation number description must be a string"));
      }
      schema.description = json.at("description").get<std::string>();
    }
    if (json.contains("minimum")) {
      if (!json.at("minimum").is_number()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation number minimum must be numeric"));
      }
      const auto value = json.at("minimum").get<double>();
      if (!protocol_number_is_finite(value)) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation number minimum must be finite"));
      }
      schema.minimum = value;
    }
    if (json.contains("maximum")) {
      if (!json.at("maximum").is_number()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation number maximum must be numeric"));
      }
      const auto value = json.at("maximum").get<double>();
      if (!protocol_number_is_finite(value)) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation number maximum must be finite"));
      }
      schema.maximum = value;
    }
    if (json.contains("default")) {
      if (!json.at("default").is_number()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation number default must be numeric"));
      }
      const auto value = json.at("default").get<double>();
      if (!protocol_number_is_finite(value)) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation number default must be finite"));
      }
      schema.default_value = value;
    }
    schema.extensions = collect_json_extensions(
        json,
        {"type", "title", "description", "minimum", "maximum", "default"});
    return schema;
  }
  if (type == "integer") {
    IntegerSchema schema;
    if (json.contains("title")) {
      if (!json.at("title").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation integer title must be a string"));
      }
      schema.title = json.at("title").get<std::string>();
    }
    if (json.contains("description")) {
      if (!json.at("description").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation integer description must be a string"));
      }
      schema.description = json.at("description").get<std::string>();
    }
    if (json.contains("minimum")) {
      if (!json.at("minimum").is_number_integer()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation integer minimum must be an integer"));
      }
      schema.minimum = json.at("minimum").get<std::int64_t>();
    }
    if (json.contains("maximum")) {
      if (!json.at("maximum").is_number_integer()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation integer maximum must be an integer"));
      }
      schema.maximum = json.at("maximum").get<std::int64_t>();
    }
    if (json.contains("default")) {
      if (!json.at("default").is_number_integer()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation integer default must be an integer"));
      }
      schema.default_value = json.at("default").get<std::int64_t>();
    }
    schema.extensions = collect_json_extensions(
        json,
        {"type", "title", "description", "minimum", "maximum", "default"});
    return schema;
  }
  if (type == "boolean") {
    BooleanSchema schema;
    if (json.contains("title")) {
      if (!json.at("title").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation boolean title must be a string"));
      }
      schema.title = json.at("title").get<std::string>();
    }
    if (json.contains("description")) {
      if (!json.at("description").is_string()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation boolean description must be a string"));
      }
      schema.description = json.at("description").get<std::string>();
    }
    if (json.contains("default")) {
      if (!json.at("default").is_boolean()) {
        return mcp::core::unexpected(elicitation_json_error(
            "elicitation boolean default must be a boolean"));
      }
      schema.default_value = json.at("default").get<bool>();
    }
    schema.extensions = collect_json_extensions(
        json, {"type", "title", "description", "default"});
    return schema;
  }

  return mcp::core::unexpected(
      elicitation_json_error("elicitation property type is not supported"));
}

}  // namespace mcp::protocol
