// Copyright (c) 2025 [caomengxuan666]
//
// OAuth 2.1 authorization server example: demonstrates configuring
// HttpTransport with RFC 8414 metadata, authorization, token, registration,
// and revocation endpoints using in-memory stores.

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "cxxmcp/auth/server_auth_endpoints.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/server/http_transport.hpp"
#include "cxxmcp/server/server.hpp"

namespace {

using mcp::auth::AuthorizationDecision;
using mcp::auth::AuthorizationRequestParams;
using mcp::auth::AuthorizationServerConfig;
using mcp::auth::ClientRegistrationRequest;
using mcp::auth::ClientRegistrationResponse;
using mcp::auth::TokenEndpointResponse;
using mcp::auth::TokenRequestParams;
using mcp::auth::TokenRevocationParams;
using mcp::server::HttpTransport;
using mcp::server::HttpTransportOptions;

// Simple in-memory authorization code store.
struct CodeEntry {
  std::string client_id;
  std::string redirect_uri;
  std::optional<std::string> scope;
  std::string code_challenge;
  std::string code_challenge_method;
  std::chrono::steady_clock::time_point expires_at;
};

struct TokenEntry {
  std::string client_id;
  std::optional<std::string> scope;
  std::chrono::steady_clock::time_point expires_at;
};

struct ClientEntry {
  std::string client_id;
  std::optional<std::string> client_secret;
  ClientRegistrationRequest metadata;
};

class InMemoryStore {
 public:
  std::string create_code(const std::string& client_id,
                          const std::string& redirect_uri,
                          const std::optional<std::string>& scope,
                          const std::string& code_challenge,
                          const std::string& code_challenge_method) {
    std::lock_guard lock(mu_);
    auto code = "code_" + std::to_string(next_code_++);
    codes_[code] =
        CodeEntry{client_id,
                  redirect_uri,
                  scope,
                  code_challenge,
                  code_challenge_method,
                  std::chrono::steady_clock::now() + std::chrono::minutes(10)};
    return code;
  }

  std::optional<CodeEntry> consume_code(const std::string& code) {
    std::lock_guard lock(mu_);
    auto it = codes_.find(code);
    if (it == codes_.end()) return std::nullopt;
    if (std::chrono::steady_clock::now() > it->second.expires_at) {
      codes_.erase(it);
      return std::nullopt;
    }
    auto entry = std::move(it->second);
    codes_.erase(it);
    return entry;
  }

  std::string create_token(const std::string& client_id,
                           const std::optional<std::string>& scope) {
    std::lock_guard lock(mu_);
    auto token = "tok_" + std::to_string(next_token_++);
    tokens_[token] =
        TokenEntry{client_id, scope,
                   std::chrono::steady_clock::now() + std::chrono::hours(1)};
    return token;
  }

  bool revoke_token(const std::string& token) {
    std::lock_guard lock(mu_);
    return tokens_.erase(token) > 0;
  }

  ClientEntry register_client(std::vector<std::string> redirect_uris,
                              std::vector<std::string> grant_types,
                              std::vector<std::string> response_types,
                              std::optional<std::string> client_name) {
    std::lock_guard lock(mu_);
    auto id = "client_" + std::to_string(next_client_++);
    auto secret = "secret_" + std::to_string(next_client_++);
    ClientRegistrationRequest metadata;
    metadata.redirect_uris = std::move(redirect_uris);
    metadata.grant_types = std::move(grant_types);
    metadata.response_types = std::move(response_types);
    metadata.client_name = std::move(client_name);
    ClientEntry entry{id, secret, std::move(metadata)};
    clients_[id] = entry;
    return entry;
  }

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, CodeEntry> codes_;
  std::unordered_map<std::string, TokenEntry> tokens_;
  std::unordered_map<std::string, ClientEntry> clients_;
  int next_code_ = 1;
  int next_token_ = 1;
  int next_client_ = 1;
};

}  // namespace

int main() {
  InMemoryStore store;

  AuthorizationServerConfig as_config;
  as_config.issuer = "http://127.0.0.1:3001";
  as_config.scopes_supported = {"openid", "profile", "mcp:tools"};

  // Authorization endpoint: creates an authorization code and redirects.
  as_config.authorization_handler =
      [&store](const AuthorizationRequestParams& params)
      -> mcp::core::Result<AuthorizationDecision> {
    auto code =
        store.create_code(params.client_id, params.redirect_uri, params.scope,
                          params.code_challenge.value_or(""),
                          params.code_challenge_method.value_or("S256"));
    return AuthorizationDecision{code, params.state, params.redirect_uri};
  };

  // Token endpoint: exchanges authorization code for access token.
  as_config.token_handler = [&store](const TokenRequestParams& params)
      -> mcp::core::Result<TokenEndpointResponse> {
    if (params.grant_type == "authorization_code") {
      if (!params.code.has_value()) {
        return mcp::core::unexpected(
            mcp::core::Error{0, "invalid_request: code is required"});
      }
      auto entry = store.consume_code(*params.code);
      if (!entry.has_value()) {
        return mcp::core::unexpected(
            mcp::core::Error{0, "invalid_grant: invalid or expired code"});
      }
      auto access_token = store.create_token(entry->client_id, entry->scope);
      TokenEndpointResponse response;
      response.access_token = access_token;
      response.token_type = "Bearer";
      response.expires_in = std::chrono::seconds{3600};
      response.scope = entry->scope;
      return response;
    }
    return mcp::core::unexpected(mcp::core::Error{0, "unsupported_grant_type"});
  };

  // Dynamic client registration endpoint.
  as_config.registration_handler =
      [&store](const ClientRegistrationRequest& params)
      -> mcp::core::Result<ClientRegistrationResponse> {
    if (params.redirect_uris.empty()) {
      return mcp::core::unexpected(
          mcp::core::Error{0, "invalid_request: redirect_uris required"});
    }
    auto entry =
        store.register_client(params.redirect_uris, params.grant_types,
                              params.response_types, params.client_name);
    ClientRegistrationResponse response;
    response.client_id = entry.client_id;
    response.client_secret = entry.client_secret;
    response.client_metadata = std::move(entry.metadata);
    return response;
  };

  // Token revocation endpoint.
  as_config.revocation_handler = [&store](const TokenRevocationParams& params)
      -> mcp::core::Result<mcp::core::Unit> {
    store.revoke_token(params.token);
    return mcp::core::Unit{};
  };

  // Build the HTTP transport with OAuth authorization server.
  HttpTransportOptions options;
  options.listen_host = "127.0.0.1";
  options.listen_port = 3001;
  options.path = "/mcp";
  options.authorization_server = std::move(as_config);

  auto transport = std::make_unique<HttpTransport>(std::move(options));

  // Build the MCP server using ServerBuilder.
  mcp::protocol::ToolDefinition greet_def;
  greet_def.name = "greet";
  greet_def.description = "Return a greeting.";
  greet_def.input_schema = mcp::protocol::Json{
      {"type", "object"},
      {"properties",
       mcp::protocol::Json{{"name", mcp::protocol::Json{{"type", "string"}}}}}};

  auto server =
      mcp::server::ServerBuilder()
          .name("cxxmcp-oauth-server")
          .version("1.0.0")
          .with_transport(std::move(transport))
          .add_tool(
              greet_def,
              [](const mcp::server::ToolContext& context)
                  -> mcp::core::Result<mcp::protocol::ToolResult> {
                auto name =
                    context.arguments.value("name", std::string("world"));
                return mcp::protocol::ToolResult::text("Hello, " + name + "!");
              })
          .build();

  if (!server.has_value()) {
    std::cerr << "failed to build server: " << server.error().message << '\n';
    return 1;
  }

  std::cout << "OAuth authorization server running on http://127.0.0.1:3001\n"
            << "  Metadata: /.well-known/oauth-authorization-server\n"
            << "  Authorize: /authorize\n"
            << "  Token:     /token\n"
            << "  Register:  /register\n"
            << "  Revoke:    /revoke\n"
            << "  MCP:       /mcp\n";

  // Block forever; the transport's background thread serves requests.
  while (true) {
    std::this_thread::sleep_for(std::chrono::hours{24});
  }
}
