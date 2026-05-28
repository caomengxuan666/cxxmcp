// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cxxmcp/auth/dpop.hpp"
#include "cxxmcp/auth/jwks.hpp"
#include "cxxmcp/auth/openssl/jws_verify.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief OpenSSL-backed JWT verification over a trusted JWKS value.

namespace mcp::auth::openssl {

namespace detail {

inline core::Error jwt_error(std::string message, std::string detail = {}) {
  return make_jose_error(JoseErrorCode::kJwtClaimValidationFailed,
                         std::move(message), std::move(detail));
}

inline TimePoint jwt_time_from_unix_seconds(std::int64_t seconds) {
  return TimePoint(std::chrono::seconds(seconds));
}

inline std::optional<std::int64_t> jwt_numeric_date(
    const nlohmann::json& object, const char* key) {
  const auto iter = object.find(key);
  if (iter == object.end() || iter->is_null()) {
    return std::nullopt;
  }
  if (iter->is_number_integer()) {
    return iter->get<std::int64_t>();
  }
  if (iter->is_number_unsigned()) {
    const auto value = iter->get<std::uint64_t>();
    if (value <= static_cast<std::uint64_t>(
                     (std::numeric_limits<std::int64_t>::max)())) {
      return static_cast<std::int64_t>(value);
    }
  }
  return std::nullopt;
}

inline std::optional<std::string> jwt_string_claim(const nlohmann::json& object,
                                                   const char* key) {
  const auto iter = object.find(key);
  if (iter == object.end() || iter->is_null() || !iter->is_string()) {
    return std::nullopt;
  }
  return iter->get<std::string>();
}

inline std::string jwt_claim_to_metadata_value(const nlohmann::json& value) {
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_boolean()) {
    return value.get<bool>() ? "true" : "false";
  }
  if (value.is_number() || value.is_array() || value.is_object()) {
    return value.dump();
  }
  return {};
}

inline bool jwt_required_claim_matches(const nlohmann::json& payload,
                                       const std::string& key,
                                       const std::string& expected) {
  const auto iter = payload.find(key);
  if (iter == payload.end() || iter->is_null()) {
    return false;
  }
  return jwt_claim_to_metadata_value(*iter) == expected;
}

inline bool jwt_audience_matches(const nlohmann::json& payload,
                                 const std::string& expected) {
  const auto iter = payload.find("aud");
  if (iter == payload.end() || iter->is_null()) {
    return false;
  }
  if (iter->is_string()) {
    return iter->get<std::string>() == expected;
  }
  if (iter->is_array()) {
    for (const auto& entry : *iter) {
      if (entry.is_string() && entry.get<std::string>() == expected) {
        return true;
      }
    }
  }
  return false;
}

inline std::string jwt_verified_audience(
    const nlohmann::json& payload, const std::optional<std::string>& expected) {
  if (expected.has_value()) {
    return *expected;
  }
  const auto iter = payload.find("aud");
  if (iter == payload.end() || iter->is_null()) {
    return {};
  }
  if (iter->is_string()) {
    return iter->get<std::string>();
  }
  if (iter->is_array()) {
    for (const auto& entry : *iter) {
      if (entry.is_string()) {
        return entry.get<std::string>();
      }
    }
  }
  return {};
}

inline MetadataMap jwt_claims_metadata(const nlohmann::json& payload) {
  MetadataMap result;
  for (auto iter = payload.begin(); iter != payload.end(); ++iter) {
    const std::string key = iter.key();
    if (key == "iss" || key == "sub" || key == "aud" || key == "exp" ||
        key == "nbf" || key == "iat" || key == "jti") {
      continue;
    }
    const auto value = jwt_claim_to_metadata_value(*iter);
    if (!value.empty()) {
      result.emplace(key, value);
    }
  }
  return result;
}

inline core::Result<nlohmann::json> parse_jwt_payload_json(
    const std::vector<unsigned char>& payload) {
  try {
    auto parsed = nlohmann::json::parse(payload.begin(), payload.end());
    if (!parsed.is_object()) {
      return core::unexpected(jwt_error("JWT payload must be a JSON object"));
    }
    return parsed;
  } catch (const nlohmann::json::parse_error& error) {
    return core::unexpected(
        jwt_error("JWT payload is not valid JSON", error.what()));
  }
}

inline bool jwt_error_should_refresh_jwks(const core::Error& error) {
  const int metadata_error =
      static_cast<int>(OAuthErrorCode::kMetadataDiscoveryFailed);
  return error.code == metadata_error ||
         error.code == static_cast<int>(JoseErrorCode::kInvalidJwk) ||
         error.code ==
             static_cast<int>(JoseErrorCode::kUnsupportedJoseAlgorithm) ||
         error.code ==
             static_cast<int>(JoseErrorCode::kSignatureVerificationFailed);
}

}  // namespace detail

inline core::Result<VerifiedJwtClaims> verify_jwt_with_jwks(
    const JwtVerificationRequest& request, const JsonWebKeySet& jwks) {
  if (request.jwt.empty()) {
    return core::unexpected(detail::jwt_error("JWT is required"));
  }

  auto decoded = decode_compact_jws(request.jwt);
  if (!decoded.has_value()) {
    return core::unexpected(decoded.error());
  }

  const auto& header = decoded->protected_header;
  if (request.required_algorithm.has_value() &&
      header.algorithm != *request.required_algorithm) {
    return core::unexpected(
        detail::jwt_error("JWT alg does not match required algorithm"));
  }

  JwkSelectionCriteria criteria;
  criteria.key_id = header.key_id;
  criteria.algorithm = header.algorithm;
  criteria.public_key_use = "sig";
  auto key = select_json_web_key(jwks, criteria);
  if (!key.has_value()) {
    criteria.public_key_use.reset();
    key = select_json_web_key(jwks, criteria);
  }
  if (!key.has_value()) {
    return core::unexpected(key.error());
  }
  if (key->public_key_use.has_value() && *key->public_key_use != "sig") {
    return core::unexpected(
        detail::jwt_error("JWKS key is not intended for signatures"));
  }
  if (!key->key_operations.empty() &&
      std::find(key->key_operations.begin(), key->key_operations.end(),
                "verify") == key->key_operations.end()) {
    return core::unexpected(
        detail::jwt_error("JWKS key is not intended for verification"));
  }

  JwsVerificationOptions verification_options;
  verification_options.required_algorithm = header.algorithm;
  verification_options.required_key_id = header.key_id;
  auto verified_signature =
      verify_compact_jws_signature(request.jwt, *key, verification_options);
  if (!verified_signature.has_value()) {
    return core::unexpected(verified_signature.error());
  }

  auto payload = detail::parse_jwt_payload_json(decoded->payload);
  if (!payload.has_value()) {
    return core::unexpected(payload.error());
  }

  const auto expires_at = detail::jwt_numeric_date(*payload, "exp");
  if (!expires_at.has_value()) {
    return core::unexpected(detail::jwt_error("JWT exp claim is required"));
  }
  if (detail::jwt_time_from_unix_seconds(*expires_at) <= request.now) {
    return core::unexpected(detail::jwt_error("JWT has expired"));
  }

  const auto not_before = detail::jwt_numeric_date(*payload, "nbf");
  if (not_before.has_value() &&
      detail::jwt_time_from_unix_seconds(*not_before) > request.now) {
    return core::unexpected(detail::jwt_error("JWT nbf is in the future"));
  }

  const auto issued_at = detail::jwt_numeric_date(*payload, "iat");
  if (issued_at.has_value() &&
      detail::jwt_time_from_unix_seconds(*issued_at) > request.now) {
    return core::unexpected(detail::jwt_error("JWT iat is in the future"));
  }

  const auto issuer = detail::jwt_string_claim(*payload, "iss");
  if (request.issuer.has_value() &&
      (!issuer.has_value() || *issuer != *request.issuer)) {
    return core::unexpected(detail::jwt_error("JWT issuer does not match"));
  }

  if (request.audience.has_value() &&
      !detail::jwt_audience_matches(*payload, *request.audience)) {
    return core::unexpected(detail::jwt_error("JWT audience does not match"));
  }

  for (const auto& required_claim : request.required_claims) {
    if (!detail::jwt_required_claim_matches(*payload, required_claim.first,
                                            required_claim.second)) {
      return core::unexpected(detail::jwt_error(
          "JWT required claim does not match", required_claim.first));
    }
  }

  VerifiedJwtClaims claims;
  claims.issuer = issuer.value_or("");
  claims.subject = detail::jwt_string_claim(*payload, "sub").value_or("");
  claims.audience = detail::jwt_verified_audience(*payload, request.audience);
  claims.expires_at = detail::jwt_time_from_unix_seconds(*expires_at);
  if (issued_at.has_value()) {
    claims.issued_at = detail::jwt_time_from_unix_seconds(*issued_at);
  }
  claims.claims = detail::jwt_claims_metadata(*payload);
  return claims;
}

class StaticJwksJwtVerifier final : public JwtVerifier {
 public:
  explicit StaticJwksJwtVerifier(JsonWebKeySet jwks) : jwks_(std::move(jwks)) {}

  core::Result<VerifiedJwtClaims> verify(
      const JwtVerificationRequest& request) override {
    return verify_jwt_with_jwks(request, jwks_);
  }

 private:
  JsonWebKeySet jwks_;
};

struct FetchingJwksJwtVerifierOptions {
  std::string jwks_uri;
  HeaderMap headers;
  bool use_cache = true;
  bool refresh_on_key_or_signature_failure = true;
};

/// @brief JWT verifier that fetches and caches JWKS through injected SDK
/// boundaries.
///
/// This class deliberately does not own HTTP. Applications provide a
/// `JwksEndpoint` implementation that performs discovery/fetching through
/// their chosen transport. The verifier owns cache policy, key-rotation retry,
/// signature verification, and claims validation.
class FetchingJwksJwtVerifier final : public JwtVerifier {
 public:
  FetchingJwksJwtVerifier(JwksEndpoint& endpoint, JwksCache* cache,
                          FetchingJwksJwtVerifierOptions options)
      : endpoint_(endpoint), cache_(cache), options_(std::move(options)) {}

  core::Result<VerifiedJwtClaims> verify(
      const JwtVerificationRequest& request) override {
    if (options_.jwks_uri.empty()) {
      return core::unexpected(
          detail::jwt_error("JWKS URI is required for JWT verification"));
    }

    if (options_.use_cache && cache_ != nullptr) {
      auto cached = cache_->load(options_.jwks_uri);
      if (!cached.has_value()) {
        return core::unexpected(cached.error());
      }
      if (cached->has_value()) {
        auto verified = verify_jwt_with_jwks(request, **cached);
        if (verified.has_value()) {
          return verified;
        }
        if (!options_.refresh_on_key_or_signature_failure ||
            !detail::jwt_error_should_refresh_jwks(verified.error())) {
          return core::unexpected(verified.error());
        }
      }
    }

    auto fetched = fetch_and_cache_jwks();
    if (!fetched.has_value()) {
      return core::unexpected(fetched.error());
    }
    return verify_jwt_with_jwks(request, *fetched);
  }

 private:
  core::Result<JsonWebKeySet> fetch_and_cache_jwks() {
    JwksFetchRequest fetch_request;
    fetch_request.jwks_uri = options_.jwks_uri;
    fetch_request.headers = options_.headers;
    auto fetched = endpoint_.fetch_jwks(fetch_request);
    if (!fetched.has_value()) {
      return core::unexpected(fetched.error());
    }
    if (cache_ != nullptr) {
      auto saved = cache_->save(options_.jwks_uri, *fetched);
      if (!saved.has_value()) {
        return core::unexpected(saved.error());
      }
    }
    return *fetched;
  }

  JwksEndpoint& endpoint_;
  JwksCache* cache_ = nullptr;
  FetchingJwksJwtVerifierOptions options_;
};

}  // namespace mcp::auth::openssl
