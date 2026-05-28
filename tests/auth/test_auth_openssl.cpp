// Copyright (c) 2025 [caomengxuan666]

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/auth/openssl/dpop.hpp"
#include "cxxmcp/auth/openssl/jwt.hpp"
#include "cxxmcp/auth/openssl/server_auth_provider.hpp"
#include "cxxmcp/auth/openssl/sha256.hpp"

namespace {

struct BignumDeleter {
  void operator()(BIGNUM* value) const noexcept { BN_free(value); }
};

struct EcdsaSigDeleter {
  void operator()(ECDSA_SIG* value) const noexcept { ECDSA_SIG_free(value); }
};

struct EvpMdCtxDeleter {
  void operator()(EVP_MD_CTX* value) const noexcept { EVP_MD_CTX_free(value); }
};

struct EvpPkeyDeleter {
  void operator()(EVP_PKEY* value) const noexcept { EVP_PKEY_free(value); }
};

struct EvpPkeyCtxDeleter {
  void operator()(EVP_PKEY_CTX* value) const noexcept {
    EVP_PKEY_CTX_free(value);
  }
};

using BignumPtr = std::unique_ptr<BIGNUM, BignumDeleter>;
using EcdsaSigPtr = std::unique_ptr<ECDSA_SIG, EcdsaSigDeleter>;
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;
using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, EvpPkeyCtxDeleter>;

class RecordingJwksEndpoint final : public mcp::auth::JwksEndpoint {
 public:
  mcp::core::Result<mcp::auth::JsonWebKeySet> fetch_jwks(
      const mcp::auth::JwksFetchRequest& request) override {
    ++fetch_count;
    last_request = request;
    return keys;
  }

  mcp::auth::JsonWebKeySet keys;
  int fetch_count = 0;
  std::optional<mcp::auth::JwksFetchRequest> last_request;
};

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

BignumPtr get_bn_param(EVP_PKEY* key, const char* name) {
  BIGNUM* raw = nullptr;
  require(EVP_PKEY_get_bn_param(key, name, &raw) == 1,
          std::string("missing OpenSSL key parameter: ") + name);
  return BignumPtr(raw);
}

std::vector<unsigned char> bignum_to_bytes(const BIGNUM* value) {
  std::vector<unsigned char> bytes(
      static_cast<std::size_t>(BN_num_bytes(value)));
  require(BN_bn2bin(value, bytes.data()) == static_cast<int>(bytes.size()),
          "failed to serialize BIGNUM");
  return bytes;
}

std::vector<unsigned char> bignum_to_fixed_bytes(const BIGNUM* value,
                                                 std::size_t size) {
  std::vector<unsigned char> bytes(size);
  require(BN_bn2binpad(value, bytes.data(), static_cast<int>(bytes.size())) ==
              static_cast<int>(bytes.size()),
          "failed to serialize fixed-size BIGNUM");
  return bytes;
}

std::vector<unsigned char> sign_message(EVP_PKEY* key, const EVP_MD* digest,
                                        std::string_view message) {
  EvpMdCtxPtr context(EVP_MD_CTX_new());
  require(context != nullptr, "failed to create OpenSSL signing context");
  require(EVP_DigestSignInit(context.get(), nullptr, digest, nullptr, key) == 1,
          "failed to initialize OpenSSL signing");
  require(
      EVP_DigestSignUpdate(context.get(), message.data(), message.size()) == 1,
      "failed to update OpenSSL signing");

  std::size_t signature_size = 0;
  require(EVP_DigestSignFinal(context.get(), nullptr, &signature_size) == 1,
          "failed to size OpenSSL signature");
  std::vector<unsigned char> signature(signature_size);
  require(EVP_DigestSignFinal(context.get(), signature.data(),
                              &signature_size) == 1,
          "failed to finalize OpenSSL signature");
  signature.resize(signature_size);
  return signature;
}

std::vector<unsigned char> ecdsa_der_to_raw(
    const std::vector<unsigned char>& der, std::size_t coordinate_size) {
  const unsigned char* cursor = der.data();
  EcdsaSigPtr signature(
      d2i_ECDSA_SIG(nullptr, &cursor,
                    static_cast<long>(der.size())));  // NOLINT(runtime/int)
  require(signature != nullptr, "failed to parse ECDSA DER signature");

  const BIGNUM* r = nullptr;
  const BIGNUM* s = nullptr;
  ECDSA_SIG_get0(signature.get(), &r, &s);
  require(r != nullptr && s != nullptr, "ECDSA signature is missing r/s");

  std::vector<unsigned char> raw;
  auto r_bytes = bignum_to_fixed_bytes(r, coordinate_size);
  auto s_bytes = bignum_to_fixed_bytes(s, coordinate_size);
  raw.reserve(r_bytes.size() + s_bytes.size());
  raw.insert(raw.end(), r_bytes.begin(), r_bytes.end());
  raw.insert(raw.end(), s_bytes.begin(), s_bytes.end());
  return raw;
}

EvpPkeyPtr generate_ec_p256_key() {
  EvpPkeyCtxPtr context(EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr));
  require(context != nullptr, "failed to create EC keygen context");
  require(EVP_PKEY_keygen_init(context.get()) == 1,
          "failed to initialize EC keygen");
  require(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(),
                                                 NID_X9_62_prime256v1) == 1,
          "failed to set EC keygen curve");

  EVP_PKEY* raw = nullptr;
  require(EVP_PKEY_keygen(context.get(), &raw) == 1,
          "failed to generate EC key");
  return EvpPkeyPtr(raw);
}

EvpPkeyPtr generate_rsa_key() {
  EvpPkeyCtxPtr context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
  require(context != nullptr, "failed to create RSA keygen context");
  require(EVP_PKEY_keygen_init(context.get()) == 1,
          "failed to initialize RSA keygen");
  require(EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) == 1,
          "failed to set RSA key size");

  EVP_PKEY* raw = nullptr;
  require(EVP_PKEY_keygen(context.get(), &raw) == 1,
          "failed to generate RSA key");
  return EvpPkeyPtr(raw);
}

mcp::auth::JsonWebKey ec_public_jwk_from_key(EVP_PKEY* key) {
  auto x = get_bn_param(key, OSSL_PKEY_PARAM_EC_PUB_X);
  auto y = get_bn_param(key, OSSL_PKEY_PARAM_EC_PUB_Y);

  mcp::auth::JsonWebKey jwk;
  jwk.key_type = "EC";
  jwk.algorithm = "ES256";
  jwk.key_id = "ec-key";
  jwk.curve = "P-256";
  jwk.x =
      mcp::auth::openssl::base64url_encode(bignum_to_fixed_bytes(x.get(), 32));
  jwk.y =
      mcp::auth::openssl::base64url_encode(bignum_to_fixed_bytes(y.get(), 32));
  return jwk;
}

mcp::auth::JsonWebKey rsa_public_jwk_from_key(EVP_PKEY* key) {
  auto n = get_bn_param(key, OSSL_PKEY_PARAM_RSA_N);
  auto e = get_bn_param(key, OSSL_PKEY_PARAM_RSA_E);

  mcp::auth::JsonWebKey jwk;
  jwk.key_type = "RSA";
  jwk.public_key_use = "sig";
  jwk.algorithm = "RS256";
  jwk.key_id = "rsa-key";
  jwk.modulus = mcp::auth::openssl::base64url_encode(bignum_to_bytes(n.get()));
  jwk.exponent = mcp::auth::openssl::base64url_encode(bignum_to_bytes(e.get()));
  return jwk;
}

std::string compact_jws_from_signature(
    std::string protected_header_json, std::string payload_json,
    const std::vector<unsigned char>& signature) {
  const std::string protected_header =
      mcp::auth::openssl::base64url_encode(protected_header_json);
  const std::string payload =
      mcp::auth::openssl::base64url_encode(payload_json);
  return protected_header + "." + payload + "." +
         mcp::auth::openssl::base64url_encode(signature);
}

std::string signed_rs256_jwt(EVP_PKEY* key, const std::string& header,
                             const std::string& payload) {
  const std::string signing_input =
      mcp::auth::openssl::base64url_encode(header) + "." +
      mcp::auth::openssl::base64url_encode(payload);
  const auto signature = sign_message(key, EVP_sha256(), signing_input);
  return compact_jws_from_signature(header, payload, signature);
}

std::string private_key_pem(EVP_PKEY* key) {
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
  require(bio != nullptr, "failed to allocate private-key BIO");
  require(PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr,
                                   nullptr) == 1,
          "failed to write private key PEM");

  BUF_MEM* buffer = nullptr;
  BIO_get_mem_ptr(bio.get(), &buffer);
  require(buffer != nullptr, "failed to read private key PEM");
  return std::string(buffer->data, buffer->length);
}

void test_dpop_access_token_hash_matches_sha256_base64url() {
  const auto hash = mcp::auth::openssl::dpop_access_token_hash("access-token");
  require(hash.has_value(), "access-token hash should compute");
  require(*hash == "Pxa-1wifRlPl7yG_0oJNfzqq7MelmOfonFgOFgapzFI",
          "access-token hash mismatch");

  const auto empty = mcp::auth::openssl::sha256_base64url("");
  require(empty.has_value(), "empty hash should compute");
  require(*empty == "47DEQpj8HBSa-_TImW-5JCeuQeRkm5NMpJWZG3hSuFU",
          "empty SHA-256 base64url mismatch");
}

void test_base64url_round_trips_without_padding() {
  const auto encoded = mcp::auth::openssl::base64url_encode("hello?");
  require(encoded == "aGVsbG8_", "base64url encoding mismatch");

  const auto decoded = mcp::auth::openssl::base64url_decode_to_string(encoded);
  require(decoded.has_value(), "base64url value should decode");
  require(*decoded == "hello?", "base64url decoded value mismatch");

  const auto empty = mcp::auth::openssl::base64url_decode("");
  require(empty.has_value(), "empty base64url value should decode");
  require(empty->empty(), "empty base64url value should decode to no bytes");
}

void test_base64url_rejects_non_jose_inputs() {
  const auto padded = mcp::auth::openssl::base64url_decode("aGVsbG8=");
  require(!padded.has_value(), "base64url padding should be rejected");
  require(padded.error().code ==
              static_cast<int>(
                  mcp::auth::openssl::JoseErrorCode::kInvalidBase64Url),
          "padding error code mismatch");

  const auto invalid_char = mcp::auth::openssl::base64url_decode("abc+");
  require(!invalid_char.has_value(),
          "base64url invalid character should be rejected");

  const auto invalid_length = mcp::auth::openssl::base64url_decode("abcde");
  require(!invalid_length.has_value(),
          "base64url invalid length should be rejected");
}

void test_compact_jws_parts_and_header_parse() {
  const std::string protected_header = mcp::auth::openssl::base64url_encode(
      R"({"alg":"ES256","kid":"key-1","typ":"dpop+jwt","custom":true})");
  const std::string payload =
      mcp::auth::openssl::base64url_encode(R"({"sub":"alice"})");
  const std::string signature =
      mcp::auth::openssl::base64url_encode("signature-bytes");
  const std::string compact =
      protected_header + "." + payload + "." + signature;

  const auto parts = mcp::auth::openssl::parse_compact_jws_parts(compact);
  require(parts.has_value(), "compact JWS parts should parse");
  require(parts->protected_header == protected_header,
          "protected header segment mismatch");
  require(parts->payload == payload, "payload segment mismatch");
  require(parts->signature == signature, "signature segment mismatch");
  require(parts->signing_input() == protected_header + "." + payload,
          "JWS signing input mismatch");

  const auto header =
      mcp::auth::openssl::parse_jose_protected_header(parts->protected_header);
  require(header.has_value(), "JWS protected header should parse");
  require(header->algorithm == "ES256", "JWS alg mismatch");
  require(header->key_id.has_value() && *header->key_id == "key-1",
          "JWS kid mismatch");
  require(header->type.has_value() && *header->type == "dpop+jwt",
          "JWS typ mismatch");
  require(header->metadata.at("custom") == "true",
          "JWS extension metadata mismatch");

  const auto decoded = mcp::auth::openssl::decode_compact_jws(compact);
  require(decoded.has_value(), "compact JWS should decode");
  require(decoded->protected_header.algorithm == "ES256",
          "decoded JWS alg mismatch");
  require(std::string(decoded->payload.begin(), decoded->payload.end()) ==
              R"({"sub":"alice"})",
          "decoded JWS payload mismatch");
  require(std::string(decoded->signature.begin(), decoded->signature.end()) ==
              "signature-bytes",
          "decoded JWS signature mismatch");
}

void test_compact_jws_rejects_malformed_structure_and_header() {
  const auto missing_segment =
      mcp::auth::openssl::parse_compact_jws_parts("header.payload");
  require(!missing_segment.has_value(),
          "compact JWS missing segment should be rejected");
  require(missing_segment.error().code ==
              static_cast<int>(
                  mcp::auth::openssl::JoseErrorCode::kInvalidCompactJws),
          "compact JWS structure error code mismatch");

  const auto empty_signature =
      mcp::auth::openssl::parse_compact_jws_parts("header.payload.");
  require(!empty_signature.has_value(),
          "compact JWS empty signature should be rejected");

  const auto missing_alg = mcp::auth::openssl::parse_jose_protected_header(
      mcp::auth::openssl::base64url_encode(R"({"kid":"key-1"})"));
  require(!missing_alg.has_value(),
          "JWS protected header without alg should be rejected");
  require(missing_alg.error().code ==
              static_cast<int>(
                  mcp::auth::openssl::JoseErrorCode::kInvalidJoseHeader),
          "JWS protected header error code mismatch");
}

void test_es256_compact_jws_signature_verifies_against_public_jwk() {
  auto key = generate_ec_p256_key();
  const auto jwk = ec_public_jwk_from_key(key.get());
  const std::string header = R"({"alg":"ES256","kid":"ec-key"})";
  const std::string payload = R"({"sub":"alice"})";
  const std::string signing_input =
      mcp::auth::openssl::base64url_encode(header) + "." +
      mcp::auth::openssl::base64url_encode(payload);
  const auto der_signature =
      sign_message(key.get(), EVP_sha256(), signing_input);
  const auto raw_signature = ecdsa_der_to_raw(der_signature, 32);
  const auto compact =
      compact_jws_from_signature(header, payload, raw_signature);

  const auto verified =
      mcp::auth::openssl::verify_compact_jws_signature(compact, jwk);
  require(verified.has_value(), "ES256 compact JWS should verify");

  const auto rejected =
      mcp::auth::openssl::verify_compact_jws_signature(compact + "A", jwk);
  require(!rejected.has_value(), "tampered ES256 compact JWS should fail");
}

void test_rs256_compact_jws_signature_verifies_against_public_jwk() {
  auto key = generate_rsa_key();
  const auto jwk = rsa_public_jwk_from_key(key.get());
  const std::string header = R"({"alg":"RS256","kid":"rsa-key"})";
  const std::string payload = R"({"scope":"tools.read"})";
  const std::string signing_input =
      mcp::auth::openssl::base64url_encode(header) + "." +
      mcp::auth::openssl::base64url_encode(payload);
  const auto signature = sign_message(key.get(), EVP_sha256(), signing_input);
  const auto compact = compact_jws_from_signature(header, payload, signature);

  const auto verified =
      mcp::auth::openssl::verify_compact_jws_signature(compact, jwk);
  require(verified.has_value(), "RS256 compact JWS should verify");

  auto mismatched_jwk = jwk;
  mismatched_jwk.key_id = "other-key";
  const auto rejected =
      mcp::auth::openssl::verify_compact_jws_signature(compact, mismatched_jwk);
  require(!rejected.has_value(), "mismatched JWK kid should fail verification");
}

void test_rs256_jwt_verifies_signature_jwks_and_claims() {
  auto key = generate_rsa_key();
  const auto jwk = rsa_public_jwk_from_key(key.get());
  mcp::auth::JsonWebKeySet jwks;
  jwks.keys.push_back(jwk);

  const mcp::auth::TimePoint now(std::chrono::seconds(1700000000));
  const std::string header = R"({"alg":"RS256","kid":"rsa-key"})";
  const std::string payload =
      R"({"iss":"https://issuer.example","sub":"alice","aud":["tools","other"],)"
      R"("exp":1700000300,"nbf":1699999900,"iat":1699999990,)"
      R"("scope":"tools.read","admin":true})";
  const std::string signing_input =
      mcp::auth::openssl::base64url_encode(header) + "." +
      mcp::auth::openssl::base64url_encode(payload);
  const auto signature = sign_message(key.get(), EVP_sha256(), signing_input);
  const auto jwt = compact_jws_from_signature(header, payload, signature);

  mcp::auth::JwtVerificationRequest request;
  request.jwt = jwt;
  request.issuer = "https://issuer.example";
  request.audience = "tools";
  request.required_algorithm = "RS256";
  request.required_claims.emplace("scope", "tools.read");
  request.required_claims.emplace("admin", "true");
  request.now = now;

  mcp::auth::openssl::StaticJwksJwtVerifier verifier(jwks);
  const auto claims = verifier.verify(request);
  require(claims.has_value(), "RS256 JWT should verify");
  require(claims->issuer == "https://issuer.example", "JWT issuer mismatch");
  require(claims->subject == "alice", "JWT subject mismatch");
  require(claims->audience == "tools", "JWT audience mismatch");
  require(claims->issued_at.has_value(), "JWT iat should be returned");
  require(claims->expires_at.has_value(), "JWT exp should be returned");
  require(claims->claims.at("scope") == "tools.read",
          "JWT metadata claim mismatch");
}

void test_rs256_jwt_rejects_bad_claims_and_signature() {
  auto key = generate_rsa_key();
  const auto jwk = rsa_public_jwk_from_key(key.get());
  mcp::auth::JsonWebKeySet jwks;
  jwks.keys.push_back(jwk);

  const mcp::auth::TimePoint now(std::chrono::seconds(1700000000));
  const std::string header = R"({"alg":"RS256","kid":"rsa-key"})";
  const std::string payload =
      R"({"iss":"https://issuer.example","sub":"alice","aud":"tools",)"
      R"("exp":1700000300,"iat":1699999990})";
  const std::string signing_input =
      mcp::auth::openssl::base64url_encode(header) + "." +
      mcp::auth::openssl::base64url_encode(payload);
  const auto signature = sign_message(key.get(), EVP_sha256(), signing_input);
  const auto jwt = compact_jws_from_signature(header, payload, signature);

  mcp::auth::JwtVerificationRequest request;
  request.jwt = jwt;
  request.issuer = "https://issuer.example";
  request.audience = "wrong-audience";
  request.required_algorithm = "RS256";
  request.now = now;
  const auto bad_audience =
      mcp::auth::openssl::verify_jwt_with_jwks(request, jwks);
  require(!bad_audience.has_value(), "JWT wrong audience should fail");

  request.audience = "tools";
  request.jwt = jwt + "A";
  const auto bad_signature =
      mcp::auth::openssl::verify_jwt_with_jwks(request, jwks);
  require(!bad_signature.has_value(), "JWT bad signature should fail");

  request.jwt = jwt;
  request.now = mcp::auth::TimePoint(std::chrono::seconds(1700000400));
  const auto expired = mcp::auth::openssl::verify_jwt_with_jwks(request, jwks);
  require(!expired.has_value(), "JWT expired token should fail");
}

void test_fetching_jwks_verifier_fetches_and_caches_on_miss() {
  auto key = generate_rsa_key();
  RecordingJwksEndpoint endpoint;
  endpoint.keys.keys.push_back(rsa_public_jwk_from_key(key.get()));
  mcp::auth::InMemoryJwksCache cache;

  mcp::auth::openssl::FetchingJwksJwtVerifierOptions options;
  options.jwks_uri = "https://issuer.example/jwks.json";
  options.headers.emplace("Accept", "application/jwk-set+json");
  mcp::auth::openssl::FetchingJwksJwtVerifier verifier(endpoint, &cache,
                                                       options);

  const auto jwt = signed_rs256_jwt(
      key.get(), R"({"alg":"RS256","kid":"rsa-key"})",
      R"({"iss":"https://issuer.example","aud":"tools","exp":1700000300})");
  mcp::auth::JwtVerificationRequest request;
  request.jwt = jwt;
  request.issuer = "https://issuer.example";
  request.audience = "tools";
  request.required_algorithm = "RS256";
  request.now = mcp::auth::TimePoint(std::chrono::seconds(1700000000));

  auto claims = verifier.verify(request);
  require(claims.has_value(), "fetching JWKS verifier should verify on miss");
  require(endpoint.fetch_count == 1, "JWKS endpoint should be fetched once");
  require(
      endpoint.last_request.has_value() &&
          endpoint.last_request->jwks_uri == "https://issuer.example/jwks.json",
      "JWKS fetch URI mismatch");
  require(
      endpoint.last_request->headers.at("Accept") == "application/jwk-set+json",
      "JWKS fetch headers mismatch");

  claims = verifier.verify(request);
  require(claims.has_value(), "cached JWKS verifier should verify");
  require(endpoint.fetch_count == 1, "cached JWKS should avoid refetch");
}

void test_fetching_jwks_verifier_refreshes_on_key_rotation() {
  auto old_key = generate_rsa_key();
  auto new_key = generate_rsa_key();

  mcp::auth::JsonWebKeySet cached_keys;
  cached_keys.keys.push_back(rsa_public_jwk_from_key(old_key.get()));
  mcp::auth::InMemoryJwksCache cache;
  require(
      cache.save("https://issuer.example/jwks.json", cached_keys).has_value(),
      "preloading JWKS cache should succeed");

  RecordingJwksEndpoint endpoint;
  endpoint.keys.keys.push_back(rsa_public_jwk_from_key(new_key.get()));

  mcp::auth::openssl::FetchingJwksJwtVerifierOptions options;
  options.jwks_uri = "https://issuer.example/jwks.json";
  mcp::auth::openssl::FetchingJwksJwtVerifier verifier(endpoint, &cache,
                                                       options);

  const auto jwt = signed_rs256_jwt(
      new_key.get(), R"({"alg":"RS256","kid":"rsa-key"})",
      R"({"iss":"https://issuer.example","aud":"tools","exp":1700000300})");
  mcp::auth::JwtVerificationRequest request;
  request.jwt = jwt;
  request.issuer = "https://issuer.example";
  request.audience = "tools";
  request.required_algorithm = "RS256";
  request.now = mcp::auth::TimePoint(std::chrono::seconds(1700000000));

  const auto claims = verifier.verify(request);
  require(claims.has_value(), "JWKS verifier should refresh rotated key");
  require(endpoint.fetch_count == 1,
          "JWKS endpoint should be fetched for rotated key");
}

void test_fetching_jwks_verifier_does_not_refresh_claim_failures() {
  auto key = generate_rsa_key();
  mcp::auth::JsonWebKeySet cached_keys;
  cached_keys.keys.push_back(rsa_public_jwk_from_key(key.get()));
  mcp::auth::InMemoryJwksCache cache;
  require(
      cache.save("https://issuer.example/jwks.json", cached_keys).has_value(),
      "preloading JWKS cache should succeed for claim failure");

  RecordingJwksEndpoint endpoint;
  endpoint.keys = cached_keys;

  mcp::auth::openssl::FetchingJwksJwtVerifierOptions options;
  options.jwks_uri = "https://issuer.example/jwks.json";
  mcp::auth::openssl::FetchingJwksJwtVerifier verifier(endpoint, &cache,
                                                       options);

  const auto jwt = signed_rs256_jwt(
      key.get(), R"({"alg":"RS256","kid":"rsa-key"})",
      R"({"iss":"https://issuer.example","aud":"tools","exp":1700000300})");
  mcp::auth::JwtVerificationRequest request;
  request.jwt = jwt;
  request.issuer = "https://issuer.example";
  request.audience = "wrong-audience";
  request.required_algorithm = "RS256";
  request.now = mcp::auth::TimePoint(std::chrono::seconds(1700000000));

  const auto claims = verifier.verify(request);
  require(!claims.has_value(), "JWT claim failure should be preserved");
  require(endpoint.fetch_count == 0,
          "JWKS endpoint should not refresh on claim failure");
}

void test_es256_dpop_signer_and_verifier_round_trip_claims() {
  auto key = generate_ec_p256_key();
  mcp::auth::DpopProofRequest request;
  request.target = {"POST", "https://resource.example/mcp"};
  request.key.algorithm = "ES256";
  request.key.key_id = "dpop-key";
  request.key.private_key_pem = private_key_pem(key.get());
  request.access_token = "access-token";
  request.nonce = "server-nonce";

  mcp::auth::openssl::OpenSslDpopSignerOptions signer_options;
  signer_options.now = [] {
    return mcp::auth::TimePoint(std::chrono::seconds(1700000000));
  };
  signer_options.jwt_id_generator = [] {
    return mcp::core::Result<std::string>{"fixed-jti"};
  };
  mcp::auth::openssl::OpenSslDpopSigner signer(signer_options);

  auto proof = signer.sign(request);
  require(proof.has_value(), "DPoP proof should sign");

  mcp::auth::openssl::OpenSslDpopVerifierOptions verifier_options;
  verifier_options.claim_validation.now =
      mcp::auth::TimePoint(std::chrono::seconds(1700000000));
  mcp::auth::openssl::OpenSslDpopVerifier verifier(verifier_options);
  auto claims = verifier.verify(*proof, request.target, request.access_token);
  require(claims.has_value(), "DPoP proof should verify");
  require(claims->jwt_id == "fixed-jti", "DPoP jti mismatch");
  require(claims->method == "POST", "DPoP htm mismatch");
  require(claims->url == "https://resource.example/mcp", "DPoP htu mismatch");
  require(claims->nonce.has_value() && *claims->nonce == "server-nonce",
          "DPoP nonce mismatch");

  auto expected_ath =
      mcp::auth::openssl::dpop_access_token_hash("access-token");
  require(expected_ath.has_value(), "expected ath should compute");
  require(claims->access_token_hash.has_value() &&
              *claims->access_token_hash == *expected_ath,
          "DPoP ath mismatch");
}

void test_es256_dpop_verifier_rejects_target_and_replay() {
  auto key = generate_ec_p256_key();
  mcp::auth::DpopProofRequest request;
  request.target = {"GET", "https://resource.example/mcp"};
  request.key.algorithm = "ES256";
  request.key.key_id = "dpop-key";
  request.key.private_key_pem = private_key_pem(key.get());
  request.access_token = "access-token";

  mcp::auth::openssl::OpenSslDpopSignerOptions signer_options;
  signer_options.now = [] {
    return mcp::auth::TimePoint(std::chrono::seconds(1700000000));
  };
  signer_options.jwt_id_generator = [] {
    return mcp::core::Result<std::string>{"replay-jti"};
  };
  mcp::auth::openssl::OpenSslDpopSigner signer(signer_options);
  auto proof = signer.sign(request);
  require(proof.has_value(), "DPoP proof should sign for rejection tests");

  mcp::auth::openssl::OpenSslDpopVerifierOptions verifier_options;
  verifier_options.claim_validation.now =
      mcp::auth::TimePoint(std::chrono::seconds(1700000000));
  mcp::auth::openssl::OpenSslDpopVerifier verifier(verifier_options);

  const auto wrong_target = verifier.verify(
      *proof, {"POST", "https://resource.example/mcp"}, request.access_token);
  require(!wrong_target.has_value(), "DPoP wrong target should fail");

  auto claims = verifier.verify(*proof, request.target, request.access_token);
  require(claims.has_value(), "DPoP proof should verify before replay check");
  mcp::auth::InMemoryDpopReplayCache replay_cache;
  mcp::auth::DpopClaimValidationOptions options;
  options.now = mcp::auth::TimePoint(std::chrono::seconds(1700000000));
  auto expected_ath =
      mcp::auth::openssl::dpop_access_token_hash(*request.access_token);
  require(expected_ath.has_value(), "expected ath should compute for replay");
  options.expected_access_token_hash = *expected_ath;
  auto first = mcp::auth::validate_dpop_proof_claims(
      *claims, request.target, request.access_token, options, &replay_cache);
  require(first.has_value(), "first DPoP replay-cache validation should pass");
  auto second = mcp::auth::validate_dpop_proof_claims(
      *claims, request.target, request.access_token, options, &replay_cache);
  require(!second.has_value(),
          "second DPoP replay-cache validation should fail");
}

void test_static_jwks_dpop_bearer_auth_provider_authenticates_and_replays() {
  auto access_key = generate_rsa_key();
  auto proof_key = generate_ec_p256_key();

  mcp::auth::JsonWebKeySet jwks;
  jwks.keys.push_back(rsa_public_jwk_from_key(access_key.get()));
  const auto access_token = signed_rs256_jwt(
      access_key.get(), R"({"alg":"RS256","kid":"rsa-key"})",
      R"({"iss":"https://issuer.example","sub":"alice","aud":"tools",)"
      R"("exp":4102444800,"scope":"tools.read"})");

  mcp::auth::DpopProofRequest proof_request;
  proof_request.target = {"POST", "https://resource.example/mcp"};
  proof_request.key.algorithm = "ES256";
  proof_request.key.key_id = "dpop-key";
  proof_request.key.private_key_pem = private_key_pem(proof_key.get());
  proof_request.access_token = access_token;
  mcp::auth::openssl::OpenSslDpopSigner signer;
  auto proof = signer.sign(proof_request);
  require(proof.has_value(), "provider DPoP proof should sign");

  mcp::auth::DpopAuthProviderOptions auth_options;
  auth_options.require_dpop = true;
  auth_options.issuer = "https://issuer.example";
  auth_options.audience = "tools";
  auth_options.required_algorithm = "RS256";
  mcp::auth::InMemoryDpopReplayCache replay_cache;
  mcp::auth::openssl::StaticJwksDpopBearerAuthProvider provider(
      jwks, &replay_cache, auth_options);

  mcp::server::AuthRequest auth_request;
  auth_request.http_method = "POST";
  auth_request.http_url = "https://resource.example/mcp";
  auth_request.headers.emplace("Authorization", "DPoP " + access_token);
  auth_request.headers.emplace("DPoP", *proof);

  const auto identity = provider.authenticate(auth_request);
  require(identity.has_value(), "OpenSSL DPoP provider should authenticate");
  require(identity->subject == "alice", "provider identity subject mismatch");
  require(identity->claims.at("scope") == "tools.read",
          "provider identity scope mismatch");

  const auto replay = provider.authenticate(auth_request);
  require(!replay.has_value(), "OpenSSL DPoP provider should reject replay");
}

}  // namespace

int main() {
  const std::vector<void (*)()> tests{
      test_dpop_access_token_hash_matches_sha256_base64url,
      test_base64url_round_trips_without_padding,
      test_base64url_rejects_non_jose_inputs,
      test_compact_jws_parts_and_header_parse,
      test_compact_jws_rejects_malformed_structure_and_header,
      test_es256_compact_jws_signature_verifies_against_public_jwk,
      test_rs256_compact_jws_signature_verifies_against_public_jwk,
      test_rs256_jwt_verifies_signature_jwks_and_claims,
      test_rs256_jwt_rejects_bad_claims_and_signature,
      test_fetching_jwks_verifier_fetches_and_caches_on_miss,
      test_fetching_jwks_verifier_refreshes_on_key_rotation,
      test_fetching_jwks_verifier_does_not_refresh_claim_failures,
      test_es256_dpop_signer_and_verifier_round_trip_claims,
      test_es256_dpop_verifier_rejects_target_and_replay,
      test_static_jwks_dpop_bearer_auth_provider_authenticates_and_replays,
  };
  for (const auto test : tests) {
    test();
  }
  return 0;
}
