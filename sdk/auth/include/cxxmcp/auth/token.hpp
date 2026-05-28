// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cxxmcp/auth/constant_time.hpp"
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
/// This class stores tokens in process memory and separates entries by the full
/// TokenKey. Durable and secure storage such as keychain, TPM, or encrypted
/// files belongs in applications.
class InMemoryTokenStore final : public TokenStore {
 public:
  core::Result<std::optional<TokenSet>> load(const TokenKey& key) override {
    const auto iter = find_entry(key);
    if (iter == entries_.end()) {
      return std::optional<TokenSet>{};
    }
    return iter->second;
  }

  core::Result<core::Unit> save(const TokenKey& key, TokenSet tokens) override {
    auto iter = find_entry(key);
    if (iter == entries_.end()) {
      entries_.emplace_back(key, std::move(tokens));
    } else {
      iter->second = std::move(tokens);
    }
    return core::Unit{};
  }

  core::Result<core::Unit> remove(const TokenKey& key) override {
    auto iter = find_entry(key);
    if (iter != entries_.end()) {
      entries_.erase(iter);
    }
    return core::Unit{};
  }

 private:
  using Entry = std::pair<TokenKey, TokenSet>;

  static bool matches(const MetadataMap& lhs, const MetadataMap& rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    bool equal = true;
    auto lhs_iter = lhs.begin();
    auto rhs_iter = rhs.begin();
    for (; lhs_iter != lhs.end(); ++lhs_iter, ++rhs_iter) {
      equal = constant_time_string_equal(lhs_iter->first, rhs_iter->first) &
              constant_time_string_equal(lhs_iter->second, rhs_iter->second) &
              equal;
    }
    return equal;
  }

  static bool matches(const TokenKey& lhs, const TokenKey& rhs) {
    return constant_time_string_equal(lhs.resource, rhs.resource) &
           constant_time_string_equal(lhs.issuer, rhs.issuer) &
           constant_time_string_equal(lhs.client_id, rhs.client_id) &
           matches(lhs.attributes, rhs.attributes);
  }

  std::vector<Entry>::iterator find_entry(const TokenKey& key) {
    for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
      if (matches(iter->first, key)) {
        return iter;
      }
    }
    return entries_.end();
  }

  std::vector<Entry>::const_iterator find_entry(const TokenKey& key) const {
    for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
      if (matches(iter->first, key)) {
        return iter;
      }
    }
    return entries_.end();
  }

  std::vector<Entry> entries_;
};

}  // namespace mcp::auth
