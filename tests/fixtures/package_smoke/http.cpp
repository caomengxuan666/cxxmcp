// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/client/http_transport.hpp>
#include <cxxmcp/server/http_transport.hpp>
#include <cxxmcp/transport/http_transport.hpp>
#include <memory>
#include <utility>

namespace {

std::string endpoint_uri() {
#if defined(CXXMCP_PACKAGE_SMOKE_OPENSSL)
  return "https://127.0.0.1:3001/mcp";
#else
  return "http://127.0.0.1:3001/mcp";
#endif
}

}  // namespace

std::unique_ptr<mcp::client::HttpTransport> make_legacy_client() {
  mcp::client::HttpTransportOptions options;
  options.uri = endpoint_uri();
  return std::make_unique<mcp::client::HttpTransport>(std::move(options));
}

std::unique_ptr<mcp::transport::StreamableHttpClientTransport> make_client() {
  mcp::transport::StreamableHttpClientTransportOptions options;
  options.uri = endpoint_uri();
  return std::make_unique<mcp::transport::StreamableHttpClientTransport>(
      std::move(options));
}

std::unique_ptr<mcp::transport::StreamableHttpServerTransport> make_server() {
  mcp::transport::StreamableHttpServerTransportOptions options;
  options.listen_port = 3001;
  return std::make_unique<mcp::transport::StreamableHttpServerTransport>(
      std::move(options));
}

int main() { return 0; }
