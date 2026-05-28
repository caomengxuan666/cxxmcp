// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/auth/openssl/dpop.hpp>
#include <cxxmcp/auth/openssl/jws_verify.hpp>
#include <cxxmcp/auth/openssl/jwt.hpp>
#include <cxxmcp/auth/openssl/sha256.hpp>

int main() {
  const auto hash = mcp::auth::openssl::dpop_access_token_hash("access-token");
  const auto decoded = mcp::auth::openssl::base64url_decode_to_string("e30");
  mcp::auth::openssl::OpenSslDpopSigner signer;
  mcp::auth::openssl::OpenSslDpopVerifier dpop_verifier;
  mcp::auth::openssl::StaticJwksJwtVerifier verifier(
      mcp::auth::JsonWebKeySet{});
  return hash.has_value() && !hash->empty() && decoded.has_value() ? 0 : 1;
}
