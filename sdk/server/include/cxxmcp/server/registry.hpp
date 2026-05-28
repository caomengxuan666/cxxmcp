// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/server/handler_types.hpp"
#include "cxxmcp/server/schema_validator.hpp"
#include "cxxmcp/server/transport.hpp"

/// @file
/// @brief In-memory registries and handler contracts for server capabilities.

namespace mcp::server {

/// @brief Registry of named MCP tools and their handlers.
///
/// The registry owns copies of tool definitions and std::function handlers.
/// It performs duplicate and bounded-name validation on add(): names must be
/// non-empty, fit the SDK length limit, and contain no control characters.
/// Calls are synchronous: the selected handler is copied under the registry
/// lock, invoked on the caller's thread outside the lock, and any handler error
/// is returned unchanged.
///
/// The registry synchronizes add/get/list/call access internally. Handler
/// callbacks may reenter the registry without deadlocking.
class ToolRegistry {
 public:
  ToolRegistry() = default;
  ToolRegistry(const ToolRegistry& other);
  ToolRegistry& operator=(const ToolRegistry& other);
  ToolRegistry(ToolRegistry&& other) noexcept;
  ToolRegistry& operator=(ToolRegistry&& other) noexcept;

  /// @brief Register a tool definition and handler.
  /// @param definition Tool metadata. The name must be non-empty, bounded,
  /// control-character-free, and unique.
  /// @param handler Callable used to execute the tool.
  /// @return core::Unit on success, or InvalidRequest for invalid name,
  /// duplicate name, or empty handler.
  core::Result<core::Unit> add(protocol::ToolDefinition definition,
                               ToolHandler handler);

  /// @brief Look up a registered tool definition by name.
  /// @return The definition, or ToolNotFound when absent.
  core::Result<protocol::ToolDefinition> get(std::string_view name) const;

  /// @brief Invoke a tool without session metadata.
  /// @param name Registered tool name.
  /// @param arguments JSON arguments passed to the handler.
  /// @return Handler result, ToolNotFound, or the handler's own error.
  core::Result<protocol::ToolResult> call(std::string_view name,
                                          protocol::Json arguments) const;

  /// @brief Invoke a tool from a parsed protocol call without session metadata.
  /// @param call Parsed protocol call including arguments and optional task
  /// request metadata.
  /// @return Handler result, ToolNotFound, task-support validation failure, or
  /// the handler's own error.
  core::Result<protocol::ToolResult> call(protocol::ToolCall call) const;

  /// @brief Validate that a parsed tool call can target a registered tool.
  /// @return Unit on success, ToolNotFound, or task-support validation failure.
  core::Result<core::Unit> validate(const protocol::ToolCall& call) const;

  /// @brief Invoke a tool with full session metadata.
  /// @param name Registered tool name.
  /// @param arguments JSON arguments passed to the handler.
  /// @param session_context Metadata copied into ToolContext.
  /// @return Handler result, ToolNotFound, or the handler's own error.
  core::Result<protocol::ToolResult> call(
      std::string_view name, protocol::Json arguments,
      const SessionContext& session_context) const;

  /// @brief Invoke a parsed protocol call with full session metadata.
  /// @param call Parsed protocol call including arguments and optional task
  /// request metadata.
  /// @param session_context Metadata copied into ToolContext.
  /// @return Handler result, ToolNotFound, task-support validation failure, or
  /// the handler's own error.
  core::Result<protocol::ToolResult> call(
      protocol::ToolCall call, const SessionContext& session_context) const;

  /// @brief Invoke a parsed protocol call with session metadata and
  /// cooperative cancellation.
  core::Result<protocol::ToolResult> call(protocol::ToolCall call,
                                          const SessionContext& session_context,
                                          CancellationToken cancellation) const;
  core::Result<protocol::ToolResult> call(
      protocol::ToolCall call, const SessionContext& session_context,
      CancellationToken cancellation,
      const JsonSchemaValidator* schema_validator) const;

  /// @brief Invoke a tool with only a session id.
  /// @param name Registered tool name.
  /// @param arguments JSON arguments passed to the handler.
  /// @param session_id Session id copied into ToolContext.
  core::Result<protocol::ToolResult> call(std::string_view name,
                                          protocol::Json arguments,
                                          const std::string& session_id) const;

  /// @brief Return registered tool definitions sorted by name.
  std::vector<protocol::ToolDefinition> list() const;

 private:
  struct Entry {
    protocol::ToolDefinition definition;
    ToolHandler handler;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> tools_;
  mutable std::vector<protocol::ToolDefinition> sorted_tools_cache_;
  mutable bool sorted_tools_cache_dirty_ = true;
};

/// @brief Registry of named MCP prompts and their handlers.
///
/// PromptRegistry owns prompt metadata and handler callables. Handler
/// invocation is synchronous, copied under the registry lock, invoked outside
/// the lock, and errors are returned to the caller without translation.
class PromptRegistry {
 public:
  PromptRegistry() = default;
  PromptRegistry(const PromptRegistry& other);
  PromptRegistry& operator=(const PromptRegistry& other);
  PromptRegistry(PromptRegistry&& other) noexcept;
  PromptRegistry& operator=(PromptRegistry&& other) noexcept;

  /// @brief Register a prompt definition and handler.
  /// @return core::Unit on success, or InvalidRequest for invalid name,
  /// duplicate name, or empty handler.
  core::Result<core::Unit> add(protocol::Prompt prompt, PromptHandler handler);

  /// @brief Render a prompt with only a session id.
  core::Result<protocol::PromptsGetResult> get(
      std::string_view name, protocol::Json arguments,
      const std::string& session_id) const;

  /// @brief Render a prompt with full session metadata.
  /// @return Handler result, an error when the prompt is not registered, or
  /// the handler's own error.
  core::Result<protocol::PromptsGetResult> get(
      std::string_view name, protocol::Json arguments,
      const SessionContext& session_context) const;
  core::Result<protocol::PromptsGetResult> get(
      std::string_view name, protocol::Json arguments,
      const SessionContext& session_context,
      CancellationToken cancellation) const;

  /// @brief Return registered prompt definitions sorted by name.
  std::vector<protocol::Prompt> list() const;

 private:
  struct Entry {
    protocol::Prompt prompt;
    PromptHandler handler;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> prompts_;
  mutable std::vector<protocol::Prompt> sorted_prompts_cache_;
  mutable bool sorted_prompts_cache_dirty_ = true;
};

/// @brief Registry of concrete MCP resources and read handlers.
///
/// ResourceRegistry owns resource metadata and handler callables. Reads are
/// synchronous and copy session metadata into ResourceContext before invoking
/// the handler outside the registry lock.
class ResourceRegistry {
 public:
  ResourceRegistry() = default;
  ResourceRegistry(const ResourceRegistry& other);
  ResourceRegistry& operator=(const ResourceRegistry& other);
  ResourceRegistry(ResourceRegistry&& other) noexcept;
  ResourceRegistry& operator=(ResourceRegistry&& other) noexcept;

  /// @brief Register a concrete resource and read handler.
  /// @return core::Unit on success, or InvalidRequest for invalid URI/name,
  /// duplicate URI, or empty handler.
  core::Result<core::Unit> add(protocol::Resource resource,
                               ResourceReadHandler handler);

  /// @brief Read a resource with only a session id.
  core::Result<protocol::ResourcesReadResult> read(
      std::string_view uri, protocol::Json params,
      const std::string& session_id) const;

  /// @brief Read a resource with full session metadata.
  /// @return Handler result, ResourceNotFound, or the handler's own error.
  core::Result<protocol::ResourcesReadResult> read(
      std::string_view uri, protocol::Json params,
      const SessionContext& session_context) const;
  core::Result<protocol::ResourcesReadResult> read(
      std::string_view uri, protocol::Json params,
      const SessionContext& session_context,
      CancellationToken cancellation) const;

  /// @brief Return registered resources sorted by URI.
  std::vector<protocol::Resource> list() const;

 private:
  struct Entry {
    protocol::Resource resource;
    ResourceReadHandler handler;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> resources_;
  mutable std::vector<protocol::Resource> sorted_resources_cache_;
  mutable bool sorted_resources_cache_dirty_ = true;
};

/// @brief Registry of advertised resource templates.
///
/// Templates are metadata only; no handler is stored here. The registry owns
/// copies of templates, validates unique bounded uriTemplate/name values
/// without control characters, and synchronizes add/list access internally.
class ResourceTemplateRegistry {
 public:
  ResourceTemplateRegistry() = default;
  ResourceTemplateRegistry(const ResourceTemplateRegistry& other);
  ResourceTemplateRegistry& operator=(const ResourceTemplateRegistry& other);
  ResourceTemplateRegistry(ResourceTemplateRegistry&& other) noexcept;
  ResourceTemplateRegistry& operator=(
      ResourceTemplateRegistry&& other) noexcept;

  /// @brief Register a resource template.
  /// @return core::Unit on success, or InvalidRequest for invalid or duplicate
  /// uriTemplate/name values.
  core::Result<core::Unit> add(protocol::ResourceTemplate resource_template);

  /// @brief Return registered resource templates sorted by uriTemplate.
  std::vector<protocol::ResourceTemplate> list() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, protocol::ResourceTemplate>
      resource_templates_;
  mutable std::vector<protocol::ResourceTemplate> sorted_templates_cache_;
  mutable bool sorted_templates_cache_dirty_ = true;
};

}  // namespace mcp::server
