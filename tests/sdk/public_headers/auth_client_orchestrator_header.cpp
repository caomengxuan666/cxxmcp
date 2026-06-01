// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/auth/client_orchestrator.hpp>

int main() {
  auto builder = mcp::auth::oauth_client_flow("https://resource.example/mcp")
                     .client_name("public-header")
                     .scopes({"mcp:tools"});
  return 0;
}
