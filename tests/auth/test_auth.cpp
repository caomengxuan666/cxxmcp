// Copyright (c) 2025 [caomengxuan666]

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "cxxmcp/auth.hpp"

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
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
              std::string(mcp::auth::WwwAuthenticateResourceMetadataParam)) ==
              "https://resource.example/.well-known/oauth-protected-resource",
          "resource_metadata parameter mismatch");
  require(mcp::auth::insufficient_scope(parsed->at(0)),
          "insufficient_scope helper mismatch");
  require(parsed->at(0).parameter(
              std::string(mcp::auth::WwwAuthenticateScopeParam)) ==
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
  require(
      malformed.error().category == std::string(mcp::auth::AuthErrorCategory),
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

}  // namespace

int main() {
  const std::vector<void (*)()> tests{
      test_www_authenticate_parser_parses_mcp_oauth_challenge,
      test_www_authenticate_parser_handles_escapes_and_token68,
      test_www_authenticate_parser_rejects_malformed_headers,
      test_in_memory_token_store_separates_token_keys,
  };

  for (const auto test : tests) {
    test();
  }
  return 0;
}
