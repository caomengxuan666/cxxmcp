// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

/// @file
/// @brief Authentication extension points for server transports.

namespace mcp::server {

namespace detail {

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

inline bool constant_time_string_equal(std::string_view lhs,
                                       std::string_view rhs) {
  const auto max_size = lhs.size() > rhs.size() ? lhs.size() : rhs.size();
  unsigned char diff = static_cast<unsigned char>(lhs.size() ^ rhs.size());
  for (std::size_t index = 0; index < max_size; ++index) {
    const auto left =
        index < lhs.size() ? static_cast<unsigned char>(lhs[index]) : 0;
    const auto right =
        index < rhs.size() ? static_cast<unsigned char>(rhs[index]) : 0;
    diff = static_cast<unsigned char>(diff | (left ^ right));
  }
  return diff == 0;
}

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

inline std::string_view bearer_token_from_authorization(
    std::string_view authorization) {
  authorization = trim_ascii(authorization);
  constexpr std::string_view kBearer = "Bearer";
  if (authorization.size() <= kBearer.size() ||
      !ascii_iequals(authorization.substr(0, kBearer.size()), kBearer) ||
      std::isspace(static_cast<unsigned char>(authorization[kBearer.size()])) ==
          0) {
    return {};
  }
  authorization.remove_prefix(kBearer.size());
  authorization = trim_ascii(authorization);
  return authorization;
}

}  // namespace detail

/// @brief Stable core::Error category used for server authentication failures.
inline constexpr std::string_view AuthErrorCategory = "auth";

/// @brief Default HTTP challenge used when a transport has no custom policy.
inline constexpr std::string_view DefaultAuthChallenge = "Bearer";

/// @brief Transport-neutral authentication input.
///
/// AuthRequest contains only data that can be obtained before dispatching an
/// MCP request. Header names and remote address formats are transport-specific;
/// implementations should normalize them before policy checks when needed.
struct AuthRequest {
  /// Request headers or metadata supplied by the transport.
  std::unordered_map<std::string, std::string> headers;
  /// Best-effort remote address for audit and policy decisions.
  std::string remote_address;
  /// HTTP request method when supplied by an HTTP-based transport.
  std::optional<std::string> http_method;
  /// Absolute HTTP request URL when supplied by an HTTP-based transport.
  std::optional<std::string> http_url;
};

/// @brief Authenticated principal and associated claims.
struct AuthIdentity {
  /// Stable principal identifier, such as a user id, service account, or token
  /// subject.
  std::string subject;
  /// Provider-specific claims copied by value into the identity.
  std::unordered_map<std::string, std::string> claims;
};

/// @brief Abstract authentication provider used by server integrations.
///
/// The provider does not own request data; authenticate() receives a borrowed
/// AuthRequest for the duration of the call and returns a value identity on
/// success. Authentication failures and provider errors are propagated through
/// core::Result so the caller can map them to protocol or HTTP errors.
class AuthProvider {
 public:
  virtual ~AuthProvider() = default;

  /// @brief Authenticate a transport request.
  /// @param request Headers and remote metadata to evaluate.
  /// @return AuthIdentity on success, or a core::Error describing denial or
  /// provider failure.
  /// @note Implementations may be called concurrently by transports that
  /// dispatch requests on multiple threads.
  virtual core::Result<AuthIdentity> authenticate(
      const AuthRequest& request) = 0;
};

/// @brief Build a structured authentication failure for transports.
inline core::Error make_auth_error(std::string message,
                                   std::string detail = {}) {
  return core::Error{
      static_cast<int>(protocol::ErrorCode::PermissionDenied),
      std::move(message),
      std::move(detail),
      std::string(AuthErrorCategory),
  };
}

/// @brief Static bearer-token AuthProvider for small embedded deployments and
/// tests.
///
/// This provider validates `Authorization: Bearer <token>` against an in-memory
/// token table and returns the configured identity. It is intentionally narrow:
/// production OAuth/DPoP deployments should use an application-specific
/// provider or the future OpenSSL/JWKS-backed provider.
class StaticBearerAuthProvider final : public AuthProvider {
 public:
  struct Entry {
    std::string token;
    AuthIdentity identity;
  };

  StaticBearerAuthProvider() = default;
  explicit StaticBearerAuthProvider(std::vector<Entry> entries)
      : entries_(std::move(entries)) {}

  void add_token(std::string token, AuthIdentity identity) {
    entries_.push_back(Entry{std::move(token), std::move(identity)});
  }

  core::Result<AuthIdentity> authenticate(const AuthRequest& request) override {
    const auto token = bearer_token(request);
    if (token.empty()) {
      return mcp::core::unexpected(
          make_auth_error("missing or invalid bearer token"));
    }

    for (const auto& entry : entries_) {
      if (detail::constant_time_string_equal(token, entry.token)) {
        return entry.identity;
      }
    }
    return mcp::core::unexpected(
        make_auth_error("missing or invalid bearer token"));
  }

 private:
  static std::string_view bearer_token(const AuthRequest& request) {
    for (const auto& header : request.headers) {
      if (detail::ascii_iequals(header.first, "Authorization")) {
        return detail::bearer_token_from_authorization(header.second);
      }
    }
    return {};
  }

  std::vector<Entry> entries_;
};

}  // namespace mcp::server
