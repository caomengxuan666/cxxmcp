// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/app/upstream_client.hpp"

#include <cctype>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "cxxmcp/client/http_transport.hpp"
#include "cxxmcp/client/process_stdio_transport.hpp"

namespace mcp::app {
namespace {

core::Error make_runtime_error(std::string message, std::string detail = {}) {
  return core::Error{static_cast<int>(protocol::ErrorCode::InternalError),
                     std::move(message), std::move(detail)};
}

core::Result<client::HttpTransportOptions> http_transport_options_from_url(
    const std::string& url) {
  constexpr std::string_view kHttpScheme = "http://";
  if (!core::starts_with(url, kHttpScheme)) {
    if (core::starts_with(url, "https://")) {
      return std::unexpected(make_runtime_error(
          "https upstream transport is not supported in this build", url));
    }
    return std::unexpected(
        make_runtime_error("http upstream url must start with http://", url));
  }

  const auto rest = std::string_view(url).substr(kHttpScheme.size());
  const auto slash = rest.find('/');
  const auto host_port = rest.substr(0, slash);
  const auto path = slash == std::string_view::npos ? std::string_view{"/"}
                                                    : rest.substr(slash);
  if (host_port.empty()) {
    return std::unexpected(
        make_runtime_error("http upstream url must include a host", url));
  }

  std::string host;
  int port = 80;

  if (core::starts_with(host_port, '[')) {
    const auto close = host_port.find(']');
    if (close == std::string_view::npos) {
      return std::unexpected(
          make_runtime_error("invalid ipv6 http upstream url", url));
    }
    host = std::string(host_port.substr(1, close - 1));
    if (close + 1 < host_port.size()) {
      if (host_port[close + 1] != ':') {
        return std::unexpected(
            make_runtime_error("invalid ipv6 http upstream url", url));
      }
      const auto port_text = host_port.substr(close + 2);
      if (port_text.empty()) {
        return std::unexpected(
            make_runtime_error("http upstream port must not be empty", url));
      }
      try {
        port = std::stoi(std::string(port_text));
      } catch (const std::exception&) {
        return std::unexpected(
            make_runtime_error("http upstream port must be numeric", url));
      }
    }
  } else {
    const auto colon = host_port.rfind(':');
    if (colon == std::string_view::npos) {
      host = std::string(host_port);
    } else {
      host = std::string(host_port.substr(0, colon));
      const auto port_text = host_port.substr(colon + 1);
      if (port_text.empty()) {
        return std::unexpected(
            make_runtime_error("http upstream port must not be empty", url));
      }
      try {
        port = std::stoi(std::string(port_text));
      } catch (const std::exception&) {
        return std::unexpected(
            make_runtime_error("http upstream port must be numeric", url));
      }
    }
  }

  if (host.empty()) {
    return std::unexpected(
        make_runtime_error("http upstream url must include a host", url));
  }
  if (port <= 0 || port > 65535) {
    return std::unexpected(
        make_runtime_error("http upstream port out of range", url));
  }

  client::HttpTransportOptions options;
  options.host = std::move(host);
  options.port = port;
  options.path = std::string(path);
  return options;
}

}  // namespace

core::Result<std::unique_ptr<client::Transport>>
make_client_transport_for_server(const McpServerDefinition& server) {
  if (!server.enabled) {
    return std::unexpected(
        make_runtime_error("cxxmcp server is disabled", server.id));
  }

  switch (server.transport) {
    case McpServerTransportKind::stdio:
      return std::make_unique<client::ProcessStdioTransport>(
          client::ProcessStdioTransportOptions{
              .command = server.stdio.command,
              .args = server.stdio.args,
              .cwd = server.stdio.cwd,
              .env = server.stdio.env,
          });
    case McpServerTransportKind::streamable_http: {
      if (server.http.url.empty()) {
        return std::unexpected(make_runtime_error(
            "http cxxmcp server url must not be empty", server.id));
      }

      auto options = http_transport_options_from_url(server.http.url);
      if (!options) {
        return std::unexpected(options.error());
      }

      options->headers = server.http.headers;
      return std::make_unique<client::HttpTransport>(std::move(*options));
    }
    case McpServerTransportKind::legacy_sse:
      return std::unexpected(make_runtime_error(
          "legacy sse upstream transport is not supported", server.id));
  }

  return std::unexpected(
      make_runtime_error("unknown cxxmcp server transport", server.id));
}

}  // namespace mcp::app
