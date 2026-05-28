// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Convenience header providing ServerPeer::Builder::run().
///
/// Include this header to use the one-call server entry point:
/// @code
/// #include <cxxmcp/run.hpp>
///
/// int main() {
///   return mcp::ServerPeer::builder()
///       .name("my-server")
///       .stdio()
///       .tool<Json, Json>("echo", [](const Json& in) { return in; })
///       .run();
/// }
/// @endcode

#include "cxxmcp/service.hpp"
