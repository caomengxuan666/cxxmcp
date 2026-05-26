// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief WWW-Authenticate challenge models and parser boundary.

namespace mcp::auth {

inline constexpr std::string_view WwwAuthenticateResourceMetadataParam =
    "resource_metadata";
inline constexpr std::string_view WwwAuthenticateErrorParam = "error";
inline constexpr std::string_view WwwAuthenticateScopeParam = "scope";
inline constexpr std::string_view WwwAuthenticateInsufficientScopeError =
    "insufficient_scope";

/// @brief Parsed authentication challenge from a WWW-Authenticate header.
struct WwwAuthenticateChallenge {
  std::string scheme;
  MetadataMap parameters;

  std::string parameter(std::string key) const {
    const auto iter = parameters.find(key);
    return iter == parameters.end() ? std::string{} : iter->second;
  }

  /// @brief Returns true when this is a Bearer challenge.
  bool bearer() const { return scheme == "Bearer" || scheme == "bearer"; }
};

/// @brief Returns the MCP OAuth protected-resource metadata URL, when present.
inline std::optional<std::string> resource_metadata_url(
    const WwwAuthenticateChallenge& challenge) {
  const auto value =
      challenge.parameter(std::string(WwwAuthenticateResourceMetadataParam));
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

/// @brief Returns true for an OAuth insufficient_scope challenge.
inline bool insufficient_scope(const WwwAuthenticateChallenge& challenge) {
  return challenge.parameter(std::string(WwwAuthenticateErrorParam)) ==
         WwwAuthenticateInsufficientScopeError;
}

/// @brief Returns the first resource_metadata parameter in parsed challenges.
inline std::optional<std::string> first_resource_metadata_url(
    const std::vector<WwwAuthenticateChallenge>& challenges) {
  for (const auto& challenge : challenges) {
    auto value = resource_metadata_url(challenge);
    if (value.has_value()) {
      return value;
    }
  }
  return std::nullopt;
}

/// @brief Parser boundary for HTTP authentication challenges.
///
/// The implementation is cxxmcp-owned because MCP OAuth needs precise handling
/// for `resource_metadata` and `insufficient_scope` challenges.
class WwwAuthenticateParser {
 public:
  virtual ~WwwAuthenticateParser() = default;

  virtual core::Result<std::vector<WwwAuthenticateChallenge>> parse(
      const std::string& header_value) const = 0;
};

}  // namespace mcp::auth
