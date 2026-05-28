// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Convenience umbrella header for the optional cxxmcp auth layer.
///
/// The auth layer is exposed only when the `cxxmcp::auth` CMake target is
/// enabled with `MCP_ENABLE_AUTH` / `CXXMCP_ENABLE_AUTH`. These headers define
/// transport-neutral OAuth 2.1, PKCE, DPoP, metadata, and token storage
/// contracts. Crypto and HTTP integration are intentionally implementation
/// details of later source files, not public API dependencies.

#include "cxxmcp/auth/constant_time.hpp"
#include "cxxmcp/auth/dpop.hpp"
#include "cxxmcp/auth/http_metadata_endpoint.hpp"
#include "cxxmcp/auth/http_token_endpoint.hpp"
#include "cxxmcp/auth/jwks.hpp"
#include "cxxmcp/auth/lifecycle.hpp"
#include "cxxmcp/auth/metadata.hpp"
#include "cxxmcp/auth/pkce.hpp"
#include "cxxmcp/auth/registration.hpp"
#include "cxxmcp/auth/server_auth_endpoints.hpp"
#include "cxxmcp/auth/token.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/auth/www_auth.hpp"
