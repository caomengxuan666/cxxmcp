// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cxxmcp/auth/dpop.hpp"
#include "cxxmcp/auth/openssl/jwk.hpp"
#include "cxxmcp/auth/openssl/jws_verify.hpp"
#include "cxxmcp/auth/openssl/sha256.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief OpenSSL-backed DPoP proof signing and verification helpers.

namespace mcp::auth::openssl {

namespace detail {

struct BioDeleter {
  void operator()(BIO* value) const noexcept { BIO_free(value); }
};

using BioPtr = std::unique_ptr<BIO, BioDeleter>;

inline core::Result<EvpPkeyPtr> private_key_from_pem(std::string_view pem) {
  if (pem.empty()) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "DPoP private key PEM is required"));
  }
  if (pem.size() >
      static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "DPoP private key PEM is too large"));
  }

  BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
  if (!bio) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidJwk,
                        "failed to allocate OpenSSL private-key BIO"));
  }

  EVP_PKEY* raw = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
  if (raw == nullptr) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "failed to parse DPoP private key PEM"));
  }
  return EvpPkeyPtr(raw);
}

inline core::Result<std::string> random_jwt_id() {
  std::vector<unsigned char> bytes(16);
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to generate DPoP jti"));
  }
  return base64url_encode(bytes);
}

inline std::int64_t unix_seconds_from_time(TimePoint value) {
  return std::chrono::duration_cast<std::chrono::seconds>(
             value.time_since_epoch())
      .count();
}

inline TimePoint time_from_unix_seconds(std::int64_t seconds) {
  return TimePoint(std::chrono::seconds(seconds));
}

inline nlohmann::json public_jwk_to_json(const JsonWebKey& jwk) {
  nlohmann::json value;
  value["kty"] = jwk.key_type;
  if (jwk.algorithm.has_value()) {
    value["alg"] = *jwk.algorithm;
  }
  if (jwk.key_id.has_value()) {
    value["kid"] = *jwk.key_id;
  }
  if (jwk.curve.has_value()) {
    value["crv"] = *jwk.curve;
  }
  if (jwk.x.has_value()) {
    value["x"] = *jwk.x;
  }
  if (jwk.y.has_value()) {
    value["y"] = *jwk.y;
  }
  if (jwk.modulus.has_value()) {
    value["n"] = *jwk.modulus;
  }
  if (jwk.exponent.has_value()) {
    value["e"] = *jwk.exponent;
  }
  return value;
}

inline core::Result<std::string> sign_compact_jws(
    EVP_PKEY* key, std::string_view algorithm, const nlohmann::json& header,
    const nlohmann::json& payload) {
  const EVP_MD* digest = digest_for_jose_algorithm(algorithm);
  if (digest == nullptr) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kUnsupportedJoseAlgorithm,
                        "unsupported DPoP signing algorithm"));
  }

  const std::string encoded_header = base64url_encode(header.dump());
  const std::string encoded_payload = base64url_encode(payload.dump());
  const std::string signing_input = encoded_header + "." + encoded_payload;

  EvpMdCtxPtr context(EVP_MD_CTX_new());
  if (!context ||
      EVP_DigestSignInit(context.get(), nullptr, digest, nullptr, key) <= 0 ||
      EVP_DigestSignUpdate(context.get(), signing_input.data(),
                           signing_input.size()) <= 0) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to initialize DPoP proof signing"));
  }

  std::size_t signature_size = 0;
  if (EVP_DigestSignFinal(context.get(), nullptr, &signature_size) <= 0) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to size DPoP proof signature"));
  }
  std::vector<unsigned char> signature(signature_size);
  if (EVP_DigestSignFinal(context.get(), signature.data(), &signature_size) <=
      0) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to sign DPoP proof"));
  }
  signature.resize(signature_size);

  if (is_ecdsa_jose_algorithm(algorithm)) {
    auto raw = ecdsa_der_signature_to_raw(signature, algorithm);
    if (!raw.has_value()) {
      return core::unexpected(raw.error());
    }
    signature = std::move(*raw);
  }

  return signing_input + "." + base64url_encode(signature);
}

inline core::Result<nlohmann::json> parse_json_object_bytes(
    const std::vector<unsigned char>& bytes, std::string message) {
  try {
    auto parsed = nlohmann::json::parse(bytes.begin(), bytes.end());
    if (!parsed.is_object()) {
      return core::unexpected(make_jose_error(
          JoseErrorCode::kJwtClaimValidationFailed, std::move(message)));
    }
    return parsed;
  } catch (const nlohmann::json::parse_error& error) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kJwtClaimValidationFailed,
                        std::move(message), error.what()));
  }
}

inline std::optional<std::string> string_claim(const nlohmann::json& object,
                                               const char* key) {
  const auto iter = object.find(key);
  if (iter == object.end() || iter->is_null() || !iter->is_string()) {
    return std::nullopt;
  }
  return iter->get<std::string>();
}

inline std::optional<std::int64_t> numeric_date_claim(
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

inline MetadataMap dpop_claim_metadata(const nlohmann::json& payload) {
  MetadataMap result;
  for (auto iter = payload.begin(); iter != payload.end(); ++iter) {
    const std::string key = iter.key();
    if (key == "jti" || key == "htm" || key == "htu" || key == "iat" ||
        key == "ath" || key == "nonce") {
      continue;
    }
    if (iter->is_string()) {
      result.emplace(key, iter->get<std::string>());
    } else if (iter->is_boolean()) {
      result.emplace(key, iter->get<bool>() ? "true" : "false");
    } else if (iter->is_number() || iter->is_array() || iter->is_object()) {
      result.emplace(key, iter->dump());
    }
  }
  return result;
}

}  // namespace detail

struct PrivateKeyJwtAssertionRequest {
  std::string private_key_pem;
  std::string algorithm = "ES256";
  std::string client_id;
  std::string audience;
  std::optional<std::string> key_id;
  std::optional<std::string> jwt_id;
  TimePoint issued_at = SystemClock::now();
  std::chrono::seconds lifetime = std::chrono::minutes(5);
};

inline core::Result<std::string> sign_private_key_jwt_assertion(
    PrivateKeyJwtAssertionRequest request) {
  if (request.client_id.empty()) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kJwtClaimValidationFailed, "client_id is required"));
  }
  if (request.audience.empty()) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kJwtClaimValidationFailed, "audience is required"));
  }

  auto private_key = detail::private_key_from_pem(request.private_key_pem);
  if (!private_key.has_value()) {
    return core::unexpected(private_key.error());
  }

  nlohmann::json header;
  header["alg"] = request.algorithm;
  if (request.key_id.has_value()) {
    header["kid"] = *request.key_id;
  }

  core::Result<std::string> jwt_id =
      request.jwt_id.has_value() ? core::Result<std::string>(*request.jwt_id)
                                 : detail::random_jwt_id();
  if (!jwt_id.has_value()) {
    return core::unexpected(jwt_id.error());
  }

  const auto issued_at = detail::unix_seconds_from_time(request.issued_at);
  const auto expires_at =
      detail::unix_seconds_from_time(request.issued_at + request.lifetime);

  nlohmann::json payload;
  payload["iss"] = request.client_id;
  payload["sub"] = request.client_id;
  payload["aud"] = request.audience;
  payload["jti"] = *jwt_id;
  payload["iat"] = issued_at;
  payload["exp"] = expires_at;

  return detail::sign_compact_jws(private_key->get(), request.algorithm, header,
                                  payload);
}

struct OpenSslDpopSignerOptions {
  std::function<TimePoint()> now;
  std::function<core::Result<std::string>()> jwt_id_generator;
};

class OpenSslDpopSigner final : public DpopSigner {
 public:
  explicit OpenSslDpopSigner(OpenSslDpopSignerOptions options = {})
      : options_(std::move(options)) {}

  core::Result<std::string> sign(const DpopProofRequest& request) override {
    if (request.target.method.empty()) {
      return core::unexpected(
          mcp::auth::detail::dpop_error("HTTP request method is required"));
    }
    if (request.target.url.empty()) {
      return core::unexpected(
          mcp::auth::detail::dpop_error("HTTP request URL is required"));
    }
    if (request.key.algorithm.empty()) {
      return core::unexpected(
          mcp::auth::detail::dpop_error("DPoP signing algorithm is required"));
    }

    auto private_key =
        detail::private_key_from_pem(request.key.private_key_pem.view());
    if (!private_key.has_value()) {
      return core::unexpected(private_key.error());
    }

    auto public_jwk = public_jwk_from_evp_pkey(
        private_key->get(), request.key.algorithm,
        request.key.key_id.empty()
            ? std::optional<std::string>{}
            : std::optional<std::string>{request.key.key_id});
    if (!public_jwk.has_value()) {
      return core::unexpected(public_jwk.error());
    }

    auto jwt_id = options_.jwt_id_generator ? options_.jwt_id_generator()
                                            : detail::random_jwt_id();
    if (!jwt_id.has_value()) {
      return core::unexpected(jwt_id.error());
    }

    const TimePoint now = options_.now ? options_.now() : SystemClock::now();
    nlohmann::json header;
    header["typ"] = "dpop+jwt";
    header["alg"] = request.key.algorithm;
    if (!request.key.key_id.empty()) {
      header["kid"] = request.key.key_id;
    }
    header["jwk"] = detail::public_jwk_to_json(*public_jwk);

    nlohmann::json payload;
    payload["jti"] = *jwt_id;
    payload["htm"] = request.target.method;
    payload["htu"] = request.target.url;
    payload["iat"] = detail::unix_seconds_from_time(now);
    if (request.access_token.has_value()) {
      auto access_token_hash = dpop_access_token_hash(*request.access_token);
      if (!access_token_hash.has_value()) {
        return core::unexpected(access_token_hash.error());
      }
      payload["ath"] = *access_token_hash;
    }
    if (request.nonce.has_value()) {
      payload["nonce"] = *request.nonce;
    }

    return detail::sign_compact_jws(private_key->get(), request.key.algorithm,
                                    header, payload);
  }

 private:
  OpenSslDpopSignerOptions options_;
};

struct OpenSslDpopVerifierOptions {
  DpopClaimValidationOptions claim_validation;
  bool validate_claims = true;
};

class OpenSslDpopVerifier final : public DpopVerifier {
 public:
  explicit OpenSslDpopVerifier(OpenSslDpopVerifierOptions options = {})
      : options_(std::move(options)) {}

  core::Result<DpopProofClaims> verify(
      const std::string& proof_jwt, const HttpRequestTarget& target,
      const std::optional<std::string>& access_token) override {
    auto decoded = decode_compact_jws(proof_jwt);
    if (!decoded.has_value()) {
      return core::unexpected(decoded.error());
    }

    const auto& header = decoded->protected_header;
    if (!header.type.has_value() || *header.type != "dpop+jwt") {
      return core::unexpected(
          make_jose_error(JoseErrorCode::kJwtClaimValidationFailed,
                          "DPoP proof typ must be dpop+jwt"));
    }
    const auto jwk_value = header.raw.find("jwk");
    if (jwk_value == header.raw.end() || !jwk_value->is_object()) {
      return core::unexpected(make_jose_error(
          JoseErrorCode::kInvalidJwk, "DPoP proof header jwk is required"));
    }

    auto jwk = parse_json_web_key(*jwk_value);
    if (!jwk.has_value()) {
      return core::unexpected(jwk.error());
    }
    if (!jwk->algorithm.has_value()) {
      jwk->algorithm = header.algorithm;
    }
    if (!jwk->key_id.has_value()) {
      jwk->key_id = header.key_id;
    }

    JwsVerificationOptions verification_options;
    verification_options.required_algorithm = header.algorithm;
    verification_options.required_key_id = header.key_id;
    auto verified =
        verify_compact_jws_signature(proof_jwt, *jwk, verification_options);
    if (!verified.has_value()) {
      return core::unexpected(verified.error());
    }

    auto payload = detail::parse_json_object_bytes(
        decoded->payload, "DPoP proof payload must be a JSON object");
    if (!payload.has_value()) {
      return core::unexpected(payload.error());
    }

    auto claims = parse_claims(*payload);
    if (!claims.has_value()) {
      return core::unexpected(claims.error());
    }

    if (options_.validate_claims) {
      auto claim_options = options_.claim_validation;
      if (access_token.has_value()) {
        auto expected_hash = dpop_access_token_hash(*access_token);
        if (!expected_hash.has_value()) {
          return core::unexpected(expected_hash.error());
        }
        claim_options.expected_access_token_hash = *expected_hash;
      }
      auto validated = validate_dpop_proof_claims(*claims, target, access_token,
                                                  claim_options, nullptr);
      if (!validated.has_value()) {
        return core::unexpected(validated.error());
      }
    }

    return *claims;
  }

 private:
  static core::Result<DpopProofClaims> parse_claims(
      const nlohmann::json& payload) {
    auto jwt_id = detail::string_claim(payload, "jti");
    auto method = detail::string_claim(payload, "htm");
    auto url = detail::string_claim(payload, "htu");
    auto issued_at = detail::numeric_date_claim(payload, "iat");
    if (!jwt_id.has_value() || jwt_id->empty()) {
      return core::unexpected(
          mcp::auth::detail::dpop_error("DPoP proof jti is required"));
    }
    if (!method.has_value() || method->empty()) {
      return core::unexpected(
          mcp::auth::detail::dpop_error("DPoP proof htm is required"));
    }
    if (!url.has_value() || url->empty()) {
      return core::unexpected(
          mcp::auth::detail::dpop_error("DPoP proof htu is required"));
    }
    if (!issued_at.has_value()) {
      return core::unexpected(
          mcp::auth::detail::dpop_error("DPoP proof iat is required"));
    }

    DpopProofClaims claims;
    claims.jwt_id = std::move(*jwt_id);
    claims.method = std::move(*method);
    claims.url = std::move(*url);
    claims.issued_at = detail::time_from_unix_seconds(*issued_at);
    claims.access_token_hash = detail::string_claim(payload, "ath");
    claims.nonce = detail::string_claim(payload, "nonce");
    claims.claims = detail::dpop_claim_metadata(payload);
    return claims;
  }

  OpenSslDpopVerifierOptions options_;
};

}  // namespace mcp::auth::openssl
