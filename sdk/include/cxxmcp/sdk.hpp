#pragma once

/// @file
/// @brief Convenience umbrella header for the complete public cxxmcp SDK.
///
/// Include this header when an application wants the client API, server API,
/// shared handlers, transport factories, peer/session abstractions, and MCP
/// protocol model types from a single entry point. Projects that need tighter
/// compile-time boundaries can include the narrower `cxxmcp/client.hpp`,
/// `cxxmcp/server.hpp`, `cxxmcp/protocol.hpp`, or transport-specific headers
/// instead.

#include "cxxmcp/client.hpp"
#include "cxxmcp/handler.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/transport.hpp"
