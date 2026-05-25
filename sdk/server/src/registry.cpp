// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/server/registry.hpp"

#include <algorithm>
#include <utility>

namespace mcp::server {

namespace {

core::Result<core::Unit> validate_tool_task_support(
    const protocol::ToolDefinition& definition,
    const protocol::ToolCall& call) {
  const bool task_requested = call.task.has_value();
  switch (definition.task_support()) {
    case protocol::TaskSupport::Forbidden:
      if (task_requested) {
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "tool does not support task-based invocation",
            definition.name,
        });
      }
      break;
    case protocol::TaskSupport::Required:
      if (!task_requested) {
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "tool requires task-based invocation",
            definition.name,
        });
      }
      break;
    case protocol::TaskSupport::Optional:
      break;
  }
  return core::Unit{};
}

}  // namespace

core::Result<core::Unit> ToolRegistry::add(protocol::ToolDefinition definition,
                                           ToolHandler handler) {
  if (definition.name.empty()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "tool name must not be empty",
        {},
    });
  }

  if (!handler) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "tool handler must be callable",
        {},
    });
  }

  const auto name = definition.name;
  auto [it, inserted] =
      tools_.emplace(name, Entry{std::move(definition), std::move(handler)});
  if (!inserted) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "tool already exists",
        {},
    });
  }

  return core::Unit{};
}

core::Result<protocol::ToolDefinition> ToolRegistry::get(
    std::string_view name) const {
  const auto it = tools_.find(std::string(name));
  if (it == tools_.end()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::ToolNotFound),
        "tool not found",
        std::string(name),
    });
  }
  return it->second.definition;
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    std::string_view name, protocol::Json arguments) const {
  return call(protocol::ToolCall{.name = std::string(name),
                                 .arguments = std::move(arguments)});
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    protocol::ToolCall call) const {
  return this->call(std::move(call), SessionContext{});
}

core::Result<core::Unit> ToolRegistry::validate(
    const protocol::ToolCall& call) const {
  const auto it = tools_.find(call.name);
  if (it == tools_.end()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::ToolNotFound),
        "tool not found",
        call.name,
    });
  }
  return validate_tool_task_support(it->second.definition, call);
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    std::string_view name, protocol::Json arguments,
    const SessionContext& session_context) const {
  return this->call(protocol::ToolCall{.name = std::string(name),
                                       .arguments = std::move(arguments)},
                    session_context);
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    protocol::ToolCall call, const SessionContext& session_context) const {
  return this->call(std::move(call), session_context, CancellationToken{});
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    protocol::ToolCall call, const SessionContext& session_context,
    CancellationToken cancellation) const {
  const auto it = tools_.find(call.name);
  if (it == tools_.end()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::ToolNotFound),
        "tool not found",
        call.name,
    });
  }

  const auto task_support = validate(call);
  if (!task_support) {
    return std::unexpected(task_support.error());
  }

  ToolContext context;
  context.session_id = session_context.session_id;
  context.remote_address = session_context.remote_address;
  context.transport = session_context.transport;
  context.arguments = std::move(call.arguments);
  context.task = std::move(call.task);
  context.cancellation = std::move(cancellation);
  const auto result = it->second.handler(context);
  if (!result) {
    return std::unexpected(result.error());
  }

  return *result;
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    std::string_view name, protocol::Json arguments,
    const std::string& session_id) const {
  return call(name, std::move(arguments),
              SessionContext{.session_id = session_id});
}

std::vector<protocol::ToolDefinition> ToolRegistry::list() const {
  std::vector<protocol::ToolDefinition> tools;
  tools.reserve(tools_.size());
  for (const auto& [name, entry] : tools_) {
    (void)name;
    tools.push_back(entry.definition);
  }

  std::sort(tools.begin(), tools.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.name < rhs.name;
  });
  return tools;
}

core::Result<core::Unit> PromptRegistry::add(protocol::Prompt prompt,
                                             PromptHandler handler) {
  if (prompt.name.empty()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "prompt name must not be empty",
        {},
    });
  }

  if (!handler) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "prompt handler must be callable",
        {},
    });
  }

  const auto name = prompt.name;
  const auto inserted =
      prompts_.emplace(name, Entry{std::move(prompt), std::move(handler)})
          .second;
  if (!inserted) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "prompt already exists",
        {},
    });
  }

  return core::Unit{};
}

core::Result<protocol::PromptsGetResult> PromptRegistry::get(
    std::string_view name, protocol::Json arguments,
    const std::string& session_id) const {
  return get(name, std::move(arguments),
             SessionContext{.session_id = session_id});
}

core::Result<protocol::PromptsGetResult> PromptRegistry::get(
    std::string_view name, protocol::Json arguments,
    const SessionContext& session_context) const {
  const auto it = prompts_.find(std::string(name));
  if (it == prompts_.end()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "prompt not found",
        std::string(name),
    });
  }

  PromptContext context;
  context.session_id = session_context.session_id;
  context.remote_address = session_context.remote_address;
  context.transport = session_context.transport;
  context.arguments = std::move(arguments);
  const auto result = it->second.handler(context);
  if (!result) {
    return std::unexpected(result.error());
  }

  return *result;
}

std::vector<protocol::Prompt> PromptRegistry::list() const {
  std::vector<protocol::Prompt> prompts;
  prompts.reserve(prompts_.size());
  for (const auto& [name, entry] : prompts_) {
    (void)name;
    prompts.push_back(entry.prompt);
  }

  std::sort(
      prompts.begin(), prompts.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
  return prompts;
}

core::Result<core::Unit> ResourceRegistry::add(protocol::Resource resource,
                                               ResourceReadHandler handler) {
  if (resource.uri.empty()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "resource uri must not be empty",
        {},
    });
  }

  if (!handler) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "resource handler must be callable",
        {},
    });
  }

  const auto uri = resource.uri;
  const auto inserted =
      resources_.emplace(uri, Entry{std::move(resource), std::move(handler)})
          .second;
  if (!inserted) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "resource already exists",
        {},
    });
  }

  return core::Unit{};
}

core::Result<protocol::ResourcesReadResult> ResourceRegistry::read(
    std::string_view uri, protocol::Json params,
    const std::string& session_id) const {
  return read(uri, std::move(params), SessionContext{.session_id = session_id});
}

core::Result<protocol::ResourcesReadResult> ResourceRegistry::read(
    std::string_view uri, protocol::Json params,
    const SessionContext& session_context) const {
  const auto it = resources_.find(std::string(uri));
  if (it == resources_.end()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::ResourceNotFound),
        "resource not found",
        std::string(uri),
    });
  }

  ResourceContext context;
  context.session_id = session_context.session_id;
  context.remote_address = session_context.remote_address;
  context.transport = session_context.transport;
  context.uri = std::string(uri);
  context.params = std::move(params);
  const auto result = it->second.handler(context);
  if (!result) {
    return std::unexpected(result.error());
  }

  return *result;
}

std::vector<protocol::Resource> ResourceRegistry::list() const {
  std::vector<protocol::Resource> resources;
  resources.reserve(resources_.size());
  for (const auto& [uri, entry] : resources_) {
    (void)uri;
    resources.push_back(entry.resource);
  }

  std::sort(resources.begin(), resources.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.uri < rhs.uri; });
  return resources;
}

core::Result<core::Unit> ResourceTemplateRegistry::add(
    protocol::ResourceTemplate resource_template) {
  if (resource_template.uri_template.empty()) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "resource template uriTemplate must not be empty",
        {},
    });
  }

  const auto uri_template = resource_template.uri_template;
  const auto inserted =
      resource_templates_.emplace(uri_template, std::move(resource_template))
          .second;
  if (!inserted) {
    return std::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "resource template already exists",
        {},
    });
  }

  return core::Unit{};
}

std::vector<protocol::ResourceTemplate> ResourceTemplateRegistry::list() const {
  std::vector<protocol::ResourceTemplate> resource_templates;
  resource_templates.reserve(resource_templates_.size());
  for (const auto& [uri_template, resource_template] : resource_templates_) {
    (void)uri_template;
    resource_templates.push_back(resource_template);
  }

  std::sort(resource_templates.begin(), resource_templates.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.uri_template < rhs.uri_template;
            });
  return resource_templates;
}

}  // namespace mcp::server
