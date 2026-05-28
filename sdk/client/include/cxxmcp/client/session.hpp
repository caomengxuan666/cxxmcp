// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Convenience session wrapper around Client initialization and
/// discovery.

#include <memory>
#include <string>
#include <vector>

#include "cxxmcp/client/client.hpp"

namespace mcp::client {

/// @brief Options used by McpClientSession during initialization.
struct McpClientSessionOptions {
  /// Client name advertised in the initialize request.
  std::string client_name = "cxxmcp";

  /// Client version advertised in the initialize request.
  std::string client_version = "0";
};

/// @brief Convenience wrapper that owns a Client and exposes common MCP
/// workflows.
///
/// McpClientSession keeps the low-level Client available while naming common
/// actions in terms of discovery and session lifecycle. It does not hide the
/// underlying result model: all operations still return core::Result<T>.
class McpClientSession {
 public:
  /// @brief Constructs a session from a transport and initialization options.
  /// @param transport Transport owned by the underlying Client. It must not be
  /// null.
  /// @param options Client name and version used by initialize().
  explicit McpClientSession(std::unique_ptr<Transport> transport,
                            McpClientSessionOptions options = {});

  /// @brief Sends the initialize request using the configured session options.
  core::Result<protocol::Json> initialize();

  /// @brief Sends the initialized notification.
  core::Result<core::Unit> mark_initialized();

  /// @brief Discovers one page of prompts.
  core::Result<std::vector<protocol::Prompt>> discover_prompts();

  /// @brief Discovers all prompts by following pagination cursors.
  core::Result<std::vector<protocol::Prompt>> discover_all_prompts();

  /// @brief Gets a prompt using protocol parameters.
  core::Result<protocol::PromptsGetResult> get_prompt(
      const protocol::PromptsGetParams& params);

  /// @brief Discovers one page of resources.
  core::Result<std::vector<protocol::Resource>> discover_resources();

  /// @brief Discovers all resources by following pagination cursors.
  core::Result<std::vector<protocol::Resource>> discover_all_resources();

  /// @brief Reads a resource using protocol parameters.
  core::Result<protocol::ResourcesReadResult> read_resource(
      const protocol::ResourcesReadParams& params);

  /// @brief Discovers one page of resource templates.
  core::Result<std::vector<protocol::ResourceTemplate>>
  discover_resource_templates();

  /// @brief Discovers all resource templates by following pagination cursors.
  core::Result<std::vector<protocol::ResourceTemplate>>
  discover_all_resource_templates();

  /// @brief Discovers one page of tools.
  core::Result<std::vector<protocol::ToolDefinition>> discover_tools();

  /// @brief Discovers all tools by following pagination cursors.
  core::Result<std::vector<protocol::ToolDefinition>> discover_all_tools();

  /// @brief Calls a tool through the underlying Client.
  core::Result<protocol::ToolResult> call_tool(const protocol::ToolCall& call);

  /// @brief Requests completion using typed protocol parameters.
  core::Result<protocol::CompleteResult> complete(
      const protocol::CompleteParams& params);

  /// @brief Sets the server logging level by level name.
  core::Result<core::Unit> set_level(std::string_view level);

  /// @brief Subscribes to resource update notifications for a URI.
  core::Result<core::Unit> subscribe(std::string_view uri);

  /// @brief Unsubscribes from resource update notifications for a URI.
  core::Result<core::Unit> unsubscribe(std::string_view uri);

  /// @brief Returns the mutable underlying Client.
  Client& client();

  /// @brief Returns the underlying Client.
  const Client& client() const;

 private:
  Client client_;
  McpClientSessionOptions options_;
};

}  // namespace mcp::client
