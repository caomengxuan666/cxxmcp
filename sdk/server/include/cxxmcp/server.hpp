#pragma once

/// @file
/// @brief Server-side high-level C++ API for MCP servers, transports, registries, and handlers.
///
/// Include this header when a server application wants the full facade, including auth,
/// rate limiting, stdio and HTTP transports, registries, peer helpers, and the fluent
/// server builder.

#include "cxxmcp/server/auth.hpp"
#include "cxxmcp/server/http_transport.hpp"
#include "cxxmcp/server/peer.hpp"
#include "cxxmcp/server/rate_limit.hpp"
#include "cxxmcp/server/registry.hpp"
#include "cxxmcp/server/server.hpp"
#include "cxxmcp/server/stdio_transport.hpp"
#include "cxxmcp/server/transport.hpp"
