// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/param_build.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/auth/jwks.hpp"
#include "cxxmcp/auth/openssl/base64url.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief OpenSSL conversion helpers for public JSON Web Keys.

namespace mcp::auth::openssl {

namespace detail {

struct BignumDeleter {
  void operator()(BIGNUM* value) const noexcept { BN_free(value); }
};

struct EvpPkeyDeleter {
  void operator()(EVP_PKEY* value) const noexcept { EVP_PKEY_free(value); }
};

struct EvpPkeyCtxDeleter {
  void operator()(EVP_PKEY_CTX* value) const noexcept {
    EVP_PKEY_CTX_free(value);
  }
};

struct OssParamBldDeleter {
  void operator()(OSSL_PARAM_BLD* value) const noexcept {
    OSSL_PARAM_BLD_free(value);
  }
};

struct OssParamDeleter {
  void operator()(OSSL_PARAM* value) const noexcept { OSSL_PARAM_free(value); }
};

using BignumPtr = std::unique_ptr<BIGNUM, BignumDeleter>;
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;
using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, EvpPkeyCtxDeleter>;
using OssParamBldPtr = std::unique_ptr<OSSL_PARAM_BLD, OssParamBldDeleter>;
using OssParamPtr = std::unique_ptr<OSSL_PARAM, OssParamDeleter>;

inline core::Result<BignumPtr> jwk_bignum_from_base64url(
    const std::optional<std::string>& value, const char* name) {
  if (!value.has_value() || value->empty()) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidJwk,
                        std::string("JWK ") + name + " is required"));
  }

  auto bytes = base64url_decode(*value);
  if (!bytes.has_value()) {
    return core::unexpected(bytes.error());
  }
  if (bytes->empty()) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, std::string("JWK ") + name + " is empty"));
  }

  BignumPtr bignum(
      BN_bin2bn(bytes->data(), static_cast<int>(bytes->size()), nullptr));
  if (!bignum) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidJwk,
                        std::string("failed to decode JWK ") + name));
  }
  return bignum;
}

inline core::Result<std::vector<unsigned char>> jwk_bytes_from_base64url(
    const std::optional<std::string>& value, const char* name,
    std::size_t expected_size) {
  if (!value.has_value() || value->empty()) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidJwk,
                        std::string("JWK ") + name + " is required"));
  }
  auto bytes = base64url_decode(*value);
  if (!bytes.has_value()) {
    return core::unexpected(bytes.error());
  }
  if (bytes->size() != expected_size) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidJwk,
                        std::string("JWK ") + name + " has an invalid size"));
  }
  return bytes;
}

inline core::Result<BignumPtr> evp_pkey_bignum_param(EVP_PKEY* key,
                                                     const char* name) {
  BIGNUM* raw = nullptr;
  if (key == nullptr || EVP_PKEY_get_bn_param(key, name, &raw) != 1) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidJwk,
                        std::string("missing OpenSSL key parameter ") + name));
  }
  return BignumPtr(raw);
}

inline std::vector<unsigned char> bignum_to_bytes(const BIGNUM* value) {
  std::vector<unsigned char> bytes(
      static_cast<std::size_t>(BN_num_bytes(value)));
  BN_bn2bin(value, bytes.data());
  return bytes;
}

inline core::Result<std::vector<unsigned char>> bignum_to_fixed_bytes(
    const BIGNUM* value, std::size_t size) {
  std::vector<unsigned char> bytes(size);
  if (BN_bn2binpad(value, bytes.data(), static_cast<int>(bytes.size())) !=
      static_cast<int>(bytes.size())) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "failed to serialize fixed-size BIGNUM"));
  }
  return bytes;
}

inline const char* ec_group_name_for_jose_algorithm(std::string_view algorithm,
                                                    std::string_view curve) {
  if (algorithm == "ES256" && curve == "P-256") {
    return SN_X9_62_prime256v1;
  }
  if (algorithm == "ES384" && curve == "P-384") {
    return SN_secp384r1;
  }
  if (algorithm == "ES512" && curve == "P-521") {
    return SN_secp521r1;
  }
  return nullptr;
}

inline std::size_t ec_coordinate_size_for_jose_algorithm(
    std::string_view algorithm) {
  if (algorithm == "ES256") {
    return 32;
  }
  if (algorithm == "ES384") {
    return 48;
  }
  if (algorithm == "ES512") {
    return 66;
  }
  return 0;
}

inline core::Result<EvpPkeyPtr> evp_pkey_from_params(const char* key_type,
                                                     OSSL_PARAM* params) {
  EvpPkeyCtxPtr context(EVP_PKEY_CTX_new_from_name(nullptr, key_type, nullptr));
  if (!context || EVP_PKEY_fromdata_init(context.get()) <= 0) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "failed to initialize OpenSSL key import"));
  }

  EVP_PKEY* raw_key = nullptr;
  if (EVP_PKEY_fromdata(context.get(), &raw_key, EVP_PKEY_PUBLIC_KEY, params) <=
      0) {
    return core::unexpected(make_jose_error(JoseErrorCode::kInvalidJwk,
                                            "failed to import JWK public key"));
  }
  return EvpPkeyPtr(raw_key);
}

}  // namespace detail

using EvpPkeyPtr = detail::EvpPkeyPtr;

struct OpenSslPublicKey {
  EvpPkeyPtr key;
  std::string algorithm;
  std::optional<std::string> key_id;
};

inline core::Result<OpenSslPublicKey> rsa_public_key_from_jwk(
    const JsonWebKey& jwk, std::string algorithm) {
  auto modulus = detail::jwk_bignum_from_base64url(jwk.modulus, "n");
  if (!modulus.has_value()) {
    return core::unexpected(modulus.error());
  }
  auto exponent = detail::jwk_bignum_from_base64url(jwk.exponent, "e");
  if (!exponent.has_value()) {
    return core::unexpected(exponent.error());
  }

  detail::OssParamBldPtr builder(OSSL_PARAM_BLD_new());
  if (!builder ||
      OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_N,
                             modulus->get()) <= 0 ||
      OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_E,
                             exponent->get()) <= 0) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "failed to build RSA JWK parameters"));
  }

  detail::OssParamPtr params(OSSL_PARAM_BLD_to_param(builder.get()));
  if (!params) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "failed to finalize RSA JWK parameters"));
  }

  auto key = detail::evp_pkey_from_params("RSA", params.get());
  if (!key.has_value()) {
    return core::unexpected(key.error());
  }

  OpenSslPublicKey result;
  result.key = std::move(*key);
  result.algorithm = std::move(algorithm);
  result.key_id = jwk.key_id;
  return result;
}

inline core::Result<OpenSslPublicKey> ec_public_key_from_jwk(
    const JsonWebKey& jwk, std::string algorithm) {
  const std::string curve = jwk.curve.value_or("");
  const char* group_name =
      detail::ec_group_name_for_jose_algorithm(algorithm, curve);
  const std::size_t coordinate_size =
      detail::ec_coordinate_size_for_jose_algorithm(algorithm);
  if (group_name == nullptr || coordinate_size == 0) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kUnsupportedJoseAlgorithm,
                        "unsupported EC JWK algorithm or curve"));
  }

  auto x = detail::jwk_bytes_from_base64url(jwk.x, "x", coordinate_size);
  if (!x.has_value()) {
    return core::unexpected(x.error());
  }
  auto y = detail::jwk_bytes_from_base64url(jwk.y, "y", coordinate_size);
  if (!y.has_value()) {
    return core::unexpected(y.error());
  }

  std::vector<unsigned char> public_key;
  public_key.reserve(1 + x->size() + y->size());
  public_key.push_back(0x04);
  public_key.insert(public_key.end(), x->begin(), x->end());
  public_key.insert(public_key.end(), y->begin(), y->end());

  detail::OssParamBldPtr builder(OSSL_PARAM_BLD_new());
  if (!builder ||
      OSSL_PARAM_BLD_push_utf8_string(builder.get(), OSSL_PKEY_PARAM_GROUP_NAME,
                                      const_cast<char*>(group_name), 0) <= 0 ||
      OSSL_PARAM_BLD_push_octet_string(builder.get(), OSSL_PKEY_PARAM_PUB_KEY,
                                       public_key.data(),
                                       public_key.size()) <= 0) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "failed to build EC JWK parameters"));
  }

  detail::OssParamPtr params(OSSL_PARAM_BLD_to_param(builder.get()));
  if (!params) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "failed to finalize EC JWK parameters"));
  }

  auto key = detail::evp_pkey_from_params("EC", params.get());
  if (!key.has_value()) {
    return core::unexpected(key.error());
  }

  OpenSslPublicKey result;
  result.key = std::move(*key);
  result.algorithm = std::move(algorithm);
  result.key_id = jwk.key_id;
  return result;
}

inline core::Result<OpenSslPublicKey> public_key_from_jwk(
    const JsonWebKey& jwk, std::optional<std::string> required_algorithm = {}) {
  std::string algorithm = required_algorithm.has_value()
                              ? *required_algorithm
                              : jwk.algorithm.value_or("");
  if (algorithm.empty()) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidJwk, "JWK alg is required"));
  }
  if (jwk.algorithm.has_value() && *jwk.algorithm != algorithm) {
    return core::unexpected(make_jose_error(
        JoseErrorCode::kInvalidJwk, "JWK alg does not match required alg"));
  }

  if (jwk.key_type == "RSA" && algorithm == "RS256") {
    return rsa_public_key_from_jwk(jwk, std::move(algorithm));
  }
  if (jwk.key_type == "EC" &&
      (algorithm == "ES256" || algorithm == "ES384" || algorithm == "ES512")) {
    return ec_public_key_from_jwk(jwk, std::move(algorithm));
  }

  return core::unexpected(
      make_jose_error(JoseErrorCode::kUnsupportedJoseAlgorithm,
                      "unsupported JWK key type or JOSE algorithm"));
}

inline core::Result<JsonWebKey> public_jwk_from_evp_pkey(
    EVP_PKEY* key, std::string algorithm,
    std::optional<std::string> key_id = {}) {
  if (key == nullptr) {
    return core::unexpected(make_jose_error(JoseErrorCode::kInvalidJwk,
                                            "OpenSSL public key is required"));
  }

  JsonWebKey jwk;
  jwk.public_key_use = "sig";
  jwk.algorithm = algorithm;
  jwk.key_id = std::move(key_id);

  if (algorithm == "RS256") {
    auto modulus = detail::evp_pkey_bignum_param(key, OSSL_PKEY_PARAM_RSA_N);
    if (!modulus.has_value()) {
      return core::unexpected(modulus.error());
    }
    auto exponent = detail::evp_pkey_bignum_param(key, OSSL_PKEY_PARAM_RSA_E);
    if (!exponent.has_value()) {
      return core::unexpected(exponent.error());
    }

    jwk.key_type = "RSA";
    jwk.modulus = base64url_encode(detail::bignum_to_bytes(modulus->get()));
    jwk.exponent = base64url_encode(detail::bignum_to_bytes(exponent->get()));
    return jwk;
  }

  const std::size_t coordinate_size =
      detail::ec_coordinate_size_for_jose_algorithm(algorithm);
  if (coordinate_size == 0) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kUnsupportedJoseAlgorithm,
                        "unsupported JOSE algorithm for public JWK export"));
  }
  auto x = detail::evp_pkey_bignum_param(key, OSSL_PKEY_PARAM_EC_PUB_X);
  if (!x.has_value()) {
    return core::unexpected(x.error());
  }
  auto y = detail::evp_pkey_bignum_param(key, OSSL_PKEY_PARAM_EC_PUB_Y);
  if (!y.has_value()) {
    return core::unexpected(y.error());
  }
  auto x_bytes = detail::bignum_to_fixed_bytes(x->get(), coordinate_size);
  if (!x_bytes.has_value()) {
    return core::unexpected(x_bytes.error());
  }
  auto y_bytes = detail::bignum_to_fixed_bytes(y->get(), coordinate_size);
  if (!y_bytes.has_value()) {
    return core::unexpected(y_bytes.error());
  }

  jwk.key_type = "EC";
  if (algorithm == "ES256") {
    jwk.curve = "P-256";
  } else if (algorithm == "ES384") {
    jwk.curve = "P-384";
  } else if (algorithm == "ES512") {
    jwk.curve = "P-521";
  }
  jwk.x = base64url_encode(*x_bytes);
  jwk.y = base64url_encode(*y_bytes);
  return jwk;
}

}  // namespace mcp::auth::openssl
