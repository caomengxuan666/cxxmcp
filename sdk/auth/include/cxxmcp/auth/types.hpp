// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <map>
#include <string>
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

}  // namespace mcp::auth
