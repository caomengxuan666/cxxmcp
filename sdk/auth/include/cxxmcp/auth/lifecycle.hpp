// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/auth/constant_time.hpp"
#include "cxxmcp/auth/metadata.hpp"
#include "cxxmcp/auth/pkce.hpp"
#include "cxxmcp/auth/registration.hpp"
#include "cxxmcp/auth/token.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/auth/www_auth.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief OAuth authorization lifecycle contracts and lightweight state logic.

namespace mcp::auth {

/// @brief OAuth lifecycle error codes used inside the stable "auth" category.
enum class OAuthErrorCode {
  kInvalidRequest = 1,
  kAuthorizationRequired = 2,
  kAuthorizationPending = 3,
  kTokenExchangeUnavailable = 4,
  kTokenExchangeFailed = 5,
  kTokenRefreshFailed = 6,
  kInsufficientScope = 7,
  kMetadataDiscoveryFailed = 8,
  kClientRegistrationUnavailable = 9,
  kClientRegistrationFailed = 10,
  kClientMetadataDocumentUnsupported = 11,
  kClientMetadataDocumentInvalid = 12,
};

/// @brief Build an auth-category lifecycle error.
inline core::Error make_oauth_error(OAuthErrorCode code, std::string message,
                                    std::string detail = {}) {
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail), std::string(AuthErrorCategory)};
}

/// @brief Stable key for OAuth credential storage.
struct CredentialKey {
  std::string resource;
  std::string issuer;
  std::string client_id;
  MetadataMap attributes;
};

/// @brief Stored OAuth credentials and granted scope bookkeeping.
struct StoredCredentials {
  std::string client_id;
  std::optional<TokenSet> token_set;
  ScopeList granted_scopes;
  std::optional<TimePoint> token_received_at;
  MetadataMap metadata;
};

/// @brief Application-provided OAuth credential persistence boundary.
class CredentialStore {
 public:
  virtual ~CredentialStore() = default;

  virtual core::Result<std::optional<StoredCredentials>> load(
      const CredentialKey& key) = 0;
  virtual core::Result<core::Unit> save(const CredentialKey& key,
                                        StoredCredentials credentials) = 0;
  virtual core::Result<core::Unit> clear(const CredentialKey& key) = 0;
};

/// @brief Non-persistent credential store for tests and simple clients.
class InMemoryCredentialStore final : public CredentialStore {
 public:
  core::Result<std::optional<StoredCredentials>> load(
      const CredentialKey& key) override {
    const auto iter = find_entry(key);
    if (iter == entries_.end()) {
      return std::optional<StoredCredentials>{};
    }
    return iter->second;
  }

  core::Result<core::Unit> save(const CredentialKey& key,
                                StoredCredentials credentials) override {
    auto iter = find_entry(key);
    if (iter == entries_.end()) {
      entries_.emplace_back(key, std::move(credentials));
    } else {
      iter->second = std::move(credentials);
    }
    return core::Unit{};
  }

  core::Result<core::Unit> clear(const CredentialKey& key) override {
    auto iter = find_entry(key);
    if (iter != entries_.end()) {
      entries_.erase(iter);
    }
    return core::Unit{};
  }

 private:
  using Entry = std::pair<CredentialKey, StoredCredentials>;

  static bool matches(const MetadataMap& lhs, const MetadataMap& rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    bool equal = true;
    auto lhs_iter = lhs.begin();
    auto rhs_iter = rhs.begin();
    for (; lhs_iter != lhs.end(); ++lhs_iter, ++rhs_iter) {
      equal = constant_time_string_equal(lhs_iter->first, rhs_iter->first) &
              constant_time_string_equal(lhs_iter->second, rhs_iter->second) &
              equal;
    }
    return equal;
  }

  static bool matches(const CredentialKey& lhs, const CredentialKey& rhs) {
    return constant_time_string_equal(lhs.resource, rhs.resource) &
           constant_time_string_equal(lhs.issuer, rhs.issuer) &
           constant_time_string_equal(lhs.client_id, rhs.client_id) &
           matches(lhs.attributes, rhs.attributes);
  }

  std::vector<Entry>::iterator find_entry(const CredentialKey& key) {
    for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
      if (matches(iter->first, key)) {
        return iter;
      }
    }
    return entries_.end();
  }

  std::vector<Entry>::const_iterator find_entry(
      const CredentialKey& key) const {
    for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
      if (matches(iter->first, key)) {
        return iter;
      }
    }
    return entries_.end();
  }

  std::vector<Entry> entries_;
};

/// @brief Stored one-time authorization state for OAuth code + PKCE flows.
struct StoredAuthorizationState {
  std::string state;
  PkceChallenge pkce;
  TimePoint created_at = SystemClock::now();
  std::string resource;
  std::string client_id;
  std::string redirect_uri;
  ScopeList requested_scopes;
  MetadataMap metadata;
};

inline constexpr std::chrono::seconds kDefaultAuthorizationStateTtl{60};

/// @brief Application-provided authorization state persistence boundary.
class StateStore {
 public:
  virtual ~StateStore() = default;

  virtual core::Result<core::Unit> save(std::string state,
                                        StoredAuthorizationState value) = 0;
  virtual core::Result<std::optional<StoredAuthorizationState>> load(
      const std::string& state) = 0;
  virtual core::Result<core::Unit> remove(const std::string& state) = 0;
};

/// @brief Non-persistent authorization state store.
class InMemoryStateStore final : public StateStore {
 public:
  core::Result<core::Unit> save(std::string state,
                                StoredAuthorizationState value) override {
    auto iter = find_entry(state);
    if (iter == entries_.end()) {
      entries_.emplace_back(std::move(state), std::move(value));
    } else {
      iter->second = std::move(value);
    }
    return core::Unit{};
  }

  core::Result<std::optional<StoredAuthorizationState>> load(
      const std::string& state) override {
    const auto iter = find_entry(state);
    if (iter == entries_.end()) {
      return std::optional<StoredAuthorizationState>{};
    }
    return iter->second;
  }

  core::Result<core::Unit> remove(const std::string& state) override {
    auto iter = find_entry(state);
    if (iter != entries_.end()) {
      entries_.erase(iter);
    }
    return core::Unit{};
  }

 private:
  using Entry = std::pair<std::string, StoredAuthorizationState>;

  std::vector<Entry>::iterator find_entry(const std::string& state) {
    for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
      if (constant_time_string_equal(iter->first, state)) {
        return iter;
      }
    }
    return entries_.end();
  }

  std::vector<Entry>::const_iterator find_entry(
      const std::string& state) const {
    for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
      if (constant_time_string_equal(iter->first, state)) {
        return iter;
      }
    }
    return entries_.end();
  }

  std::vector<Entry> entries_;
};

/// @brief Authorization URL construction input.
struct AuthorizationUrlRequest {
  OAuthClientConfig client;
  AuthorizationServerMetadata authorization_server;
  std::string resource;
  ScopeList scopes;
  PkceChallenge pkce;
  std::string state;
  MetadataMap additional_parameters;
};

/// @brief Authorization URL plus the state that must be stored for callback.
struct AuthorizationUrlResult {
  std::string url;
  StoredAuthorizationState state;
};

/// @brief Token endpoint exchange input. Implementations perform network I/O.
struct TokenExchangeRequest {
  OAuthClientConfig client;
  AuthorizationServerMetadata authorization_server;
  std::string resource;
  std::string authorization_code;
  StoredAuthorizationState state;
  MetadataMap additional_parameters;
};

/// @brief Token endpoint refresh input. Implementations perform network I/O.
struct TokenRefreshRequest {
  OAuthClientConfig client;
  AuthorizationServerMetadata authorization_server;
  std::string resource;
  std::string refresh_token;
  ScopeList scopes;
  MetadataMap additional_parameters;
};

/// @brief Token exchange and refresh network boundary.
///
/// The SDK scaffold owns lifecycle state, but does not provide fake HTTP or
/// crypto. Production code supplies an implementation behind this interface.
class OAuthTokenEndpoint {
 public:
  virtual ~OAuthTokenEndpoint() = default;

  virtual core::Result<TokenSet> exchange_authorization_code(
      const TokenExchangeRequest& request) = 0;
  virtual core::Result<TokenRefreshResult> refresh_access_token(
      const TokenRefreshRequest& request) = 0;
};

/// @brief Runtime state for the interactive OAuth lifecycle.
enum class OAuthLifecycleState {
  kUnauthorized,
  kAuthorizationPending,
  kAuthorized,
};

/// @brief Scope upgrade policy used after insufficient_scope challenges.
struct ScopeUpgradeConfig {
  bool auto_upgrade = true;
  std::uint32_t max_upgrade_attempts = 3;
};

/// @brief Parsed client action from HTTP auth response metadata.
enum class AuthResponseAction {
  kProceed,
  kAuthorizationRequired,
  kScopeUpgradeRequired,
};

/// @brief Auth response decision derived from status and WWW-Authenticate.
struct AuthResponseDecision {
  AuthResponseAction action = AuthResponseAction::kProceed;
  std::optional<WwwAuthenticateChallenge> challenge;
  ScopeList required_scopes;
  std::optional<std::string> resource_metadata_url;
  std::optional<std::string> error_description;
};

/// @brief Result of evaluating an HTTP auth response for one-shot retry.
struct OAuthRefreshRetryResult {
  AuthResponseDecision decision;
  bool should_retry = false;
  std::optional<std::string> bearer_token;
};

/// @brief Metadata fetch request routed through application/transport code.
struct MetadataFetchRequest {
  std::string url;
  HeaderMap headers;
};

/// @brief OAuth metadata network boundary.
///
/// Implementations may use the SDK HTTP transport, an application HTTP stack,
/// or tests. The auth public API does not expose a concrete HTTP library.
class OAuthMetadataEndpoint {
 public:
  virtual ~OAuthMetadataEndpoint() = default;

  virtual core::Result<ProtectedResourceMetadata>
  fetch_protected_resource_metadata(const MetadataFetchRequest& request) = 0;

  virtual core::Result<AuthorizationServerMetadata>
  fetch_authorization_server_metadata(const MetadataFetchRequest& request) = 0;
};

/// @brief Candidate metadata URLs for an MCP Streamable HTTP resource.
struct MetadataDiscoveryPlan {
  StringList protected_resource_metadata_urls;
  StringList authorization_server_metadata_urls;
};

/// @brief Options for metadata discovery execution.
struct MetadataDiscoveryOptions {
  HeaderMap headers;
  StringList authorization_server_metadata_urls;
};

/// @brief Inputs for RMCP-style authorization scope selection.
struct ScopeSelectionContext {
  ScopeList www_authenticate_scopes;
  ScopeList protected_resource_scopes;
  ScopeList authorization_server_scopes;
  ScopeList default_scopes;
};

namespace detail {

inline std::string oauth_url_encode(std::string_view value) {
  std::ostringstream out;
  out << std::uppercase << std::hex;
  for (const auto raw_ch : value) {
    const auto ch = static_cast<unsigned char>(raw_ch);
    if (std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' ||
        ch == '~') {
      out << static_cast<char>(ch);
    } else {
      out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return out.str();
}

inline std::string join_scopes(const ScopeList& scopes) {
  std::string joined;
  for (const auto& scope : scopes) {
    if (scope.empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined.push_back(' ');
    }
    joined.append(scope);
  }
  return joined;
}

inline ScopeList split_scopes(std::string_view scopes) {
  ScopeList result;
  std::size_t pos = 0;
  while (pos < scopes.size()) {
    while (pos < scopes.size() &&
           std::isspace(static_cast<unsigned char>(scopes[pos])) != 0) {
      ++pos;
    }
    const auto begin = pos;
    while (pos < scopes.size() &&
           std::isspace(static_cast<unsigned char>(scopes[pos])) == 0) {
      ++pos;
    }
    if (begin != pos) {
      result.emplace_back(scopes.substr(begin, pos - begin));
    }
  }
  return result;
}

inline std::string pkce_method_name(PkceCodeChallengeMethod method) {
  switch (method) {
    case PkceCodeChallengeMethod::kS256:
      return "S256";
  }
  return "S256";
}

inline void append_query_param(std::string* url, const std::string& name,
                               const std::string& value) {
  url->push_back(url->find('?') == std::string::npos ? '?' : '&');
  url->append(oauth_url_encode(name));
  url->push_back('=');
  url->append(oauth_url_encode(value));
}

inline bool has_scope(const ScopeList& scopes, const std::string& scope) {
  return std::find(scopes.begin(), scopes.end(), scope) != scopes.end();
}

inline void append_unique(StringList* values, std::string value) {
  if (value.empty()) {
    return;
  }
  if (std::find(values->begin(), values->end(), value) == values->end()) {
    values->push_back(std::move(value));
  }
}

inline std::string strip_query_and_fragment(std::string url) {
  const auto fragment = url.find('#');
  if (fragment != std::string::npos) {
    url.erase(fragment);
  }
  const auto query = url.find('?');
  if (query != std::string::npos) {
    url.erase(query);
  }
  return url;
}

inline std::string origin_from_url(std::string_view url) {
  const auto scheme = url.find("://");
  if (scheme == std::string_view::npos) {
    return {};
  }
  const auto authority_start = scheme + 3;
  const auto path_start = url.find('/', authority_start);
  if (path_start == std::string_view::npos) {
    return std::string(url);
  }
  return std::string(url.substr(0, path_start));
}

inline std::string path_from_url(std::string_view url) {
  const auto scheme = url.find("://");
  if (scheme == std::string_view::npos) {
    return {};
  }
  const auto authority_start = scheme + 3;
  const auto path_start = url.find('/', authority_start);
  if (path_start == std::string_view::npos) {
    return "/";
  }
  auto path = std::string(url.substr(path_start));
  const auto query = path.find('?');
  if (query != std::string::npos) {
    path.erase(query);
  }
  const auto fragment = path.find('#');
  if (fragment != std::string::npos) {
    path.erase(fragment);
  }
  return path.empty() ? std::string("/") : path;
}

inline bool url_has_fragment(std::string_view url) {
  return url.find('#') != std::string_view::npos;
}

inline std::string url_scheme(std::string_view url) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return {};
  }
  return std::string(url.substr(0, scheme_end));
}

inline bool url_has_userinfo(std::string_view url) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return false;
  }
  const auto authority_start = scheme_end + 3;
  const auto authority_end = url.find_first_of("/?#", authority_start);
  const auto authority =
      url.substr(authority_start, authority_end == std::string_view::npos
                                      ? std::string_view::npos
                                      : authority_end - authority_start);
  return authority.find('@') != std::string_view::npos;
}

inline std::string url_host(std::string_view url) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return {};
  }
  const auto authority_start = scheme_end + 3;
  const auto authority_end = url.find_first_of("/?#", authority_start);
  auto authority =
      url.substr(authority_start, authority_end == std::string_view::npos
                                      ? std::string_view::npos
                                      : authority_end - authority_start);
  const auto userinfo = authority.rfind('@');
  if (userinfo != std::string_view::npos) {
    authority.remove_prefix(userinfo + 1);
  }
  if (authority.empty()) {
    return {};
  }
  if (authority.front() == '[') {
    const auto bracket = authority.find(']');
    if (bracket == std::string_view::npos) {
      return {};
    }
    return std::string(authority.substr(1, bracket - 1));
  }
  const auto port = authority.find(':');
  return std::string(authority.substr(0, port));
}

inline bool url_uses_https(std::string_view url) {
  return ascii_iequals(url_scheme(url), "https");
}

inline bool url_uses_loopback_http(std::string_view url) {
  if (!ascii_iequals(url_scheme(url), "http")) {
    return false;
  }
  const auto host = url_host(url);
  return ascii_iequals(host, "localhost") || host == "127.0.0.1" ||
         host == "::1";
}

inline bool redirect_uri_is_secure(std::string_view url) {
  return url_uses_https(url) || url_uses_loopback_http(url);
}

inline bool metadata_discovery_url_is_safe(std::string_view url) {
  if (url.empty() || url_has_fragment(url) || url_has_userinfo(url)) {
    return false;
  }
  if (!url_uses_https(url) && !url_uses_loopback_http(url)) {
    return false;
  }
  if (url_host(url).empty()) {
    return false;
  }
  if (url_contains_dot_segment(path_from_url(url))) {
    return false;
  }
  return true;
}

inline std::string trim_leading_slash(std::string value) {
  while (!value.empty() && value.front() == '/') {
    value.erase(value.begin());
  }
  return value;
}

inline std::optional<std::string> header_value(const HeaderMap& headers,
                                               std::string_view name) {
  for (const auto& entry : headers) {
    if (ascii_iequals(entry.first, name)) {
      return entry.second;
    }
  }
  return std::nullopt;
}

}  // namespace detail

/// @brief Build RFC 9728 protected-resource metadata candidates.
///
/// A `resource_metadata` value from `WWW-Authenticate` is tried first, then
/// well-known paths derived from the resource URL. This mirrors the RMCP
/// discovery order while keeping actual HTTP I/O outside the SDK model layer.
inline StringList build_protected_resource_metadata_urls(
    const std::string& resource_url,
    std::optional<std::string> challenged_resource_metadata_url =
        std::nullopt) {
  StringList urls;
  if (challenged_resource_metadata_url.has_value() &&
      detail::metadata_discovery_url_is_safe(
          *challenged_resource_metadata_url)) {
    detail::append_unique(&urls, detail::strip_query_and_fragment(std::move(
                                     *challenged_resource_metadata_url)));
  }

  if (!detail::metadata_discovery_url_is_safe(resource_url)) {
    return urls;
  }
  const auto normalized_resource =
      detail::strip_query_and_fragment(resource_url);
  const auto origin = detail::origin_from_url(normalized_resource);
  if (origin.empty()) {
    return urls;
  }

  const auto path = detail::path_from_url(normalized_resource);
  if (path != "/") {
    detail::append_unique(&urls, origin +
                                     "/.well-known/oauth-protected-resource/" +
                                     detail::trim_leading_slash(path));
  }
  detail::append_unique(&urls,
                        origin + "/.well-known/oauth-protected-resource");
  return urls;
}

/// @brief Build RFC 8414 authorization-server metadata candidates.
inline StringList build_authorization_server_metadata_urls(
    const std::string& issuer_or_base_url) {
  StringList urls;
  if (!detail::metadata_discovery_url_is_safe(issuer_or_base_url)) {
    return urls;
  }
  const auto normalized = detail::strip_query_and_fragment(issuer_or_base_url);
  const auto origin = detail::origin_from_url(normalized);
  if (origin.empty()) {
    return urls;
  }

  const auto path = detail::path_from_url(normalized);
  if (path != "/") {
    detail::append_unique(&urls,
                          origin + "/.well-known/oauth-authorization-server/" +
                              detail::trim_leading_slash(path));
  }
  detail::append_unique(&urls,
                        origin + "/.well-known/oauth-authorization-server");
  return urls;
}

/// @brief Build the metadata discovery plan from a resource URL and auth hint.
inline MetadataDiscoveryPlan build_metadata_discovery_plan(
    const std::string& resource_url,
    const AuthResponseDecision& decision = AuthResponseDecision{}) {
  MetadataDiscoveryPlan plan;
  plan.protected_resource_metadata_urls =
      build_protected_resource_metadata_urls(resource_url,
                                             decision.resource_metadata_url);
  return plan;
}

/// @brief Execute protected-resource and authorization-server discovery.
///
/// The executor owns RMCP-style ordering and fallback behavior, while the
/// supplied OAuthMetadataEndpoint owns the concrete HTTP/transport I/O.
class MetadataDiscoveryExecutor {
 public:
  explicit MetadataDiscoveryExecutor(OAuthMetadataEndpoint& endpoint)
      : endpoint_(endpoint) {}

  core::Result<MetadataDiscoveryResult> discover(
      const std::string& resource_url,
      const AuthResponseDecision& decision = AuthResponseDecision{},
      MetadataDiscoveryOptions options = {}) {
    const auto plan = build_metadata_discovery_plan(resource_url, decision);
    if (plan.protected_resource_metadata_urls.empty()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kInvalidRequest,
          "resource URL cannot produce metadata candidates", resource_url));
    }

    std::optional<core::Error> last_error;
    std::optional<ProtectedResourceMetadata> protected_resource;
    for (const auto& url : plan.protected_resource_metadata_urls) {
      auto fetched = endpoint_.fetch_protected_resource_metadata(
          MetadataFetchRequest{url, options.headers});
      if (fetched.has_value()) {
        protected_resource = std::move(*fetched);
        break;
      }
      last_error = fetched.error();
    }
    if (!protected_resource.has_value()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kMetadataDiscoveryFailed,
          "protected resource metadata discovery failed",
          last_error.has_value() ? last_error->message : std::string{}));
    }

    MetadataDiscoveryResult result;
    result.protected_resource = std::move(*protected_resource);

    StringList authorization_urls = options.authorization_server_metadata_urls;
    for (const auto& server : result.protected_resource.authorization_servers) {
      for (auto candidate : build_authorization_server_metadata_urls(server)) {
        detail::append_unique(&authorization_urls, std::move(candidate));
      }
    }
    if (authorization_urls.empty()) {
      return result;
    }

    last_error.reset();
    for (const auto& url : authorization_urls) {
      auto fetched = endpoint_.fetch_authorization_server_metadata(
          MetadataFetchRequest{url, options.headers});
      if (fetched.has_value()) {
        result.authorization_server = std::move(*fetched);
        return result;
      }
      last_error = fetched.error();
    }

    return mcp::core::unexpected(make_oauth_error(
        OAuthErrorCode::kMetadataDiscoveryFailed,
        "authorization server metadata discovery failed",
        last_error.has_value() ? last_error->message : std::string{}));
  }

 private:
  OAuthMetadataEndpoint& endpoint_;
};

/// @brief Select scopes using the RMCP priority order.
///
/// Order: WWW-Authenticate scope, protected-resource metadata,
/// authorization-server metadata, then application defaults.
inline ScopeList select_authorization_scopes(
    const ScopeSelectionContext& context) {
  if (!context.www_authenticate_scopes.empty()) {
    return context.www_authenticate_scopes;
  }
  if (!context.protected_resource_scopes.empty()) {
    return context.protected_resource_scopes;
  }
  if (!context.authorization_server_scopes.empty()) {
    return context.authorization_server_scopes;
  }
  return context.default_scopes;
}

/// @brief User-facing authorization session for an active code flow.
struct OAuthSession {
  AuthorizationUrlResult authorization;
  std::string redirect_uri;

  const std::string& authorization_url() const { return authorization.url; }
  const std::string& state() const { return authorization.state.state; }
  const ScopeList& requested_scopes() const {
    return authorization.state.requested_scopes;
  }
};

/// @brief Full client-id selection plus authorization URL request.
///
/// Browser launching and loopback redirect receivers are deliberately absent.
/// Applications use this value to create an authorization URL, then perform
/// their own user interaction and callback handling.
struct AuthorizationSessionRequest {
  ClientIdConfigurationOptions client;
  PkceChallenge pkce;
  std::string state;
  MetadataMap additional_authorization_parameters;
};

/// @brief Build an OAuth authorization URL without performing network I/O.
inline core::Result<AuthorizationUrlResult> build_authorization_url(
    const AuthorizationUrlRequest& request) {
  if (request.authorization_server.authorization_endpoint.empty()) {
    return mcp::core::unexpected(make_oauth_error(
        OAuthErrorCode::kInvalidRequest, "authorization endpoint is required"));
  }
  if (request.client.client_id.empty()) {
    return mcp::core::unexpected(make_oauth_error(
        OAuthErrorCode::kInvalidRequest, "client_id is required"));
  }
  if (request.client.redirect_uri.empty()) {
    return mcp::core::unexpected(make_oauth_error(
        OAuthErrorCode::kInvalidRequest, "redirect_uri is required"));
  }
  if (!detail::url_uses_https(
          request.authorization_server.authorization_endpoint)) {
    return mcp::core::unexpected(
        make_oauth_error(OAuthErrorCode::kInvalidRequest,
                         "authorization endpoint must use HTTPS"));
  }
  if (!request.authorization_server.token_endpoint.empty() &&
      !detail::url_uses_https(request.authorization_server.token_endpoint)) {
    return mcp::core::unexpected(make_oauth_error(
        OAuthErrorCode::kInvalidRequest, "token endpoint must use HTTPS"));
  }
  if (!detail::redirect_uri_is_secure(request.client.redirect_uri)) {
    return mcp::core::unexpected(
        make_oauth_error(OAuthErrorCode::kInvalidRequest,
                         "redirect_uri must use HTTPS or loopback HTTP"));
  }
  if (request.state.empty()) {
    return mcp::core::unexpected(
        make_oauth_error(OAuthErrorCode::kInvalidRequest, "state is required"));
  }
  if (request.pkce.code_challenge.empty() ||
      request.pkce.code_verifier.empty()) {
    return mcp::core::unexpected(
        make_oauth_error(OAuthErrorCode::kInvalidRequest,
                         "PKCE verifier and challenge are required"));
  }
  auto url = request.authorization_server.authorization_endpoint;
  detail::append_query_param(&url, "response_type", "code");
  detail::append_query_param(&url, "client_id", request.client.client_id);
  detail::append_query_param(&url, "redirect_uri", request.client.redirect_uri);
  detail::append_query_param(&url, "state", request.state);
  detail::append_query_param(&url, "code_challenge",
                             request.pkce.code_challenge);
  detail::append_query_param(&url, "code_challenge_method",
                             detail::pkce_method_name(request.pkce.method));
  if (!request.resource.empty()) {
    detail::append_query_param(&url, "resource", request.resource);
  }
  const auto scope = detail::join_scopes(request.scopes);
  if (!scope.empty()) {
    detail::append_query_param(&url, "scope", scope);
  }
  for (const auto& parameter : request.additional_parameters) {
    detail::append_query_param(&url, parameter.first, parameter.second);
  }

  StoredAuthorizationState stored;
  stored.state = request.state;
  stored.pkce = request.pkce;
  stored.resource = request.resource;
  stored.client_id = request.client.client_id;
  stored.redirect_uri = request.client.redirect_uri;
  stored.requested_scopes = request.scopes;

  return AuthorizationUrlResult{std::move(url), std::move(stored)};
}

/// @brief Analyze status and WWW-Authenticate metadata for OAuth next action.
inline core::Result<AuthResponseDecision> analyze_auth_response(
    const HttpResponseMetadata& response) {
  AuthResponseDecision decision;
  if (response.status_code != 401 && response.status_code != 403) {
    return decision;
  }

  const auto header =
      detail::header_value(response.headers, "WWW-Authenticate");
  if (!header.has_value()) {
    decision.action = response.status_code == 401
                          ? AuthResponseAction::kAuthorizationRequired
                          : AuthResponseAction::kScopeUpgradeRequired;
    return decision;
  }

  auto parsed = parse_www_authenticate(*header);
  if (!parsed.has_value()) {
    return mcp::core::unexpected(parsed.error());
  }

  for (const auto& challenge : *parsed) {
    if (!challenge.bearer()) {
      continue;
    }
    decision.challenge = challenge;
    decision.resource_metadata_url = resource_metadata_url(challenge);
    const auto description = challenge.parameter("error_description");
    if (!description.empty()) {
      decision.error_description = description;
    }
    const auto scope =
        challenge.parameter(std::string(WwwAuthenticateScopeParam));
    if (!scope.empty()) {
      decision.required_scopes = detail::split_scopes(scope);
    }
    decision.action = insufficient_scope(challenge)
                          ? AuthResponseAction::kScopeUpgradeRequired
                          : AuthResponseAction::kAuthorizationRequired;
    return decision;
  }

  decision.action = response.status_code == 401
                        ? AuthResponseAction::kAuthorizationRequired
                        : AuthResponseAction::kScopeUpgradeRequired;
  return decision;
}

/// @brief Transport-neutral OAuth authorization lifecycle manager.
///
/// This class intentionally contains no crypto and no HTTP client. Callers
/// provide PKCE values and an OAuthTokenEndpoint implementation.
class AuthorizationManager {
 public:
  AuthorizationManager() = default;

  AuthorizationManager(std::string resource,
                       AuthorizationServerMetadata metadata,
                       OAuthClientConfig client)
      : resource_(std::move(resource)),
        metadata_(std::move(metadata)),
        client_(std::move(client)) {}

  void set_resource(std::string resource) { resource_ = std::move(resource); }
  void set_authorization_server_metadata(AuthorizationServerMetadata metadata) {
    metadata_ = std::move(metadata);
  }
  void configure_client(OAuthClientConfig client) {
    client_ = std::move(client);
  }
  void set_credential_store(std::shared_ptr<CredentialStore> store) {
    credential_store_ = std::move(store);
  }
  void set_state_store(std::shared_ptr<StateStore> store) {
    state_store_ = std::move(store);
  }
  void set_token_endpoint(std::shared_ptr<OAuthTokenEndpoint> endpoint) {
    token_endpoint_ = std::move(endpoint);
  }
  void set_client_registration_endpoint(
      std::shared_ptr<OAuthClientRegistrationEndpoint> endpoint) {
    registration_endpoint_ = std::move(endpoint);
  }
  void set_scope_upgrade_config(ScopeUpgradeConfig config) {
    scope_upgrade_config_ = config;
  }
  void set_authorization_state_ttl(std::chrono::seconds ttl) {
    authorization_state_ttl_ = ttl;
  }

  OAuthLifecycleState lifecycle_state() const { return state_; }
  const OAuthClientConfig& client_config() const { return client_; }
  const ScopeList& current_scopes() const { return current_scopes_; }
  std::chrono::seconds authorization_state_ttl() const {
    return authorization_state_ttl_;
  }
  std::uint32_t scope_upgrade_attempts() const {
    return scope_upgrade_attempts_;
  }

  CredentialKey credential_key() const {
    return CredentialKey{resource_, metadata_.issuer, client_.client_id, {}};
  }

  core::Result<OAuthClientConfig> configure_client_id(
      std::string client_id, std::string redirect_uri = {},
      ScopeList scopes = {}) {
    if (client_id.empty()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kInvalidRequest, "client_id is required"));
    }
    if (redirect_uri.empty()) {
      redirect_uri = client_.redirect_uri;
    }
    if (redirect_uri.empty()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kInvalidRequest, "redirect_uri is required"));
    }
    OAuthClientConfig config;
    config.client_id = std::move(client_id);
    config.client_secret = client_.client_secret;
    config.redirect_uri = std::move(redirect_uri);
    config.scopes = std::move(scopes);
    configure_client(config);
    return client_;
  }

  core::Result<OAuthClientConfig> configure_client_id_metadata_url(
      std::string client_id_metadata_url, std::string redirect_uri,
      ScopeList scopes = {}) {
    if (!supports_client_id_metadata_document(metadata_)) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kClientMetadataDocumentUnsupported,
          "authorization server does not advertise Client ID Metadata "
          "Document support"));
    }
    if (!is_valid_client_id_metadata_url(client_id_metadata_url)) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kClientMetadataDocumentInvalid,
          "client_id metadata document URL must be HTTPS with a non-root path",
          client_id_metadata_url));
    }
    return configure_client_id(std::move(client_id_metadata_url),
                               std::move(redirect_uri), std::move(scopes));
  }

  core::Result<OAuthClientConfig> register_client(
      ClientRegistrationOptions options, HeaderMap headers = {}) {
    if (!registration_endpoint_) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kClientRegistrationUnavailable,
          "dynamic client registration endpoint is not configured"));
    }
    if (!metadata_.registration_endpoint.has_value() ||
        metadata_.registration_endpoint->empty()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kClientRegistrationUnavailable,
          "authorization server metadata has no registration endpoint"));
    }

    auto registration = build_client_registration_request(options);
    if (!registration.has_value()) {
      return mcp::core::unexpected(registration.error());
    }

    ClientRegistrationEndpointRequest request;
    request.registration_endpoint = *metadata_.registration_endpoint;
    request.headers = std::move(headers);
    request.registration = std::move(*registration);

    auto response = registration_endpoint_->register_client(request);
    if (!response.has_value()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kClientRegistrationFailed,
          "dynamic client registration failed", response.error().message));
    }
    if (response->client_id.empty()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kClientRegistrationFailed,
          "dynamic client registration response did not include client_id"));
    }

    auto config = oauth_client_config_from_registration_response(
        *response, options.redirect_uri, std::move(options.scopes));
    configure_client(config);
    return client_;
  }

  core::Result<OAuthClientConfig> configure_client_for_authorization(
      ClientIdConfigurationOptions options) {
    if (options.client_id_metadata_url.has_value() &&
        supports_client_id_metadata_document(metadata_)) {
      return configure_client_id_metadata_url(*options.client_id_metadata_url,
                                              std::move(options.redirect_uri),
                                              std::move(options.scopes));
    }

    ClientRegistrationOptions registration_options;
    registration_options.client_name = std::move(options.client_name);
    registration_options.redirect_uri = std::move(options.redirect_uri);
    registration_options.scopes = std::move(options.scopes);
    registration_options.metadata = std::move(options.metadata);
    return register_client(std::move(registration_options),
                           std::move(options.headers));
  }

  core::Result<OAuthSession> start_session(
      AuthorizationSessionRequest request) {
    if (client_.client_id.empty() ||
        client_.redirect_uri != request.client.redirect_uri) {
      auto configured = configure_client_for_authorization(request.client);
      if (!configured.has_value()) {
        return mcp::core::unexpected(configured.error());
      }
    }

    auto authorization = start_authorization(
        request.client.scopes, std::move(request.pkce),
        std::move(request.state),
        std::move(request.additional_authorization_parameters));
    if (!authorization.has_value()) {
      return mcp::core::unexpected(authorization.error());
    }
    return OAuthSession{std::move(*authorization), client_.redirect_uri};
  }

  core::Result<AuthorizationUrlResult> start_authorization(
      ScopeList scopes, PkceChallenge pkce, std::string state,
      MetadataMap additional_parameters = {}) {
    AuthorizationUrlRequest request;
    request.client = client_;
    request.authorization_server = metadata_;
    request.resource = resource_;
    request.scopes = std::move(scopes);
    request.pkce = std::move(pkce);
    request.state = std::move(state);
    request.additional_parameters = std::move(additional_parameters);

    auto result = build_authorization_url(request);
    if (!result.has_value()) {
      return mcp::core::unexpected(result.error());
    }

    auto store_result = state_store().save(result->state.state, result->state);
    if (!store_result.has_value()) {
      return mcp::core::unexpected(store_result.error());
    }
    state_ = OAuthLifecycleState::kAuthorizationPending;
    return result;
  }

  core::Result<TokenSet> exchange_authorization_code(
      std::string authorization_code, const std::string& state) {
    if (!token_endpoint_) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kTokenExchangeUnavailable,
                           "OAuth token endpoint is not configured"));
    }

    auto loaded_state = state_store().load(state);
    if (!loaded_state.has_value()) {
      return mcp::core::unexpected(loaded_state.error());
    }
    if (!loaded_state->has_value()) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kAuthorizationRequired,
                           "authorization state was not found"));
    }
    auto remove_result = state_store().remove(state);
    if (!remove_result.has_value()) {
      return mcp::core::unexpected(remove_result.error());
    }

    auto stored_state = std::move(**loaded_state);
    const auto now = SystemClock::now();
    if (authorization_state_ttl_ <= std::chrono::seconds::zero() ||
        stored_state.created_at + authorization_state_ttl_ <= now) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kAuthorizationRequired,
                           "authorization state has expired"));
    }

    TokenExchangeRequest request;
    request.client = client_;
    request.authorization_server = metadata_;
    request.resource = resource_;
    request.authorization_code = std::move(authorization_code);
    request.state = std::move(stored_state);

    auto token_result = token_endpoint_->exchange_authorization_code(request);
    if (!token_result.has_value()) {
      return mcp::core::unexpected(token_result.error());
    }

    current_scopes_ = token_result->scopes.empty()
                          ? request.state.requested_scopes
                          : token_result->scopes;
    scope_upgrade_attempts_ = 0;
    StoredCredentials credentials;
    credentials.client_id = client_.client_id;
    credentials.token_set = *token_result;
    credentials.granted_scopes = current_scopes_;
    credentials.token_received_at = SystemClock::now();
    auto save_result =
        credential_store().save(credential_key(), std::move(credentials));
    if (!save_result.has_value()) {
      return mcp::core::unexpected(save_result.error());
    }

    state_ = OAuthLifecycleState::kAuthorized;
    return *token_result;
  }

  core::Result<TokenRefreshResult> refresh_access_token() {
    if (!token_endpoint_) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kTokenExchangeUnavailable,
                           "OAuth token endpoint is not configured"));
    }

    auto loaded = credential_store().load(credential_key());
    if (!loaded.has_value()) {
      return mcp::core::unexpected(loaded.error());
    }
    if (!loaded->has_value() || !(**loaded).token_set.has_value()) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kAuthorizationRequired,
                           "stored credentials are not available"));
    }

    auto credentials = **loaded;
    const auto& current = *credentials.token_set;
    if (!current.refresh_token.has_value() || current.refresh_token->empty()) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kTokenRefreshFailed,
                           "refresh token is not available"));
    }

    TokenRefreshRequest request;
    request.client = client_;
    request.authorization_server = metadata_;
    request.resource = resource_;
    request.refresh_token = *current.refresh_token;
    request.scopes = credentials.granted_scopes;

    auto refreshed = token_endpoint_->refresh_access_token(request);
    if (!refreshed.has_value()) {
      return mcp::core::unexpected(refreshed.error());
    }
    if (!refreshed->token_set.refresh_token.has_value()) {
      refreshed->token_set.refresh_token = current.refresh_token;
      refreshed->refresh_token_rotated = false;
    } else {
      refreshed->refresh_token_rotated =
          refreshed->token_set.refresh_token != current.refresh_token;
    }

    current_scopes_ = refreshed->token_set.scopes.empty()
                          ? credentials.granted_scopes
                          : refreshed->token_set.scopes;
    credentials.token_set = refreshed->token_set;
    credentials.granted_scopes = current_scopes_;
    credentials.token_received_at = SystemClock::now();
    auto save_result =
        credential_store().save(credential_key(), std::move(credentials));
    if (!save_result.has_value()) {
      return mcp::core::unexpected(save_result.error());
    }

    state_ = OAuthLifecycleState::kAuthorized;
    return *refreshed;
  }

  core::Result<std::string> get_access_token(
      std::chrono::seconds refresh_skew = std::chrono::seconds(30)) {
    auto loaded = credential_store().load(credential_key());
    if (!loaded.has_value()) {
      return mcp::core::unexpected(loaded.error());
    }
    if (!loaded->has_value() || !(**loaded).token_set.has_value()) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kAuthorizationRequired,
                           "stored credentials are not available"));
    }

    const auto& token_set = *(**loaded).token_set;
    if (!token_set.expires_at.has_value() ||
        *token_set.expires_at > SystemClock::now() + refresh_skew) {
      return token_set.access_token;
    }
    if (!token_set.refresh_token.has_value()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kAuthorizationRequired,
          "access token is expired and no refresh token is available"));
    }

    auto refreshed = refresh_access_token();
    if (!refreshed.has_value()) {
      return mcp::core::unexpected(refreshed.error());
    }
    return refreshed->token_set.access_token;
  }

  core::Result<OAuthRefreshRetryResult> refresh_after_unauthorized_response(
      const HttpResponseMetadata& response) {
    auto decision = analyze_auth_response(response);
    if (!decision.has_value()) {
      return mcp::core::unexpected(decision.error());
    }

    OAuthRefreshRetryResult retry;
    retry.decision = std::move(*decision);
    if (response.status_code != 401 ||
        retry.decision.action != AuthResponseAction::kAuthorizationRequired) {
      return retry;
    }

    auto refreshed = refresh_access_token();
    if (!refreshed.has_value()) {
      return mcp::core::unexpected(refreshed.error());
    }

    retry.should_retry = true;
    retry.bearer_token = refreshed->token_set.access_token;
    return retry;
  }

  bool can_attempt_scope_upgrade() const {
    return scope_upgrade_config_.auto_upgrade &&
           scope_upgrade_attempts_ < scope_upgrade_config_.max_upgrade_attempts;
  }

  core::Result<AuthorizationUrlResult> request_scope_upgrade(
      const WwwAuthenticateChallenge& challenge, PkceChallenge pkce,
      std::string state, MetadataMap additional_parameters = {}) {
    if (!insufficient_scope(challenge)) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kInsufficientScope,
          "WWW-Authenticate challenge is not insufficient_scope"));
    }
    if (!can_attempt_scope_upgrade()) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kInsufficientScope,
                           "scope upgrade attempts are exhausted"));
    }

    const auto required_scope =
        challenge.parameter(std::string(WwwAuthenticateScopeParam));
    const auto upgraded_scopes =
        compute_scope_union(current_scopes_, required_scope);
    ++scope_upgrade_attempts_;
    return start_authorization(upgraded_scopes, std::move(pkce),
                               std::move(state),
                               std::move(additional_parameters));
  }

  static ScopeList compute_scope_union(const ScopeList& current,
                                       std::string_view required_scope) {
    ScopeList result = current;
    for (const auto& scope : detail::split_scopes(required_scope)) {
      if (!detail::has_scope(result, scope)) {
        result.push_back(scope);
      }
    }
    return result;
  }

 private:
  CredentialStore& credential_store() {
    if (!credential_store_) {
      credential_store_ = std::make_shared<InMemoryCredentialStore>();
    }
    return *credential_store_;
  }

  StateStore& state_store() {
    if (!state_store_) {
      state_store_ = std::make_shared<InMemoryStateStore>();
    }
    return *state_store_;
  }

  std::string resource_;
  AuthorizationServerMetadata metadata_;
  OAuthClientConfig client_;
  std::shared_ptr<CredentialStore> credential_store_;
  std::shared_ptr<StateStore> state_store_;
  std::shared_ptr<OAuthTokenEndpoint> token_endpoint_;
  std::shared_ptr<OAuthClientRegistrationEndpoint> registration_endpoint_;
  ScopeUpgradeConfig scope_upgrade_config_;
  OAuthLifecycleState state_ = OAuthLifecycleState::kUnauthorized;
  ScopeList current_scopes_;
  std::uint32_t scope_upgrade_attempts_ = 0;
  std::chrono::seconds authorization_state_ttl_ = kDefaultAuthorizationStateTtl;
};

}  // namespace mcp::auth
