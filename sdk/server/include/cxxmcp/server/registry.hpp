// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/server/peer.hpp"
#include "cxxmcp/server/transport.hpp"

/// @file
/// @brief In-memory registries and handler contracts for server capabilities.

namespace mcp::server {

/// @brief Copyable cooperative cancellation token for server handlers.
class CancellationToken {
 public:
  CancellationToken() : cancelled_(std::make_shared<std::atomic_bool>(false)) {}

  /// @brief Returns true after the owning operation has been cancelled.
  bool cancelled() const noexcept { return cancelled_->load(); }

 private:
  friend class TaskOperationProcessor;

  void cancel() const noexcept { cancelled_->store(true); }

  std::shared_ptr<std::atomic_bool> cancelled_;
};

/// @brief Invocation context passed to tool handlers.
///
/// ToolContext copies the SessionContext metadata for the active request and
/// adds tool arguments. It does not own the transport; client() returns a
/// non-owning ClientPeer for optional server-to-client calls during handling.
struct ToolContext : SessionContext {
  /// @brief Return a non-owning peer facade for this invocation's client.
  ClientPeer client() const noexcept { return client_peer(*this); }

  /// JSON arguments supplied with the tool call.
  protocol::Json arguments = protocol::Json::object();
  /// Optional task request metadata supplied with the tool call.
  std::optional<protocol::TaskRequestParameters> task;
  /// Cooperative cancellation token for task-aware executions.
  CancellationToken cancellation;

  /// @brief Convenience check for cancellation-aware handlers.
  bool cancelled() const noexcept { return cancellation.cancelled(); }
};

/// @brief Invocation context passed to prompt handlers.
struct PromptContext : SessionContext {
  /// @brief Return a non-owning peer facade for this invocation's client.
  ClientPeer client() const noexcept { return client_peer(*this); }

  /// JSON arguments supplied with the prompt request.
  protocol::Json arguments = protocol::Json::object();
};

/// @brief Invocation context passed to resource read handlers.
struct ResourceContext : SessionContext {
  /// @brief Return a non-owning peer facade for this invocation's client.
  ClientPeer client() const noexcept { return client_peer(*this); }

  /// Requested resource URI.
  std::string uri;
  /// Raw resource read parameters supplied by the client.
  protocol::Json params = protocol::Json::object();
};

/// @brief Application callback that executes a tool.
/// @return A ToolResult, or a core::Error propagated to the caller.
using ToolHandler =
    std::function<core::Result<protocol::ToolResult>(const ToolContext&)>;

/// @brief Application callback that renders a prompt.
/// @return A prompt result, or a core::Error propagated to the caller.
using PromptHandler = std::function<core::Result<protocol::PromptsGetResult>(
    const PromptContext&)>;

/// @brief Application callback that reads a resource.
/// @return Resource contents, or a core::Error propagated to the caller.
using ResourceReadHandler =
    std::function<core::Result<protocol::ResourcesReadResult>(
        const ResourceContext&)>;

/// @brief Registry of named MCP tools and their handlers.
///
/// The registry owns copies of tool definitions and std::function handlers.
/// It performs duplicate and empty-name validation on add(). Calls are
/// synchronous: the selected handler is invoked on the caller's thread, and any
/// handler error is returned unchanged.
///
/// The registry has no internal synchronization. Configure it before concurrent
/// access or protect it externally if registering and calling in parallel.
class ToolRegistry {
 public:
  /// @brief Register a tool definition and handler.
  /// @param definition Tool metadata. The name must be non-empty and unique.
  /// @param handler Callable used to execute the tool.
  /// @return core::Unit on success, or InvalidRequest for empty name,
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

  std::unordered_map<std::string, Entry> tools_;
};

/// @brief Registry of named MCP prompts and their handlers.
///
/// PromptRegistry owns prompt metadata and handler callables. Handler
/// invocation is synchronous and errors are returned to the caller without
/// translation. The registry is not internally synchronized.
class PromptRegistry {
 public:
  /// @brief Register a prompt definition and handler.
  /// @return core::Unit on success, or InvalidRequest for empty name,
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

  /// @brief Return registered prompt definitions sorted by name.
  std::vector<protocol::Prompt> list() const;

 private:
  struct Entry {
    protocol::Prompt prompt;
    PromptHandler handler;
  };

  std::unordered_map<std::string, Entry> prompts_;
};

/// @brief Registry of concrete MCP resources and read handlers.
///
/// ResourceRegistry owns resource metadata and handler callables. Reads are
/// synchronous and copy session metadata into ResourceContext before invoking
/// the handler. The registry is not internally synchronized.
class ResourceRegistry {
 public:
  /// @brief Register a concrete resource and read handler.
  /// @return core::Unit on success, or InvalidRequest for empty URI,
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

  /// @brief Return registered resources sorted by URI.
  std::vector<protocol::Resource> list() const;

 private:
  struct Entry {
    protocol::Resource resource;
    ResourceReadHandler handler;
  };

  std::unordered_map<std::string, Entry> resources_;
};

/// @brief Registry of advertised resource templates.
///
/// Templates are metadata only; no handler is stored here. The registry owns
/// copies of templates, validates unique non-empty uriTemplate values, and is
/// not internally synchronized.
class ResourceTemplateRegistry {
 public:
  /// @brief Register a resource template.
  /// @return core::Unit on success, or InvalidRequest for empty or duplicate
  /// uriTemplate values.
  core::Result<core::Unit> add(protocol::ResourceTemplate resource_template);

  /// @brief Return registered resource templates sorted by uriTemplate.
  std::vector<protocol::ResourceTemplate> list() const;

 private:
  std::unordered_map<std::string, protocol::ResourceTemplate>
      resource_templates_;
};

}  // namespace mcp::server
