// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "cxxmcp/auth/token.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief DPoP proof model and signing/verification boundaries.

namespace mcp::auth {

namespace detail {

inline core::Error dpop_error(std::string message, std::string detail = {}) {
  return core::Error{1, std::move(message), std::move(detail),
                     std::string(AuthErrorCategory)};
}

inline std::string uppercase_ascii(std::string_view value) {
  std::string output(value);
  std::transform(
      output.begin(), output.end(), output.begin(),
      [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return output;
}

}  // namespace detail

/// @brief Small owning string wrapper that zeroizes stored bytes on reset and
/// destruction.
///
/// This is not a substitute for OS-backed locked memory, but it prevents SDK
/// auth key material from being kept in an ordinary std::string field with no
/// cleanup policy.
class SecureString {
 public:
  SecureString() = default;
  explicit SecureString(const char* value)
      : value_(value == nullptr ? "" : value) {}
  explicit SecureString(std::string value) : value_(std::move(value)) {}
  explicit SecureString(std::string_view value) : value_(value) {}

  SecureString(const SecureString&) = default;
  SecureString& operator=(const SecureString&) = default;

  SecureString(SecureString&& other) noexcept
      : value_(std::move(other.value_)) {
    other.zeroize();
  }

  SecureString& operator=(SecureString&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    zeroize();
    value_ = std::move(other.value_);
    other.zeroize();
    return *this;
  }

  SecureString& operator=(std::string value) {
    reset(std::move(value));
    return *this;
  }

  ~SecureString() { zeroize(); }

  void reset(std::string value = {}) {
    zeroize();
    value_ = std::move(value);
  }

  std::string_view view() const noexcept { return value_; }
  const std::string& str() const noexcept { return value_; }
  bool empty() const noexcept { return value_.empty(); }
  std::size_t size() const noexcept { return value_.size(); }

 private:
  void zeroize() noexcept {
    if (value_.empty()) {
      return;
    }
    volatile char* data = value_.empty() ? nullptr : &value_[0];
    for (std::size_t index = 0; index < value_.size(); ++index) {
      data[index] = '\0';
    }
    value_.clear();
  }

  std::string value_;
};

/// @brief Private key handle for DPoP proof generation.
///
/// The key material format is intentionally opaque at the public boundary. The
/// OpenSSL-backed implementation will define accepted encodings privately.
struct DpopKey {
  std::string key_id;
  std::string algorithm;
  SecureString private_key_pem;
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

/// @brief Replay cache boundary used by DPoP proof validators.
///
/// Implementations must atomically remember a JWT ID until `expires_at` and
/// return `false` when the same still-live ID has already been seen.
class DpopReplayCache {
 public:
  virtual ~DpopReplayCache() = default;

  virtual core::Result<bool> remember_once(std::string jwt_id,
                                           TimePoint expires_at,
                                           TimePoint now) = 0;
};

/// @brief Thread-safe in-memory replay cache for process-local DPoP validation.
///
/// This is suitable for embedded/single-process servers. Multi-process or
/// distributed deployments should provide a shared cache implementation.
class InMemoryDpopReplayCache final : public DpopReplayCache {
 public:
  core::Result<bool> remember_once(std::string jwt_id, TimePoint expires_at,
                                   TimePoint now) override {
    if (jwt_id.empty()) {
      return mcp::core::unexpected(
          detail::dpop_error("DPoP proof jti is required"));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto iter = seen_.begin(); iter != seen_.end();) {
      if (iter->second <= now) {
        iter = seen_.erase(iter);
      } else {
        ++iter;
      }
    }

    const auto existing = seen_.find(jwt_id);
    if (existing != seen_.end() && existing->second > now) {
      return false;
    }
    seen_[std::move(jwt_id)] = expires_at;
    return true;
  }

 private:
  std::mutex mutex_;
  std::unordered_map<std::string, TimePoint> seen_;
};

/// @brief Options for validating verified DPoP claims against an HTTP request.
struct DpopClaimValidationOptions {
  TimePoint now = SystemClock::now();
  std::chrono::seconds clock_skew_tolerance{300};
  std::chrono::seconds replay_ttl{300};
  bool case_sensitive_method = true;
  std::optional<std::string> expected_access_token_hash;
};

/// @brief Validate DPoP claims after JWT signature verification.
///
/// This helper deliberately does not decode or verify JWT signatures. A real
/// DPoP verifier should first validate the JWT cryptographically, then call
/// this helper to enforce replay, clock skew, htm/htu, and ath binding rules.
inline core::Result<core::Unit> validate_dpop_proof_claims(
    const DpopProofClaims& claims, const HttpRequestTarget& target,
    const std::optional<std::string>& access_token,
    const DpopClaimValidationOptions& options = {},
    DpopReplayCache* replay_cache = nullptr) {
  if (claims.jwt_id.empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("DPoP proof jti is required"));
  }
  if (target.method.empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("HTTP request method is required"));
  }
  if (target.url.empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("HTTP request URL is required"));
  }
  if (claims.method.empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("DPoP proof htm is required"));
  }
  if (claims.url.empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("DPoP proof htu is required"));
  }

  const bool method_matches = options.case_sensitive_method
                                  ? claims.method == target.method
                                  : detail::uppercase_ascii(claims.method) ==
                                        detail::uppercase_ascii(target.method);
  if (!method_matches) {
    return mcp::core::unexpected(detail::dpop_error(
        "DPoP proof htm does not match request method", claims.method));
  }
  if (claims.url != target.url) {
    return mcp::core::unexpected(detail::dpop_error(
        "DPoP proof htu does not match request URL", claims.url));
  }

  if (claims.issued_at > options.now + options.clock_skew_tolerance) {
    return mcp::core::unexpected(
        detail::dpop_error("DPoP proof iat is too far in the future"));
  }
  if (claims.issued_at + options.clock_skew_tolerance < options.now) {
    return mcp::core::unexpected(
        detail::dpop_error("DPoP proof iat is too old"));
  }

  if (access_token.has_value()) {
    if (!claims.access_token_hash.has_value() ||
        claims.access_token_hash->empty()) {
      return mcp::core::unexpected(
          detail::dpop_error("DPoP proof ath is required"));
    }
    if (!options.expected_access_token_hash.has_value() ||
        options.expected_access_token_hash->empty()) {
      return mcp::core::unexpected(
          detail::dpop_error("expected DPoP access-token hash is required"));
    }
    if (*claims.access_token_hash != *options.expected_access_token_hash) {
      return mcp::core::unexpected(
          detail::dpop_error("DPoP proof ath does not match access token"));
    }
  }

  if (replay_cache != nullptr) {
    const auto remembered = replay_cache->remember_once(
        claims.jwt_id, options.now + options.replay_ttl, options.now);
    if (!remembered.has_value()) {
      return mcp::core::unexpected(remembered.error());
    }
    if (!*remembered) {
      return mcp::core::unexpected(
          detail::dpop_error("DPoP proof replay detected"));
    }
  }

  return core::Unit{};
}

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

/// @brief Input for authorizing an HTTP resource request with DPoP.
struct DpopAuthorizationRequest {
  HttpRequestTarget target;
  DpopKey key;
  std::string access_token;
  std::optional<std::string> nonce;
  std::string authorization_scheme = "DPoP";
};

/// @brief Headers and proof produced for a DPoP-authorized request.
struct DpopAuthorizationHeaders {
  HeaderMap headers;
  std::string proof;
};

/// @brief Build only the `DPoP` proof header for an HTTP request.
///
/// The supplied signer owns JWS construction and cryptographic signing. This
/// helper only validates required request-target fields and packages the
/// returned proof into HTTP headers.
inline core::Result<DpopAuthorizationHeaders> build_dpop_proof_headers(
    DpopSigner& signer, DpopProofRequest request) {
  if (request.target.method.empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("HTTP request method is required"));
  }
  if (request.target.url.empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("HTTP request URL is required"));
  }

  auto proof = signer.sign(request);
  if (!proof.has_value()) {
    return mcp::core::unexpected(proof.error());
  }
  if (proof->empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("DPoP signer returned an empty proof"));
  }

  DpopAuthorizationHeaders result;
  result.proof = std::move(*proof);
  result.headers.emplace("DPoP", result.proof);
  return result;
}

/// @brief Build `Authorization` and `DPoP` headers for a resource request.
inline core::Result<DpopAuthorizationHeaders> build_dpop_authorization_headers(
    DpopSigner& signer, DpopAuthorizationRequest request) {
  if (request.access_token.empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("DPoP access token is required"));
  }
  if (request.authorization_scheme.empty()) {
    return mcp::core::unexpected(
        detail::dpop_error("DPoP authorization scheme is required"));
  }

  DpopProofRequest proof_request;
  proof_request.target = std::move(request.target);
  proof_request.key = std::move(request.key);
  proof_request.access_token = request.access_token;
  proof_request.nonce = std::move(request.nonce);

  auto headers = build_dpop_proof_headers(signer, std::move(proof_request));
  if (!headers.has_value()) {
    return mcp::core::unexpected(headers.error());
  }
  headers->headers.emplace("Authorization", request.authorization_scheme + " " +
                                                request.access_token);
  return headers;
}

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
