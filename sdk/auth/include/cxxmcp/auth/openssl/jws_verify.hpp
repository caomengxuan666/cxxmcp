// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/auth/openssl/jwk.hpp"
#include "cxxmcp/auth/openssl/jws.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief OpenSSL-backed compact JWS signature verification helpers.

namespace mcp::auth::openssl {

namespace detail {

struct EcdsaSigDeleter {
  void operator()(ECDSA_SIG* value) const noexcept { ECDSA_SIG_free(value); }
};

struct EvpMdCtxDeleter {
  void operator()(EVP_MD_CTX* value) const noexcept { EVP_MD_CTX_free(value); }
};

using EcdsaSigPtr = std::unique_ptr<ECDSA_SIG, EcdsaSigDeleter>;
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

inline const EVP_MD* digest_for_jose_algorithm(std::string_view algorithm) {
  if (algorithm == "RS256" || algorithm == "ES256") {
    return EVP_sha256();
  }
  if (algorithm == "ES384") {
    return EVP_sha384();
  }
  if (algorithm == "ES512") {
    return EVP_sha512();
  }
  return nullptr;
}

inline core::Result<std::vector<unsigned char>> ecdsa_raw_signature_to_der(
    const std::vector<unsigned char>& raw_signature,
    std::string_view algorithm) {
  const std::size_t coordinate_size =
      ec_coordinate_size_for_jose_algorithm(algorithm);
  if (coordinate_size == 0 || raw_signature.size() != coordinate_size * 2) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "JWS ECDSA signature has an invalid size"));
  }

  BignumPtr r(BN_bin2bn(raw_signature.data(), static_cast<int>(coordinate_size),
                        nullptr));
  BignumPtr s(BN_bin2bn(raw_signature.data() + coordinate_size,
                        static_cast<int>(coordinate_size), nullptr));
  if (!r || !s) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to decode JWS ECDSA signature"));
  }

  EcdsaSigPtr signature(ECDSA_SIG_new());
  if (!signature ||
      ECDSA_SIG_set0(signature.get(), r.release(), s.release()) != 1) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to build JWS ECDSA signature"));
  }

  const int der_size = i2d_ECDSA_SIG(signature.get(), nullptr);
  if (der_size <= 0) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to size JWS ECDSA signature"));
  }

  std::vector<unsigned char> der(static_cast<std::size_t>(der_size));
  unsigned char* cursor = der.data();
  if (i2d_ECDSA_SIG(signature.get(), &cursor) != der_size) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to encode JWS ECDSA signature"));
  }
  return der;
}

inline core::Result<std::vector<unsigned char>> ecdsa_der_signature_to_raw(
    const std::vector<unsigned char>& der_signature,
    std::string_view algorithm) {
  const std::size_t coordinate_size =
      ec_coordinate_size_for_jose_algorithm(algorithm);
  if (coordinate_size == 0) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kUnsupportedJoseAlgorithm,
                        "unsupported JWS ECDSA signature algorithm"));
  }

  const unsigned char* cursor = der_signature.data();
  EcdsaSigPtr signature(d2i_ECDSA_SIG(
      nullptr, &cursor,
      static_cast<long>(der_signature.size())));  // NOLINT(runtime/int)
  if (!signature) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to decode JWS ECDSA DER signature"));
  }

  const BIGNUM* r = nullptr;
  const BIGNUM* s = nullptr;
  ECDSA_SIG_get0(signature.get(), &r, &s);
  if (r == nullptr || s == nullptr) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "JWS ECDSA DER signature is missing r/s"));
  }

  std::vector<unsigned char> raw;
  auto r_bytes = bignum_to_fixed_bytes(r, coordinate_size);
  if (!r_bytes.has_value()) {
    return core::unexpected(r_bytes.error());
  }
  auto s_bytes = bignum_to_fixed_bytes(s, coordinate_size);
  if (!s_bytes.has_value()) {
    return core::unexpected(s_bytes.error());
  }
  raw.reserve(r_bytes->size() + s_bytes->size());
  raw.insert(raw.end(), r_bytes->begin(), r_bytes->end());
  raw.insert(raw.end(), s_bytes->begin(), s_bytes->end());
  return raw;
}

inline bool is_ecdsa_jose_algorithm(std::string_view algorithm) {
  return algorithm == "ES256" || algorithm == "ES384" || algorithm == "ES512";
}

}  // namespace detail

struct JwsVerificationOptions {
  std::optional<std::string> required_algorithm;
  std::optional<std::string> required_key_id;
};

inline core::Result<core::Unit> verify_compact_jws_signature(
    std::string_view compact_jws, const JsonWebKey& jwk,
    const JwsVerificationOptions& options = {}) {
  auto decoded = decode_compact_jws(compact_jws);
  if (!decoded.has_value()) {
    return core::unexpected(decoded.error());
  }

  const auto& header = decoded->protected_header;
  if (options.required_algorithm.has_value() &&
      header.algorithm != *options.required_algorithm) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "JWS alg does not match required algorithm"));
  }
  if (jwk.algorithm.has_value() && header.algorithm != *jwk.algorithm) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "JWS alg does not match JWK alg"));
  }
  if (options.required_key_id.has_value() &&
      (!header.key_id.has_value() ||
       *header.key_id != *options.required_key_id)) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "JWS kid does not match required key id"));
  }
  if (jwk.key_id.has_value() &&
      (!header.key_id.has_value() || *header.key_id != *jwk.key_id)) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "JWS kid does not match JWK kid"));
  }

  auto public_key = public_key_from_jwk(jwk, header.algorithm);
  if (!public_key.has_value()) {
    return core::unexpected(public_key.error());
  }

  const EVP_MD* digest = detail::digest_for_jose_algorithm(header.algorithm);
  if (digest == nullptr) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kUnsupportedJoseAlgorithm,
                        "unsupported JWS signature algorithm"));
  }

  std::vector<unsigned char> signature = decoded->signature;
  if (detail::is_ecdsa_jose_algorithm(header.algorithm)) {
    auto der = detail::ecdsa_raw_signature_to_der(signature, header.algorithm);
    if (!der.has_value()) {
      return core::unexpected(der.error());
    }
    signature = std::move(*der);
  }

  detail::EvpMdCtxPtr context(EVP_MD_CTX_new());
  const std::string signing_input = decoded->parts.signing_input();
  if (!context ||
      EVP_DigestVerifyInit(context.get(), nullptr, digest, nullptr,
                           public_key->key.get()) <= 0 ||
      EVP_DigestVerifyUpdate(context.get(), signing_input.data(),
                             signing_input.size()) <= 0) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "failed to initialize JWS signature verification"));
  }

  const int verified =
      EVP_DigestVerifyFinal(context.get(), signature.data(), signature.size());
  if (verified == 1) {
    return core::Unit{};
  }
  if (verified == 0) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                        "JWS signature verification failed"));
  }
  return core::unexpected(
      make_jose_error(JoseErrorCode::kSignatureVerificationFailed,
                      "OpenSSL failed while verifying JWS signature"));
}

}  // namespace mcp::auth::openssl
