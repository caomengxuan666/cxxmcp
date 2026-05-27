// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/auth.hpp"

#include <optional>
#include <string>

int main() {
  mcp::auth::ClientRegistrationOptions registration_options;
  registration_options.client_name = "package-smoke";
  registration_options.redirect_uri = "http://localhost/callback";
  registration_options.scopes = {"tools:read"};
  auto registration =
      mcp::auth::build_client_registration_request(registration_options);
  if (!registration.has_value()) {
    return 1;
  }

  mcp::auth::HttpOAuthMetadataEndpoint endpoint(
      [](const mcp::auth::MetadataFetchRequest&)
          -> mcp::core::Result<mcp::auth::OAuthHttpResponse> {
        return mcp::auth::OAuthHttpResponse{200,
                                            {},
                                            R"json({
              "issuer": "https://issuer.example",
              "authorization_endpoint": "https://issuer.example/authorize",
              "token_endpoint": "https://issuer.example/token",
              "client_id_metadata_document_supported": true
            })json"};
      });

  auto metadata = endpoint.fetch_authorization_server_metadata(
      {"https://issuer.example", {}});
  if (!metadata.has_value()) {
    return 1;
  }
  if (!mcp::auth::supports_client_id_metadata_document(*metadata)) {
    return 1;
  }
  return mcp::auth::is_valid_client_id_metadata_url(
             "https://client.example/metadata/cxxmcp.json")
             ? 0
             : 1;
}
