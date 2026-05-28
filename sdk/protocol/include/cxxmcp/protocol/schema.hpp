// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/schema.hpp
/// @brief Small JSON Schema builders for MCP tool and elicitation metadata.

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cxxmcp/protocol/reflect.hpp"
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

/// @brief Type-to-schema customization point for typed SDK helpers.
template <class T, class Enable = void>
struct SchemaTraits {
  static Json schema() { return JsonSchema::object(); }
};

template <>
struct SchemaTraits<Json> {
  static Json schema() { return JsonSchema::any(); }
};

template <>
struct SchemaTraits<std::string> {
  static Json schema() { return JsonSchema::string(); }
};

template <>
struct SchemaTraits<bool> {
  static Json schema() { return JsonSchema::boolean(); }
};

template <class T>
struct SchemaTraits<
    T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
  static Json schema() { return JsonSchema::integer(); }
};

template <class T>
struct SchemaTraits<T, std::enable_if_t<std::is_floating_point_v<T>>> {
  static Json schema() { return JsonSchema::number(); }
};

/// @brief Returns the JSON Schema advertised for a C++ type.
template <class T>
inline Json schema_for();

template <class T>
struct SchemaTraits<std::vector<T>> {
  static Json schema() { return JsonSchema::array(schema_for<T>()); }
};

template <class T>
struct SchemaTraits<std::optional<T>> {
  static Json schema() { return schema_for<T>(); }
};

/// @brief Auto-generates JSON Schema from a Reflect<T> specialization.
namespace detail {

template <typename Struct, typename Field>
inline void add_reflected_field_schema(
    Json& properties, Json& required,
    const FieldDescriptor<Struct, Field>& fd) {
  if constexpr (is_optional_v<Field>) {
    properties[fd.wire_name] = schema_for<typename Field::value_type>();
  } else {
    properties[fd.wire_name] = schema_for<Field>();
    required.push_back(fd.wire_name);
  }
}

template <typename Struct>
inline void add_reflected_field_schema(Json&, Json&,
                                       const ExtensionsField<Struct>&) {}

template <typename Struct, typename Field>
inline void add_reflected_field_schema(
    Json&, Json&, const DeserializeOnlyField<Struct, Field>&) {}

template <class T>
inline Json reflect_schema() {
  Json properties = Json::object();
  Json required = Json::array();

  std::apply(
      [&](const auto&... fds) {
        (add_reflected_field_schema(properties, required, fds), ...);
      },
      Reflect<T>::fields());

  Json result = {{"type", "object"}, {"properties", std::move(properties)}};
  if (!required.empty()) {
    result["required"] = std::move(required);
  }
  result["additionalProperties"] = false;
  return result;
}

}  // namespace detail

/// @brief Returns the JSON Schema advertised for a C++ type.
///
/// For types with a Reflect<T> specialization, generates schema from reflected
/// fields. For all other types, delegates to SchemaTraits<T>.
template <class T>
inline Json schema_for() {
  using Decayed = std::decay_t<T>;
  if constexpr (has_reflect_v<Decayed>) {
    return detail::reflect_schema<Decayed>();
  } else {
    return SchemaTraits<Decayed>::schema();
  }
}

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
