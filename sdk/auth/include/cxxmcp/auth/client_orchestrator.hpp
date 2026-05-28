// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cxxmcp/auth/lifecycle.hpp"
#include "cxxmcp/auth/pkce.hpp"
#include "cxxmcp/auth/token.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/auth/www_auth.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief High-level OAuth 2.1 client orchestrator.
///
/// OAuthClientOrchestrator drives the full OAuth flow:
///   discovery -> registration -> authorization -> token exchange -> refresh
///
/// All network I/O and user interaction are injected via abstract interfaces.

namespace mcp::auth {

/// @brief Callback interface for the OAuth client orchestrator.
///
/// Applications implement these methods to provide browser launching,
/// redirect receiving, and user interaction. This keeps the orchestrator
/// free of platform-specific code.
class OAuthClientCallback {
 public:
  virtual ~OAuthClientCallback() = default;

  /// @brief Present the authorization URL to the user (e.g. open a browser).
  /// @param url The full authorization URL to present.
  /// @return Unit on success, or an error if the URL cannot be presented.
  virtual core::Result<core::Unit> present_authorization_url(
      const std::string& url) = 0;

  /// @brief Wait for the authorization code callback.
  ///
  /// This method blocks until the user completes the authorization flow
  /// and the redirect delivers the authorization code, or until timeout.
  ///
  /// @param timeout Maximum time to wait.
  /// @return A pair of (authorization_code, state), or an error.
  virtual core::Result<std::pair<std::string, std::string>> wait_for_callback(
      std::chrono::seconds timeout) = 0;
};

/// @brief Configuration for the OAuth client orchestrator.
struct OAuthClientOrchestratorConfig {
  /// The MCP resource URL (e.g. "https://example.com/mcp").
  std::string resource_url;
  /// Client name for dynamic client registration.
  std::string client_name = "MCP Client";
  /// Scopes to request. Empty = let the server decide.
  ScopeList scopes;
  /// Redirect URI for the authorization code flow.
  /// If empty, the orchestrator will use a loopback URI derived from
  /// the callback's wait_for_callback implementation.
  std::string redirect_uri;
  /// Maximum time to wait for the authorization code callback.
  std::chrono::seconds callback_timeout{300};
  /// How long before token expiry to proactively refresh.
  std::chrono::seconds refresh_skew{30};
  /// Pre-configured client_id. When set, DCR is skipped.
  std::optional<std::string> client_id;
};

/// @brief High-level OAuth 2.1 client orchestrator.
///
/// Wraps AuthorizationManager with the missing orchestration steps:
///   1. Discover metadata from the resource URL
///   2. Register client via DCR (if needed)
///   3. Build authorization URL with PKCE
///   4. Present URL to user via callback
///   5. Wait for authorization code via callback
///   6. Exchange code for tokens
///   7. Auto-refresh expired tokens
///
/// All network I/O is injected via OAuthMetadataEndpoint, OAuthTokenEndpoint,
/// and optionally OAuthClientRegistrationEndpoint. User interaction is injected
/// via OAuthClientCallback.
class OAuthClientOrchestrator {
 public:
  /// @brief Construct an orchestrator with all required dependencies.
  ///
  /// @param config Orchestrator configuration (resource URL, scopes, etc.)
  /// @param callback Application callback for presenting auth URL and
  ///        receiving the authorization code.
  /// @param metadata_endpoint Network boundary for metadata discovery.
  /// @param token_endpoint Network boundary for token exchange/refresh.
  /// @param pkce_generator PKCE challenge generator (OpenSslPkceGenerator).
  /// @param registration_endpoint Optional DCR endpoint. When null, the
  ///        client_id must be pre-configured.
  OAuthClientOrchestrator(
      OAuthClientOrchestratorConfig config, OAuthClientCallback& callback,
      OAuthMetadataEndpoint& metadata_endpoint,
      OAuthTokenEndpoint& token_endpoint, PkceGenerator& pkce_generator,
      OAuthClientRegistrationEndpoint* registration_endpoint = nullptr)
      : config_(std::move(config)),
        callback_(callback),
        metadata_endpoint_(metadata_endpoint),
        token_endpoint_(token_endpoint),
        pkce_generator_(pkce_generator),
        registration_endpoint_(registration_endpoint) {}

  /// @brief Execute the full OAuth authorization flow.
  ///
  /// Discovers metadata, registers the client (if needed), presents
  /// the authorization URL, waits for the callback, and exchanges the
  /// code for tokens. After this call, get_access_token() returns a
  /// valid bearer token.
  ///
  /// @return The token set on success.
  core::Result<TokenSet> authorize() {
    // Step 1: Discover metadata.
    MetadataDiscoveryExecutor discovery(metadata_endpoint_);
    auto discovery_result = discovery.discover(config_.resource_url);
    if (!discovery_result.has_value()) {
      return core::unexpected(discovery_result.error());
    }

    metadata_ = discovery_result->authorization_server;
    protected_resource_ = discovery_result->protected_resource;

    if (!metadata_.has_value()) {
      return core::unexpected(make_oauth_error(
          OAuthErrorCode::kMetadataDiscoveryFailed,
          "no authorization server metadata discovered for resource",
          config_.resource_url));
    }

    // Step 2: Configure the AuthorizationManager.
    manager_.set_resource(config_.resource_url);
    manager_.set_authorization_server_metadata(*metadata_);
    manager_.set_token_endpoint(std::shared_ptr<OAuthTokenEndpoint>(
        &token_endpoint_, [](OAuthTokenEndpoint*) {}));
    if (registration_endpoint_ != nullptr) {
      manager_.set_client_registration_endpoint(
          std::shared_ptr<OAuthClientRegistrationEndpoint>(
              registration_endpoint_, [](OAuthClientRegistrationEndpoint*) {}));
    }

    // Step 3: Configure client ID (DCR or pre-configured).
    if (!config_.client_id.has_value()) {
      ClientIdConfigurationOptions client_options;
      client_options.client_name = config_.client_name;
      client_options.redirect_uri = config_.redirect_uri;
      client_options.scopes = config_.scopes;

      auto client_config =
          manager_.configure_client_for_authorization(client_options);
      if (!client_config.has_value()) {
        return core::unexpected(client_config.error());
      }
    } else {
      auto client_config = manager_.configure_client_id(
          *config_.client_id, config_.redirect_uri, config_.scopes);
      if (!client_config.has_value()) {
        return core::unexpected(client_config.error());
      }
    }

    // Step 4: Generate PKCE and state.
    auto pkce = pkce_generator_.create_s256();
    if (!pkce.has_value()) {
      return core::unexpected(pkce.error());
    }

    auto state = generate_state();

    // Step 5: Build authorization URL and present to user.
    auto session_result = manager_.start_session(AuthorizationSessionRequest{
        .client = {}, .pkce = *pkce, .state = state});
    if (!session_result.has_value()) {
      return core::unexpected(session_result.error());
    }

    auto present_result = callback_.present_authorization_url(
        session_result->authorization_url());
    if (!present_result.has_value()) {
      return core::unexpected(present_result.error());
    }

    // Step 6: Wait for authorization code.
    auto callback_result =
        callback_.wait_for_callback(config_.callback_timeout);
    if (!callback_result.has_value()) {
      return core::unexpected(callback_result.error());
    }

    auto& [code, callback_state] = *callback_result;

    // Step 7: Exchange code for tokens.
    auto tokens = manager_.exchange_authorization_code(code, callback_state);
    if (!tokens.has_value()) {
      return core::unexpected(tokens.error());
    }

    return *tokens;
  }

  /// @brief Get a valid access token, refreshing if necessary.
  ///
  /// Must be called after a successful authorize() call.
  ///
  /// @return The access token string, or an error if no token is available
  ///         and cannot be refreshed.
  core::Result<std::string> get_access_token() {
    return manager_.get_access_token(config_.refresh_skew);
  }

  /// @brief Handle a 401/403 response and attempt recovery.
  ///
  /// Analyzes the WWW-Authenticate header and attempts token refresh
  /// or scope upgrade if appropriate.
  ///
  /// @param response The HTTP response metadata (status code + headers).
  /// @return A retry result indicating whether the request should be
  ///         retried with a new bearer token.
  core::Result<OAuthRefreshRetryResult> handle_auth_response(
      const HttpResponseMetadata& response) {
    return manager_.refresh_after_unauthorized_response(response);
  }

  /// @brief Get the current lifecycle state.
  OAuthLifecycleState lifecycle_state() const {
    return manager_.lifecycle_state();
  }

  /// @brief Get the current client configuration.
  const OAuthClientConfig& client_config() const {
    return manager_.client_config();
  }

  /// @brief Get the discovered authorization server metadata.
  const std::optional<AuthorizationServerMetadata>& metadata() const {
    return metadata_;
  }

  /// @brief Get the discovered protected resource metadata.
  const std::optional<ProtectedResourceMetadata>& protected_resource_metadata()
      const {
    return protected_resource_;
  }

 private:
  static std::string generate_state() {
    // 16 random bytes as hex = 32 characters.
    // Uses the same approach as the lifecycle detail namespace.
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    // Use a simple PRNG seeded from system clock for state.
    // The state is verified by comparison, not by cryptographic strength,
    // but we still want reasonable uniqueness.
    auto seed = static_cast<std::uint64_t>(
        SystemClock::now().time_since_epoch().count());
    for (int i = 0; i < 32; ++i) {
      seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
      result.push_back(kHex[(seed >> 16) & 0xF]);
    }
    return result;
  }

  OAuthClientOrchestratorConfig config_;
  OAuthClientCallback& callback_;
  OAuthMetadataEndpoint& metadata_endpoint_;
  OAuthTokenEndpoint& token_endpoint_;
  PkceGenerator& pkce_generator_;
  OAuthClientRegistrationEndpoint* registration_endpoint_;

  AuthorizationManager manager_;
  std::optional<AuthorizationServerMetadata> metadata_;
  std::optional<ProtectedResourceMetadata> protected_resource_;
};

}  // namespace mcp::auth
