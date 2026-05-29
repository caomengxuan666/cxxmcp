// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/core/string_constant.hpp"

/// @file
/// @brief Shared lightweight value types for cxxmcp auth contracts.

namespace mcp::auth {

using HeaderMap = std::map<std::string, std::string>;
using MetadataMap = std::map<std::string, std::string>;
using ScopeList = std::vector<std::string>;
using StringList = std::vector<std::string>;
using SystemClock = std::chrono::system_clock;
using TimePoint = SystemClock::time_point;

inline constexpr core::StringConstant AuthErrorCategory{"auth"};

/// @brief Transport-neutral HTTP request descriptor used by auth helpers.
struct HttpRequestTarget {
  std::string method;
  std::string url;
};

/// @brief Transport-neutral HTTP response descriptor used by auth helpers.
struct HttpResponseMetadata {
  int status_code = 0;
  HeaderMap headers;
};

/// @brief Public OAuth client configuration used by lifecycle helpers.
struct OAuthClientConfig {
  std::string client_id;
  std::optional<std::string> client_secret;
  std::string redirect_uri;
  ScopeList scopes;
  MetadataMap metadata;
};

/// @brief OAuth 2.0 Client Credentials flow configuration (SEP-1046).
///
/// Supports client_secret_post authentication for machine-to-machine flows.
/// Private-key JWT (RFC 7523) is deferred to the optional OpenSSL-backed auth
/// feature.
struct ClientCredentialsConfig {
  std::string client_id;
  std::string client_secret;
  ScopeList scopes;
  std::string resource;
  MetadataMap metadata;
};

}  // namespace mcp::auth
