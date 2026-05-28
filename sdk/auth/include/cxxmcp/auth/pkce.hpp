// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string>

#include "cxxmcp/core/result.hpp"

/// @file
/// @brief PKCE contracts for OAuth authorization-code flows.

namespace mcp::auth {

enum class PkceCodeChallengeMethod {
  kS256,
};

/// @brief PKCE verifier/challenge pair used by OAuth 2.1 authorization flows.
struct PkceChallenge {
  std::string code_verifier;
  std::string code_challenge;
  PkceCodeChallengeMethod method = PkceCodeChallengeMethod::kS256;
};

/// @brief Public wrapper around the SDK's private PKCE implementation.
///
/// The default implementation will be backed by MiniOAuth2 once the optional
/// auth implementation target is added. The interface stays independent from
/// MiniOAuth2 headers so the helper can be replaced without public API churn.
class PkceGenerator {
 public:
  virtual ~PkceGenerator() = default;

  virtual core::Result<PkceChallenge> create_s256() = 0;
  virtual core::Result<bool> verify(const PkceChallenge& challenge) = 0;
};

}  // namespace mcp::auth
