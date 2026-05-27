// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief Shared lightweight value types for cxxmcp auth contracts.

namespace mcp::auth {

using HeaderMap = std::map<std::string, std::string>;
using MetadataMap = std::map<std::string, std::string>;
using ScopeList = std::vector<std::string>;
using StringList = std::vector<std::string>;
using SystemClock = std::chrono::system_clock;
using TimePoint = SystemClock::time_point;

inline constexpr std::string_view AuthErrorCategory = "auth";

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

}  // namespace mcp::auth
