// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/schema.hpp
/// @brief Small JSON Schema builders for MCP tool and elicitation metadata.

#include <string>
#include <utility>
#include <vector>

#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Primitive JSON Schema helpers used by public SDK builders.
class JsonSchema {
 public:
  static Json any() { return Json::object(); }

  static Json object() { return Json{{"type", "object"}}; }

  static Json string() { return Json{{"type", "string"}}; }

  static Json integer() { return Json{{"type", "integer"}}; }

  static Json number() { return Json{{"type", "number"}}; }

  static Json boolean() { return Json{{"type", "boolean"}}; }

  static Json array(Json items) {
    return Json{{"type", "array"}, {"items", std::move(items)}};
  }

  static Json string_enum(std::vector<std::string> values) {
    return Json{{"type", "string"}, {"enum", std::move(values)}};
  }
};

/// @brief Fluent object-schema builder for tool input and output schemas.
class ObjectSchemaBuilder {
 public:
  ObjectSchemaBuilder() {
    schema_["type"] = "object";
    schema_["properties"] = Json::object();
  }

  ObjectSchemaBuilder& title(std::string value) {
    schema_["title"] = std::move(value);
    return *this;
  }

  ObjectSchemaBuilder& description(std::string value) {
    schema_["description"] = std::move(value);
    return *this;
  }

  ObjectSchemaBuilder& property(std::string name, Json schema) {
    schema_["properties"][std::move(name)] = std::move(schema);
    return *this;
  }

  ObjectSchemaBuilder& optional_property(std::string name, Json schema) {
    return property(std::move(name), std::move(schema));
  }

  ObjectSchemaBuilder& required(std::string name) {
    schema_["required"].push_back(std::move(name));
    return *this;
  }

  ObjectSchemaBuilder& required_property(std::string name, Json schema) {
    const auto required_name = name;
    property(std::move(name), std::move(schema));
    return required(required_name);
  }

  ObjectSchemaBuilder& additional_properties(bool value) {
    schema_["additionalProperties"] = value;
    return *this;
  }

  Json build() const& { return schema_; }

  Json build() && { return std::move(schema_); }

 private:
  Json schema_ = Json::object();
};

/// @brief Creates a fluent object-schema builder.
inline ObjectSchemaBuilder object_schema() { return ObjectSchemaBuilder{}; }

}  // namespace mcp::protocol
