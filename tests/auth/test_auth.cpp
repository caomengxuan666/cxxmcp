// Copyright (c) 2025 [caomengxuan666]

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cxxmcp/auth.hpp"
#ifdef CXXMCP_AUTH_TEST_HAS_SERVER
#include "cxxmcp/auth/server_auth_provider.hpp"
#endif

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void test_constant_time_string_equal_handles_lengths_and_empty_values() {
  require(mcp::auth::constant_time_string_equal("state-123", "state-123"),
          "constant-time compare should accept equal strings");
  require(!mcp::auth::constant_time_string_equal("state-123", "state-124"),
          "constant-time compare should reject equal-length mismatch");
  require(!mcp::auth::constant_time_string_equal("state-123", "state-1234"),
          "constant-time compare should reject different-length mismatch");
  require(mcp::auth::constant_time_string_equal("", ""),
          "constant-time compare should accept two empty strings");
  require(!mcp::auth::constant_time_string_equal("", "state"),
          "constant-time compare should reject empty/non-empty mismatch");
}

void test_secure_string_wraps_dpop_key_material() {
  mcp::auth::SecureString secret("private-key");
  require(secret.view() == "private-key", "secure string view mismatch");
  require(secret.size() == 11, "secure string size mismatch");

  mcp::auth::DpopKey key;
  key.key_id = "kid-1";
  key.algorithm = "ES256";
  key.private_key_pem = std::string("pem-material");
  require(key.private_key_pem.view() == "pem-material",
          "DPoP key should store secure key material");

  auto moved = std::move(key.private_key_pem);
  require(moved.view() == "pem-material",
          "moving secure string should preserve destination value");
  require(key.private_key_pem.empty(),
          "moving secure string should clear the source wrapper");

  moved.reset("replacement");
  require(moved.view() == "replacement", "secure string reset mismatch");
}

void test_dpop_claim_validation_enforces_target_clock_and_ath() {
  const auto now = mcp::auth::SystemClock::now();
  mcp::auth::HttpRequestTarget target;
  target.method = "POST";
  target.url = "https://resource.example/mcp";

  mcp::auth::DpopProofClaims claims;
  claims.jwt_id = "proof-1";
  claims.method = "POST";
  claims.url = target.url;
  claims.issued_at = now;
  claims.access_token_hash = "expected-ath";

  mcp::auth::DpopClaimValidationOptions options;
  options.now = now;
  options.clock_skew_tolerance = std::chrono::seconds(30);
  options.expected_access_token_hash = "expected-ath";

  auto validated = mcp::auth::validate_dpop_proof_claims(
      claims, target, std::optional<std::string>{"access-token"}, options);
  require(validated.has_value(), "valid DPoP claims should pass");

  auto wrong_method = claims;
  wrong_method.method = "GET";
  validated = mcp::auth::validate_dpop_proof_claims(
      wrong_method, target, std::optional<std::string>{"access-token"},
      options);
  require(!validated.has_value(), "DPoP htm mismatch should fail");

  auto wrong_url = claims;
  wrong_url.url = "https://resource.example/other";
  validated = mcp::auth::validate_dpop_proof_claims(
      wrong_url, target, std::optional<std::string>{"access-token"}, options);
  require(!validated.has_value(), "DPoP htu mismatch should fail");

  auto stale = claims;
  stale.issued_at = now - std::chrono::seconds(31);
  validated = mcp::auth::validate_dpop_proof_claims(
      stale, target, std::optional<std::string>{"access-token"}, options);
  require(!validated.has_value(), "stale DPoP iat should fail");

  auto missing_ath = claims;
  missing_ath.access_token_hash.reset();
  validated = mcp::auth::validate_dpop_proof_claims(
      missing_ath, target, std::optional<std::string>{"access-token"}, options);
  require(!validated.has_value(),
          "DPoP ath should be required when access token is bound");

  auto missing_expected_ath = claims;
  options.expected_access_token_hash.reset();
  validated = mcp::auth::validate_dpop_proof_claims(
      missing_expected_ath, target, std::optional<std::string>{"access-token"},
      options);
  require(!validated.has_value(),
          "expected DPoP ath should be required when access token is bound");
  options.expected_access_token_hash = "expected-ath";

  auto wrong_ath = claims;
  wrong_ath.access_token_hash = "wrong-ath";
  validated = mcp::auth::validate_dpop_proof_claims(
      wrong_ath, target, std::optional<std::string>{"access-token"}, options);
  require(!validated.has_value(), "DPoP ath mismatch should fail");
}

void test_dpop_replay_cache_rejects_duplicate_jti() {
  const auto now = mcp::auth::SystemClock::now();
  mcp::auth::HttpRequestTarget target;
  target.method = "GET";
  target.url = "https://resource.example/mcp";

  mcp::auth::DpopProofClaims claims;
  claims.jwt_id = "proof-replay";
  claims.method = "GET";
  claims.url = target.url;
  claims.issued_at = now;

  mcp::auth::DpopClaimValidationOptions options;
  options.now = now;
  options.replay_ttl = std::chrono::seconds(60);

  mcp::auth::InMemoryDpopReplayCache replay_cache;
  auto validated = mcp::auth::validate_dpop_proof_claims(
      claims, target, std::nullopt, options, &replay_cache);
  require(validated.has_value(), "first DPoP jti should be accepted");

  validated = mcp::auth::validate_dpop_proof_claims(
      claims, target, std::nullopt, options, &replay_cache);
  require(!validated.has_value(), "replayed DPoP jti should be rejected");

  options.now = now + std::chrono::seconds(61);
  claims.issued_at = options.now;
  validated = mcp::auth::validate_dpop_proof_claims(
      claims, target, std::nullopt, options, &replay_cache);
  require(validated.has_value(),
          "expired DPoP replay-cache entry should be reusable");
}

class RecordingDpopSigner final : public mcp::auth::DpopSigner {
 public:
  std::optional<mcp::auth::DpopProofRequest> request;
  std::string proof = "signed-proof";

  mcp::core::Result<std::string> sign(
      const mcp::auth::DpopProofRequest& value) override {
    request = value;
    return proof;
  }
};

void test_dpop_authorization_headers_use_injected_signer() {
  RecordingDpopSigner signer;

  mcp::auth::DpopAuthorizationRequest request;
  request.target.method = "POST";
  request.target.url = "https://resource.example/mcp";
  request.key.key_id = "kid-1";
  request.key.algorithm = "ES256";
  request.key.private_key_pem = std::string("key");
  request.access_token = "access-token";
  request.nonce = "server-nonce";

  auto headers =
      mcp::auth::build_dpop_authorization_headers(signer, std::move(request));
  require(headers.has_value(), "DPoP authorization headers should build");
  require(headers->proof == "signed-proof", "DPoP proof mismatch");
  require(headers->headers.at("DPoP") == "signed-proof",
          "DPoP proof header mismatch");
  require(headers->headers.at("Authorization") == "DPoP access-token",
          "DPoP authorization header mismatch");
  require(signer.request.has_value(), "DPoP signer should receive request");
  require(signer.request->target.method == "POST",
          "DPoP signer method mismatch");
  require(signer.request->target.url == "https://resource.example/mcp",
          "DPoP signer URL mismatch");
  require(signer.request->access_token == "access-token",
          "DPoP signer access token mismatch");
  require(signer.request->nonce == "server-nonce",
          "DPoP signer nonce mismatch");

  signer.request.reset();
  mcp::auth::DpopProofRequest proof_request;
  proof_request.target.method = "GET";
  proof_request.target.url = "https://resource.example/events";
  auto proof_headers =
      mcp::auth::build_dpop_proof_headers(signer, std::move(proof_request));
  require(proof_headers.has_value(), "DPoP proof-only headers should build");
  require(proof_headers->headers.at("DPoP") == "signed-proof",
          "DPoP proof-only header mismatch");
  require(proof_headers->headers.find("Authorization") ==
              proof_headers->headers.end(),
          "DPoP proof-only helper should not add Authorization");

  mcp::auth::DpopAuthorizationRequest missing_token;
  missing_token.target.method = "POST";
  missing_token.target.url = "https://resource.example/mcp";
  auto rejected =
      mcp::auth::build_dpop_authorization_headers(signer, missing_token);
  require(!rejected.has_value(), "DPoP auth should require access token");
}

void test_jwks_parser_selector_and_cache() {
  const auto parsed =
      mcp::auth::parse_json_web_key_set(nlohmann::json::parse(R"({
        "keys": [
          {
            "kty": "EC",
            "use": "sig",
            "key_ops": ["verify"],
            "alg": "ES256",
            "kid": "ec-key",
            "crv": "P-256",
            "x": "x-coordinate",
            "y": "y-coordinate",
            "custom": true
          },
          {
            "kty": "RSA",
            "alg": "RS256",
            "kid": "rsa-key",
            "n": "modulus",
            "e": "AQAB"
          }
        ],
        "issuer": "https://issuer.example"
      })"));
  require(parsed.has_value(), "JWKS should parse");
  require(parsed->keys.size() == 2, "JWKS key count mismatch");
  require(parsed->metadata.at("issuer") == "https://issuer.example",
          "JWKS extension metadata mismatch");
  require(parsed->keys.at(0).key_type == "EC", "JWK kty mismatch");
  require(parsed->keys.at(0).public_key_use == "sig", "JWK use mismatch");
  require(parsed->keys.at(0).key_operations.size() == 1,
          "JWK key_ops mismatch");
  require(parsed->keys.at(0).curve == "P-256", "JWK curve mismatch");
  require(parsed->keys.at(0).metadata.at("custom") == "true",
          "JWK extension metadata mismatch");

  mcp::auth::JwkSelectionCriteria criteria;
  criteria.key_id = "ec-key";
  criteria.algorithm = "ES256";
  criteria.key_type = "EC";
  criteria.public_key_use = "sig";
  auto selected = mcp::auth::select_json_web_key(*parsed, criteria);
  require(selected.has_value(), "JWK selection should succeed");
  require(selected->key_id == "ec-key", "selected JWK kid mismatch");

  criteria.key_id.reset();
  selected = mcp::auth::select_json_web_key(*parsed, criteria);
  require(selected.has_value(),
          "JWK selection should still succeed with alg/kty/use");
  criteria.algorithm.reset();
  criteria.key_type.reset();
  criteria.public_key_use.reset();
  selected = mcp::auth::select_json_web_key(*parsed, criteria);
  require(!selected.has_value(), "ambiguous JWK selection should fail");

  mcp::auth::InMemoryJwksCache cache;
  const std::string uri = "https://issuer.example/jwks.json";
  require(cache.save(uri, *parsed).has_value(), "JWKS cache save failed");
  auto cached = cache.load(uri);
  require(cached.has_value() && cached->has_value(), "JWKS cache load failed");
  require((*cached)->keys.size() == 2, "JWKS cached key count mismatch");
  require(cache.clear(uri).has_value(), "JWKS cache clear failed");
  cached = cache.load(uri);
  require(cached.has_value() && !cached->has_value(),
          "JWKS cache entry should be cleared");
}

void test_www_authenticate_parser_parses_mcp_oauth_challenge() {
  const auto parsed = mcp::auth::parse_www_authenticate(
      "Bearer realm=\"mcp, api\", "
      "resource_metadata=\"https://resource.example/.well-known/"
      "oauth-protected-resource\", "
      "error=insufficient_scope, scope=\"tools:call prompts:read\", "
      "Basic realm=\"fallback\"");

  require(parsed.has_value(), "WWW-Authenticate parser should succeed");
  require(parsed->size() == 2, "challenge count mismatch");
  require(parsed->at(0).bearer(), "first challenge should be bearer");
  require(parsed->at(0).parameter("realm") == "mcp, api",
          "quoted comma parameter mismatch");
  require(parsed->at(0).parameter(
              mcp::auth::WwwAuthenticateResourceMetadataParam) ==
              "https://resource.example/.well-known/oauth-protected-resource",
          "resource_metadata parameter mismatch");
  require(mcp::auth::insufficient_scope(parsed->at(0)),
          "insufficient_scope helper mismatch");
  require(parsed->at(0).parameter(mcp::auth::WwwAuthenticateScopeParam) ==
              "tools:call prompts:read",
          "scope parameter mismatch");
  require(parsed->at(1).scheme == "Basic", "second challenge scheme mismatch");
  require(parsed->at(1).parameter("realm") == "fallback",
          "second challenge parameter mismatch");
}

void test_www_authenticate_parser_handles_escapes_and_token68() {
  const auto escaped = mcp::auth::parse_www_authenticate(
      "Bearer realm=\"quoted \\\"realm\\\"\"");
  require(escaped.has_value(), "escaped quoted value should parse");
  require(escaped->at(0).parameter("realm") == "quoted \"realm\"",
          "escaped quoted value mismatch");

  const auto token68 =
      mcp::auth::parse_www_authenticate("Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
  require(token68.has_value(), "token68 challenge should parse");
  require(token68->size() == 1, "token68 challenge count mismatch");
  require(token68->at(0).token68.has_value(), "token68 value missing");
  require(*token68->at(0).token68 == "QWxhZGRpbjpvcGVuIHNlc2FtZQ==",
          "token68 value mismatch");
}

void test_www_authenticate_parser_rejects_malformed_headers() {
  const auto malformed =
      mcp::auth::parse_www_authenticate("Bearer realm=\"unterminated");
  require(!malformed.has_value(), "unterminated quoted value should fail");
  require(malformed.error().category == mcp::auth::AuthErrorCategory,
          "parse error category mismatch");
}

void test_in_memory_token_store_separates_token_keys() {
  mcp::auth::InMemoryTokenStore store;
  mcp::auth::TokenSet first;
  first.access_token = "access-a";
  first.refresh_token = "refresh-a";
  first.expires_at = mcp::auth::SystemClock::now() + std::chrono::seconds(60);

  mcp::auth::TokenSet second;
  second.access_token = "access-b";
  second.refresh_token = "refresh-b";

  mcp::auth::TokenKey first_key{
      "https://resource.example",
      "https://issuer.example",
      "client-a",
      {{"tenant", "one"}},
  };
  mcp::auth::TokenKey second_key{
      "https://resource.example",
      "https://issuer.example",
      "client-b",
      {{"tenant", "two"}},
  };

  require(store.save(first_key, first).has_value(),
          "saving first token should succeed");
  require(store.save(second_key, second).has_value(),
          "saving second token should succeed");

  auto loaded_first = store.load(first_key);
  auto loaded_second = store.load(second_key);
  require(loaded_first.has_value() && loaded_first->has_value(),
          "first token should load");
  require(loaded_second.has_value() && loaded_second->has_value(),
          "second token should load");
  require((*loaded_first)->access_token == "access-a",
          "first token value mismatch");
  require((*loaded_second)->access_token == "access-b",
          "second token value mismatch");

  require(store.remove(first_key).has_value(),
          "removing first token should succeed");
  loaded_first = store.load(first_key);
  loaded_second = store.load(second_key);
  require(loaded_first.has_value() && !loaded_first->has_value(),
          "removed first token should not load");
  require(loaded_second.has_value() && loaded_second->has_value(),
          "removing first token should not remove second token");
}

mcp::auth::AuthorizationServerMetadata test_authorization_metadata() {
  mcp::auth::AuthorizationServerMetadata metadata;
  metadata.issuer = "https://issuer.example";
  metadata.authorization_endpoint = "https://issuer.example/authorize";
  metadata.token_endpoint = "https://issuer.example/token";
  metadata.scopes_supported = {"tools:read", "tools:call", "prompts:read",
                               "offline_access"};
  metadata.code_challenge_methods_supported = {"S256"};
  return metadata;
}

mcp::auth::OAuthClientConfig test_client_config() {
  mcp::auth::OAuthClientConfig client;
  client.client_id = "client-id";
  client.redirect_uri = "http://localhost/callback";
  client.scopes = {"tools:read"};
  return client;
}

mcp::auth::PkceChallenge test_pkce() {
  mcp::auth::PkceChallenge pkce;
  pkce.code_verifier = "verifier-123";
  pkce.code_challenge = "challenge-456";
  pkce.method = mcp::auth::PkceCodeChallengeMethod::kS256;
  return pkce;
}

class RecordingTokenEndpoint final : public mcp::auth::OAuthTokenEndpoint {
 public:
  mcp::auth::TokenSet exchange_result;
  mcp::auth::TokenRefreshResult refresh_result;
  mcp::auth::TokenSet client_credentials_result;
  std::optional<mcp::auth::TokenExchangeRequest> exchange_request;
  std::optional<mcp::auth::TokenRefreshRequest> refresh_request;
  std::optional<mcp::auth::TokenClientCredentialsRequest>
      client_credentials_request;

  mcp::core::Result<mcp::auth::TokenSet> exchange_authorization_code(
      const mcp::auth::TokenExchangeRequest& request) override {
    exchange_request = request;
    return exchange_result;
  }

  mcp::core::Result<mcp::auth::TokenRefreshResult> refresh_access_token(
      const mcp::auth::TokenRefreshRequest& request) override {
    refresh_request = request;
    return refresh_result;
  }

  mcp::core::Result<mcp::auth::TokenSet> exchange_client_credentials(
      const mcp::auth::TokenClientCredentialsRequest& request) override {
    client_credentials_request = request;
    return client_credentials_result;
  }
};

class RecordingMetadataEndpoint final
    : public mcp::auth::OAuthMetadataEndpoint {
 public:
  std::map<std::string, mcp::auth::ProtectedResourceMetadata>
      protected_resources;
  std::map<std::string, mcp::auth::AuthorizationServerMetadata>
      authorization_servers;
  std::vector<std::string> protected_requests;
  std::vector<std::string> authorization_requests;

  mcp::core::Result<mcp::auth::ProtectedResourceMetadata>
  fetch_protected_resource_metadata(
      const mcp::auth::MetadataFetchRequest& request) override {
    protected_requests.push_back(request.url);
    const auto iter = protected_resources.find(request.url);
    if (iter == protected_resources.end()) {
      return mcp::core::unexpected(mcp::auth::make_oauth_error(
          mcp::auth::OAuthErrorCode::kMetadataDiscoveryFailed,
          "protected metadata not found", request.url));
    }
    return iter->second;
  }

  mcp::core::Result<mcp::auth::AuthorizationServerMetadata>
  fetch_authorization_server_metadata(
      const mcp::auth::MetadataFetchRequest& request) override {
    authorization_requests.push_back(request.url);
    const auto iter = authorization_servers.find(request.url);
    if (iter == authorization_servers.end()) {
      return mcp::core::unexpected(mcp::auth::make_oauth_error(
          mcp::auth::OAuthErrorCode::kMetadataDiscoveryFailed,
          "authorization metadata not found", request.url));
    }
    return iter->second;
  }
};

class RecordingRegistrationEndpoint final
    : public mcp::auth::OAuthClientRegistrationEndpoint {
 public:
  mcp::auth::ClientRegistrationResponse response;
  std::optional<mcp::auth::ClientRegistrationEndpointRequest> request;

  mcp::core::Result<mcp::auth::ClientRegistrationResponse> register_client(
      const mcp::auth::ClientRegistrationEndpointRequest& value) override {
    request = value;
    return response;
  }
};

class RecordingJwtVerifier final : public mcp::auth::JwtVerifier {
 public:
  std::optional<mcp::auth::JwtVerificationRequest> request;

  mcp::core::Result<mcp::auth::VerifiedJwtClaims> verify(
      const mcp::auth::JwtVerificationRequest& value) override {
    request = value;
    mcp::auth::VerifiedJwtClaims claims;
    claims.issuer = value.issuer.value_or("");
    claims.audience = value.audience.value_or("");
    claims.subject = "subject";
    claims.expires_at = value.now + std::chrono::minutes(5);
    return claims;
  }
};

#ifdef CXXMCP_AUTH_TEST_HAS_SERVER
class RecordingDpopVerifier final : public mcp::auth::DpopVerifier {
 public:
  std::optional<std::string> proof;
  std::optional<mcp::auth::HttpRequestTarget> target;
  std::optional<std::string> access_token;

  mcp::core::Result<mcp::auth::DpopProofClaims> verify(
      const std::string& proof_jwt,
      const mcp::auth::HttpRequestTarget& request_target,
      const std::optional<std::string>& token) override {
    proof = proof_jwt;
    target = request_target;
    access_token = token;

    mcp::auth::DpopProofClaims claims;
    claims.jwt_id = "proof-jti";
    claims.method = request_target.method;
    claims.url = request_target.url;
    claims.issued_at = mcp::auth::SystemClock::now();
    claims.access_token_hash = "expected-ath";
    return claims;
  }
};
#endif

void test_in_memory_credential_and_state_stores() {
  mcp::auth::InMemoryCredentialStore credential_store;
  mcp::auth::CredentialKey first_key{
      "https://resource.example", "https://issuer.example", "client-a", {}};
  mcp::auth::CredentialKey second_key{
      "https://resource.example", "https://issuer.example", "client-b", {}};

  mcp::auth::StoredCredentials first;
  first.client_id = "client-a";
  mcp::auth::TokenSet first_tokens;
  first_tokens.access_token = "access-a";
  first.token_set = first_tokens;

  mcp::auth::StoredCredentials second;
  second.client_id = "client-b";
  mcp::auth::TokenSet second_tokens;
  second_tokens.access_token = "access-b";
  second.token_set = second_tokens;

  require(credential_store.save(first_key, first).has_value(),
          "saving first credentials should succeed");
  require(credential_store.save(second_key, second).has_value(),
          "saving second credentials should succeed");

  auto loaded_first = credential_store.load(first_key);
  auto loaded_second = credential_store.load(second_key);
  require(loaded_first.has_value() && loaded_first->has_value(),
          "first credentials should load");
  require(loaded_second.has_value() && loaded_second->has_value(),
          "second credentials should load");
  require((*loaded_first)->token_set->access_token == "access-a",
          "first credential token mismatch");
  require((*loaded_second)->token_set->access_token == "access-b",
          "second credential token mismatch");

  require(credential_store.clear(first_key).has_value(),
          "clearing first credentials should succeed");
  loaded_first = credential_store.load(first_key);
  loaded_second = credential_store.load(second_key);
  require(loaded_first.has_value() && !loaded_first->has_value(),
          "cleared credentials should be absent");
  require(loaded_second.has_value() && loaded_second->has_value(),
          "clearing one credential key should not clear another");

  mcp::auth::InMemoryStateStore state_store;
  mcp::auth::StoredAuthorizationState state;
  state.state = "csrf-123";
  state.pkce = test_pkce();
  state.client_id = "client-id";
  require(state_store.save(state.state, state).has_value(),
          "saving authorization state should succeed");
  auto loaded_state = state_store.load("csrf-123");
  require(loaded_state.has_value() && loaded_state->has_value(),
          "authorization state should load");
  require((*loaded_state)->pkce.code_verifier == "verifier-123",
          "authorization state PKCE verifier mismatch");
  require(state_store.remove("csrf-123").has_value(),
          "removing authorization state should succeed");
  loaded_state = state_store.load("csrf-123");
  require(loaded_state.has_value() && !loaded_state->has_value(),
          "removed authorization state should be absent");
}

void test_authorization_manager_builds_url_and_stores_state() {
  auto state_store = std::make_shared<mcp::auth::InMemoryStateStore>();
  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          test_authorization_metadata(),
                                          test_client_config());
  manager.set_state_store(state_store);

  auto auth = manager.start_authorization({"tools:read", "tools:call"},
                                          test_pkce(), "csrf value");
  require(auth.has_value(), "authorization URL should be built");
  require(auth->url.find("response_type=code") != std::string::npos,
          "authorization URL should request authorization code flow");
  require(auth->url.find("client_id=client-id") != std::string::npos,
          "authorization URL should include client_id");
  require(auth->url.find("redirect_uri=http%3A%2F%2Flocalhost%2Fcallback") !=
              std::string::npos,
          "authorization URL should encode redirect_uri");
  require(auth->url.find("state=csrf%20value") != std::string::npos,
          "authorization URL should encode state");
  require(auth->url.find("code_challenge=challenge-456") != std::string::npos,
          "authorization URL should include PKCE challenge");
  require(auth->url.find("code_challenge_method=S256") != std::string::npos,
          "authorization URL should include PKCE method");
  require(auth->url.find("resource=https%3A%2F%2Fresource.example%2Fmcp") !=
              std::string::npos,
          "authorization URL should include resource");
  require(auth->url.find("scope=tools%3Aread%20tools%3Acall%20"
                         "offline_access") != std::string::npos,
          "authorization URL should encode scopes with offline_access");
  require(manager.lifecycle_state() ==
              mcp::auth::OAuthLifecycleState::kAuthorizationPending,
          "manager should enter authorization pending state");

  auto stored = state_store->load("csrf value");
  require(stored.has_value() && stored->has_value(),
          "authorization state should be stored");
  require((*stored)->pkce.code_verifier == "verifier-123",
          "stored state should retain PKCE verifier");
  require(
      (*stored)->requested_scopes ==
          mcp::auth::ScopeList{"tools:read", "tools:call", "offline_access"},
      "stored state should retain requested scopes with offline_access");
}

void test_authorization_url_requires_secure_endpoints_and_redirects() {
  {
    auto metadata = test_authorization_metadata();
    metadata.authorization_endpoint = "http://issuer.example/authorize";
    mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                            metadata, test_client_config());
    const auto auth =
        manager.start_authorization({"tools:read"}, test_pkce(), "csrf");
    require(!auth.has_value(),
            "authorization endpoint over http should be rejected");
    require(auth.error().message.find("authorization endpoint") !=
                std::string::npos,
            "authorization endpoint error should name endpoint");
  }
  {
    auto metadata = test_authorization_metadata();
    metadata.token_endpoint = "http://issuer.example/token";
    mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                            metadata, test_client_config());
    const auto auth =
        manager.start_authorization({"tools:read"}, test_pkce(), "csrf");
    require(!auth.has_value(), "token endpoint over http should be rejected");
    require(auth.error().message.find("token endpoint") != std::string::npos,
            "token endpoint error should name endpoint");
  }
  {
    auto client = test_client_config();
    client.redirect_uri = "http://client.example/callback";
    mcp::auth::AuthorizationManager manager(
        "https://resource.example/mcp", test_authorization_metadata(), client);
    const auto auth =
        manager.start_authorization({"tools:read"}, test_pkce(), "csrf");
    require(!auth.has_value(),
            "non-loopback http redirect_uri should be rejected");
    require(auth.error().message.find("redirect_uri") != std::string::npos,
            "redirect_uri error should name redirect_uri");
  }
  {
    auto client = test_client_config();
    client.redirect_uri = "http://127.0.0.1:3921/callback";
    mcp::auth::AuthorizationManager manager(
        "https://resource.example/mcp", test_authorization_metadata(), client);
    const auto auth =
        manager.start_authorization({"tools:read"}, test_pkce(), "csrf");
    require(auth.has_value(), "loopback http redirect_uri should be allowed");
  }
  {
    auto client = test_client_config();
    client.redirect_uri = "http://[::1]:3921/callback";
    mcp::auth::AuthorizationManager manager(
        "https://resource.example/mcp", test_authorization_metadata(), client);
    const auto auth =
        manager.start_authorization({"tools:read"}, test_pkce(), "csrf");
    require(auth.has_value(),
            "IPv6 loopback http redirect_uri should be allowed");
  }
}

void test_authorization_manager_exchanges_code_and_saves_credentials() {
  auto credential_store =
      std::make_shared<mcp::auth::InMemoryCredentialStore>();
  auto state_store = std::make_shared<mcp::auth::InMemoryStateStore>();
  auto endpoint = std::make_shared<RecordingTokenEndpoint>();
  endpoint->exchange_result.access_token = "access-token";
  endpoint->exchange_result.refresh_token = "refresh-token";
  endpoint->exchange_result.scopes = {"tools:read", "tools:call"};

  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          test_authorization_metadata(),
                                          test_client_config());
  manager.set_credential_store(credential_store);
  manager.set_state_store(state_store);
  manager.set_token_endpoint(endpoint);

  auto auth = manager.start_authorization({"tools:read"}, test_pkce(), "csrf");
  require(auth.has_value(), "authorization should start before exchange");

  auto exchanged = manager.exchange_authorization_code("code-123", "csrf");
  require(exchanged.has_value(), "authorization code exchange should succeed");
  require(endpoint->exchange_request.has_value(),
          "token endpoint should receive exchange request");
  require(endpoint->exchange_request->authorization_code == "code-123",
          "exchange request authorization code mismatch");
  require(
      endpoint->exchange_request->state.pkce.code_verifier == "verifier-123",
      "exchange request should include stored PKCE verifier");
  require(
      manager.lifecycle_state() == mcp::auth::OAuthLifecycleState::kAuthorized,
      "manager should enter authorized state");
  require(manager.current_scopes().size() == 2,
          "manager should track granted scopes");

  auto consumed = state_store->load("csrf");
  require(consumed.has_value() && !consumed->has_value(),
          "authorization state should be one-time use");

  auto stored = credential_store->load(manager.credential_key());
  require(stored.has_value() && stored->has_value(),
          "exchanged credentials should be stored");
  require((*stored)->token_set->access_token == "access-token",
          "stored access token mismatch");
}

void test_authorization_manager_rejects_expired_authorization_state() {
  auto state_store = std::make_shared<mcp::auth::InMemoryStateStore>();
  auto endpoint = std::make_shared<RecordingTokenEndpoint>();
  endpoint->exchange_result.access_token = "access-token";

  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          test_authorization_metadata(),
                                          test_client_config());
  manager.set_state_store(state_store);
  manager.set_token_endpoint(endpoint);

  auto auth = manager.start_authorization({"tools:read"}, test_pkce(), "csrf");
  require(auth.has_value(), "authorization should start before expiry test");

  auto loaded = state_store->load("csrf");
  require(loaded.has_value() && loaded->has_value(),
          "state should be available for expiry test");
  auto expired_state = **loaded;
  expired_state.created_at =
      mcp::auth::SystemClock::now() - std::chrono::seconds(61);
  require(state_store->save("csrf", expired_state).has_value(),
          "expired state should be saved for test");

  auto exchanged = manager.exchange_authorization_code("code-123", "csrf");
  require(!exchanged.has_value(), "expired authorization state should fail");
  require(
      exchanged.error().code ==
          static_cast<int>(mcp::auth::OAuthErrorCode::kAuthorizationRequired),
      "expired state error code mismatch");
  require(exchanged.error().message.find("expired") != std::string::npos,
          "expired state error message should mention expiry");
  require(!endpoint->exchange_request.has_value(),
          "expired state should not reach token endpoint");

  auto consumed = state_store->load("csrf");
  require(consumed.has_value() && !consumed->has_value(),
          "expired authorization state should still be consumed");
}

void test_authorization_manager_uses_configured_authorization_state_ttl() {
  auto state_store = std::make_shared<mcp::auth::InMemoryStateStore>();
  auto endpoint = std::make_shared<RecordingTokenEndpoint>();
  endpoint->exchange_result.access_token = "access-token";

  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          test_authorization_metadata(),
                                          test_client_config());
  manager.set_state_store(state_store);
  manager.set_token_endpoint(endpoint);
  manager.set_authorization_state_ttl(std::chrono::seconds(300));
  require(manager.authorization_state_ttl() == std::chrono::seconds(300),
          "configured authorization state TTL mismatch");

  auto auth = manager.start_authorization({"tools:read"}, test_pkce(), "csrf");
  require(auth.has_value(),
          "authorization should start before configured TTL test");

  auto loaded = state_store->load("csrf");
  require(loaded.has_value() && loaded->has_value(),
          "state should be available for configured TTL test");
  auto retained_state = **loaded;
  retained_state.created_at =
      mcp::auth::SystemClock::now() - std::chrono::seconds(120);
  require(state_store->save("csrf", retained_state).has_value(),
          "retained state should be saved for test");

  auto exchanged = manager.exchange_authorization_code("code-123", "csrf");
  require(exchanged.has_value(),
          "configured authorization state TTL should allow fresh state");
  require(endpoint->exchange_request.has_value(),
          "fresh state should reach token endpoint");
}

void test_authorization_manager_refresh_preserves_and_rotates_refresh_token() {
  auto credential_store =
      std::make_shared<mcp::auth::InMemoryCredentialStore>();
  auto endpoint = std::make_shared<RecordingTokenEndpoint>();
  endpoint->refresh_result.token_set.access_token = "new-access";
  endpoint->refresh_result.token_set.scopes = {"tools:read"};

  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          test_authorization_metadata(),
                                          test_client_config());
  manager.set_credential_store(credential_store);
  manager.set_token_endpoint(endpoint);

  mcp::auth::StoredCredentials credentials;
  credentials.client_id = "client-id";
  credentials.granted_scopes = {"tools:read"};
  mcp::auth::TokenSet old_tokens;
  old_tokens.access_token = "old-access";
  old_tokens.refresh_token = "old-refresh";
  old_tokens.scopes = {"tools:read"};
  credentials.token_set = old_tokens;
  require(
      credential_store->save(manager.credential_key(), credentials).has_value(),
      "saving credentials before refresh should succeed");

  auto refreshed = manager.refresh_access_token();
  require(refreshed.has_value(), "refresh should succeed");
  require(!refreshed->refresh_token_rotated,
          "missing refresh token in response should preserve old token");
  require(refreshed->token_set.refresh_token == "old-refresh",
          "old refresh token should be preserved");
  require(endpoint->refresh_request.has_value(),
          "token endpoint should receive refresh request");
  require(endpoint->refresh_request->refresh_token == "old-refresh",
          "refresh request token mismatch");

  endpoint->refresh_result.token_set.access_token = "rotated-access";
  endpoint->refresh_result.token_set.refresh_token = "rotated-refresh";
  refreshed = manager.refresh_access_token();
  require(refreshed.has_value(), "rotating refresh should succeed");
  require(refreshed->refresh_token_rotated,
          "new refresh token should be marked as rotated");
  require(refreshed->token_set.refresh_token == "rotated-refresh",
          "rotated refresh token mismatch");
}

void test_authorization_manager_get_access_token_refreshes_near_expiry() {
  auto credential_store =
      std::make_shared<mcp::auth::InMemoryCredentialStore>();
  auto endpoint = std::make_shared<RecordingTokenEndpoint>();
  endpoint->refresh_result.token_set.access_token = "fresh-access";
  endpoint->refresh_result.token_set.refresh_token = "refresh-token";
  endpoint->refresh_result.token_set.expires_at =
      mcp::auth::SystemClock::now() + std::chrono::hours(1);

  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          test_authorization_metadata(),
                                          test_client_config());
  manager.set_credential_store(credential_store);
  manager.set_token_endpoint(endpoint);

  mcp::auth::StoredCredentials credentials;
  credentials.client_id = "client-id";
  mcp::auth::TokenSet expiring;
  expiring.access_token = "old-access";
  expiring.refresh_token = "refresh-token";
  expiring.expires_at = mcp::auth::SystemClock::now() + std::chrono::seconds(5);
  credentials.token_set = expiring;
  require(
      credential_store->save(manager.credential_key(), credentials).has_value(),
      "saving expiring credentials should succeed");

  auto token = manager.get_access_token(std::chrono::seconds(30));
  require(token.has_value(), "get_access_token should refresh near expiry");
  require(*token == "fresh-access", "refreshed access token mismatch");
}

void test_authorization_manager_refresh_retry_helper_returns_new_bearer() {
  auto endpoint = std::make_shared<RecordingTokenEndpoint>();
  endpoint->refresh_result.token_set.access_token = "retry-access";
  endpoint->refresh_result.token_set.refresh_token = "refresh-token";
  endpoint->refresh_result.token_set.scopes = {"tools:read"};

  auto credential_store =
      std::make_shared<mcp::auth::InMemoryCredentialStore>();
  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          test_authorization_metadata(),
                                          test_client_config());
  manager.set_token_endpoint(endpoint);
  manager.set_credential_store(credential_store);

  mcp::auth::TokenSet stored_tokens;
  stored_tokens.access_token = "stale-access";
  stored_tokens.refresh_token = "refresh-token";
  mcp::auth::StoredCredentials credentials;
  credentials.client_id = "client-id";
  credentials.token_set = stored_tokens;
  credentials.granted_scopes = {"tools:read"};
  require(
      credential_store->save(manager.credential_key(), credentials).has_value(),
      "saving credentials before retry refresh should succeed");

  mcp::auth::HttpResponseMetadata response;
  response.status_code = 401;
  response.headers.emplace(
      "WWW-Authenticate",
      "Bearer resource_metadata=\"https://resource.example/.well-known/"
      "oauth-protected-resource\"");
  auto retry = manager.refresh_after_unauthorized_response(response);
  require(retry.has_value(), "401 refresh retry helper should succeed");
  require(retry->should_retry, "401 should request one retry after refresh");
  require(retry->bearer_token == "retry-access",
          "retry helper should return refreshed bearer token");
  require(endpoint->refresh_request.has_value(),
          "retry helper should call token refresh endpoint");

  response.status_code = 403;
  response.headers["WWW-Authenticate"] =
      "Bearer error=\"insufficient_scope\", scope=\"tools:call\"";
  retry = manager.refresh_after_unauthorized_response(response);
  require(retry.has_value(), "403 analysis should succeed");
  require(!retry->should_retry, "403 should not refresh-and-retry");
  require(retry->decision.action ==
              mcp::auth::AuthResponseAction::kScopeUpgradeRequired,
          "403 insufficient_scope should remain a scope-upgrade decision");
}

void test_scope_upgrade_uses_www_authenticate_boundary() {
  auto parsed = mcp::auth::parse_www_authenticate(
      "Bearer error=\"insufficient_scope\", scope=\"tools:call prompts:read\", "
      "resource_metadata=\"https://resource.example/.well-known/"
      "oauth-protected-resource\"");
  require(parsed.has_value(), "scope upgrade challenge should parse");

  mcp::auth::HttpResponseMetadata response;
  response.status_code = 403;
  response.headers.emplace(
      "www-authenticate",
      "Bearer error=\"insufficient_scope\", scope=\"tools:call prompts:read\", "
      "error_description=\"more scope required\"");
  auto decision = mcp::auth::analyze_auth_response(response);
  require(decision.has_value(), "auth response should analyze");
  require(
      decision->action == mcp::auth::AuthResponseAction::kScopeUpgradeRequired,
      "403 insufficient_scope should request scope upgrade");
  require(decision->required_scopes.size() == 2,
          "required scopes should be parsed from challenge");
  require(decision->error_description == "more scope required",
          "error_description should be exposed");

  auto endpoint = std::make_shared<RecordingTokenEndpoint>();
  endpoint->exchange_result.access_token = "access-token";
  endpoint->exchange_result.scopes = {"tools:read"};
  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          test_authorization_metadata(),
                                          test_client_config());
  manager.set_token_endpoint(endpoint);

  auto auth = manager.start_authorization({"tools:read"}, test_pkce(), "csrf");
  require(auth.has_value(), "initial authorization should start");
  auto exchanged = manager.exchange_authorization_code("code", "csrf");
  require(exchanged.has_value(), "initial exchange should establish scopes");

  auto upgraded = manager.request_scope_upgrade(parsed->at(0), test_pkce(),
                                                "upgrade-state");
  require(upgraded.has_value(), "scope upgrade should build auth URL");
  require(upgraded->url.find("scope=tools%3Aread%20tools%3Acall%20prompts%"
                             "3Aread") != std::string::npos,
          "scope upgrade should request union of current and required scopes");
  require(manager.scope_upgrade_attempts() == 1,
          "scope upgrade attempt count mismatch");

  mcp::auth::ScopeUpgradeConfig config;
  config.max_upgrade_attempts = 1;
  manager.set_scope_upgrade_config(config);
  auto exhausted = manager.request_scope_upgrade(parsed->at(0), test_pkce(),
                                                 "upgrade-state-2");
  require(!exhausted.has_value(), "scope upgrade limit should be enforced");
  require(exhausted.error().code ==
              static_cast<int>(mcp::auth::OAuthErrorCode::kInsufficientScope),
          "scope upgrade exhaustion error code mismatch");
}

void test_metadata_discovery_plan_prefers_www_authenticate_resource_metadata() {
  mcp::auth::HttpResponseMetadata response;
  response.status_code = 401;
  response.headers.emplace(
      "WWW-Authenticate",
      "Bearer resource_metadata=\"https://resource.example/.well-known/"
      "oauth-protected-resource/api\"");
  const auto decision = mcp::auth::analyze_auth_response(response);
  require(decision.has_value(), "auth response should analyze");

  const auto urls = mcp::auth::build_protected_resource_metadata_urls(
      "https://resource.example/api/mcp?session=1",
      decision->resource_metadata_url);
  require(urls.size() == 3, "metadata discovery URL count mismatch");
  require(urls[0] ==
              "https://resource.example/.well-known/oauth-protected-resource/"
              "api",
          "WWW-Authenticate resource_metadata should be first");
  require(urls[1] ==
              "https://resource.example/.well-known/oauth-protected-resource/"
              "api/mcp",
          "resource path protected-resource well-known URL mismatch");
  require(urls[2] ==
              "https://resource.example/.well-known/oauth-protected-resource",
          "origin protected-resource well-known URL mismatch");
}

void test_metadata_discovery_url_candidates_reject_ssrf_prone_inputs() {
  const auto invalid_scheme = mcp::auth::build_protected_resource_metadata_urls(
      "http://resource.example/api/mcp");
  require(invalid_scheme.empty(),
          "non-loopback HTTP resource metadata discovery should be rejected");

  const auto userinfo = mcp::auth::build_protected_resource_metadata_urls(
      "https://user@resource.example/api/mcp");
  require(userinfo.empty(),
          "resource metadata discovery should reject userinfo");

  const auto dot_segment = mcp::auth::build_protected_resource_metadata_urls(
      "https://resource.example/api/../mcp");
  require(dot_segment.empty(),
          "resource metadata discovery should reject dot segments");

  const auto encoded_dot_segment =
      mcp::auth::build_protected_resource_metadata_urls(
          "https://resource.example/api/%2e%2e/mcp");
  require(encoded_dot_segment.empty(),
          "resource metadata discovery should reject encoded dot segments");

  const auto fragment = mcp::auth::build_protected_resource_metadata_urls(
      "https://resource.example/api/mcp#fragment");
  require(fragment.empty(),
          "resource metadata discovery should reject fragments");

  const auto loopback = mcp::auth::build_protected_resource_metadata_urls(
      "http://localhost:8787/api/mcp");
  require(loopback.size() == 2,
          "loopback HTTP resource metadata discovery should be allowed");
  require(loopback[0] ==
              "http://localhost:8787/.well-known/oauth-protected-resource/"
              "api/mcp",
          "loopback HTTP path metadata URL mismatch");

  const auto invalid_challenge =
      mcp::auth::build_protected_resource_metadata_urls(
          "https://resource.example/api/mcp",
          "http://resource.example/.well-known/oauth-protected-resource");
  require(invalid_challenge.size() == 2,
          "invalid challenged metadata URL should be skipped");
  require(
      invalid_challenge[0].find("http://resource.example") == std::string::npos,
      "invalid challenged metadata URL should not be retained");
}

void test_authorization_server_metadata_url_candidates() {
  const auto path_urls = mcp::auth::build_authorization_server_metadata_urls(
      "https://issuer.example/tenant/a?ignored=1");
  require(path_urls.size() == 4,
          "path issuer authorization metadata URL count mismatch");
  require(path_urls[0] ==
              "https://issuer.example/.well-known/"
              "oauth-authorization-server/tenant/a",
          "path issuer metadata URL mismatch");
  require(path_urls[1] ==
              "https://issuer.example/tenant/a/"
              ".well-known/openid-configuration",
          "path issuer OpenID configuration URL mismatch");
  require(path_urls[2] ==
              "https://issuer.example/.well-known/oauth-authorization-server",
          "origin issuer metadata URL mismatch");
  require(
      path_urls[3] == "https://issuer.example/.well-known/openid-configuration",
      "origin issuer OpenID configuration URL mismatch");

  const auto origin_urls = mcp::auth::build_authorization_server_metadata_urls(
      "https://issuer.example");
  require(origin_urls.size() == 2,
          "origin issuer should have two metadata URLs");
  require(origin_urls[0] ==
              "https://issuer.example/.well-known/oauth-authorization-server",
          "origin-only metadata URL mismatch");
  require(origin_urls[1] ==
              "https://issuer.example/.well-known/openid-configuration",
          "origin-only OpenID configuration URL mismatch");

  require(mcp::auth::build_authorization_server_metadata_urls(
              "http://issuer.example/tenant")
              .empty(),
          "non-loopback HTTP issuer metadata discovery should be rejected");
  require(mcp::auth::build_authorization_server_metadata_urls(
              "https://user@issuer.example/tenant")
              .empty(),
          "issuer metadata discovery should reject userinfo");
  require(mcp::auth::build_authorization_server_metadata_urls(
              "https://issuer.example/tenant/../a")
              .empty(),
          "issuer metadata discovery should reject dot segments");
  require(mcp::auth::build_authorization_server_metadata_urls(
              "https://issuer.example/tenant#fragment")
              .empty(),
          "issuer metadata discovery should reject fragments");

  const auto loopback_urls =
      mcp::auth::build_authorization_server_metadata_urls(
          "http://127.0.0.1:8787/tenant");
  require(loopback_urls.size() == 4,
          "loopback HTTP issuer metadata discovery should be allowed");
}

void test_scope_selection_uses_rmcp_priority_order() {
  mcp::auth::ScopeSelectionContext context;
  context.default_scopes = {"default"};
  context.authorization_server_scopes = {"as"};
  context.protected_resource_scopes = {"resource"};
  context.www_authenticate_scopes = {"challenge"};

  auto selected = mcp::auth::select_authorization_scopes(context);
  require(selected == mcp::auth::ScopeList{"challenge"},
          "WWW-Authenticate scopes should win");

  context.www_authenticate_scopes.clear();
  selected = mcp::auth::select_authorization_scopes(context);
  require(selected == mcp::auth::ScopeList{"resource"},
          "protected-resource scopes should win after challenge scopes");

  context.protected_resource_scopes.clear();
  selected = mcp::auth::select_authorization_scopes(context);
  require(selected == mcp::auth::ScopeList{"as"},
          "authorization-server scopes should win after resource scopes");

  context.authorization_server_scopes.clear();
  selected = mcp::auth::select_authorization_scopes(context);
  require(selected == mcp::auth::ScopeList{"default"},
          "default scopes should be fallback");
}

void test_offline_access_scope_append_is_advertisement_gated() {
  auto metadata = test_authorization_metadata();
  mcp::auth::ScopeList scopes = {"tools:read"};

  mcp::auth::add_offline_access_if_supported(scopes, metadata);
  require(scopes == mcp::auth::ScopeList{"tools:read", "offline_access"},
          "offline_access should be appended when advertised");

  mcp::auth::add_offline_access_if_supported(scopes, metadata);
  require(scopes == mcp::auth::ScopeList{"tools:read", "offline_access"},
          "offline_access should not be duplicated");

  metadata.scopes_supported = {"tools:read"};
  scopes = {"tools:read"};
  mcp::auth::add_offline_access_if_supported(scopes, metadata);
  require(scopes == mcp::auth::ScopeList{"tools:read"},
          "offline_access should not be appended when unsupported");

  scopes.clear();
  metadata.scopes_supported = {"offline_access"};
  mcp::auth::add_offline_access_if_supported(scopes, metadata);
  require(scopes.empty(), "empty scope requests should remain empty");
}

void test_client_credentials_metadata_validation() {
  auto metadata = test_authorization_metadata();
  metadata.grant_types_supported = {"client_credentials"};
  metadata.token_endpoint_auth_methods_supported = {"client_secret_post"};

  auto valid = mcp::auth::validate_client_credentials_metadata(metadata);
  require(valid.has_value(), "client credentials metadata should validate");

  metadata.grant_types_supported = {"authorization_code"};
  auto missing_grant =
      mcp::auth::validate_client_credentials_metadata(metadata);
  require(!missing_grant.has_value(),
          "client credentials validation should require advertised grant");
  require(missing_grant.error().message.find("client_credentials") !=
              std::string::npos,
          "missing grant error should name client_credentials");

  metadata.grant_types_supported = {"client_credentials"};
  metadata.token_endpoint_auth_methods_supported = {"client_secret_basic"};
  auto unsupported_method =
      mcp::auth::validate_client_credentials_metadata(metadata);
  require(
      !unsupported_method.has_value(),
      "client credentials validation should reject unsupported auth method");
  require(unsupported_method.error().message.find("client_secret_post") !=
              std::string::npos,
          "unsupported auth method error should name client_secret_post");
}

void test_authorization_manager_authenticates_client_credentials() {
  auto metadata = test_authorization_metadata();
  metadata.grant_types_supported = {"client_credentials"};
  metadata.token_endpoint_auth_methods_supported = {"client_secret_post"};

  auto credential_store =
      std::make_shared<mcp::auth::InMemoryCredentialStore>();
  auto endpoint = std::make_shared<RecordingTokenEndpoint>();
  endpoint->client_credentials_result.access_token = "machine-access";
  endpoint->client_credentials_result.scopes = {"tools:read"};

  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          metadata, {});
  manager.set_credential_store(credential_store);
  manager.set_token_endpoint(endpoint);

  mcp::auth::ClientCredentialsConfig config;
  config.client_id = "machine-client";
  config.client_secret = "machine-secret";
  config.resource = "https://resource.example/mcp";
  config.scopes = {"tools:read"};

  auto tokens = manager.authenticate_client_credentials(config);
  require(tokens.has_value(),
          "client credentials authentication should return tokens");
  require(tokens->access_token == "machine-access",
          "client credentials access token mismatch");
  require(endpoint->client_credentials_request.has_value(),
          "token endpoint should receive client credentials request");
  require(endpoint->client_credentials_request->credentials.client_id ==
              "machine-client",
          "client credentials request client_id mismatch");
  require(
      manager.lifecycle_state() == mcp::auth::OAuthLifecycleState::kAuthorized,
      "client credentials flow should authorize manager");
  require(manager.current_scopes() == mcp::auth::ScopeList{"tools:read"},
          "client credentials granted scopes mismatch");

  const auto stored = credential_store->load(manager.credential_key());
  require(stored.has_value() && stored->has_value(),
          "client credentials should be stored");
  require((*stored)->token_set->access_token == "machine-access",
          "stored client credentials access token mismatch");
}

void test_metadata_discovery_executor_uses_rmcp_order() {
  RecordingMetadataEndpoint endpoint;
  mcp::auth::ProtectedResourceMetadata resource;
  resource.resource = "https://resource.example/api/mcp";
  resource.authorization_servers = {"https://issuer.example/tenant"};
  resource.scopes_supported = {"tools:read"};
  endpoint.protected_resources.emplace(
      "https://resource.example/.well-known/oauth-protected-resource/api/mcp",
      resource);

  auto authorization_metadata = test_authorization_metadata();
  endpoint.authorization_servers.emplace(
      "https://issuer.example/.well-known/oauth-authorization-server/tenant",
      authorization_metadata);

  mcp::auth::HttpResponseMetadata response;
  response.status_code = 401;
  response.headers.emplace(
      "WWW-Authenticate",
      "Bearer resource_metadata=\"https://resource.example/.well-known/"
      "oauth-protected-resource/missing\"");
  const auto decision = mcp::auth::analyze_auth_response(response);
  require(decision.has_value(), "auth challenge should analyze for discovery");

  mcp::auth::MetadataDiscoveryExecutor executor(endpoint);
  const auto discovered =
      executor.discover("https://resource.example/api/mcp", *decision);
  require(discovered.has_value(), "metadata discovery should succeed");
  require(discovered->protected_resource.resource ==
              "https://resource.example/api/mcp",
          "protected resource discovery result mismatch");
  require(discovered->authorization_server.has_value(),
          "authorization server metadata should be discovered");
  require(
      discovered->authorization_server->issuer == authorization_metadata.issuer,
      "authorization server discovery result mismatch");

  require(endpoint.protected_requests.size() == 2,
          "protected metadata discovery should try challenge then well-known");
  require(endpoint.protected_requests[0] ==
              "https://resource.example/.well-known/oauth-protected-resource/"
              "missing",
          "challenge resource_metadata should be tried first");
  require(endpoint.protected_requests[1] ==
              "https://resource.example/.well-known/oauth-protected-resource/"
              "api/mcp",
          "resource path well-known should be tried second");
  require(endpoint.authorization_requests.size() == 1,
          "authorization metadata should stop after first success");
  require(endpoint.authorization_requests[0] ==
              "https://issuer.example/.well-known/"
              "oauth-authorization-server/tenant",
          "authorization server metadata URL mismatch");
}

void test_metadata_discovery_executor_reports_failures() {
  RecordingMetadataEndpoint endpoint;
  mcp::auth::MetadataDiscoveryExecutor executor(endpoint);
  const auto discovered = executor.discover("https://resource.example/api/mcp");
  require(!discovered.has_value(),
          "metadata discovery should fail when no candidate succeeds");
  require(
      discovered.error().code ==
          static_cast<int>(mcp::auth::OAuthErrorCode::kMetadataDiscoveryFailed),
      "metadata discovery failure code mismatch");
  require(discovered.error().message ==
              "protected resource metadata discovery failed",
          "metadata discovery failure message mismatch");
}

void test_http_metadata_endpoint_parses_oauth_metadata() {
  std::vector<std::string> requested_urls;
  mcp::auth::HttpOAuthMetadataEndpoint endpoint(
      [&requested_urls](const mcp::auth::MetadataFetchRequest& request)
          -> mcp::core::Result<mcp::auth::OAuthHttpResponse> {
        requested_urls.push_back(request.url);
        if (request.url.find("oauth-protected-resource") != std::string::npos) {
          return mcp::auth::OAuthHttpResponse{200,
                                              {},
                                              R"json({
                "resource": "https://resource.example/api/mcp",
                "authorization_servers": ["https://issuer.example/tenant"],
                "scopes_supported": ["tools:read", "tools:call"],
                "resource_name": "Resource API"
              })json"};
        }
        return mcp::auth::OAuthHttpResponse{200,
                                            {},
                                            R"json({
              "issuer": "https://issuer.example/tenant",
              "authorization_endpoint": "https://issuer.example/authorize",
              "token_endpoint": "https://issuer.example/token",
              "registration_endpoint": "https://issuer.example/register",
              "response_types_supported": ["code"],
              "grant_types_supported": ["authorization_code", "refresh_token"],
              "code_challenge_methods_supported": ["S256"],
              "client_id_metadata_document_supported": true
            })json"};
      });

  const auto resource = endpoint.fetch_protected_resource_metadata(
      {"https://resource.example/.well-known/oauth-protected-resource", {}});
  require(resource.has_value(), "protected metadata should parse");
  require(resource->resource == "https://resource.example/api/mcp",
          "protected metadata resource mismatch");
  require(resource->authorization_servers ==
              mcp::auth::StringList{"https://issuer.example/tenant"},
          "protected metadata authorization server mismatch");

  const auto server = endpoint.fetch_authorization_server_metadata(
      {"https://issuer.example/.well-known/oauth-authorization-server", {}});
  require(server.has_value(), "authorization metadata should parse");
  require(server->issuer == "https://issuer.example/tenant",
          "authorization metadata issuer mismatch");
  require(server->registration_endpoint ==
              std::optional<std::string>{"https://issuer.example/register"},
          "authorization metadata registration endpoint mismatch");
  require(mcp::auth::supports_client_id_metadata_document(*server),
          "authorization metadata should retain CIMD support flag");
  require(requested_urls.size() == 2,
          "metadata endpoint request count mismatch");
}

void test_http_metadata_endpoint_reports_http_and_json_errors() {
  mcp::auth::HttpOAuthMetadataEndpoint http_error_endpoint(
      [](const mcp::auth::MetadataFetchRequest&)
          -> mcp::core::Result<mcp::auth::OAuthHttpResponse> {
        return mcp::auth::OAuthHttpResponse{404, {}, "{}"};
      });
  const auto http_error =
      http_error_endpoint.fetch_protected_resource_metadata({"https://x", {}});
  require(!http_error.has_value(), "HTTP metadata 404 should fail");
  require(http_error.error().category == mcp::auth::AuthErrorCategory,
          "HTTP metadata error category mismatch");

  mcp::auth::HttpOAuthMetadataEndpoint json_error_endpoint(
      [](const mcp::auth::MetadataFetchRequest&)
          -> mcp::core::Result<mcp::auth::OAuthHttpResponse> {
        return mcp::auth::OAuthHttpResponse{200, {}, "{not-json"};
      });
  const auto json_error =
      json_error_endpoint.fetch_authorization_server_metadata(
          {"https://x", {}});
  require(!json_error.has_value(), "invalid metadata JSON should fail");
  require(json_error.error().message ==
              "OAuth metadata endpoint returned invalid JSON",
          "invalid metadata JSON error mismatch");
}

void test_http_token_endpoint_exchanges_code_and_parses_token_response() {
  std::optional<mcp::auth::OAuthHttpRequest> observed;
  const auto now = mcp::auth::SystemClock::now();
  mcp::auth::HttpOAuthTokenEndpoint endpoint(
      [&observed](const mcp::auth::OAuthHttpRequest& request)
          -> mcp::core::Result<mcp::auth::OAuthHttpResponse> {
        observed = request;
        return mcp::auth::OAuthHttpResponse{
            200,
            {},
            R"({"access_token":"access","token_type":"DPoP","refresh_token":"refresh","expires_in":60,"scope":"tools:read prompts:read","extra":true})"};
      },
      now);

  mcp::auth::StoredAuthorizationState state;
  state.state = "csrf";
  state.pkce = test_pkce();
  state.requested_scopes = {"tools:read"};

  mcp::auth::TokenExchangeRequest request;
  request.client = test_client_config();
  request.client.client_secret = "secret value";
  request.authorization_server = test_authorization_metadata();
  request.resource = "https://resource.example/mcp";
  request.authorization_code = "code value";
  request.state = state;
  request.additional_parameters.emplace("audience", "mcp api");

  const auto token = endpoint.exchange_authorization_code(request);
  require(token.has_value(), "HTTP token endpoint exchange should parse token");
  require(observed.has_value(), "HTTP token endpoint should issue POST");
  require(observed->method == "POST", "token request method mismatch");
  require(observed->url == request.authorization_server.token_endpoint,
          "token request URL mismatch");
  require(observed->headers.at("Content-Type") ==
              "application/x-www-form-urlencoded",
          "token request content type mismatch");
  require(
      observed->body.find("grant_type=authorization_code") != std::string::npos,
      "token exchange body should include authorization_code grant");
  require(observed->body.find("code=code%20value") != std::string::npos,
          "token exchange body should URL encode code");
  require(
      observed->body.find("code_verifier=verifier-123") != std::string::npos,
      "token exchange body should include PKCE verifier");
  require(
      observed->body.find("client_secret=secret%20value") != std::string::npos,
      "token exchange body should include encoded client secret");
  require(observed->body.find("audience=mcp%20api") != std::string::npos,
          "token exchange body should include additional parameters");
  require(token->access_token == "access", "access token mismatch");
  require(token->token_type == "DPoP", "token type mismatch");
  require(token->refresh_token == "refresh", "refresh token mismatch");
  require(token->scopes.size() == 2 && token->scopes.front() == "tools:read",
          "token scopes mismatch");
  require(token->expires_at.has_value() && *token->expires_at > now,
          "token expiry should be derived from expires_in");
  require(token->metadata.at("extra") == "true",
          "token extension metadata mismatch");
}

void test_http_token_endpoint_exchanges_client_credentials() {
  std::optional<mcp::auth::OAuthHttpRequest> observed;
  mcp::auth::HttpOAuthTokenEndpoint endpoint([&observed](
                                                 const mcp::auth::
                                                     OAuthHttpRequest& request)
                                                 -> mcp::core::Result<
                                                     mcp::auth::
                                                         OAuthHttpResponse> {
    observed = request;
    return mcp::auth::OAuthHttpResponse{
        200,
        {},
        R"({"access_token":"machine-access","expires_in":60,"scope":"tools:read"})"};
  });

  mcp::auth::TokenClientCredentialsRequest request;
  request.authorization_server = test_authorization_metadata();
  request.credentials.client_id = "machine client";
  request.credentials.client_secret = "machine secret";
  request.credentials.resource = "https://resource.example/mcp";
  request.credentials.scopes = {"tools:read"};
  request.additional_parameters.emplace("audience", "mcp api");

  auto token = endpoint.exchange_client_credentials(request);
  require(token.has_value(),
          "HTTP token endpoint should exchange client credentials");
  require(observed.has_value(),
          "HTTP token endpoint should issue client credentials POST");
  require(observed->method == "POST",
          "client credentials request method mismatch");
  require(observed->url == request.authorization_server.token_endpoint,
          "client credentials request URL mismatch");
  require(
      observed->body.find("grant_type=client_credentials") != std::string::npos,
      "client credentials body should include grant type");
  require(
      observed->body.find("client_id=machine%20client") != std::string::npos,
      "client credentials body should encode client_id");
  require(observed->body.find("client_secret=machine%20secret") !=
              std::string::npos,
          "client credentials body should encode client_secret");
  require(
      observed->body.find("resource=https%3A%2F%2Fresource.example%2Fmcp") !=
          std::string::npos,
      "client credentials body should encode resource");
  require(observed->body.find("scope=tools%3Aread") != std::string::npos,
          "client credentials body should include scopes");
  require(observed->body.find("audience=mcp%20api") != std::string::npos,
          "client credentials body should include additional parameters");
  require(token->access_token == "machine-access",
          "client credentials parsed access token mismatch");
}

void test_http_token_endpoint_refreshes_and_reports_errors() {
  int calls = 0;
  const auto now = mcp::auth::SystemClock::now();
  mcp::auth::HttpOAuthTokenEndpoint endpoint(
      [&calls](const mcp::auth::OAuthHttpRequest& request)
          -> mcp::core::Result<mcp::auth::OAuthHttpResponse> {
        ++calls;
        require(
            request.body.find("grant_type=refresh_token") != std::string::npos,
            "refresh body should include refresh grant");
        require(
            request.body.find("refresh_token=old-refresh") != std::string::npos,
            "refresh body should include refresh token");
        if (calls == 1) {
          return mcp::auth::OAuthHttpResponse{
              200,
              {},
              R"({"access_token":"new-access","expires_in":120,"scope":"tools:read"})"};
        }
        return mcp::auth::OAuthHttpResponse{500, {}, "{}"};
      },
      now);

  mcp::auth::TokenRefreshRequest request;
  request.client = test_client_config();
  request.authorization_server = test_authorization_metadata();
  request.resource = "https://resource.example/mcp";
  request.refresh_token = "old-refresh";
  request.scopes = {"tools:read"};

  auto refreshed = endpoint.refresh_access_token(request);
  require(refreshed.has_value(), "HTTP token refresh should parse token");
  require(refreshed->token_set.access_token == "new-access",
          "refreshed access token mismatch");
  require(!refreshed->refresh_token_rotated,
          "missing refresh token should not be marked as rotated");

  refreshed = endpoint.refresh_access_token(request);
  require(!refreshed.has_value(),
          "HTTP token refresh should report HTTP error");
  require(refreshed.error().code ==
              static_cast<int>(mcp::auth::OAuthErrorCode::kTokenRefreshFailed),
          "refresh HTTP error code mismatch");
}

void test_client_registration_request_uses_public_client_defaults() {
  mcp::auth::ClientRegistrationOptions options;
  options.client_name = "cxxmcp test client";
  options.redirect_uri = "http://localhost/callback";
  options.scopes = {"tools:read", "offline_access"};

  const auto request = mcp::auth::build_client_registration_request(options);
  require(request.has_value(), "client registration request should build");
  require(request->redirect_uris ==
              mcp::auth::StringList{"http://localhost/callback"},
          "registration redirect URI mismatch");
  require(request->grant_types ==
              mcp::auth::StringList{"authorization_code", "refresh_token"},
          "registration grant types mismatch");
  require(request->response_types == mcp::auth::StringList{"code"},
          "registration response types mismatch");
  require(request->scope == options.scopes, "registration scope mismatch");
  require(request->token_endpoint_auth_method == "none",
          "public client auth method mismatch");

  options.redirect_uri.clear();
  const auto missing_redirect =
      mcp::auth::build_client_registration_request(options);
  require(!missing_redirect.has_value(),
          "registration request should require redirect_uri");
}

void test_authorization_manager_registers_dynamic_client() {
  auto metadata = test_authorization_metadata();
  metadata.registration_endpoint = "https://issuer.example/register";

  auto endpoint = std::make_shared<RecordingRegistrationEndpoint>();
  endpoint->response.client_id = "registered-client";
  endpoint->response.client_secret = "";
  endpoint->response.metadata.emplace("registration", "ok");

  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          metadata, {});
  manager.set_client_registration_endpoint(endpoint);

  mcp::auth::ClientRegistrationOptions options;
  options.client_name = "cxxmcp";
  options.redirect_uri = "http://localhost/callback";
  options.scopes = {"tools:read"};
  const auto configured = manager.register_client(options);
  require(configured.has_value(), "dynamic client registration should succeed");
  require(configured->client_id == "registered-client",
          "registered client_id mismatch");
  require(!configured->client_secret.has_value(),
          "empty client_secret should be treated as public client");
  require(manager.client_config().client_id == "registered-client",
          "manager should store registered client config");
  require(endpoint->request.has_value(),
          "registration endpoint should receive request");
  require(endpoint->request->registration_endpoint ==
              "https://issuer.example/register",
          "registration endpoint URL mismatch");
  require(endpoint->request->registration.client_name == "cxxmcp",
          "registration client_name mismatch");
}

void test_authorization_session_prefers_client_id_metadata_url() {
  auto metadata = test_authorization_metadata();
  metadata.registration_endpoint = "https://issuer.example/register";
  metadata.metadata.emplace("client_id_metadata_document_supported", "true");

  auto endpoint = std::make_shared<RecordingRegistrationEndpoint>();
  endpoint->response.client_id = "should-not-register";

  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          metadata, {});
  manager.set_client_registration_endpoint(endpoint);

  mcp::auth::AuthorizationSessionRequest request;
  request.client.client_id_metadata_url =
      "https://client.example/metadata/cxxmcp.json";
  request.client.redirect_uri = "http://localhost/callback";
  request.client.scopes = {"tools:read"};
  request.pkce = test_pkce();
  request.state = "csrf";

  const auto session = manager.start_session(request);
  require(session.has_value(),
          "authorization session should use client metadata URL");
  require(manager.client_config().client_id ==
              "https://client.example/metadata/cxxmcp.json",
          "metadata URL should become client_id");
  require(!endpoint->request.has_value(),
          "client metadata URL support should avoid dynamic registration");
  require(session->authorization_url().find(
              "client_id=https%3A%2F%2Fclient.example%2Fmetadata%2Fcxxmcp."
              "json") != std::string::npos,
          "authorization URL should encode URL client_id");
}

void test_authorization_session_falls_back_to_dynamic_registration() {
  auto metadata = test_authorization_metadata();
  metadata.registration_endpoint = "https://issuer.example/register";

  auto endpoint = std::make_shared<RecordingRegistrationEndpoint>();
  endpoint->response.client_id = "registered-client";

  mcp::auth::AuthorizationManager manager("https://resource.example/mcp",
                                          metadata, {});
  manager.set_client_registration_endpoint(endpoint);

  mcp::auth::AuthorizationSessionRequest request;
  request.client.client_id_metadata_url =
      "https://client.example/metadata/cxxmcp.json";
  request.client.client_name = "fallback client";
  request.client.redirect_uri = "http://localhost/callback";
  request.client.scopes = {"tools:read"};
  request.pkce = test_pkce();
  request.state = "csrf";

  const auto session = manager.start_session(request);
  require(session.has_value(),
          "authorization session should fall back to dynamic registration");
  require(endpoint->request.has_value(),
          "fallback should invoke dynamic registration endpoint");
  require(manager.client_config().client_id == "registered-client",
          "registered fallback client_id mismatch");
  require(session->authorization_url().find("client_id=registered-client") !=
              std::string::npos,
          "authorization URL should use registered client_id");
}

void test_client_id_metadata_url_validation() {
  require(mcp::auth::is_valid_client_id_metadata_url(
              "https://client.example/metadata/cxxmcp.json"),
          "HTTPS client metadata URL with path should be valid");
  require(!mcp::auth::is_valid_client_id_metadata_url(
              "http://client.example/metadata/cxxmcp.json"),
          "non-HTTPS client metadata URL should be invalid");
  require(!mcp::auth::is_valid_client_id_metadata_url("https://client.example"),
          "root client metadata URL should be invalid");
  require(!mcp::auth::is_valid_client_id_metadata_url(
              "https://user@client.example/metadata/cxxmcp.json"),
          "client metadata URL with userinfo should be invalid");
  require(!mcp::auth::is_valid_client_id_metadata_url(
              "https://client.example/metadata/../cxxmcp.json"),
          "client metadata URL with dot segment should be invalid");
}

void test_jwt_verifier_boundary_is_verify_only() {
  RecordingJwtVerifier verifier;
  mcp::auth::JwtVerificationRequest request;
  request.jwt = "header.payload.signature";
  request.purpose = mcp::auth::JwtVerificationPurpose::kAccessToken;
  request.issuer = "https://issuer.example";
  request.audience = "https://resource.example";
  request.required_algorithm = "ES256";
  request.required_claims.emplace("scope", "tools:read");

  const auto verified = verifier.verify(request);
  require(verified.has_value(), "JWT verifier boundary should verify");
  require(verifier.request.has_value(), "JWT verifier should receive request");
  require(verifier.request->required_algorithm == "ES256",
          "JWT verifier should receive required algorithm");
  require(verified->issuer == "https://issuer.example",
          "verified JWT issuer mismatch");
  require(verified->audience == "https://resource.example",
          "verified JWT audience mismatch");
}

#ifdef CXXMCP_AUTH_TEST_HAS_SERVER
void test_dpop_bearer_auth_provider_uses_injected_verifiers() {
  RecordingJwtVerifier jwt_verifier;
  RecordingDpopVerifier dpop_verifier;
  mcp::auth::InMemoryDpopReplayCache replay_cache;

  mcp::auth::DpopAuthProviderOptions options;
  options.issuer = "https://issuer.example";
  options.audience = "https://resource.example";
  options.require_dpop = true;
  options.dpop_claims.clock_skew_tolerance = std::chrono::seconds(30);

  mcp::auth::DpopBearerAuthProvider provider(
      jwt_verifier, dpop_verifier,
      [](std::string_view token) -> mcp::core::Result<std::string> {
        require(token == "access-token", "hash callback token mismatch");
        return std::string("expected-ath");
      },
      &replay_cache, options);

  mcp::server::AuthRequest request;
  request.headers.emplace("Authorization", "DPoP access-token");
  request.headers.emplace("DPoP", "proof.jwt");
  request.http_method = "POST";
  request.http_url = "https://resource.example/mcp";

  auto identity = provider.authenticate(request);
  require(identity.has_value(), "DPoP provider should authenticate");
  require(identity->subject == "subject", "DPoP provider subject mismatch");
  require(identity->claims.at("iss") == "https://issuer.example",
          "DPoP provider issuer claim mismatch");
  require(jwt_verifier.request.has_value(),
          "DPoP provider should verify access token");
  require(jwt_verifier.request->jwt == "access-token",
          "JWT verifier access token mismatch");
  require(dpop_verifier.proof == "proof.jwt", "DPoP verifier proof mismatch");
  require(dpop_verifier.target.has_value(),
          "DPoP verifier should receive HTTP target");
  require(dpop_verifier.target->method == "POST",
          "DPoP verifier method mismatch");
  require(dpop_verifier.target->url == "https://resource.example/mcp",
          "DPoP verifier URL mismatch");

  auto replayed = provider.authenticate(request);
  require(!replayed.has_value(), "replayed DPoP proof should fail");

  mcp::server::AuthRequest missing_target = request;
  missing_target.http_url.reset();
  auto missing = provider.authenticate(missing_target);
  require(!missing.has_value(), "DPoP provider should require HTTP target");
}
#endif

}  // namespace

int main() {
  const std::vector<void (*)()> tests{
      test_constant_time_string_equal_handles_lengths_and_empty_values,
      test_secure_string_wraps_dpop_key_material,
      test_dpop_claim_validation_enforces_target_clock_and_ath,
      test_dpop_replay_cache_rejects_duplicate_jti,
      test_dpop_authorization_headers_use_injected_signer,
      test_jwks_parser_selector_and_cache,
      test_www_authenticate_parser_parses_mcp_oauth_challenge,
      test_www_authenticate_parser_handles_escapes_and_token68,
      test_www_authenticate_parser_rejects_malformed_headers,
      test_in_memory_token_store_separates_token_keys,
      test_in_memory_credential_and_state_stores,
      test_authorization_manager_builds_url_and_stores_state,
      test_authorization_url_requires_secure_endpoints_and_redirects,
      test_authorization_manager_exchanges_code_and_saves_credentials,
      test_authorization_manager_rejects_expired_authorization_state,
      test_authorization_manager_uses_configured_authorization_state_ttl,
      test_authorization_manager_refresh_preserves_and_rotates_refresh_token,
      test_authorization_manager_get_access_token_refreshes_near_expiry,
      test_authorization_manager_refresh_retry_helper_returns_new_bearer,
      test_scope_upgrade_uses_www_authenticate_boundary,
      test_metadata_discovery_plan_prefers_www_authenticate_resource_metadata,
      test_metadata_discovery_url_candidates_reject_ssrf_prone_inputs,
      test_authorization_server_metadata_url_candidates,
      test_scope_selection_uses_rmcp_priority_order,
      test_offline_access_scope_append_is_advertisement_gated,
      test_client_credentials_metadata_validation,
      test_authorization_manager_authenticates_client_credentials,
      test_metadata_discovery_executor_uses_rmcp_order,
      test_metadata_discovery_executor_reports_failures,
      test_http_metadata_endpoint_parses_oauth_metadata,
      test_http_metadata_endpoint_reports_http_and_json_errors,
      test_http_token_endpoint_exchanges_code_and_parses_token_response,
      test_http_token_endpoint_exchanges_client_credentials,
      test_http_token_endpoint_refreshes_and_reports_errors,
      test_client_registration_request_uses_public_client_defaults,
      test_authorization_manager_registers_dynamic_client,
      test_authorization_session_prefers_client_id_metadata_url,
      test_authorization_session_falls_back_to_dynamic_registration,
      test_client_id_metadata_url_validation,
      test_jwt_verifier_boundary_is_verify_only,
#ifdef CXXMCP_AUTH_TEST_HAS_SERVER
      test_dpop_bearer_auth_provider_uses_injected_verifiers,
#endif
  };

  for (const auto test : tests) {
    test();
  }
  return 0;
}
