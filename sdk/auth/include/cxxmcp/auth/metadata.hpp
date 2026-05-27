// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <optional>
#include <string>

#include "cxxmcp/auth/types.hpp"

/// @file
/// @brief RFC 9728 and RFC 8414 metadata value models.

namespace mcp::auth {

/// @brief RFC 9728 OAuth protected resource metadata.
struct ProtectedResourceMetadata {
  std::string resource;
  StringList authorization_servers;
  StringList bearer_methods_supported;
  StringList resource_signing_alg_values_supported;
  ScopeList scopes_supported;
  std::optional<std::string> resource_name;
  std::optional<std::string> resource_documentation;
  MetadataMap metadata;
};

/// @brief RFC 8414 OAuth authorization server metadata.
struct AuthorizationServerMetadata {
  std::string issuer;
  std::string authorization_endpoint;
  std::string token_endpoint;
  std::optional<std::string> registration_endpoint;
  std::optional<std::string> jwks_uri;
  StringList response_types_supported;
  StringList grant_types_supported;
  StringList code_challenge_methods_supported;
  StringList token_endpoint_auth_methods_supported;
  StringList dpop_signing_alg_values_supported;
  ScopeList scopes_supported;
  MetadataMap metadata;
};

/// @brief Discovery endpoints selected from MCP resource metadata.
struct MetadataDiscoveryRequest {
  std::string protected_resource_metadata_url;
  std::optional<std::string> authorization_server_metadata_url;
};

/// @brief Complete discovery result for an MCP Streamable HTTP resource.
struct MetadataDiscoveryResult {
  ProtectedResourceMetadata protected_resource;
  std::optional<AuthorizationServerMetadata> authorization_server;
};

}  // namespace mcp::auth
