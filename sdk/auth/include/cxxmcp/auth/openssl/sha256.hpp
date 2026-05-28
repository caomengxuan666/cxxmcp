// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <openssl/sha.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "cxxmcp/auth/openssl/base64url.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief OpenSSL-backed SHA-256 helpers for optional auth crypto.

namespace mcp::auth::openssl {

/// @brief Compute base64url(SHA-256(data)).
inline core::Result<std::string> sha256_base64url(std::string_view data) {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest.data());
  return base64url_encode_bytes(digest.data(), digest.size());
}

/// @brief Compute the RFC 9449 DPoP `ath` value for an access token.
inline core::Result<std::string> dpop_access_token_hash(
    std::string_view access_token) {
  return sha256_base64url(access_token);
}

}  // namespace mcp::auth::openssl
