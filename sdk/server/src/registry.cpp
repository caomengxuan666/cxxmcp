// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/server/registry.hpp"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace mcp::server {

namespace {

constexpr std::size_t kMaxRegistryNameBytes = 1024;
constexpr std::size_t kMaxRegistryUriBytes = 4096;

bool contains_control_character(std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return byte < 0x20 || byte == 0x7F;
  });
}

core::Error registry_name_error(std::string message, std::string detail) {
  return core::Error{
      static_cast<int>(protocol::ErrorCode::InvalidRequest),
      std::move(message),
      std::move(detail),
  };
}

core::Result<core::Unit> validate_registry_text(std::string_view value,
                                                std::string_view field,
                                                std::size_t max_bytes) {
  if (value.empty()) {
    return mcp::core::unexpected(registry_name_error(
        std::string(field) + " must not be empty", std::string(field)));
  }

  if (value.size() > max_bytes) {
    return mcp::core::unexpected(
        registry_name_error(std::string(field) + " must not exceed " +
                                std::to_string(max_bytes) + " bytes",
                            std::string(field)));
  }

  if (contains_control_character(value)) {
    return mcp::core::unexpected(registry_name_error(
        std::string(field) + " must not contain control characters",
        std::string(field)));
  }

  return core::Unit{};
}

core::Result<core::Unit> validate_registry_name(std::string_view value,
                                                std::string_view field) {
  return validate_registry_text(value, field, kMaxRegistryNameBytes);
}

core::Result<core::Unit> validate_registry_uri(std::string_view value,
                                               std::string_view field) {
  return validate_registry_text(value, field, kMaxRegistryUriBytes);
}

core::Result<core::Unit> validate_tool_task_support(
    const protocol::ToolDefinition& definition,
    const protocol::ToolCall& call) {
  const bool task_requested = call.task.has_value();
  switch (definition.task_support()) {
    case protocol::TaskSupport::Forbidden:
      if (task_requested) {
        return mcp::core::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "tool does not support task-based invocation",
            definition.name,
        });
      }
      break;
    case protocol::TaskSupport::Required:
      if (!task_requested) {
        return mcp::core::unexpected(core::Error{
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

core::Error schema_validation_error(protocol::ErrorCode code,
                                    std::string message, std::string tool_name,
                                    const core::Error& cause) {
  if (!cause.message.empty()) {
    message += ": ";
    message += cause.message;
  }
  std::string detail = tool_name;
  if (!cause.detail.empty()) {
    detail += ": ";
    detail += cause.detail;
  }
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail), "schema"};
}

core::Result<core::Unit> validate_tool_input_schema(
    const protocol::ToolDefinition& definition, const protocol::Json& arguments,
    const JsonSchemaValidator* schema_validator) {
  if (schema_validator == nullptr || definition.input_schema.empty()) {
    return core::Unit{};
  }
  SchemaValidationContext context;
  context.target = SchemaValidationTarget::ToolInput;
  context.tool_name = definition.name;
  const auto valid =
      schema_validator->validate(definition.input_schema, arguments, context);
  if (!valid) {
    return mcp::core::unexpected(schema_validation_error(
        protocol::ErrorCode::InvalidParams,
        "tool input failed schema validation", definition.name, valid.error()));
  }
  return core::Unit{};
}

core::Result<core::Unit> validate_tool_output_schema(
    const protocol::ToolDefinition& definition,
    const protocol::ToolResult& result,
    const JsonSchemaValidator* schema_validator) {
  const bool has_output_schema =
      definition.output_schema_present || !definition.output_schema.empty();
  if (schema_validator == nullptr || !has_output_schema) {
    return core::Unit{};
  }
  if (!result.structured_content.has_value()) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InternalError),
        "tool output failed schema validation: missing structuredContent",
        definition.name,
        "schema",
    });
  }
  SchemaValidationContext context;
  context.target = SchemaValidationTarget::ToolOutput;
  context.tool_name = definition.name;
  const auto valid = schema_validator->validate(
      definition.output_schema, *result.structured_content, context);
  if (!valid) {
    return mcp::core::unexpected(
        schema_validation_error(protocol::ErrorCode::InternalError,
                                "tool output failed schema validation",
                                definition.name, valid.error()));
  }
  return core::Unit{};
}

}  // namespace

ToolRegistry::ToolRegistry(const ToolRegistry& other) {
  std::lock_guard<std::mutex> lock(other.mutex_);
  tools_ = other.tools_;
  sorted_tools_cache_ = other.sorted_tools_cache_;
  sorted_tools_cache_dirty_ = other.sorted_tools_cache_dirty_;
}

ToolRegistry& ToolRegistry::operator=(const ToolRegistry& other) {
  if (this == &other) {
    return *this;
  }
  std::scoped_lock lock(mutex_, other.mutex_);
  tools_ = other.tools_;
  sorted_tools_cache_ = other.sorted_tools_cache_;
  sorted_tools_cache_dirty_ = other.sorted_tools_cache_dirty_;
  return *this;
}

ToolRegistry::ToolRegistry(ToolRegistry&& other) noexcept {
  std::lock_guard<std::mutex> lock(other.mutex_);
  tools_ = std::move(other.tools_);
  sorted_tools_cache_ = std::move(other.sorted_tools_cache_);
  sorted_tools_cache_dirty_ = other.sorted_tools_cache_dirty_;
  other.sorted_tools_cache_dirty_ = true;
}

ToolRegistry& ToolRegistry::operator=(ToolRegistry&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  std::scoped_lock lock(mutex_, other.mutex_);
  tools_ = std::move(other.tools_);
  sorted_tools_cache_ = std::move(other.sorted_tools_cache_);
  sorted_tools_cache_dirty_ = other.sorted_tools_cache_dirty_;
  other.sorted_tools_cache_dirty_ = true;
  return *this;
}

core::Result<core::Unit> ToolRegistry::add(protocol::ToolDefinition definition,
                                           ToolHandler handler) {
  const auto valid_name = validate_registry_name(definition.name, "tool name");
  if (!valid_name) {
    return mcp::core::unexpected(valid_name.error());
  }

  if (!handler) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "tool handler must be callable",
        {},
    });
  }

  const auto name = definition.name;
  std::lock_guard<std::mutex> lock(mutex_);
  auto [it, inserted] =
      tools_.emplace(name, Entry{std::move(definition), std::move(handler)});
  if (!inserted) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "tool already exists",
        {},
    });
  }
  sorted_tools_cache_dirty_ = true;

  return core::Unit{};
}

core::Result<protocol::ToolDefinition> ToolRegistry::get(
    std::string_view name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = tools_.find(std::string(name));
  if (it == tools_.end()) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::ToolNotFound),
        "tool not found",
        std::string(name),
    });
  }
  return it->second.definition;
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    std::string_view name, protocol::Json arguments) const {
  protocol::ToolCall call;
  call.name = std::string(name);
  call.arguments = std::move(arguments);
  return this->call(std::move(call));
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    protocol::ToolCall call) const {
  return this->call(std::move(call), SessionContext{});
}

core::Result<core::Unit> ToolRegistry::validate(
    const protocol::ToolCall& call) const {
  protocol::ToolDefinition definition;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = tools_.find(call.name);
    if (it == tools_.end()) {
      return mcp::core::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::ToolNotFound),
          "tool not found",
          call.name,
      });
    }
    definition = it->second.definition;
  }
  return validate_tool_task_support(definition, call);
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    std::string_view name, protocol::Json arguments,
    const SessionContext& session_context) const {
  protocol::ToolCall call;
  call.name = std::string(name);
  call.arguments = std::move(arguments);
  return this->call(std::move(call), session_context);
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    protocol::ToolCall call, const SessionContext& session_context) const {
  return this->call(std::move(call), session_context, CancellationToken{});
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    protocol::ToolCall call, const SessionContext& session_context,
    CancellationToken cancellation) const {
  return this->call(std::move(call), session_context, std::move(cancellation),
                    nullptr);
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    protocol::ToolCall call, const SessionContext& session_context,
    CancellationToken cancellation,
    const JsonSchemaValidator* schema_validator) const {
  Entry entry;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = tools_.find(call.name);
    if (it == tools_.end()) {
      return mcp::core::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::ToolNotFound),
          "tool not found",
          call.name,
      });
    }
    entry = it->second;
  }

  const auto task_support = validate_tool_task_support(entry.definition, call);
  if (!task_support) {
    return mcp::core::unexpected(task_support.error());
  }

  const auto input_valid = validate_tool_input_schema(
      entry.definition, call.arguments, schema_validator);
  if (!input_valid) {
    return mcp::core::unexpected(input_valid.error());
  }

  ToolContext context;
  context.session_id = session_context.session_id;
  context.remote_address = session_context.remote_address;
  context.headers = session_context.headers;
  context.auth_identity = session_context.auth_identity;
  context.transport = session_context.transport;
  context.transport_lifetime = session_context.transport_lifetime;
  context.arguments = std::move(call.arguments);
  context.task = std::move(call.task);
  context.cancellation = std::move(cancellation);
  const auto result = entry.handler(context);
  if (!result) {
    return mcp::core::unexpected(result.error());
  }

  const auto output_valid =
      validate_tool_output_schema(entry.definition, *result, schema_validator);
  if (!output_valid) {
    return mcp::core::unexpected(output_valid.error());
  }

  return *result;
}

core::Result<protocol::ToolResult> ToolRegistry::call(
    std::string_view name, protocol::Json arguments,
    const std::string& session_id) const {
  SessionContext context;
  context.session_id = session_id;
  return call(name, std::move(arguments), context);
}

std::vector<protocol::ToolDefinition> ToolRegistry::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sorted_tools_cache_dirty_) {
    sorted_tools_cache_.clear();
    sorted_tools_cache_.reserve(tools_.size());
    for (const auto& [name, entry] : tools_) {
      (void)name;
      sorted_tools_cache_.push_back(entry.definition);
    }
    std::sort(
        sorted_tools_cache_.begin(), sorted_tools_cache_.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
    sorted_tools_cache_dirty_ = false;
  }
  return sorted_tools_cache_;
}

PromptRegistry::PromptRegistry(const PromptRegistry& other) {
  std::lock_guard<std::mutex> lock(other.mutex_);
  prompts_ = other.prompts_;
  sorted_prompts_cache_ = other.sorted_prompts_cache_;
  sorted_prompts_cache_dirty_ = other.sorted_prompts_cache_dirty_;
}

PromptRegistry& PromptRegistry::operator=(const PromptRegistry& other) {
  if (this == &other) {
    return *this;
  }
  std::scoped_lock lock(mutex_, other.mutex_);
  prompts_ = other.prompts_;
  sorted_prompts_cache_ = other.sorted_prompts_cache_;
  sorted_prompts_cache_dirty_ = other.sorted_prompts_cache_dirty_;
  return *this;
}

PromptRegistry::PromptRegistry(PromptRegistry&& other) noexcept {
  std::lock_guard<std::mutex> lock(other.mutex_);
  prompts_ = std::move(other.prompts_);
  sorted_prompts_cache_ = std::move(other.sorted_prompts_cache_);
  sorted_prompts_cache_dirty_ = other.sorted_prompts_cache_dirty_;
  other.sorted_prompts_cache_dirty_ = true;
}

PromptRegistry& PromptRegistry::operator=(PromptRegistry&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  std::scoped_lock lock(mutex_, other.mutex_);
  prompts_ = std::move(other.prompts_);
  sorted_prompts_cache_ = std::move(other.sorted_prompts_cache_);
  sorted_prompts_cache_dirty_ = other.sorted_prompts_cache_dirty_;
  other.sorted_prompts_cache_dirty_ = true;
  return *this;
}

core::Result<core::Unit> PromptRegistry::add(protocol::Prompt prompt,
                                             PromptHandler handler) {
  const auto valid_name = validate_registry_name(prompt.name, "prompt name");
  if (!valid_name) {
    return mcp::core::unexpected(valid_name.error());
  }

  if (!handler) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "prompt handler must be callable",
        {},
    });
  }

  const auto name = prompt.name;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto inserted =
      prompts_.emplace(name, Entry{std::move(prompt), std::move(handler)})
          .second;
  if (!inserted) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "prompt already exists",
        {},
    });
  }
  sorted_prompts_cache_dirty_ = true;

  return core::Unit{};
}

core::Result<protocol::PromptsGetResult> PromptRegistry::get(
    std::string_view name, protocol::Json arguments,
    const std::string& session_id) const {
  SessionContext context;
  context.session_id = session_id;
  return get(name, std::move(arguments), context, CancellationToken{});
}

core::Result<protocol::PromptsGetResult> PromptRegistry::get(
    std::string_view name, protocol::Json arguments,
    const SessionContext& session_context) const {
  return get(name, std::move(arguments), session_context, CancellationToken{});
}

core::Result<protocol::PromptsGetResult> PromptRegistry::get(
    std::string_view name, protocol::Json arguments,
    const SessionContext& session_context,
    CancellationToken cancellation) const {
  Entry entry;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = prompts_.find(std::string(name));
    if (it == prompts_.end()) {
      return mcp::core::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "prompt not found",
          std::string(name),
      });
    }
    entry = it->second;
  }

  PromptContext context;
  context.session_id = session_context.session_id;
  context.remote_address = session_context.remote_address;
  context.headers = session_context.headers;
  context.auth_identity = session_context.auth_identity;
  context.transport = session_context.transport;
  context.transport_lifetime = session_context.transport_lifetime;
  context.arguments = std::move(arguments);
  context.cancellation = std::move(cancellation);
  const auto result = entry.handler(context);
  if (!result) {
    return mcp::core::unexpected(result.error());
  }

  return *result;
}

std::vector<protocol::Prompt> PromptRegistry::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sorted_prompts_cache_dirty_) {
    sorted_prompts_cache_.clear();
    sorted_prompts_cache_.reserve(prompts_.size());
    for (const auto& [name, entry] : prompts_) {
      (void)name;
      sorted_prompts_cache_.push_back(entry.prompt);
    }
    std::sort(
        sorted_prompts_cache_.begin(), sorted_prompts_cache_.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
    sorted_prompts_cache_dirty_ = false;
  }
  return sorted_prompts_cache_;
}

ResourceRegistry::ResourceRegistry(const ResourceRegistry& other) {
  std::lock_guard<std::mutex> lock(other.mutex_);
  resources_ = other.resources_;
  sorted_resources_cache_ = other.sorted_resources_cache_;
  sorted_resources_cache_dirty_ = other.sorted_resources_cache_dirty_;
}

ResourceRegistry& ResourceRegistry::operator=(const ResourceRegistry& other) {
  if (this == &other) {
    return *this;
  }
  std::scoped_lock lock(mutex_, other.mutex_);
  resources_ = other.resources_;
  sorted_resources_cache_ = other.sorted_resources_cache_;
  sorted_resources_cache_dirty_ = other.sorted_resources_cache_dirty_;
  return *this;
}

ResourceRegistry::ResourceRegistry(ResourceRegistry&& other) noexcept {
  std::lock_guard<std::mutex> lock(other.mutex_);
  resources_ = std::move(other.resources_);
  sorted_resources_cache_ = std::move(other.sorted_resources_cache_);
  sorted_resources_cache_dirty_ = other.sorted_resources_cache_dirty_;
  other.sorted_resources_cache_dirty_ = true;
}

ResourceRegistry& ResourceRegistry::operator=(
    ResourceRegistry&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  std::scoped_lock lock(mutex_, other.mutex_);
  resources_ = std::move(other.resources_);
  sorted_resources_cache_ = std::move(other.sorted_resources_cache_);
  sorted_resources_cache_dirty_ = other.sorted_resources_cache_dirty_;
  other.sorted_resources_cache_dirty_ = true;
  return *this;
}

core::Result<core::Unit> ResourceRegistry::add(protocol::Resource resource,
                                               ResourceReadHandler handler) {
  const auto valid_uri = validate_registry_uri(resource.uri, "resource uri");
  if (!valid_uri) {
    return mcp::core::unexpected(valid_uri.error());
  }

  const auto valid_name =
      validate_registry_name(resource.name, "resource name");
  if (!valid_name) {
    return mcp::core::unexpected(valid_name.error());
  }

  if (!handler) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "resource handler must be callable",
        {},
    });
  }

  const auto uri = resource.uri;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto inserted =
      resources_.emplace(uri, Entry{std::move(resource), std::move(handler)})
          .second;
  if (!inserted) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "resource already exists",
        {},
    });
  }
  sorted_resources_cache_dirty_ = true;

  return core::Unit{};
}

core::Result<protocol::ResourcesReadResult> ResourceRegistry::read(
    std::string_view uri, protocol::Json params,
    const std::string& session_id) const {
  SessionContext context;
  context.session_id = session_id;
  return read(uri, std::move(params), context, CancellationToken{});
}

core::Result<protocol::ResourcesReadResult> ResourceRegistry::read(
    std::string_view uri, protocol::Json params,
    const SessionContext& session_context) const {
  return read(uri, std::move(params), session_context, CancellationToken{});
}

core::Result<protocol::ResourcesReadResult> ResourceRegistry::read(
    std::string_view uri, protocol::Json params,
    const SessionContext& session_context,
    CancellationToken cancellation) const {
  Entry entry;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = resources_.find(std::string(uri));
    if (it == resources_.end()) {
      return mcp::core::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::ResourceNotFound),
          "resource not found",
          std::string(uri),
      });
    }
    entry = it->second;
  }

  ResourceContext context;
  context.session_id = session_context.session_id;
  context.remote_address = session_context.remote_address;
  context.headers = session_context.headers;
  context.auth_identity = session_context.auth_identity;
  context.transport = session_context.transport;
  context.transport_lifetime = session_context.transport_lifetime;
  context.uri = std::string(uri);
  context.params = std::move(params);
  context.cancellation = std::move(cancellation);
  const auto result = entry.handler(context);
  if (!result) {
    return mcp::core::unexpected(result.error());
  }

  return *result;
}

std::vector<protocol::Resource> ResourceRegistry::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sorted_resources_cache_dirty_) {
    sorted_resources_cache_.clear();
    sorted_resources_cache_.reserve(resources_.size());
    for (const auto& [uri, entry] : resources_) {
      (void)uri;
      sorted_resources_cache_.push_back(entry.resource);
    }
    std::sort(
        sorted_resources_cache_.begin(), sorted_resources_cache_.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.uri < rhs.uri; });
    sorted_resources_cache_dirty_ = false;
  }
  return sorted_resources_cache_;
}

ResourceTemplateRegistry::ResourceTemplateRegistry(
    const ResourceTemplateRegistry& other) {
  std::lock_guard<std::mutex> lock(other.mutex_);
  resource_templates_ = other.resource_templates_;
  sorted_templates_cache_ = other.sorted_templates_cache_;
  sorted_templates_cache_dirty_ = other.sorted_templates_cache_dirty_;
}

ResourceTemplateRegistry& ResourceTemplateRegistry::operator=(
    const ResourceTemplateRegistry& other) {
  if (this == &other) {
    return *this;
  }
  std::scoped_lock lock(mutex_, other.mutex_);
  resource_templates_ = other.resource_templates_;
  sorted_templates_cache_ = other.sorted_templates_cache_;
  sorted_templates_cache_dirty_ = other.sorted_templates_cache_dirty_;
  return *this;
}

ResourceTemplateRegistry::ResourceTemplateRegistry(
    ResourceTemplateRegistry&& other) noexcept {
  std::lock_guard<std::mutex> lock(other.mutex_);
  resource_templates_ = std::move(other.resource_templates_);
  sorted_templates_cache_ = std::move(other.sorted_templates_cache_);
  sorted_templates_cache_dirty_ = other.sorted_templates_cache_dirty_;
  other.sorted_templates_cache_dirty_ = true;
}

ResourceTemplateRegistry& ResourceTemplateRegistry::operator=(
    ResourceTemplateRegistry&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  std::scoped_lock lock(mutex_, other.mutex_);
  resource_templates_ = std::move(other.resource_templates_);
  sorted_templates_cache_ = std::move(other.sorted_templates_cache_);
  sorted_templates_cache_dirty_ = other.sorted_templates_cache_dirty_;
  other.sorted_templates_cache_dirty_ = true;
  return *this;
}

core::Result<core::Unit> ResourceTemplateRegistry::add(
    protocol::ResourceTemplate resource_template) {
  const auto valid_uri_template = validate_registry_uri(
      resource_template.uri_template, "resource template uriTemplate");
  if (!valid_uri_template) {
    return mcp::core::unexpected(valid_uri_template.error());
  }

  const auto valid_name =
      validate_registry_name(resource_template.name, "resource template name");
  if (!valid_name) {
    return mcp::core::unexpected(valid_name.error());
  }

  const auto uri_template = resource_template.uri_template;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto inserted =
      resource_templates_.emplace(uri_template, std::move(resource_template))
          .second;
  if (!inserted) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "resource template already exists",
        {},
    });
  }
  sorted_templates_cache_dirty_ = true;

  return core::Unit{};
}

std::vector<protocol::ResourceTemplate> ResourceTemplateRegistry::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sorted_templates_cache_dirty_) {
    sorted_templates_cache_.clear();
    sorted_templates_cache_.reserve(resource_templates_.size());
    for (const auto& [uri_template, resource_template] : resource_templates_) {
      (void)uri_template;
      sorted_templates_cache_.push_back(resource_template);
    }
    std::sort(sorted_templates_cache_.begin(), sorted_templates_cache_.end(),
              [](const auto& lhs, const auto& rhs) {
                return lhs.uri_template < rhs.uri_template;
              });
    sorted_templates_cache_dirty_ = false;
  }
  return sorted_templates_cache_;
}

}  // namespace mcp::server
