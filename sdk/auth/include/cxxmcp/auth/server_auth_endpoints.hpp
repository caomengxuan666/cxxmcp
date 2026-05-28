// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "cxxmcp/auth/metadata.hpp"
#include "cxxmcp/auth/registration.hpp"
#include "cxxmcp/auth/token.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief Abstract interfaces for server-side OAuth 2.1 endpoint handlers.
///
/// Applications implement these interfaces to plug authorization server
/// behavior into the MCP HTTP transport. The SDK owns the HTTP protocol
/// wiring (route registration, request parsing, response serialization);
/// the application owns authorization decisions, token minting, and
/// client registration policy.

namespace mcp::auth {

/// @brief Parameters parsed from an authorization request (RFC 6749
/// Section 4.1.1).
struct AuthorizationRequestParams {
  std::string response_type;
  std::string client_id;
  std::string redirect_uri;
  std::string state;
  std::optional<std::string> scope;
  std::optional<std::string> code_challenge;
  std::optional<std::string> code_challenge_method;
  std::optional<std::string> resource;
  std::optional<std::string> nonce;
};

/// @brief Result of a successful authorization decision.
struct AuthorizationDecision {
  /// The authorization code to return to the client via redirect.
  std::string authorization_code;
  /// The state parameter to echo back (must match the request).
  std::string state;
  /// The redirect URI to send the client to.
  std::string redirect_uri;
};

/// @brief Abstract handler for the authorization endpoint.
///
/// The transport calls `authorize()` when a GET /authorize request arrives.
/// The handler validates the request, presents consent (if needed), generates
/// an authorization code, and returns the redirect information.
///
/// For interactive flows, the handler may block until the user grants consent.
class AuthorizationEndpointHandler {
 public:
  virtual ~AuthorizationEndpointHandler() = default;

  /// @brief Process an authorization request.
  ///
  /// @param params Parsed query parameters from the authorization request.
  /// @return A redirect with authorization code, or an error. On error the
  ///         transport returns an error response (or redirects with error
  ///         params if a redirect_uri is available).
  virtual core::Result<AuthorizationDecision> authorize(
      const AuthorizationRequestParams& params) = 0;
};

/// @brief Parameters parsed from a token request (RFC 6749 Section 4.1.3).
struct TokenRequestParams {
  std::string grant_type;
  std::optional<std::string> code;
  std::optional<std::string> redirect_uri;
  std::optional<std::string> client_id;
  std::optional<std::string> client_secret;
  std::optional<std::string> code_verifier;
  std::optional<std::string> refresh_token;
  std::optional<std::string> scope;
  std::optional<std::string> resource;
};

/// @brief Abstract handler for the token endpoint.
///
/// The transport calls `issue_token()` when a POST /token request arrives.
/// The handler validates the grant, exchanges authorization codes or refresh
/// tokens, and returns a token set.
class TokenEndpointHandler {
 public:
  virtual ~TokenEndpointHandler() = default;

  /// @brief Process a token request.
  ///
  /// @param params Parsed form-encoded POST body parameters.
  /// @return A token set on success, or an error.
  virtual core::Result<TokenSet> issue_token(
      const TokenRequestParams& params) = 0;
};

/// @brief Abstract handler for the dynamic client registration endpoint.
///
/// The transport calls `register_client()` when a POST /register request
/// arrives. The handler validates the request, creates a client record,
/// and returns the registration response.
class ClientRegistrationEndpointHandler {
 public:
  virtual ~ClientRegistrationEndpointHandler() = default;

  /// @brief Process a client registration request.
  ///
  /// @param request Parsed registration request body.
  /// @return A registration response on success, or an error.
  virtual core::Result<ClientRegistrationResponse> register_client(
      const ClientRegistrationRequest& request) = 0;
};

/// @brief Parameters parsed from a revocation request (RFC 7009).
struct TokenRevocationParams {
  std::string token;
  std::optional<std::string> token_type_hint;
  std::optional<std::string> client_id;
  std::optional<std::string> client_secret;
};

/// @brief Abstract handler for the token revocation endpoint.
///
/// The transport calls `revoke_token()` when a POST /revoke request arrives.
class TokenRevocationEndpointHandler {
 public:
  virtual ~TokenRevocationEndpointHandler() = default;

  /// @brief Process a token revocation request.
  ///
  /// @param params Parsed form-encoded POST body parameters.
  /// @return Unit on success (even if the token was already invalid).
  virtual core::Result<core::Unit> revoke_token(
      const TokenRevocationParams& params) = 0;
};

/// @brief Token endpoint response serialized by an authorization server.
///
/// This is intentionally separate from TokenSet. TokenSet is the client-side
/// credential state model; TokenEndpointResponse is the HTTP response contract
/// for an authorization server token endpoint.
struct TokenEndpointResponse {
  std::string access_token;
  std::string token_type = "Bearer";
  std::optional<std::string> refresh_token;
  std::optional<std::chrono::seconds> expires_in;
  std::optional<std::string> scope;
  MetadataMap metadata;
};

/// @brief Function callback for the authorization endpoint.
using AuthorizationEndpointCallback =
    std::function<core::Result<AuthorizationDecision>(
        const AuthorizationRequestParams&)>;

/// @brief Function callback for the token endpoint.
using TokenEndpointCallback = std::function<core::Result<TokenEndpointResponse>(
    const TokenRequestParams&)>;

/// @brief Function callback for dynamic client registration.
using ClientRegistrationEndpointCallback =
    std::function<core::Result<ClientRegistrationResponse>(
        const ClientRegistrationRequest&)>;

/// @brief Function callback for token revocation.
using TokenRevocationEndpointCallback =
    std::function<core::Result<core::Unit>(const TokenRevocationParams&)>;

/// @brief OAuth authorization server endpoint wiring owned by the auth layer.
///
/// HTTP transports may use this configuration to expose RFC 8414 metadata and
/// OAuth endpoint routes, but authorization decisions, token minting,
/// registration policy, and revocation remain application-owned callbacks.
struct AuthorizationServerConfig {
  std::string issuer;
  std::string authorize_path = "/authorize";
  std::string token_path = "/token";
  std::string registration_path = "/register";
  std::string revocation_path = "/revoke";
  ScopeList scopes_supported;
  StringList grant_types_supported = {"authorization_code", "refresh_token"};
  StringList response_types_supported = {"code"};
  StringList code_challenge_methods_supported = {"S256"};
  StringList token_endpoint_auth_methods_supported = {"none"};
  std::optional<std::string> jwks_uri;
  std::optional<std::string> service_documentation;
  AuthorizationEndpointCallback authorization_handler;
  TokenEndpointCallback token_handler;
  ClientRegistrationEndpointCallback registration_handler;
  TokenRevocationEndpointCallback revocation_handler;
  MetadataMap metadata;
};

}  // namespace mcp::auth
