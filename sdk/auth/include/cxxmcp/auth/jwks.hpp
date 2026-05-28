// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cxxmcp/auth/lifecycle.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief JWKS value models, parsing, fetch, and cache contracts.

namespace mcp::auth {

/// @brief Public JSON Web Key value model.
///
/// This type intentionally stores key material as public JWK parameters only.
/// It does not perform JWT decoding, signature verification, or key trust
/// decisions by itself.
struct JsonWebKey {
  std::string key_type;
  std::optional<std::string> public_key_use;
  StringList key_operations;
  std::optional<std::string> algorithm;
  std::optional<std::string> key_id;
  std::optional<std::string> curve;
  std::optional<std::string> x;
  std::optional<std::string> y;
  std::optional<std::string> modulus;
  std::optional<std::string> exponent;
  StringList certificate_chain;
  MetadataMap metadata;
};

/// @brief JSON Web Key Set value model.
struct JsonWebKeySet {
  std::vector<JsonWebKey> keys;
  MetadataMap metadata;
};

/// @brief Criteria for selecting a public JWK before verification.
struct JwkSelectionCriteria {
  std::optional<std::string> key_id;
  std::optional<std::string> algorithm;
  std::optional<std::string> key_type;
  std::optional<std::string> public_key_use;
};

/// @brief Request for retrieving a JWKS document.
struct JwksFetchRequest {
  std::string jwks_uri;
  HeaderMap headers;
};

/// @brief Application-provided JWKS retrieval boundary.
class JwksEndpoint {
 public:
  virtual ~JwksEndpoint() = default;

  virtual core::Result<JsonWebKeySet> fetch_jwks(
      const JwksFetchRequest& request) = 0;
};

/// @brief Application-provided JWKS cache boundary.
class JwksCache {
 public:
  virtual ~JwksCache() = default;

  virtual core::Result<std::optional<JsonWebKeySet>> load(
      const std::string& jwks_uri) = 0;
  virtual core::Result<core::Unit> save(std::string jwks_uri,
                                        JsonWebKeySet keys) = 0;
  virtual core::Result<core::Unit> clear(const std::string& jwks_uri) = 0;
};

/// @brief In-process JWKS cache for tests and embedded clients.
class InMemoryJwksCache final : public JwksCache {
 public:
  core::Result<std::optional<JsonWebKeySet>> load(
      const std::string& jwks_uri) override {
    for (const auto& entry : entries_) {
      if (constant_time_string_equal(entry.first, jwks_uri)) {
        return entry.second;
      }
    }
    return std::optional<JsonWebKeySet>{};
  }

  core::Result<core::Unit> save(std::string jwks_uri,
                                JsonWebKeySet keys) override {
    for (auto& entry : entries_) {
      if (constant_time_string_equal(entry.first, jwks_uri)) {
        entry.second = std::move(keys);
        return core::Unit{};
      }
    }
    entries_.emplace_back(std::move(jwks_uri), std::move(keys));
    return core::Unit{};
  }

  core::Result<core::Unit> clear(const std::string& jwks_uri) override {
    for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
      if (constant_time_string_equal(iter->first, jwks_uri)) {
        entries_.erase(iter);
        break;
      }
    }
    return core::Unit{};
  }

 private:
  std::vector<std::pair<std::string, JsonWebKeySet>> entries_;
};

namespace jwks_detail {

using Json = nlohmann::json;

inline std::optional<std::string> optional_string(const Json& value,
                                                  const char* key) {
  const auto iter = value.find(key);
  if (iter == value.end() || iter->is_null()) {
    return std::nullopt;
  }
  if (iter->is_string()) {
    return iter->get<std::string>();
  }
  return std::nullopt;
}

inline std::string string_or_empty(const Json& value, const char* key) {
  auto result = optional_string(value, key);
  return result.has_value() ? *result : std::string{};
}

inline StringList string_list_or_empty(const Json& value, const char* key) {
  StringList result;
  const auto iter = value.find(key);
  if (iter == value.end() || !iter->is_array()) {
    return result;
  }
  for (const auto& entry : *iter) {
    if (entry.is_string()) {
      result.push_back(entry.get<std::string>());
    }
  }
  return result;
}

inline MetadataMap extension_metadata(const Json& value) {
  MetadataMap result;
  for (auto iter = value.begin(); iter != value.end(); ++iter) {
    const auto key = iter.key();
    if (key == "keys" || key == "kty" || key == "use" || key == "key_ops" ||
        key == "alg" || key == "kid" || key == "crv" || key == "x" ||
        key == "y" || key == "n" || key == "e" || key == "x5c") {
      continue;
    }
    if (iter->is_string()) {
      result.emplace(key, iter->get<std::string>());
    } else if (iter->is_boolean()) {
      result.emplace(key, iter->get<bool>() ? "true" : "false");
    } else if (iter->is_number() || iter->is_object() || iter->is_array()) {
      result.emplace(key, iter->dump());
    }
  }
  return result;
}

inline bool optional_matches(const std::optional<std::string>& expected,
                             const std::optional<std::string>& actual) {
  return !expected.has_value() || (actual.has_value() && *actual == *expected);
}

}  // namespace jwks_detail

/// @brief Parse a public JWK from JSON without performing trust decisions.
inline core::Result<JsonWebKey> parse_json_web_key(
    const nlohmann::json& value) {
  if (!value.is_object()) {
    return mcp::core::unexpected(
        make_oauth_error(OAuthErrorCode::kMetadataDiscoveryFailed,
                         "JWK JSON value must be an object"));
  }

  JsonWebKey key;
  key.key_type = jwks_detail::string_or_empty(value, "kty");
  if (key.key_type.empty()) {
    return mcp::core::unexpected(make_oauth_error(
        OAuthErrorCode::kMetadataDiscoveryFailed, "JWK kty is required"));
  }
  key.public_key_use = jwks_detail::optional_string(value, "use");
  key.key_operations = jwks_detail::string_list_or_empty(value, "key_ops");
  key.algorithm = jwks_detail::optional_string(value, "alg");
  key.key_id = jwks_detail::optional_string(value, "kid");
  key.curve = jwks_detail::optional_string(value, "crv");
  key.x = jwks_detail::optional_string(value, "x");
  key.y = jwks_detail::optional_string(value, "y");
  key.modulus = jwks_detail::optional_string(value, "n");
  key.exponent = jwks_detail::optional_string(value, "e");
  key.certificate_chain = jwks_detail::string_list_or_empty(value, "x5c");
  key.metadata = jwks_detail::extension_metadata(value);
  return key;
}

/// @brief Parse a public JWKS document from JSON.
inline core::Result<JsonWebKeySet> parse_json_web_key_set(
    const nlohmann::json& value) {
  if (!value.is_object()) {
    return mcp::core::unexpected(
        make_oauth_error(OAuthErrorCode::kMetadataDiscoveryFailed,
                         "JWKS JSON value must be an object"));
  }
  const auto keys = value.find("keys");
  if (keys == value.end() || !keys->is_array()) {
    return mcp::core::unexpected(
        make_oauth_error(OAuthErrorCode::kMetadataDiscoveryFailed,
                         "JWKS keys array is required"));
  }

  JsonWebKeySet set;
  for (const auto& entry : *keys) {
    auto parsed = parse_json_web_key(entry);
    if (!parsed.has_value()) {
      return mcp::core::unexpected(parsed.error());
    }
    set.keys.push_back(std::move(*parsed));
  }
  set.metadata = jwks_detail::extension_metadata(value);
  return set;
}

/// @brief Select a single JWK by stable JWT header criteria.
///
/// If the criteria match no key or multiple keys, the result is an error so a
/// verifier cannot accidentally continue with an ambiguous key.
inline core::Result<JsonWebKey> select_json_web_key(
    const JsonWebKeySet& set, const JwkSelectionCriteria& criteria) {
  std::optional<JsonWebKey> selected;
  for (const auto& key : set.keys) {
    if (!jwks_detail::optional_matches(criteria.key_id, key.key_id) ||
        !jwks_detail::optional_matches(criteria.algorithm, key.algorithm) ||
        !jwks_detail::optional_matches(
            criteria.key_type, std::optional<std::string>{key.key_type}) ||
        !jwks_detail::optional_matches(criteria.public_key_use,
                                       key.public_key_use)) {
      continue;
    }
    if (selected.has_value()) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kMetadataDiscoveryFailed,
                           "JWKS key selection is ambiguous"));
    }
    selected = key;
  }
  if (!selected.has_value()) {
    return mcp::core::unexpected(
        make_oauth_error(OAuthErrorCode::kMetadataDiscoveryFailed,
                         "JWKS key selection found no matching key"));
  }
  return *selected;
}

}  // namespace mcp::auth
