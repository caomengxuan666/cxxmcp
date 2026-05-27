// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

#include "cxxmcp/auth/lifecycle.hpp"

/// @file
/// @brief Default HTTP metadata endpoint parser for OAuth discovery.

namespace mcp::auth {

/// @brief Transport-neutral HTTP response used by the auth metadata endpoint.
struct OAuthHttpResponse {
  int status_code = 0;
  HeaderMap headers;
  std::string body;
};

/// @brief Application or SDK transport adapter used for metadata GET calls.
using OAuthHttpGet =
    std::function<core::Result<OAuthHttpResponse>(const MetadataFetchRequest&)>;

/// @brief Default metadata endpoint implementation over an injected HTTP GET.
///
/// This class owns status handling and RFC 9728 / RFC 8414 JSON parsing. It
/// does not own sockets, TLS, redirects, retries, browser launching, or
/// loopback receivers.
class HttpOAuthMetadataEndpoint final : public OAuthMetadataEndpoint {
 public:
  explicit HttpOAuthMetadataEndpoint(OAuthHttpGet get) : get_(std::move(get)) {}

  core::Result<ProtectedResourceMetadata> fetch_protected_resource_metadata(
      const MetadataFetchRequest& request) override {
    auto response = fetch_json(request);
    if (!response.has_value()) {
      return std::unexpected(response.error());
    }
    return parse_protected_resource_metadata(*response);
  }

  core::Result<AuthorizationServerMetadata> fetch_authorization_server_metadata(
      const MetadataFetchRequest& request) override {
    auto response = fetch_json(request);
    if (!response.has_value()) {
      return std::unexpected(response.error());
    }
    return parse_authorization_server_metadata(*response);
  }

 private:
  using Json = nlohmann::json;

  core::Result<Json> fetch_json(const MetadataFetchRequest& request) {
    if (!get_) {
      return std::unexpected(make_oauth_error(
          OAuthErrorCode::kMetadataDiscoveryFailed,
          "OAuth metadata HTTP GET endpoint is not configured"));
    }

    auto response = get_(request);
    if (!response.has_value()) {
      return std::unexpected(response.error());
    }
    if (response->status_code < 200 || response->status_code >= 300) {
      return std::unexpected(make_oauth_error(
          OAuthErrorCode::kMetadataDiscoveryFailed,
          "OAuth metadata endpoint returned non-success HTTP status",
          std::to_string(response->status_code)));
    }

    try {
      return Json::parse(response->body);
    } catch (const Json::exception& error) {
      return std::unexpected(make_oauth_error(
          OAuthErrorCode::kMetadataDiscoveryFailed,
          "OAuth metadata endpoint returned invalid JSON", error.what()));
    }
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

  static std::string string_or_empty(const Json& value, const char* key) {
    auto result = optional_string(value, key);
    return result.has_value() ? *result : std::string{};
  }

  static StringList string_list_or_empty(const Json& value, const char* key) {
    StringList result;
    const auto iter = value.find(key);
    if (iter == value.end() || !iter->is_array()) {
      return result;
    }
    for (const auto& entry : *iter) {
      if (entry.is_string()) {
        result.push_back(entry.get<std::string>());
      }
    }
    return result;
  }

  static MetadataMap extension_metadata(const Json& value) {
    MetadataMap result;
    for (auto iter = value.begin(); iter != value.end(); ++iter) {
      if (iter->is_string()) {
        result.emplace(iter.key(), iter->get<std::string>());
      } else if (iter->is_boolean()) {
        result.emplace(iter.key(), iter->get<bool>() ? "true" : "false");
      } else if (iter->is_number() || iter->is_object() || iter->is_array()) {
        result.emplace(iter.key(), iter->dump());
      }
    }
    return result;
  }

  static core::Result<ProtectedResourceMetadata>
  parse_protected_resource_metadata(const Json& value) {
    if (!value.is_object()) {
      return std::unexpected(make_oauth_error(
          OAuthErrorCode::kMetadataDiscoveryFailed,
          "protected resource metadata JSON must be an object"));
    }

    ProtectedResourceMetadata metadata;
    metadata.resource = string_or_empty(value, "resource");
    metadata.authorization_servers =
        string_list_or_empty(value, "authorization_servers");
    metadata.bearer_methods_supported =
        string_list_or_empty(value, "bearer_methods_supported");
    metadata.resource_signing_alg_values_supported =
        string_list_or_empty(value, "resource_signing_alg_values_supported");
    metadata.scopes_supported = string_list_or_empty(value, "scopes_supported");
    metadata.resource_name = optional_string(value, "resource_name");
    metadata.resource_documentation =
        optional_string(value, "resource_documentation");
    metadata.metadata = extension_metadata(value);
    return metadata;
  }

  static core::Result<AuthorizationServerMetadata>
  parse_authorization_server_metadata(const Json& value) {
    if (!value.is_object()) {
      return std::unexpected(make_oauth_error(
          OAuthErrorCode::kMetadataDiscoveryFailed,
          "authorization server metadata JSON must be an object"));
    }

    AuthorizationServerMetadata metadata;
    metadata.issuer = string_or_empty(value, "issuer");
    metadata.authorization_endpoint =
        string_or_empty(value, "authorization_endpoint");
    metadata.token_endpoint = string_or_empty(value, "token_endpoint");
    metadata.registration_endpoint =
        optional_string(value, "registration_endpoint");
    metadata.jwks_uri = optional_string(value, "jwks_uri");
    metadata.response_types_supported =
        string_list_or_empty(value, "response_types_supported");
    metadata.grant_types_supported =
        string_list_or_empty(value, "grant_types_supported");
    metadata.code_challenge_methods_supported =
        string_list_or_empty(value, "code_challenge_methods_supported");
    metadata.token_endpoint_auth_methods_supported =
        string_list_or_empty(value, "token_endpoint_auth_methods_supported");
    metadata.dpop_signing_alg_values_supported =
        string_list_or_empty(value, "dpop_signing_alg_values_supported");
    metadata.scopes_supported = string_list_or_empty(value, "scopes_supported");
    metadata.metadata = extension_metadata(value);
    return metadata;
  }

  OAuthHttpGet get_;
};

}  // namespace mcp::auth
