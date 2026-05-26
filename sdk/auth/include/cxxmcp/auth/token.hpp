// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <optional>
#include <string>
#include <utility>

#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief OAuth token models and storage contracts.

namespace mcp::auth {

/// @brief OAuth access and refresh token state owned by the application.
struct TokenSet {
  std::string access_token;
  std::string token_type = "Bearer";
  std::optional<std::string> refresh_token;
  std::optional<TimePoint> expires_at;
  ScopeList scopes;
  MetadataMap metadata;

  /// @brief Returns true when an expiry timestamp exists and is not in the
  /// future. Callers may apply their own refresh skew before using this helper.
  bool expired(TimePoint now = SystemClock::now()) const {
    return expires_at.has_value() && *expires_at <= now;
  }
};

/// @brief Token refresh result, including optional refresh-token rotation.
struct TokenRefreshResult {
  TokenSet token_set;
  bool refresh_token_rotated = false;
};

/// @brief Stable key for token storage.
///
/// Persistent storage remains an application concern. The SDK uses this key to
/// separate credentials for different resource, issuer, and client triples.
struct TokenKey {
  std::string resource;
  std::string issuer;
  std::string client_id;
  MetadataMap attributes;
};

/// @brief Application-provided OAuth token persistence boundary.
class TokenStore {
 public:
  virtual ~TokenStore() = default;

  virtual core::Result<std::optional<TokenSet>> load(const TokenKey& key) = 0;
  virtual core::Result<core::Unit> save(const TokenKey& key,
                                        TokenSet tokens) = 0;
  virtual core::Result<core::Unit> remove(const TokenKey& key) = 0;
};

/// @brief Minimal non-persistent token store useful for tests and simple apps.
///
/// This class intentionally stores a single token set. Durable and secure
/// storage such as keychain, TPM, or encrypted files belongs in applications.
class InMemoryTokenStore final : public TokenStore {
 public:
  core::Result<std::optional<TokenSet>> load(const TokenKey& key) override {
    if (!matches(key)) {
      return std::optional<TokenSet>{};
    }
    return tokens_;
  }

  core::Result<core::Unit> save(const TokenKey& key, TokenSet tokens) override {
    key_ = key;
    tokens_ = std::move(tokens);
    return core::Unit{};
  }

  core::Result<core::Unit> remove(const TokenKey& key) override {
    if (matches(key)) {
      key_.reset();
      tokens_.reset();
    }
    return core::Unit{};
  }

 private:
  bool matches(const TokenKey& key) const {
    return key_.has_value() && key_->resource == key.resource &&
           key_->issuer == key.issuer && key_->client_id == key.client_id;
  }

  std::optional<TokenKey> key_;
  std::optional<TokenSet> tokens_;
};

}  // namespace mcp::auth
