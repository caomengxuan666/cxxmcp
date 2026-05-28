// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Convenience header providing one-call entry points for
/// ServerPeer::Builder::run() and ClientPeer::Builder::run().
///
/// Server - build, serve, and block until shutdown:
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
///
/// Client - build, serve, run a callback, then stop:
/// @code
/// #include <cxxmcp/run.hpp>
///
/// int main() {
///   return mcp::ClientPeer::builder()
///       .streamable_http("http://127.0.0.1:3000/mcp")
///       .run([](auto& svc) {
///         svc.peer().initialize();
///         svc.peer().call_tool("echo", Json{{"value", "hello"}});
///       });
/// }
/// @endcode

#include "cxxmcp/service.hpp"
