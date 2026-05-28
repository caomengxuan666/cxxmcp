// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief Loopback HTTP redirect receiver for OAuth authorization code flows.

namespace mcp::auth {

/// @brief Configuration for the loopback redirect receiver.
struct LoopbackReceiverConfig {
  /// Port to listen on. 0 = auto-assign an available port.
  int port = 0;
  /// Path to expect the callback on (e.g. "/callback").
  std::string path = "/callback";
  /// Maximum time to wait for the callback before timing out.
  std::chrono::seconds timeout{300};
};

/// @brief Result from the loopback redirect receiver.
struct LoopbackCallbackResult {
  std::string authorization_code;
  std::string state;
};

/// @brief Callback result from the receiver including the redirect URI used.
struct LoopbackReceiverResult {
  std::string redirect_uri;
  LoopbackCallbackResult callback;
};

}  // namespace mcp::auth
