// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string>
#include <vector>

#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief WWW-Authenticate challenge models and parser boundary.

namespace mcp::auth {

/// @brief Parsed authentication challenge from a WWW-Authenticate header.
struct WwwAuthenticateChallenge {
  std::string scheme;
  MetadataMap parameters;

  std::string parameter(std::string key) const {
    const auto iter = parameters.find(key);
    return iter == parameters.end() ? std::string{} : iter->second;
  }
};

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
