// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <functional>
#include <string>
#include <utility>

#include "cxxmcp/auth/http_metadata_endpoint.hpp"
#include "cxxmcp/auth/jwks.hpp"
#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"
#include "nlohmann/json.hpp"

/// @file
/// @brief Concrete JWKS endpoint that fetches keys over HTTP.

namespace mcp::auth {

/// @brief Concrete implementation of JwksEndpoint that fetches JWKS over HTTP.
///
/// Uses an injected OAuthHttpGet function (same signature as
/// HttpOAuthMetadataEndpoint) so the transport layer remains decoupled.
class HttpJwksEndpoint final : public JwksEndpoint {
 public:
  explicit HttpJwksEndpoint(OAuthHttpGet get) : get_(std::move(get)) {}

  core::Result<JsonWebKeySet> fetch_jwks(
      const JwksFetchRequest& request) override {
    if (request.jwks_uri.empty()) {
      return core::unexpected(
          core::Error{static_cast<int>(OAuthErrorCode::kInvalidRequest),
                      "JWKS URI is required",
                      {},
                      std::string(AuthErrorCategory)});
    }

    MetadataFetchRequest fetch_request;
    fetch_request.url = request.jwks_uri;
    fetch_request.headers = request.headers;

    auto response = get_(fetch_request);
    if (!response.has_value()) {
      return core::unexpected(response.error());
    }
    if (response->status_code < 200 || response->status_code >= 300) {
      return core::unexpected(core::Error{
          static_cast<int>(OAuthErrorCode::kMetadataDiscoveryFailed),
          "JWKS fetch failed with HTTP " +
              std::to_string(response->status_code),
          {},
          std::string(AuthErrorCategory)});
    }

    nlohmann::json parsed;
    try {
      parsed = nlohmann::json::parse(response->body);
    } catch (const nlohmann::json::parse_error& ex) {
      return core::unexpected(core::Error{
          static_cast<int>(OAuthErrorCode::kMetadataDiscoveryFailed),
          "JWKS response is not valid JSON", ex.what(),
          std::string(AuthErrorCategory)});
    }

    return parse_json_web_key_set(parsed);
  }

 private:
  OAuthHttpGet get_;
};

}  // namespace mcp::auth
