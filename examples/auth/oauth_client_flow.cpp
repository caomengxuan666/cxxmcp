// Copyright (c) 2025 [caomengxuan666]
//
// Example: Client-side OAuth 2.1 authorization flow using
// OAuthClientFlowBuilder.
//
// This example is parked behind CXXMCP_BUILD_EXPERIMENTAL_OAUTH_EXAMPLES until
// the OAuth public API shape is finalized.

#include <iostream>
#include <memory>
#include <string>

#include "cxxmcp/auth/client_orchestrator.hpp"
#include "cxxmcp/auth/openssl/pkce.hpp"
#include "httplib.h"

namespace {

class StdinCallback : public mcp::auth::OAuthClientCallback {
 public:
  mcp::core::Result<mcp::core::Unit> present_authorization_url(
      const std::string& url) override {
    std::cout << "\n=== Authorization Required ===\n";
    std::cout << "Open this URL in your browser:\n\n  " << url << "\n\n";
    std::cout << "After authorizing, paste the authorization code below.\n";
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::pair<std::string, std::string>> wait_for_callback(
      std::chrono::seconds /*timeout*/) override {
    std::string code;
    std::string state;
    std::cout << "Authorization code: ";
    std::getline(std::cin, code);
    std::cout << "State: ";
    std::getline(std::cin, state);
    return std::make_pair(code, state);
  }
};

// Parse a URL into scheme, host, port, and path components.
struct ParsedUrl {
  std::string host;
  int port = 80;
  std::string path = "/";
};

ParsedUrl parse_url(const std::string& url) {
  ParsedUrl result;
  auto rest = url;
  if (rest.starts_with("https://")) {
    result.port = 443;
    rest = rest.substr(8);
  } else if (rest.starts_with("http://")) {
    rest = rest.substr(7);
  }
  auto slash = rest.find('/');
  auto host_port = rest.substr(0, slash);
  result.path = slash != std::string::npos ? rest.substr(slash) : "/";
  auto colon = host_port.find(':');
  result.host = host_port.substr(0, colon);
  if (colon != std::string::npos) {
    result.port = std::stoi(host_port.substr(colon + 1));
  }
  return result;
}

mcp::core::Result<mcp::auth::OAuthHttpResponse> http_get(
    const mcp::auth::MetadataFetchRequest& request) {
  auto parsed = parse_url(request.url);
  httplib::Client client(parsed.host, parsed.port);
  auto response = client.Get(parsed.path);
  if (!response) {
    return mcp::core::unexpected(
        mcp::core::Error{0, "HTTP GET failed", {}, "transport"});
  }

  mcp::auth::OAuthHttpResponse result;
  result.status_code = response->status;
  result.body = response->body;
  for (const auto& [key, value] : response->headers) {
    result.headers[key] = value;
  }
  return result;
}

mcp::core::Result<mcp::auth::OAuthHttpResponse> http_post(
    const mcp::auth::OAuthHttpRequest& request) {
  auto parsed = parse_url(request.url);
  httplib::Client client(parsed.host, parsed.port);
  httplib::Headers headers;
  for (const auto& [key, value] : request.headers) {
    headers.emplace(key, value);
  }

  auto response = client.Post(parsed.path, headers, request.body,
                              "application/x-www-form-urlencoded");
  if (!response) {
    return mcp::core::unexpected(
        mcp::core::Error{0, "HTTP POST failed", {}, "transport"});
  }

  mcp::auth::OAuthHttpResponse result;
  result.status_code = response->status;
  result.body = response->body;
  for (const auto& [key, value] : response->headers) {
    result.headers[key] = value;
  }
  return result;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <resource-url>\n";
    std::cerr << "  e.g. " << argv[0] << " https://example.com/mcp\n";
    return 1;
  }

  const std::string resource_url = argv[1];

  StdinCallback callback;
  auto pkce_generator =
      std::make_shared<mcp::auth::openssl::OpenSslPkceGenerator>();

  auto flow = mcp::auth::oauth_client_flow(resource_url)
                  .client_name("cxxmcp-example-client")
                  .scopes({"mcp:tools", "mcp:resources"})
                  .callback_timeout(std::chrono::seconds(300))
                  .callback(callback)
                  .http_endpoints(http_get, http_post)
                  .pkce_generator(std::move(pkce_generator))
                  .build();
  if (!flow.has_value()) {
    std::cerr << "OAuth flow setup failed: " << flow.error().message << "\n";
    return 1;
  }

  std::cout << "Starting OAuth authorization flow for " << resource_url << "\n";

  auto tokens = (*flow)->authorize();
  if (!tokens.has_value()) {
    std::cerr << "Authorization failed: " << tokens.error().message << "\n";
    if (!tokens.error().detail.empty()) {
      std::cerr << "  Detail: " << tokens.error().detail << "\n";
    }
    return 1;
  }

  std::cout << "\n=== Authorization Successful ===\n";
  std::cout << "Access token: " << tokens->access_token.substr(0, 16)
            << "...\n";
  std::cout << "Token type:   " << tokens->token_type << "\n";
  if (tokens->refresh_token.has_value()) {
    std::cout << "Refresh token: " << tokens->refresh_token->substr(0, 16)
              << "...\n";
  }
  if (!tokens->scopes.empty()) {
    std::cout << "Scopes:       ";
    for (const auto& scope : tokens->scopes) {
      std::cout << scope << " ";
    }
    std::cout << "\n";
  }

  auto token = (*flow)->get_access_token();
  if (token.has_value()) {
    std::cout << "\nValid access token available: " << token->substr(0, 16)
              << "...\n";
  }

  return 0;
}
