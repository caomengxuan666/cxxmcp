// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <optional>
#include <string>

#include "cxxmcp/auth/token.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief DPoP proof model and signing/verification boundaries.

namespace mcp::auth {

/// @brief Private key handle for DPoP proof generation.
///
/// The key material format is intentionally opaque at the public boundary. The
/// OpenSSL-backed implementation will define accepted encodings privately.
struct DpopKey {
  std::string key_id;
  std::string algorithm;
  std::string private_key_pem;
};

/// @brief Input for constructing a DPoP proof JWT.
struct DpopProofRequest {
  HttpRequestTarget target;
  DpopKey key;
  std::optional<std::string> access_token;
  std::optional<std::string> nonce;
};

/// @brief Parsed or verified DPoP proof claims.
struct DpopProofClaims {
  std::string jwt_id;
  std::string method;
  std::string url;
  TimePoint issued_at;
  std::optional<std::string> access_token_hash;
  std::optional<std::string> nonce;
  MetadataMap claims;
};

/// @brief DPoP proof construction boundary.
class DpopSigner {
 public:
  virtual ~DpopSigner() = default;

  virtual core::Result<std::string> sign(const DpopProofRequest& request) = 0;
};

/// @brief DPoP proof verification boundary for server-side auth providers.
class DpopVerifier {
 public:
  virtual ~DpopVerifier() = default;

  virtual core::Result<DpopProofClaims> verify(
      const std::string& proof_jwt, const HttpRequestTarget& target,
      const std::optional<std::string>& access_token) = 0;
};

}  // namespace mcp::auth
