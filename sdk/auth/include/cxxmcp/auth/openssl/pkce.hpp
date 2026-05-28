// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <openssl/rand.h>

#include <array>
#include <cstddef>
#include <string>

#include "cxxmcp/auth/openssl/base64url.hpp"
#include "cxxmcp/auth/openssl/sha256.hpp"
#include "cxxmcp/auth/pkce.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief OpenSSL-backed PKCE S256 generator and verifier.

namespace mcp::auth::openssl {

/// @brief PKCE code_verifier length in bytes (43 characters when base64url
/// encoded, satisfying RFC 7636 §4.1 minimum of 43).
inline constexpr std::size_t kPkceVerifierBytes = 32;

/// @brief OpenSSL-backed implementation of the PkceGenerator contract.
///
/// Generates RFC 7636 S256 code_verifier / code_challenge pairs using
/// RAND_bytes for cryptographic randomness and SHA-256 for the challenge hash.
class OpenSslPkceGenerator final : public PkceGenerator {
 public:
  core::Result<PkceChallenge> create_s256() override {
    std::array<unsigned char, kPkceVerifierBytes> random_bytes{};
    if (RAND_bytes(random_bytes.data(),
                   static_cast<int>(random_bytes.size())) != 1) {
      return core::unexpected(
          core::Error{0,
                      "failed to generate PKCE code_verifier random bytes",
                      {},
                      std::string(AuthErrorCategory)});
    }

    PkceChallenge challenge;
    challenge.code_verifier =
        base64url_encode_bytes(random_bytes.data(), random_bytes.size());
    challenge.method = PkceCodeChallengeMethod::kS256;

    auto hashed = sha256_base64url(challenge.code_verifier);
    if (!hashed.has_value()) {
      return core::unexpected(hashed.error());
    }
    challenge.code_challenge = std::move(*hashed);

    return challenge;
  }

  core::Result<bool> verify(const PkceChallenge& challenge) override {
    if (challenge.method != PkceCodeChallengeMethod::kS256) {
      return core::unexpected(core::Error{
          0,
          "unsupported PKCE code_challenge_method; only S256 is supported",
          {},
          std::string(AuthErrorCategory)});
    }

    auto expected = sha256_base64url(challenge.code_verifier);
    if (!expected.has_value()) {
      return core::unexpected(expected.error());
    }

    return *expected == challenge.code_challenge;
  }
};

}  // namespace mcp::auth::openssl
