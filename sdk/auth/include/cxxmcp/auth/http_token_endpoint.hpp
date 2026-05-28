// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

#include "cxxmcp/auth/http_metadata_endpoint.hpp"
#include "cxxmcp/auth/lifecycle.hpp"

/// @file
/// @brief Default OAuth token endpoint form encoder and JSON response parser.

namespace mcp::auth {

/// @brief Transport-neutral HTTP request used by the auth token endpoint.
struct OAuthHttpRequest {
  std::string method = "POST";
  std::string url;
  HeaderMap headers;
  std::string body;
};

/// @brief Application or SDK transport adapter used for token POST calls.
using OAuthHttpPost =
    std::function<core::Result<OAuthHttpResponse>(const OAuthHttpRequest&)>;

/// @brief Default OAuth token endpoint implementation over an injected HTTP
/// POST.
///
/// This class owns OAuth form construction and token-response parsing. It does
/// not own sockets, TLS, redirects, retries, client authentication policy
/// beyond form fields, DPoP proof generation, or JWKS/JWT verification.
class HttpOAuthTokenEndpoint final : public OAuthTokenEndpoint {
 public:
  explicit HttpOAuthTokenEndpoint(OAuthHttpPost post,
                                  TimePoint now = SystemClock::now())
      : post_(std::move(post)), now_(now) {}

  core::Result<TokenSet> exchange_authorization_code(
      const TokenExchangeRequest& request) override {
    if (request.authorization_server.token_endpoint.empty()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kTokenExchangeFailed, "token endpoint is required"));
    }

    auto form = FormBuilder{};
    form.add("grant_type", "authorization_code");
    form.add("code", request.authorization_code);
    form.add("redirect_uri", request.client.redirect_uri);
    form.add("client_id", request.client.client_id);
    if (request.client.client_secret.has_value()) {
      form.add("client_secret", *request.client.client_secret);
    }
    form.add("code_verifier", request.state.pkce.code_verifier);
    if (!request.resource.empty()) {
      form.add("resource", request.resource);
    }
    append_additional_parameters(&form, request.additional_parameters);

    auto response = post_form(request.authorization_server.token_endpoint,
                              std::move(form).body());
    if (!response.has_value()) {
      return mcp::core::unexpected(response.error());
    }
    return parse_token_set(*response, OAuthErrorCode::kTokenExchangeFailed);
  }

  core::Result<TokenRefreshResult> refresh_access_token(
      const TokenRefreshRequest& request) override {
    if (request.authorization_server.token_endpoint.empty()) {
      return mcp::core::unexpected(make_oauth_error(
          OAuthErrorCode::kTokenRefreshFailed, "token endpoint is required"));
    }

    auto form = FormBuilder{};
    form.add("grant_type", "refresh_token");
    form.add("refresh_token", request.refresh_token);
    form.add("client_id", request.client.client_id);
    if (request.client.client_secret.has_value()) {
      form.add("client_secret", *request.client.client_secret);
    }
    if (!request.resource.empty()) {
      form.add("resource", request.resource);
    }
    const auto scope = detail::join_scopes(request.scopes);
    if (!scope.empty()) {
      form.add("scope", scope);
    }
    append_additional_parameters(&form, request.additional_parameters);

    auto response = post_form(request.authorization_server.token_endpoint,
                              std::move(form).body());
    if (!response.has_value()) {
      return mcp::core::unexpected(response.error());
    }
    auto token_set =
        parse_token_set(*response, OAuthErrorCode::kTokenRefreshFailed);
    if (!token_set.has_value()) {
      return mcp::core::unexpected(token_set.error());
    }
    TokenRefreshResult result;
    result.refresh_token_rotated = token_set->refresh_token.has_value();
    result.token_set = std::move(*token_set);
    return result;
  }

  core::Result<TokenSet> exchange_client_credentials(
      const TokenClientCredentialsRequest& request) override {
    if (request.authorization_server.token_endpoint.empty()) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kClientCredentialsFailed,
                           "token endpoint is required"));
    }

    auto form = FormBuilder{};
    form.add("grant_type", "client_credentials");
    form.add("client_id", request.credentials.client_id);
    form.add("client_secret", request.credentials.client_secret);
    form.add("resource", request.credentials.resource);
    const auto scope = detail::join_scopes(request.credentials.scopes);
    if (!scope.empty()) {
      form.add("scope", scope);
    }
    append_additional_parameters(&form, request.additional_parameters);

    auto response = post_form(request.authorization_server.token_endpoint,
                              std::move(form).body());
    if (!response.has_value()) {
      return mcp::core::unexpected(response.error());
    }
    return parse_token_set(*response, OAuthErrorCode::kClientCredentialsFailed);
  }

 private:
  using Json = nlohmann::json;

  class FormBuilder {
   public:
    void add(const std::string& name, const std::string& value) {
      if (!body_.empty()) {
        body_.push_back('&');
      }
      body_.append(detail::oauth_url_encode(name));
      body_.push_back('=');
      body_.append(detail::oauth_url_encode(value));
    }

    std::string body() && { return std::move(body_); }

   private:
    std::string body_;
  };

  static void append_additional_parameters(FormBuilder* form,
                                           const MetadataMap& parameters) {
    for (const auto& parameter : parameters) {
      form->add(parameter.first, parameter.second);
    }
  }

  core::Result<OAuthHttpResponse> post_form(std::string url,
                                            std::string body) const {
    if (!post_) {
      return mcp::core::unexpected(
          make_oauth_error(OAuthErrorCode::kTokenExchangeUnavailable,
                           "OAuth token HTTP POST endpoint is not configured"));
    }

    OAuthHttpRequest request;
    request.url = std::move(url);
    request.headers.emplace("Accept", "application/json");
    request.headers.emplace("Content-Type",
                            "application/x-www-form-urlencoded");
    request.body = std::move(body);

    auto response = post_(request);
    if (!response.has_value()) {
      return mcp::core::unexpected(response.error());
    }
    return *response;
  }

  core::Result<TokenSet> parse_token_set(const OAuthHttpResponse& response,
                                         OAuthErrorCode error_code) const {
    if (response.status_code < 200 || response.status_code >= 300) {
      return mcp::core::unexpected(make_oauth_error(
          error_code, "OAuth token endpoint returned non-success HTTP status",
          std::to_string(response.status_code)));
    }

    Json value;
    try {
      value = Json::parse(response.body);
    } catch (const Json::exception& error) {
      return mcp::core::unexpected(make_oauth_error(
          error_code, "OAuth token endpoint returned invalid JSON",
          error.what()));
    }
    if (!value.is_object()) {
      return mcp::core::unexpected(make_oauth_error(
          error_code, "OAuth token endpoint response must be an object"));
    }

    const auto access_token = optional_string(value, "access_token");
    if (!access_token.has_value() || access_token->empty()) {
      return mcp::core::unexpected(make_oauth_error(
          error_code, "OAuth token endpoint response requires access_token"));
    }

    TokenSet token_set;
    token_set.access_token = *access_token;
    token_set.token_type =
        optional_string(value, "token_type").value_or("Bearer");
    token_set.refresh_token = optional_string(value, "refresh_token");
    if (auto scope = optional_string(value, "scope"); scope.has_value()) {
      token_set.scopes = detail::split_scopes(*scope);
    }
    if (auto expires_in = optional_integer(value, "expires_in");
        expires_in.has_value() && *expires_in > 0) {
      token_set.expires_at = now_ + std::chrono::seconds(*expires_in);
    }
    token_set.metadata = extension_metadata(value);
    return token_set;
  }

  static std::optional<std::string> optional_string(const Json& value,
                                                    const char* key) {
    const auto iter = value.find(key);
    if (iter == value.end() || iter->is_null()) {
      return std::nullopt;
    }
    if (iter->is_string()) {
      return iter->get<std::string>();
    }
    return std::nullopt;
  }

  static std::optional<std::int64_t> optional_integer(const Json& value,
                                                      const char* key) {
    const auto iter = value.find(key);
    if (iter == value.end() || iter->is_null()) {
      return std::nullopt;
    }
    if (iter->is_number_integer() || iter->is_number_unsigned()) {
      return iter->get<std::int64_t>();
    }
    return std::nullopt;
  }

  static MetadataMap extension_metadata(const Json& value) {
    MetadataMap result;
    for (auto iter = value.begin(); iter != value.end(); ++iter) {
      const auto& key = iter.key();
      if (key == "access_token" || key == "token_type" ||
          key == "refresh_token" || key == "expires_in" || key == "scope") {
        continue;
      }
      if (iter->is_string()) {
        result.emplace(key, iter->get<std::string>());
      } else if (iter->is_boolean()) {
        result.emplace(key, iter->get<bool>() ? "true" : "false");
      } else if (iter->is_number() || iter->is_object() || iter->is_array()) {
        result.emplace(key, iter->dump());
      }
    }
    return result;
  }

  OAuthHttpPost post_;
  TimePoint now_;
};

}  // namespace mcp::auth
