// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/reflect.hpp
/// @brief C++17 tuple-reflection infrastructure for zero-boilerplate DTO
/// serialization.
///
/// Each DTO that opts into reflection provides a `Reflect<T>` specialization
/// with `fields()` returning a tuple of FieldDescriptor and `known_keys()`
/// returning non-extension field names. The generic `reflect_to_json()` and
/// `reflect_from_json<T>()` functions then handle serialization automatically.
///
/// Type-specific logic is dispatched through `JsonFieldTraits<T>` partial
/// specializations for optional, vector, string, enum, variant, and nested-DTO
/// types.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

// ---------------------------------------------------------------------------
// Field descriptor
// ---------------------------------------------------------------------------

/// @brief Describes a single DTO field: its JSON wire name, pointer-to-member,
/// and optional post-deserialization validator.
template <typename Struct, typename Field>
struct FieldDescriptor {
  const char* wire_name;
  Field Struct::* pointer;
  using field_type = Field;
  using struct_type = Struct;
  core::Result<core::Unit> (*post_validate)(const Field&) = nullptr;
  bool absent_as_monostate = false;
};

/// @brief Tag-only field descriptor for the extensions member.
///
/// Extensions are not serialized as a nested JSON key; their contents are
/// merged into the parent object.
template <typename Struct>
struct ExtensionsField {
  Json Struct::* pointer;
  std::vector<std::string> own_keys;
  using field_type = Json;
  using struct_type = Struct;
};

/// @brief Tag-only field descriptor for fields that are populated during
/// deserialization only (e.g. `output_schema_present`).
template <typename Struct, typename Field>
struct DeserializeOnlyField {
  const char* wire_name;
  Field Struct::* pointer;
  using field_type = Field;
  using struct_type = Struct;
};

/// @brief Creates a FieldDescriptor with wire name and pointer-to-member.
template <typename Struct, typename Field>
constexpr FieldDescriptor<Struct, Field> field(const char* wire_name,
                                               Field Struct::* pointer) {
  return {wire_name, pointer, nullptr};
}

/// @brief Creates a FieldDescriptor with a post-deserialization validator.
template <typename Struct, typename Field>
constexpr FieldDescriptor<Struct, Field> validated_field(
    const char* wire_name, Field Struct::* pointer,
    core::Result<core::Unit> (*validator)(const Field&)) {
  return {wire_name, pointer, validator};
}

/// @brief Creates an ExtensionsField descriptor.
template <typename Struct>
ExtensionsField<Struct> extensions_field(Json Struct::* pointer,
                                         std::vector<std::string> own_keys) {
  return {pointer, std::move(own_keys)};
}

/// @brief Creates a DeserializeOnlyField descriptor.
template <typename Struct, typename Field>
constexpr DeserializeOnlyField<Struct, Field> deserialize_only(
    const char* wire_name, Field Struct::* pointer) {
  return {wire_name, pointer};
}

// ---------------------------------------------------------------------------
// Utility type traits
// ---------------------------------------------------------------------------

/// @brief Detects std::optional<T>.
template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

/// @brief Detects std::variant containing std::monostate (nullable on wire).
template <typename T>
struct is_nullable_variant : std::false_type {};

template <typename... Ts>
struct is_nullable_variant<std::variant<Ts...>>
    : std::disjunction<std::is_same<Ts, std::monostate>...> {};

template <typename T>
inline constexpr bool is_nullable_variant_v = is_nullable_variant<T>::value;

/// @brief Creates a FieldDescriptor whose missing wire value maps to
/// std::monostate.
///
/// Use this only for protocol fields where an absent member and an explicit
/// JSON null have the same semantics. Normal variants remain required unless
/// they use this descriptor.
template <typename Struct, typename Field>
constexpr FieldDescriptor<Struct, Field> nullable_field(
    const char* wire_name, Field Struct::* pointer) {
  static_assert(is_nullable_variant_v<Field>,
                "nullable_field requires a std::variant containing "
                "std::monostate");
  return {wire_name, pointer, nullptr, true};
}

/// @brief Detects whether a type has an `extensions` member of type Json.
template <typename T, typename = void>
struct has_extensions_member : std::false_type {};

template <typename T>
struct has_extensions_member<
    T, std::void_t<decltype(std::declval<T>().extensions)>>
    : std::bool_constant<
          std::is_same_v<decltype(std::declval<T>().extensions), Json>> {};

template <typename T>
inline constexpr bool has_extensions_member_v = has_extensions_member<T>::value;

// ---------------------------------------------------------------------------
// Primary Reflect template (unspecialized types are not reflectable)
// ---------------------------------------------------------------------------

/// @brief Primary template. Types that support reflection must provide a full
/// specialization with `fields()`, `known_keys()`, and `defined = true`.
template <typename T>
struct Reflect {
  static constexpr bool defined = false;
};

// ---------------------------------------------------------------------------
// Type trait: detect whether a type has a Reflect specialization
// ---------------------------------------------------------------------------

/// @brief Detects whether `Reflect<T>` is specialized for a given type.
template <typename T, typename = void>
struct has_reflect : std::false_type {};

template <typename T>
struct has_reflect<T, std::void_t<decltype(Reflect<T>::defined)>>
    : std::bool_constant<Reflect<T>::defined> {};

template <typename T>
inline constexpr bool has_reflect_v = has_reflect<T>::value;

// Forward declarations for reflect_to_json / reflect_from_json.
template <typename T>
Json reflect_to_json(const T& obj);
template <typename T>
core::Result<T> reflect_from_json(const Json& json);

// ---------------------------------------------------------------------------
// JsonFieldTraits: primary template for scalars (int64, double, bool)
// ---------------------------------------------------------------------------

/// @brief Type-specific serialization and deserialization logic.
///
/// The primary template handles scalar types that nlohmann/json converts
/// directly. Specializations handle optional, vector, string, enum, variant,
/// and nested-DTO types.
template <typename T, typename Enable = void>
struct JsonFieldTraits {
  static void serialize(Json& json, const char* key, const T& value) {
    json[key] = value;
  }

  static bool deserialize(const Json& json, const char* key, T& target) {
    if (!json.contains(key)) {
      return false;
    }
    target = json.at(key).get<T>();
    return true;
  }
};

// ---------------------------------------------------------------------------
// Traits for std::string (omit empty on serialize, no default on deserialize)
// ---------------------------------------------------------------------------

template <>
struct JsonFieldTraits<std::string> {
  static void serialize(Json& json, const char* key, const std::string& value) {
    if (!value.empty()) {
      json[key] = value;
    }
  }

  static bool deserialize(const Json& json, const char* key,
                          std::string& target) {
    if (!json.contains(key)) {
      return false;
    }
    if (!json.at(key).is_string()) {
      return false;
    }
    target = json.at(key).get<std::string>();
    return true;
  }
};

// ---------------------------------------------------------------------------
// Traits for Json (raw JSON passthrough)
// ---------------------------------------------------------------------------

template <>
struct JsonFieldTraits<Json> {
  static void serialize(Json& json, const char* key, const Json& value) {
    if (!value.is_null() && !value.empty()) {
      json[key] = value;
    }
  }

  static bool deserialize(const Json& json, const char* key, Json& target) {
    if (!json.contains(key)) {
      return false;
    }
    target = json.at(key);
    return true;
  }
};

template <>
struct JsonFieldTraits<IconTheme> {
  static void serialize(Json& json, const char* key, IconTheme value) {
    json[key] = std::string(icon_theme_to_string(value));
  }

  static bool deserialize(const Json& json, const char* key,
                          IconTheme& target) {
    if (!json.contains(key) || !json.at(key).is_string()) {
      return false;
    }
    auto value = icon_theme_from_string(json.at(key).get<std::string>());
    if (!value.has_value()) {
      return false;
    }
    target = *value;
    return true;
  }
};

template <>
struct JsonFieldTraits<Icon> {
  static void serialize(Json& json, const char* key, const Icon& value) {
    json[key] = icon_to_json(value);
  }

  static bool deserialize(const Json& json, const char* key, Icon& target) {
    if (!json.contains(key)) {
      return false;
    }
    auto value = icon_from_json(json.at(key));
    if (!value.has_value()) {
      return false;
    }
    target = std::move(*value);
    return true;
  }
};

template <>
struct JsonFieldTraits<std::vector<Icon>> {
  static void serialize(Json& json, const char* key,
                        const std::vector<Icon>& value) {
    if (value.empty()) {
      return;
    }
    json[key] = Json::array();
    for (const auto& icon : value) {
      json[key].push_back(icon_to_json(icon));
    }
  }

  static bool deserialize(const Json& json, const char* key,
                          std::vector<Icon>& target) {
    if (!json.contains(key)) {
      return true;
    }
    if (!json.at(key).is_array()) {
      return false;
    }
    target.clear();
    target.reserve(json.at(key).size());
    for (const auto& item : json.at(key)) {
      auto icon = icon_from_json(item);
      if (!icon.has_value()) {
        return false;
      }
      target.push_back(std::move(*icon));
    }
    return true;
  }
};

// ---------------------------------------------------------------------------
// Traits for std::optional<T>
// ---------------------------------------------------------------------------

template <typename T>
struct JsonFieldTraits<std::optional<T>> {
  static void serialize(Json& json, const char* key,
                        const std::optional<T>& value) {
    if (value.has_value()) {
      JsonFieldTraits<T>::serialize(json, key, *value);
    }
  }

  static bool deserialize(const Json& json, const char* key,
                          std::optional<T>& target) {
    if (!json.contains(key)) {
      return true;  // optional: missing is OK
    }
    T inner{};
    if (!JsonFieldTraits<T>::deserialize(json, key, inner)) {
      return false;
    }
    target = std::move(inner);
    return true;
  }
};

// ---------------------------------------------------------------------------
// Traits for std::optional<Json> (e.g. `_meta`)
// ---------------------------------------------------------------------------

template <>
struct JsonFieldTraits<std::optional<Json>> {
  static void serialize(Json& json, const char* key,
                        const std::optional<Json>& value) {
    if (value.has_value() && !value->is_null()) {
      json[key] = *value;
    }
  }

  static bool deserialize(const Json& json, const char* key,
                          std::optional<Json>& target) {
    if (!json.contains(key)) {
      return true;
    }
    target = json.at(key);
    return true;
  }
};

// ---------------------------------------------------------------------------
// Traits for std::vector<T> (omit empty, recurse elements)
// ---------------------------------------------------------------------------

template <typename T>
struct JsonFieldTraits<std::vector<T>> {
  static void serialize(Json& json, const char* key,
                        const std::vector<T>& value) {
    if (value.empty()) {
      return;
    }
    json[key] = Json::array();
    for (const auto& item : value) {
      Json element = Json::object();
      JsonFieldTraits<T>::serialize(element, "_item", item);
      json[key].push_back(std::move(element["_item"]));
    }
  }

  static bool deserialize(const Json& json, const char* key,
                          std::vector<T>& target) {
    if (!json.contains(key)) {
      return true;  // missing vector = empty
    }
    if (!json.at(key).is_array()) {
      return false;
    }
    target.reserve(json.at(key).size());
    for (const auto& item : json.at(key)) {
      T element{};
      // For nested DTOs, pass the item directly
      if constexpr (has_reflect_v<T>) {
        auto result = reflect_from_json<T>(item);
        if (!result) {
          return false;
        }
        target.push_back(std::move(*result));
      } else {
        // For scalars, extract directly
        element = item.get<T>();
        target.push_back(std::move(element));
      }
    }
    return true;
  }
};

// ---------------------------------------------------------------------------
// Traits for std::vector<std::string>
// ---------------------------------------------------------------------------

template <>
struct JsonFieldTraits<std::vector<std::string>> {
  static void serialize(Json& json, const char* key,
                        const std::vector<std::string>& value) {
    if (value.empty()) {
      return;
    }
    json[key] = value;
  }

  static bool deserialize(const Json& json, const char* key,
                          std::vector<std::string>& target) {
    if (!json.contains(key)) {
      return true;
    }
    if (!json.at(key).is_array()) {
      return false;
    }
    target.reserve(json.at(key).size());
    for (const auto& item : json.at(key)) {
      if (!item.is_string()) {
        return false;
      }
      target.push_back(item.get<std::string>());
    }
    return true;
  }
};

// ---------------------------------------------------------------------------
// Traits for std::vector<Icon> (Icon has icon_to_json/icon_from_json)
// ---------------------------------------------------------------------------

// Note: Icon vector traits are registered in types.hpp after Icon is defined.
// This forward declaration is resolved by the include order.

// ---------------------------------------------------------------------------
// Traits for std::variant<std::monostate, std::int64_t> (nullable int)
// ---------------------------------------------------------------------------

template <>
struct JsonFieldTraits<std::variant<std::monostate, std::int64_t>> {
  static void serialize(Json& json, const char* key,
                        const std::variant<std::monostate, std::int64_t>& val) {
    if (std::holds_alternative<std::int64_t>(val)) {
      json[key] = std::get<std::int64_t>(val);
    } else {
      json[key] = nullptr;
    }
  }

  static bool deserialize(const Json& json, const char* key,
                          std::variant<std::monostate, std::int64_t>& target) {
    if (!json.contains(key)) {
      target = std::monostate{};
      return true;
    }
    if (json.at(key).is_null()) {
      target = std::monostate{};
      return true;
    }
    if (json.at(key).is_number_integer()) {
      target = json.at(key).get<std::int64_t>();
      return true;
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Traits for std::variant<int64_t, std::string> (RequestId / ProgressToken)
// ---------------------------------------------------------------------------

template <>
struct JsonFieldTraits<std::variant<std::int64_t, std::string>> {
  static void serialize(Json& json, const char* key,
                        const std::variant<std::int64_t, std::string>& val) {
    json[key] = std::visit([](const auto& v) { return Json(v); }, val);
  }

  static bool deserialize(const Json& json, const char* key,
                          std::variant<std::int64_t, std::string>& target) {
    if (!json.contains(key)) {
      return false;
    }
    const auto& val = json.at(key);
    if (val.is_number_integer()) {
      target = val.get<std::int64_t>();
      return true;
    }
    if (val.is_string()) {
      target = val.get<std::string>();
      return true;
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Generic fold-expression helpers for serialize/deserialize one field
// ---------------------------------------------------------------------------

/// @brief Serializes a single field into the JSON object.
template <typename Struct, typename Field>
void serialize_one(Json& json, const Struct& obj,
                   const FieldDescriptor<Struct, Field>& fd) {
  JsonFieldTraits<Field>::serialize(json, fd.wire_name, obj.*(fd.pointer));
}

/// @brief No-op for deserialize-only fields during serialization.
template <typename Struct, typename Field>
void serialize_one(Json&, const Struct&,
                   const DeserializeOnlyField<Struct, Field>&) {}

/// @brief Merges extension members into the parent JSON object.
template <typename Struct>
void serialize_one(Json& json, const Struct& obj,
                   const ExtensionsField<Struct>& fd) {
  append_json_extensions(json, obj.*(fd.pointer));
}

/// @brief Deserializes a single field from the JSON object.
///
/// Returns true on success (including "optional field absent"). Returns false
/// only on a type mismatch or validation failure.
template <typename Struct, typename Field>
bool deserialize_one(const Json& json, Struct& obj,
                     const FieldDescriptor<Struct, Field>& fd,
                     core::Result<core::Unit>& status) {
  const bool present = json.contains(fd.wire_name);
  if (!present) {
    // For optional types, absent is fine. Nullable variants must opt in
    // explicitly with nullable_field(); otherwise they are required fields.
    if constexpr (is_optional_v<Field>) {
      return true;
    }
    if constexpr (is_nullable_variant_v<Field>) {
      if (fd.absent_as_monostate) {
        obj.*(fd.pointer) = Field{std::monostate{}};
        status = core::Unit{};
        return true;
      }
    }
    status = mcp::core::unexpected(core::Error{
        static_cast<int>(ErrorCode::InvalidRequest),
        "missing required field '" + std::string(fd.wire_name) + "'",
        {}});
    return false;
  }
  Field temp{};
  if (!JsonFieldTraits<Field>::deserialize(json, fd.wire_name, temp)) {
    status = mcp::core::unexpected(
        core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                    "invalid field '" + std::string(fd.wire_name) + "'",
                    {}});
    return false;
  }
  if (fd.post_validate) {
    auto validation = fd.post_validate(temp);
    if (!validation) {
      status = mcp::core::unexpected(validation.error());
      return false;
    }
  }
  obj.*(fd.pointer) = std::move(temp);
  status = core::Unit{};
  return true;
}

/// @brief No-op for extensions fields during deserialization.
template <typename Struct>
bool deserialize_one(const Json&, Struct&, const ExtensionsField<Struct>&,
                     core::Result<core::Unit>&) {
  return true;
}

/// @brief Deserializes a deserialize-only field.
template <typename Struct, typename Field>
bool deserialize_one(const Json& json, Struct& obj,
                     const DeserializeOnlyField<Struct, Field>& fd,
                     core::Result<core::Unit>& status) {
  if constexpr (is_optional_v<Field>) {
    JsonFieldTraits<Field>::deserialize(json, fd.wire_name, obj.*(fd.pointer));
    status = core::Unit{};
    return true;
  } else {
    return deserialize_one(
        json, obj,
        FieldDescriptor<Struct, Field>{fd.wire_name, fd.pointer, nullptr},
        status);
  }
}

// ---------------------------------------------------------------------------
// reflect_to_json / reflect_from_json
// ---------------------------------------------------------------------------

/// @brief Serializes a DTO to JSON using its Reflect<T> trait.
///
/// Iterates all fields via fold expression, then merges extension members.
template <typename T>
Json reflect_to_json(const T& obj) {
  static_assert(has_reflect_v<T>,
                "Reflect<T> must be specialized for this type");
  Json json = Json::object();

  // Serialize each field.
  std::apply([&](const auto&... fds) { (serialize_one(json, obj, fds), ...); },
             Reflect<T>::fields());

  // Merge extension members last (they fill gaps left by typed fields).
  if constexpr (has_extensions_member_v<T>) {
    append_json_extensions(json, obj.extensions);
  }
  return json;
}

/// @brief Deserializes a DTO from JSON using its Reflect<T> trait.
///
/// Validates required fields, parses types, runs post-validation hooks,
/// and collects unknown JSON keys into the extensions member.
template <typename T>
core::Result<T> reflect_from_json(const Json& json) {
  static_assert(has_reflect_v<T>,
                "Reflect<T> must be specialized for this type");
  if (!json.is_object()) {
    return mcp::core::unexpected(
        core::Error{static_cast<int>(ErrorCode::InvalidRequest),
                    "expected a JSON object",
                    {}});
  }

  T obj{};
  core::Result<core::Unit> status = core::Unit{};

  // Early-exit fold: on first error, skip remaining fields.
  std::apply(
      [&](const auto&... fds) {
        auto one = [&](const auto& fd) {
          if (status) {
            deserialize_one(json, obj, fd, status);
          }
        };
        (one(fds), ...);
      },
      Reflect<T>::fields());

  if (!status) {
    return mcp::core::unexpected(status.error());
  }

  // Collect unknown keys into extensions.
  if constexpr (has_extensions_member_v<T>) {
    auto known = Reflect<T>::known_keys();
    obj.extensions = collect_json_extensions(json, known);
  }

  return obj;
}

}  // namespace mcp::protocol
