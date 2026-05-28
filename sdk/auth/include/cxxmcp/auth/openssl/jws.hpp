// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/auth/openssl/base64url.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief Safe parsing boundaries for JOSE compact JWS values.

namespace mcp::auth::openssl {

struct CompactJwsParts {
  std::string protected_header;
  std::string payload;
  std::string signature;

  std::string signing_input() const { return protected_header + "." + payload; }
};

struct JoseProtectedHeader {
  std::string algorithm;
  std::optional<std::string> key_id;
  std::optional<std::string> type;
  std::optional<std::string> content_type;
  nlohmann::json raw;
  MetadataMap metadata;
};

struct DecodedCompactJws {
  CompactJwsParts parts;
  JoseProtectedHeader protected_header;
  std::vector<unsigned char> payload;
  std::vector<unsigned char> signature;
};

namespace detail {

inline std::optional<std::string> jose_optional_string(
    const nlohmann::json& object, const char* key) {
  const auto iter = object.find(key);
  if (iter == object.end() || iter->is_null()) {
    return std::nullopt;
  }
  if (!iter->is_string()) {
    return std::nullopt;
  }
  return iter->get<std::string>();
}

inline MetadataMap jose_extension_metadata(const nlohmann::json& object) {
  MetadataMap result;
  for (auto iter = object.begin(); iter != object.end(); ++iter) {
    const auto key = iter.key();
    if (key == "alg" || key == "kid" || key == "typ" || key == "cty" ||
        key == "jwk" || key == "x5c" || key == "x5t" || key == "x5t#S256" ||
        key == "crit") {
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

inline core::Result<nlohmann::json> parse_json_object(std::string_view data,
                                                      std::string message) {
  nlohmann::json value;
  try {
    value = nlohmann::json::parse(data.begin(), data.end());
  } catch (const nlohmann::json::parse_error& error) {
    return core::unexpected(make_jose_error(JoseErrorCode::kInvalidJoseHeader,
                                            std::move(message), error.what()));
  }
  if (!value.is_object()) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidJoseHeader, std::move(message)));
  }
  return value;
}

}  // namespace detail

inline core::Result<CompactJwsParts> parse_compact_jws_parts(
    std::string_view compact_jws) {
  const auto first_dot = compact_jws.find('.');
  if (first_dot == std::string_view::npos) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidCompactJws,
        "compact JWS must contain three dot-separated segments"));
  }
  const auto second_dot = compact_jws.find('.', first_dot + 1);
  if (second_dot == std::string_view::npos ||
      compact_jws.find('.', second_dot + 1) != std::string_view::npos) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidCompactJws,
                        "compact JWS must contain exactly three segments"));
  }

  CompactJwsParts parts;
  parts.protected_header = std::string(compact_jws.substr(0, first_dot));
  parts.payload = std::string(
      compact_jws.substr(first_dot + 1, second_dot - first_dot - 1));
  parts.signature = std::string(compact_jws.substr(second_dot + 1));

  if (parts.protected_header.empty()) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidCompactJws,
                        "compact JWS protected header segment is required"));
  }
  if (parts.payload.empty()) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidCompactJws,
                        "compact JWS payload segment is required"));
  }
  if (parts.signature.empty()) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidCompactJws,
                        "compact JWS signature segment is required"));
  }

  return parts;
}

inline core::Result<JoseProtectedHeader> parse_jose_protected_header(
    std::string_view protected_header_segment) {
  auto decoded = base64url_decode_to_string(protected_header_segment);
  if (!decoded.has_value()) {
    return core::unexpected(decoded.error());
  }

  auto header_json = detail::parse_json_object(
      *decoded, "JWS protected header must be a JSON object");
  if (!header_json.has_value()) {
    return core::unexpected(header_json.error());
  }

  JoseProtectedHeader header;
  header.raw = std::move(*header_json);
  const auto algorithm = detail::jose_optional_string(header.raw, "alg");
  if (!algorithm.has_value() || algorithm->empty()) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidJoseHeader,
                        "JWS protected header alg is required"));
  }
  header.algorithm = *algorithm;
  header.key_id = detail::jose_optional_string(header.raw, "kid");
  header.type = detail::jose_optional_string(header.raw, "typ");
  header.content_type = detail::jose_optional_string(header.raw, "cty");
  header.metadata = detail::jose_extension_metadata(header.raw);
  return header;
}

inline core::Result<DecodedCompactJws> decode_compact_jws(
    std::string_view compact_jws) {
  auto parts = parse_compact_jws_parts(compact_jws);
  if (!parts.has_value()) {
    return core::unexpected(parts.error());
  }

  auto header = parse_jose_protected_header(parts->protected_header);
  if (!header.has_value()) {
    return core::unexpected(header.error());
  }

  auto payload = base64url_decode(parts->payload);
  if (!payload.has_value()) {
    return core::unexpected(payload.error());
  }
  auto signature = base64url_decode(parts->signature);
  if (!signature.has_value()) {
    return core::unexpected(signature.error());
  }

  DecodedCompactJws decoded;
  decoded.parts = std::move(*parts);
  decoded.protected_header = std::move(*header);
  decoded.payload = std::move(*payload);
  decoded.signature = std::move(*signature);
  return decoded;
}

}  // namespace mcp::auth::openssl
