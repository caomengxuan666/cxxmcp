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

/// @brief JWT verification purpose for OAuth/DPoP deployments.
enum class JwtVerificationPurpose {
  kAccessToken,
  kIdToken,
  kClientAssertion,
  kDpopProof,
};

/// @brief Input for signature- and claims-verified JWT validation.
///
/// This is intentionally not a decode API. Implementations must verify the
/// signature, issuer, audience, expiry, and any deployment-specific claims
/// before returning VerifiedJwtClaims.
struct JwtVerificationRequest {
  std::string jwt;
  JwtVerificationPurpose purpose = JwtVerificationPurpose::kAccessToken;
  std::optional<std::string> issuer;
  std::optional<std::string> audience;
  std::optional<std::string> required_algorithm;
  MetadataMap required_claims;
  TimePoint now = SystemClock::now();
};

/// @brief Claims returned only after JWT signature and claim validation.
struct VerifiedJwtClaims {
  std::string issuer;
  std::string subject;
  std::string audience;
  std::optional<TimePoint> issued_at;
  std::optional<TimePoint> expires_at;
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

/// @brief JWT verification boundary for access tokens and client assertions.
class JwtVerifier {
 public:
  virtual ~JwtVerifier() = default;

  virtual core::Result<VerifiedJwtClaims> verify(
      const JwtVerificationRequest& request) = 0;
};

}  // namespace mcp::auth
