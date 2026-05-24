#pragma once

/// @file
/// @brief Convenience include for client and server MCP transport types.
///
/// Include this umbrella header when an SDK user needs the standard transport
/// interfaces and built-in HTTP, stdio, or process-stdio implementations.

#include "cxxmcp/client/http_transport.hpp"
#include "cxxmcp/client/process_stdio_transport.hpp"
#include "cxxmcp/client/stdio_transport.hpp"
#include "cxxmcp/server/http_transport.hpp"
#include "cxxmcp/server/stdio_transport.hpp"
#include "cxxmcp/server/transport.hpp"
