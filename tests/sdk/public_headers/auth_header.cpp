// Copyright (c) 2025 [caomengxuan666]

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "cxxmcp/auth.hpp"

int main() {
  mcp::auth::TokenSet tokens;
  tokens.access_token = "access";
  tokens.refresh_token = "refresh";
  tokens.expires_at = mcp::auth::SystemClock::now() - std::chrono::seconds(1);
  if (!tokens.expired()) {
    return 1;
  }

  mcp::auth::InMemoryTokenStore store;
  mcp::auth::TokenKey key{
      "https://resource.example",
      "https://issuer.example",
      "client-id",
      {},
  };
  if (!store.save(key, tokens).has_value()) {
    return 1;
  }
  auto loaded = store.load(key);
  if (!loaded.has_value() || !loaded->has_value()) {
    return 1;
  }

  mcp::auth::WwwAuthenticateChallenge challenge;
  challenge.scheme = "Bearer";
  challenge.parameters.emplace(
      std::string(mcp::auth::WwwAuthenticateResourceMetadataParam),
      "https://resource.example/.well-known/oauth-protected-resource");
  challenge.parameters.emplace(
      std::string(mcp::auth::WwwAuthenticateErrorParam),
      std::string(mcp::auth::WwwAuthenticateInsufficientScopeError));
  const std::vector<mcp::auth::WwwAuthenticateChallenge> challenges{challenge};
  if (!challenge.bearer() || !mcp::auth::insufficient_scope(challenge) ||
      !mcp::auth::first_resource_metadata_url(challenges).has_value()) {
    return 1;
  }

  mcp::auth::MetadataDiscoveryRequest discovery;
  discovery.protected_resource_metadata_url =
      *mcp::auth::first_resource_metadata_url(challenges);
  return discovery.protected_resource_metadata_url.empty() ? 1 : 0;
}
