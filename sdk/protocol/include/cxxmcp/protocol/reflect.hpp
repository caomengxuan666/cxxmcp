// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/reflect.hpp
/// @brief C++17 tuple-reflection infrastructure for zero-boilerplate DTO
/// serialization.
///
/// Each DTO that opts into reflection provides a `Reflect<T>` specialization
/// with `fields()` returning a tuple of FieldDescriptor. The generic
/// `reflect_to_json()` and `reflect_from_json<T>()` functions then handle
/// serialization automatically.  `known_keys()` is optional; when absent,
/// wire names are extracted from the `fields()` tuple at runtime.
///
/// Type-specific logic is dispatched through `JsonFieldTraits<T>` partial
/// specializations for optional, vector, string, enum, variant, and nested-DTO
/// types.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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
  bool absent_as_default = false;
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

/// @brief Creates a FieldDescriptor whose missing wire value keeps the C++
/// default value.
template <typename Struct, typename Field>
constexpr FieldDescriptor<Struct, Field> defaulted_field(
    const char* wire_name, Field Struct::* pointer) {
  return {wire_name, pointer, nullptr, false, true};
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
// SFINAE: detect optional known_keys() on Reflect<T>
// ---------------------------------------------------------------------------

namespace detail {
template <typename T, typename = void>
struct has_known_keys : std::false_type {};

template <typename T>
struct has_known_keys<T, std::void_t<decltype(T::known_keys())>>
    : std::true_type {};

/// @brief Overloads that extract wire_name from field descriptors.
/// ExtensionsField has no wire_name; the others do.
template <typename S, typename F>
void push_wire_name(std::vector<std::string>& keys,
                    const FieldDescriptor<S, F>& fd) {
  keys.push_back(fd.wire_name);
}
template <typename S, typename F>
void push_wire_name(std::vector<std::string>& keys,
                    const DeserializeOnlyField<S, F>& fd) {
  keys.push_back(fd.wire_name);
}
template <typename S>
void push_wire_name(std::vector<std::string>&, const ExtensionsField<S>&) {}
}  // namespace detail

// ---------------------------------------------------------------------------
// Primary Reflect template (unspecialized types are not reflectable)
// ---------------------------------------------------------------------------

/// @brief Primary template. Types that support reflection must provide a
/// specialization with `fields()` and `defined = true`.  `known_keys()` is
/// optional; when absent, wire names are derived from the fields() tuple.
template <typename T>
struct Reflect {
  static constexpr bool defined = false;
};

/// @brief Extracts known wire names from the fields() tuple at runtime.
///
/// This is the fallback used when Reflect<T> does not define known_keys().
template <typename T>
std::vector<std::string> extract_known_keys() {
  std::vector<std::string> keys;
  std::apply(
      [&keys](const auto&... fds) { (detail::push_wire_name(keys, fds), ...); },
      Reflect<T>::fields());
  return keys;
}

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
    const auto& val = json.at(key);
    if constexpr (std::is_same_v<T, bool>) {
      if (!val.is_boolean()) {
        return false;
      }
    } else if constexpr (std::is_floating_point_v<T>) {
      if (!val.is_number()) {
        return false;
      }
    } else if constexpr (std::is_integral_v<T>) {
      if (!val.is_number_integer()) {
        return false;
      }
    }
    target = val.get<T>();
    return true;
  }
};

/// @brief Generic support for nested DTOs that opt into Reflect<T>.
template <typename T>
struct JsonFieldTraits<T, std::enable_if_t<has_reflect_v<T>>> {
  static void serialize(Json& json, const char* key, const T& value) {
    json[key] = reflect_to_json(value);
  }

  static bool deserialize(const Json& json, const char* key, T& target) {
    if (!json.contains(key)) {
      return false;
    }
    auto value = reflect_from_json<T>(json.at(key));
    if (!value) {
      return false;
    }
    target = std::move(*value);
    return true;
  }
};

// ---------------------------------------------------------------------------
// Traits for std::string (omit empty on serialize, no default on deserialize)
// ---------------------------------------------------------------------------

template <>
struct JsonFieldTraits<std::string> {
  static void serialize(Json& json, const char* key, const std::string& value) {
    json[key] = value;
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

// ---------------------------------------------------------------------------
// Note: JsonFieldTraits<Icon> and JsonFieldTraits<std::vector<Icon>> are no
// longer needed. Icon uses Reflect<Icon> (in types_reflect.hpp) via the
// generic vector<T> trait and reflect_to_json/reflect_from_json.
// ---------------------------------------------------------------------------

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
    target.clear();
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
// Traits for std::map<std::string, std::string> (string-to-string maps)
// ---------------------------------------------------------------------------

template <>
struct JsonFieldTraits<std::map<std::string, std::string>> {
  static void serialize(Json& json, const char* key,
                        const std::map<std::string, std::string>& value) {
    if (value.empty()) {
      return;
    }
    json[key] = Json::object();
    for (const auto& [k, v] : value) {
      json[key][k] = v;
    }
  }

  static bool deserialize(const Json& json, const char* key,
                          std::map<std::string, std::string>& target) {
    if (!json.contains(key)) {
      return true;  // missing map = empty
    }
    if (!json.at(key).is_object()) {
      return false;
    }
    target.clear();
    for (const auto& [k, v] : json.at(key).items()) {
      if (!v.is_string()) {
        return false;
      }
      target[k] = v.get<std::string>();
    }
    return true;
  }
};

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
    if (fd.absent_as_default) {
      obj.*(fd.pointer) = Field{};
      status = core::Unit{};
      return true;
    }
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
    auto known = [] {
      if constexpr (detail::has_known_keys<Reflect<T>>::value) {
        return Reflect<T>::known_keys();
      } else {
        return extract_known_keys<T>();
      }
    }();
    obj.extensions = collect_json_extensions(json, known);
  }

  return obj;
}

// ---------------------------------------------------------------------------
// CXXMCP_REFLECT: one-line Reflect<T> specialization
// ---------------------------------------------------------------------------

#define CXXMCP_REFL_IMPL_1(T, f1)                                        \
  template <>                                                            \
  struct mcp::protocol::Reflect<T> {                                     \
    static constexpr bool defined = true;                                \
    static auto fields() { return std::make_tuple(field(#f1, &T::f1)); } \
  };

#define CXXMCP_REFL_IMPL_2(T, f1, f2)                                 \
  template <>                                                         \
  struct mcp::protocol::Reflect<T> {                                  \
    static constexpr bool defined = true;                             \
    static auto fields() {                                            \
      return std::make_tuple(field(#f1, &T::f1), field(#f2, &T::f2)); \
    }                                                                 \
  };

#define CXXMCP_REFL_IMPL_3(T, f1, f2, f3)                            \
  template <>                                                        \
  struct mcp::protocol::Reflect<T> {                                 \
    static constexpr bool defined = true;                            \
    static auto fields() {                                           \
      return std::make_tuple(field(#f1, &T::f1), field(#f2, &T::f2), \
                             field(#f3, &T::f3));                    \
    }                                                                \
  };

#define CXXMCP_REFL_IMPL_4(T, f1, f2, f3, f4)                         \
  template <>                                                         \
  struct mcp::protocol::Reflect<T> {                                  \
    static constexpr bool defined = true;                             \
    static auto fields() {                                            \
      return std::make_tuple(field(#f1, &T::f1), field(#f2, &T::f2),  \
                             field(#f3, &T::f3), field(#f4, &T::f4)); \
    }                                                                 \
  };

#define CXXMCP_REFL_IMPL_5(T, f1, f2, f3, f4, f5)                    \
  template <>                                                        \
  struct mcp::protocol::Reflect<T> {                                 \
    static constexpr bool defined = true;                            \
    static auto fields() {                                           \
      return std::make_tuple(field(#f1, &T::f1), field(#f2, &T::f2), \
                             field(#f3, &T::f3), field(#f4, &T::f4), \
                             field(#f5, &T::f5));                    \
    }                                                                \
  };

#define CXXMCP_REFL_IMPL_6(T, f1, f2, f3, f4, f5, f6)                 \
  template <>                                                         \
  struct mcp::protocol::Reflect<T> {                                  \
    static constexpr bool defined = true;                             \
    static auto fields() {                                            \
      return std::make_tuple(field(#f1, &T::f1), field(#f2, &T::f2),  \
                             field(#f3, &T::f3), field(#f4, &T::f4),  \
                             field(#f5, &T::f5), field(#f6, &T::f6)); \
    }                                                                 \
  };

#define CXXMCP_REFL_IMPL_7(T, f1, f2, f3, f4, f5, f6, f7)            \
  template <>                                                        \
  struct mcp::protocol::Reflect<T> {                                 \
    static constexpr bool defined = true;                            \
    static auto fields() {                                           \
      return std::make_tuple(field(#f1, &T::f1), field(#f2, &T::f2), \
                             field(#f3, &T::f3), field(#f4, &T::f4), \
                             field(#f5, &T::f5), field(#f6, &T::f6), \
                             field(#f7, &T::f7));                    \
    }                                                                \
  };

#define CXXMCP_REFL_IMPL_8(T, f1, f2, f3, f4, f5, f6, f7, f8)         \
  template <>                                                         \
  struct mcp::protocol::Reflect<T> {                                  \
    static constexpr bool defined = true;                             \
    static auto fields() {                                            \
      return std::make_tuple(field(#f1, &T::f1), field(#f2, &T::f2),  \
                             field(#f3, &T::f3), field(#f4, &T::f4),  \
                             field(#f5, &T::f5), field(#f6, &T::f6),  \
                             field(#f7, &T::f7), field(#f8, &T::f8)); \
    }                                                                 \
  };

#define CXXMCP_REFL_IMPL_9(T, f1, f2, f3, f4, f5, f6, f7, f8, f9)      \
  template <>                                                          \
  struct mcp::protocol::Reflect<T> {                                   \
    static constexpr bool defined = true;                              \
    static auto fields() {                                             \
      return std::make_tuple(                                          \
          field(#f1, &T::f1), field(#f2, &T::f2), field(#f3, &T::f3),  \
          field(#f4, &T::f4), field(#f5, &T::f5), field(#f6, &T::f6),  \
          field(#f7, &T::f7), field(#f8, &T::f8), field(#f9, &T::f9)); \
    }                                                                  \
  };

#define CXXMCP_REFL_IMPL_10(T, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) \
  template <>                                                           \
  struct mcp::protocol::Reflect<T> {                                    \
    static constexpr bool defined = true;                               \
    static auto fields() {                                              \
      return std::make_tuple(field(#f1, &T::f1), field(#f2, &T::f2),    \
                             field(#f3, &T::f3), field(#f4, &T::f4),    \
                             field(#f5, &T::f5), field(#f6, &T::f6),    \
                             field(#f7, &T::f7), field(#f8, &T::f8),    \
                             field(#f9, &T::f9), field(#f10, &T::f10)); \
    }                                                                   \
  };

#define CXXMCP_REFL_IMPL_11(T, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) \
  template <>                                                                \
  struct mcp::protocol::Reflect<T> {                                         \
    static constexpr bool defined = true;                                    \
    static auto fields() {                                                   \
      return std::make_tuple(                                                \
          field(#f1, &T::f1), field(#f2, &T::f2), field(#f3, &T::f3),        \
          field(#f4, &T::f4), field(#f5, &T::f5), field(#f6, &T::f6),        \
          field(#f7, &T::f7), field(#f8, &T::f8), field(#f9, &T::f9),        \
          field(#f10, &T::f10), field(#f11, &T::f11));                       \
    }                                                                        \
  };

#define CXXMCP_REFL_IMPL_12(T, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, \
                            f12)                                             \
  template <>                                                                \
  struct mcp::protocol::Reflect<T> {                                         \
    static constexpr bool defined = true;                                    \
    static auto fields() {                                                   \
      return std::make_tuple(                                                \
          field(#f1, &T::f1), field(#f2, &T::f2), field(#f3, &T::f3),        \
          field(#f4, &T::f4), field(#f5, &T::f5), field(#f6, &T::f6),        \
          field(#f7, &T::f7), field(#f8, &T::f8), field(#f9, &T::f9),        \
          field(#f10, &T::f10), field(#f11, &T::f11), field(#f12, &T::f12)); \
    }                                                                        \
  };

#define CXXMCP_REFL_IMPL_13(T, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, \
                            f12, f13)                                        \
  template <>                                                                \
  struct mcp::protocol::Reflect<T> {                                         \
    static constexpr bool defined = true;                                    \
    static auto fields() {                                                   \
      return std::make_tuple(                                                \
          field(#f1, &T::f1), field(#f2, &T::f2), field(#f3, &T::f3),        \
          field(#f4, &T::f4), field(#f5, &T::f5), field(#f6, &T::f6),        \
          field(#f7, &T::f7), field(#f8, &T::f8), field(#f9, &T::f9),        \
          field(#f10, &T::f10), field(#f11, &T::f11), field(#f12, &T::f12),  \
          field(#f13, &T::f13));                                             \
    }                                                                        \
  };

#define CXXMCP_REFL_IMPL_14(T, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, \
                            f12, f13, f14)                                   \
  template <>                                                                \
  struct mcp::protocol::Reflect<T> {                                         \
    static constexpr bool defined = true;                                    \
    static auto fields() {                                                   \
      return std::make_tuple(                                                \
          field(#f1, &T::f1), field(#f2, &T::f2), field(#f3, &T::f3),        \
          field(#f4, &T::f4), field(#f5, &T::f5), field(#f6, &T::f6),        \
          field(#f7, &T::f7), field(#f8, &T::f8), field(#f9, &T::f9),        \
          field(#f10, &T::f10), field(#f11, &T::f11), field(#f12, &T::f12),  \
          field(#f13, &T::f13), field(#f14, &T::f14));                       \
    }                                                                        \
  };

#define CXXMCP_REFL_IMPL_15(T, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, \
                            f12, f13, f14, f15)                              \
  template <>                                                                \
  struct mcp::protocol::Reflect<T> {                                         \
    static constexpr bool defined = true;                                    \
    static auto fields() {                                                   \
      return std::make_tuple(                                                \
          field(#f1, &T::f1), field(#f2, &T::f2), field(#f3, &T::f3),        \
          field(#f4, &T::f4), field(#f5, &T::f5), field(#f6, &T::f6),        \
          field(#f7, &T::f7), field(#f8, &T::f8), field(#f9, &T::f9),        \
          field(#f10, &T::f10), field(#f11, &T::f11), field(#f12, &T::f12),  \
          field(#f13, &T::f13), field(#f14, &T::f14), field(#f15, &T::f15)); \
    }                                                                        \
  };

#define CXXMCP_REFL_IMPL_16(T, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, \
                            f12, f13, f14, f15, f16)                         \
  template <>                                                                \
  struct mcp::protocol::Reflect<T> {                                         \
    static constexpr bool defined = true;                                    \
    static auto fields() {                                                   \
      return std::make_tuple(                                                \
          field(#f1, &T::f1), field(#f2, &T::f2), field(#f3, &T::f3),        \
          field(#f4, &T::f4), field(#f5, &T::f5), field(#f6, &T::f6),        \
          field(#f7, &T::f7), field(#f8, &T::f8), field(#f9, &T::f9),        \
          field(#f10, &T::f10), field(#f11, &T::f11), field(#f12, &T::f12),  \
          field(#f13, &T::f13), field(#f14, &T::f14), field(#f15, &T::f15),  \
          field(#f16, &T::f16));                                             \
    }                                                                        \
  };

// Internal dispatch by selecting the implementation from the argument list.
#define CXXMCP_REFLECT_CHOOSER(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, \
                               _12, _13, _14, _15, _16, NAME, ...)           \
  NAME
#define CXXMCP_REFLECT_EXPAND(expr) expr

/// @brief One-line Reflect<T> specialization.
///
/// Usage (at namespace scope, usually in the global namespace):
/// @code
///   CXXMCP_REFLECT(MyStruct, field1, field2, field3)
/// @endcode
///
/// Supports 1 to 16 fields. For more, use a manual Reflect<> specialization.
#define CXXMCP_REFLECT(Type, ...)                                    \
  using ::mcp::protocol::Reflect;                                    \
  using ::mcp::protocol::field;                                      \
  CXXMCP_REFLECT_EXPAND(CXXMCP_REFLECT_CHOOSER(                      \
      __VA_ARGS__, CXXMCP_REFL_IMPL_16, CXXMCP_REFL_IMPL_15,         \
      CXXMCP_REFL_IMPL_14, CXXMCP_REFL_IMPL_13, CXXMCP_REFL_IMPL_12, \
      CXXMCP_REFL_IMPL_11, CXXMCP_REFL_IMPL_10, CXXMCP_REFL_IMPL_9,  \
      CXXMCP_REFL_IMPL_8, CXXMCP_REFL_IMPL_7, CXXMCP_REFL_IMPL_6,    \
      CXXMCP_REFL_IMPL_5, CXXMCP_REFL_IMPL_4, CXXMCP_REFL_IMPL_3,    \
      CXXMCP_REFL_IMPL_2, CXXMCP_REFL_IMPL_1)(Type, __VA_ARGS__))

// ---------------------------------------------------------------------------
// Compile-time reflection completeness check
// ---------------------------------------------------------------------------

/// @brief Validates that a Reflect<> specialization covers the expected number
/// of fields.
///
/// Place this macro immediately after the Reflect<> specialization to catch
/// field count mismatches at compile time. Example:
///
/// @code
/// template <>
/// struct Reflect<MyStruct> { ... };
/// CXXMCP_REFLECT_CHECK(MyStruct, 5);
/// @endcode
///
/// The second argument must equal the number of field() + extensions_field()
/// descriptors in the specialization. A mismatch produces a compile error.
#define CXXMCP_REFLECT_CHECK(Struct, expected_count)                      \
  static_assert(                                                          \
      std::tuple_size_v<                                                  \
          decltype(::mcp::protocol::Reflect<Struct>::fields())> ==        \
          (expected_count),                                               \
      "Reflect<" #Struct                                                  \
      ">::fields() has "                                                  \
      "a different number of descriptors than expected (" #expected_count \
      "). Update the count or add/remove field descriptors.")

}  // namespace mcp::protocol
