// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cxxmcp/auth/metadata.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

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

/// @brief Request sent through an application-provided DCR transport.
struct ClientRegistrationEndpointRequest {
  std::string registration_endpoint;
  HeaderMap headers;
  ClientRegistrationRequest registration;
};

/// @brief Request sent through an application-provided CIMD fetch transport.
struct ClientIdMetadataDocumentFetchRequest {
  std::string client_id_metadata_url;
  HeaderMap headers;
};

/// @brief Dynamic client registration network boundary.
///
/// The SDK owns request shape, defaults, and response normalization. The
/// application owns HTTP, retries, TLS policy, and persistence.
class OAuthClientRegistrationEndpoint {
 public:
  virtual ~OAuthClientRegistrationEndpoint() = default;

  virtual core::Result<ClientRegistrationResponse> register_client(
      const ClientRegistrationEndpointRequest& request) = 0;
};

/// @brief URL-based Client ID Metadata Document network boundary.
///
/// Clients do not need this boundary to use a URL client_id, but servers or
/// tools that want to preflight a document can plug transport I/O in here.
class ClientIdMetadataDocumentEndpoint {
 public:
  virtual ~ClientIdMetadataDocumentEndpoint() = default;

  virtual core::Result<ClientIdMetadataDocument> fetch_client_metadata_document(
      const ClientIdMetadataDocumentFetchRequest& request) = 0;
};

/// @brief Options used to build a dynamic client registration request.
struct ClientRegistrationOptions {
  std::string client_name = "MCP Client";
  std::string redirect_uri;
  ScopeList scopes;
  MetadataMap metadata;
};

/// @brief Options used when selecting a client_id before authorization.
struct ClientIdConfigurationOptions {
  std::optional<std::string> client_id_metadata_url;
  std::string client_name = "MCP Client";
  std::string redirect_uri;
  ScopeList scopes;
  HeaderMap headers;
  MetadataMap metadata;
};

/// @brief Build the default MCP OAuth DCR request for a public client.
inline core::Result<ClientRegistrationRequest>
build_client_registration_request(const ClientRegistrationOptions& options) {
  if (options.redirect_uri.empty()) {
    return std::unexpected(
        core::Error{1,
                    "redirect_uri is required for dynamic client registration",
                    {},
                    std::string(AuthErrorCategory)});
  }

  ClientRegistrationRequest request;
  request.redirect_uris = {options.redirect_uri};
  request.grant_types = {"authorization_code", "refresh_token"};
  request.response_types = {"code"};
  request.scope = options.scopes;
  request.client_name =
      options.client_name.empty() ? "MCP Client" : options.client_name;
  request.token_endpoint_auth_method = "none";
  request.metadata = options.metadata;
  return request;
}

/// @brief Convert a successful DCR response into the SDK OAuth client config.
inline OAuthClientConfig oauth_client_config_from_registration_response(
    const ClientRegistrationResponse& response, std::string redirect_uri,
    ScopeList scopes) {
  OAuthClientConfig config;
  config.client_id = response.client_id;
  if (response.client_secret.has_value() && !response.client_secret->empty()) {
    config.client_secret = response.client_secret;
  }
  config.redirect_uri = std::move(redirect_uri);
  config.scopes = std::move(scopes);
  config.metadata = response.metadata;
  return config;
}

/// @brief Convert a Client ID Metadata Document into the SDK OAuth config.
inline OAuthClientConfig oauth_client_config_from_metadata_document(
    const ClientIdMetadataDocument& document, std::string redirect_uri,
    ScopeList scopes) {
  OAuthClientConfig config;
  config.client_id = document.client_id;
  config.redirect_uri = std::move(redirect_uri);
  config.scopes = std::move(scopes);
  config.metadata = document.metadata;
  return config;
}

namespace detail {

inline bool metadata_flag_enabled(const MetadataMap& metadata,
                                  std::string_view key) {
  const auto iter = metadata.find(std::string(key));
  if (iter == metadata.end()) {
    return false;
  }
  return iter->second == "true" || iter->second == "1" || iter->second == "yes";
}

inline bool url_contains_dot_segment(std::string_view path) {
  std::size_t pos = 0;
  while (pos <= path.size()) {
    const auto next = path.find('/', pos);
    const auto end = next == std::string_view::npos ? path.size() : next;
    const auto segment = path.substr(pos, end - pos);
    if (segment == "." || segment == "..") {
      return true;
    }
    if (next == std::string_view::npos) {
      break;
    }
    pos = next + 1;
  }
  return false;
}

}  // namespace detail

/// @brief Whether authorization metadata advertises URL-based client_id docs.
inline bool supports_client_id_metadata_document(
    const AuthorizationServerMetadata& metadata) {
  return detail::metadata_flag_enabled(metadata.metadata,
                                       "client_id_metadata_document_supported");
}

/// @brief Validate an SEP-991-style HTTPS URL client_id.
inline bool is_valid_client_id_metadata_url(std::string_view url) {
  constexpr std::string_view kHttps = "https://";
  if (url.substr(0, kHttps.size()) != kHttps) {
    return false;
  }
  const auto authority_start = kHttps.size();
  const auto path_start = url.find('/', authority_start);
  if (path_start == std::string_view::npos || path_start + 1 >= url.size()) {
    return false;
  }
  const auto authority =
      url.substr(authority_start, path_start - authority_start);
  if (authority.empty() || authority.find('@') != std::string_view::npos) {
    return false;
  }
  const auto path = url.substr(path_start);
  if (path.find('#') != std::string_view::npos ||
      detail::url_contains_dot_segment(path)) {
    return false;
  }
  return true;
}

}  // namespace mcp::auth
