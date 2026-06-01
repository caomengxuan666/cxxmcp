// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/auth/client_orchestrator.hpp>

int main() {
  mcp::auth::OAuthClientOrchestratorConfig config;
  config.resource_url = "https://resource.example/mcp";
  config.client_name = "public-header";
  return config.resource_url.empty() ? 1 : 0;
}
