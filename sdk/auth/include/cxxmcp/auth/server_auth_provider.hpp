// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <cctype>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "cxxmcp/auth/dpop.hpp"
#include "cxxmcp/server/auth.hpp"

/// @file
/// @brief Optional bridge from auth verifiers to server AuthProvider.

namespace mcp::auth {

namespace server_auth_detail {

inline std::string_view trim_ascii(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return value;
}

inline bool ascii_iequals(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

inline std::optional<std::string_view> header_value(
    const std::unordered_map<std::string, std::string>& headers,
    std::string_view name) {
  for (const auto& header : headers) {
    if (ascii_iequals(header.first, name)) {
      return header.second;
    }
  }
  return std::nullopt;
}

inline std::optional<std::pair<std::string_view, std::string_view>>
authorization_scheme_and_token(std::string_view authorization) {
  authorization = trim_ascii(authorization);
  const auto split = authorization.find_first_of(" \t\r\n");
  if (split == std::string_view::npos) {
    return std::nullopt;
  }
  auto scheme = authorization.substr(0, split);
  authorization.remove_prefix(split);
  const auto token = trim_ascii(authorization);
  if (scheme.empty() || token.empty()) {
    return std::nullopt;
  }
  return std::make_pair(scheme, token);
}

}  // namespace server_auth_detail

/// @brief Options for DPoP-aware server authentication over verified tokens.
struct DpopAuthProviderOptions {
  std::optional<std::string> issuer;
  std::optional<std::string> audience;
  std::optional<std::string> required_algorithm;
  MetadataMap required_claims;
  DpopClaimValidationOptions dpop_claims;
  bool require_dpop = false;
};

/// @brief Server AuthProvider backed by injected JWT and DPoP verifiers.
///
/// This provider does not parse or trust JWT payloads by itself. Access-token
/// validation, proof signature verification, and access-token hash calculation
/// are supplied by application or future OpenSSL-backed auth implementations.
class DpopBearerAuthProvider final : public server::AuthProvider {
 public:
  using AccessTokenHashFunction =
      std::function<core::Result<std::string>(std::string_view access_token)>;

  DpopBearerAuthProvider(JwtVerifier& jwt_verifier, DpopVerifier& dpop_verifier,
                         AccessTokenHashFunction access_token_hash,
                         DpopReplayCache* replay_cache = nullptr,
                         DpopAuthProviderOptions options = {})
      : jwt_verifier_(jwt_verifier),
        dpop_verifier_(dpop_verifier),
        access_token_hash_(std::move(access_token_hash)),
        replay_cache_(replay_cache),
        options_(std::move(options)) {}

  core::Result<server::AuthIdentity> authenticate(
      const server::AuthRequest& request) override {
    const auto authorization =
        server_auth_detail::header_value(request.headers, "Authorization");
    if (!authorization.has_value()) {
      return mcp::core::unexpected(
          server::make_auth_error("missing authorization header"));
    }

    const auto parsed =
        server_auth_detail::authorization_scheme_and_token(*authorization);
    if (!parsed.has_value()) {
      return mcp::core::unexpected(
          server::make_auth_error("invalid authorization header"));
    }

    const auto scheme = parsed->first;
    const auto access_token = parsed->second;
    const bool dpop_scheme = server_auth_detail::ascii_iequals(scheme, "DPoP");
    if (!dpop_scheme && !server_auth_detail::ascii_iequals(scheme, "Bearer")) {
      return mcp::core::unexpected(
          server::make_auth_error("unsupported authorization scheme"));
    }

    auto jwt = verify_access_token(access_token);
    if (!jwt.has_value()) {
      return mcp::core::unexpected(server::make_auth_error(
          "access token verification failed", jwt.error().message));
    }

    const bool has_dpop_header =
        server_auth_detail::header_value(request.headers, "DPoP").has_value();
    if (options_.require_dpop || dpop_scheme || has_dpop_header) {
      auto proof = verify_dpop_proof(request, access_token);
      if (!proof.has_value()) {
        return mcp::core::unexpected(proof.error());
      }
    }

    server::AuthIdentity identity;
    identity.subject = jwt->subject;
    identity.claims = jwt_claims_to_identity(*jwt);
    return identity;
  }

 private:
  core::Result<VerifiedJwtClaims> verify_access_token(
      std::string_view access_token) {
    JwtVerificationRequest jwt_request;
    jwt_request.jwt = std::string(access_token);
    jwt_request.purpose = JwtVerificationPurpose::kAccessToken;
    jwt_request.issuer = options_.issuer;
    jwt_request.audience = options_.audience;
    jwt_request.required_algorithm = options_.required_algorithm;
    jwt_request.required_claims = options_.required_claims;
    jwt_request.now = SystemClock::now();
    return jwt_verifier_.verify(jwt_request);
  }

  core::Result<core::Unit> verify_dpop_proof(const server::AuthRequest& request,
                                             std::string_view access_token) {
    if (!request.http_method.has_value() || request.http_method->empty() ||
        !request.http_url.has_value() || request.http_url->empty()) {
      return mcp::core::unexpected(server::make_auth_error(
          "DPoP authentication requires HTTP method and URL"));
    }

    const auto proof_header =
        server_auth_detail::header_value(request.headers, "DPoP");
    if (!proof_header.has_value() || proof_header->empty()) {
      return mcp::core::unexpected(
          server::make_auth_error("missing DPoP proof header"));
    }
    if (!access_token_hash_) {
      return mcp::core::unexpected(server::make_auth_error(
          "DPoP authentication requires an access-token hash function"));
    }

    const HttpRequestTarget target{*request.http_method, *request.http_url};
    auto claims = dpop_verifier_.verify(
        std::string(*proof_header), target,
        std::optional<std::string>{std::string(access_token)});
    if (!claims.has_value()) {
      return mcp::core::unexpected(server::make_auth_error(
          "DPoP proof verification failed", claims.error().message));
    }

    auto expected_hash = access_token_hash_(access_token);
    if (!expected_hash.has_value()) {
      return mcp::core::unexpected(server::make_auth_error(
          "DPoP access-token hash failed", expected_hash.error().message));
    }

    auto claim_options = options_.dpop_claims;
    claim_options.now = SystemClock::now();
    claim_options.expected_access_token_hash = *expected_hash;
    auto validated = validate_dpop_proof_claims(
        *claims, target, std::optional<std::string>{std::string(access_token)},
        claim_options, replay_cache_);
    if (!validated.has_value()) {
      return mcp::core::unexpected(server::make_auth_error(
          "DPoP proof claims are invalid", validated.error().message));
    }
    return core::Unit{};
  }

  static std::unordered_map<std::string, std::string> jwt_claims_to_identity(
      const VerifiedJwtClaims& claims) {
    std::unordered_map<std::string, std::string> identity_claims;
    if (!claims.issuer.empty()) {
      identity_claims.emplace("iss", claims.issuer);
    }
    if (!claims.audience.empty()) {
      identity_claims.emplace("aud", claims.audience);
    }
    for (const auto& claim : claims.claims) {
      identity_claims.emplace(claim.first, claim.second);
    }
    return identity_claims;
  }

  JwtVerifier& jwt_verifier_;
  DpopVerifier& dpop_verifier_;
  AccessTokenHashFunction access_token_hash_;
  DpopReplayCache* replay_cache_ = nullptr;
  DpopAuthProviderOptions options_;
};

}  // namespace mcp::auth
