// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <utility>

#include "cxxmcp/auth/openssl/dpop.hpp"
#include "cxxmcp/auth/openssl/jwt.hpp"
#include "cxxmcp/auth/server_auth_provider.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/server/auth.hpp"

/// @file
/// @brief Server AuthProvider presets backed by OpenSSL JWT/DPoP verifiers.

namespace mcp::auth::openssl {

/// @brief Owns static-JWKS JWT verification plus OpenSSL DPoP proof
/// verification for server deployments.
class StaticJwksDpopBearerAuthProvider final : public server::AuthProvider {
 public:
  StaticJwksDpopBearerAuthProvider(JsonWebKeySet jwks,
                                   DpopReplayCache* replay_cache = nullptr,
                                   DpopAuthProviderOptions auth_options = {},
                                   OpenSslDpopVerifierOptions dpop_options = {})
      : jwt_verifier_(std::move(jwks)),
        dpop_verifier_(std::move(dpop_options)),
        provider_(jwt_verifier_, dpop_verifier_, dpop_access_token_hash,
                  replay_cache, std::move(auth_options)) {}

  core::Result<server::AuthIdentity> authenticate(
      const server::AuthRequest& request) override {
    return provider_.authenticate(request);
  }

 private:
  StaticJwksJwtVerifier jwt_verifier_;
  OpenSslDpopVerifier dpop_verifier_;
  DpopBearerAuthProvider provider_;
};

/// @brief Owns fetching-JWKS JWT verification plus OpenSSL DPoP proof
/// verification for server deployments.
class FetchingJwksDpopBearerAuthProvider final : public server::AuthProvider {
 public:
  FetchingJwksDpopBearerAuthProvider(
      JwksEndpoint& endpoint, JwksCache* cache,
      FetchingJwksJwtVerifierOptions jwks_options,
      DpopReplayCache* replay_cache = nullptr,
      DpopAuthProviderOptions auth_options = {},
      OpenSslDpopVerifierOptions dpop_options = {})
      : jwt_verifier_(endpoint, cache, std::move(jwks_options)),
        dpop_verifier_(std::move(dpop_options)),
        provider_(jwt_verifier_, dpop_verifier_, dpop_access_token_hash,
                  replay_cache, std::move(auth_options)) {}

  core::Result<server::AuthIdentity> authenticate(
      const server::AuthRequest& request) override {
    return provider_.authenticate(request);
  }

 private:
  FetchingJwksJwtVerifier jwt_verifier_;
  OpenSslDpopVerifier dpop_verifier_;
  DpopBearerAuthProvider provider_;
};

}  // namespace mcp::auth::openssl
