// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <optional>
#include <string>

#include "cxxmcp/auth/types.hpp"

/// @file
/// @brief Dynamic Client Registration and client metadata models.

namespace mcp::auth {

/// @brief RFC 7591 dynamic client registration request.
struct ClientRegistrationRequest {
  StringList redirect_uris;
  StringList grant_types;
  StringList response_types;
  ScopeList scope;
  std::optional<std::string> client_name;
  std::optional<std::string> client_uri;
  std::optional<std::string> logo_uri;
  std::optional<std::string> contacts;
  std::optional<std::string> token_endpoint_auth_method;
  MetadataMap metadata;
};

/// @brief Dynamic client registration response.
struct ClientRegistrationResponse {
  std::string client_id;
  std::optional<std::string> client_secret;
  std::optional<std::string> registration_access_token;
  std::optional<std::string> registration_client_uri;
  std::optional<std::string> client_id_issued_at;
  std::optional<std::string> client_secret_expires_at;
  ClientRegistrationRequest client_metadata;
  MetadataMap metadata;
};

/// @brief Client ID Metadata Document shape used by MCP OAuth clients.
struct ClientIdMetadataDocument {
  std::string client_id;
  ClientRegistrationRequest client_metadata;
  MetadataMap metadata;
};

}  // namespace mcp::auth
