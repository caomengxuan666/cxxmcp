// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/tool.hpp
/// @brief Tool definition, call, and result payloads.
///
/// Tools are server-advertised operations invoked through `tools/call`.
/// Definitions are returned by listing or lookup methods, calls carry JSON
/// arguments, and results return content blocks plus optional structured data.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Per-tool support mode for task-based invocation.
enum class TaskSupport {
  /// Clients must not invoke this tool as a task.
  Forbidden,
  /// Clients may invoke this tool normally or as a task.
  Optional,
  /// Clients must invoke this tool as a task.
  Required,
};

/// @brief Execution configuration advertised with a tool definition.
struct ToolExecution {
  /// Optional task support mode. Missing means forbidden by default.
  std::optional<TaskSupport> task_support;

  /// @brief Sets the optional task support mode on an lvalue execution object.
  ToolExecution& with_task_support(TaskSupport value) & {
    task_support = value;
    return *this;
  }

  /// @brief Sets the optional task support mode while preserving fluent
  /// temporary use.
  ToolExecution&& with_task_support(TaskSupport value) && {
    task_support = value;
    return std::move(*this);
  }
};

/// @brief A single content item returned by a tool or embedded in prompts.
struct ContentBlock {
  /// Content kind. Common values are `text`, `image`, `audio`, `resource`, and
  /// `resource_link`.
  std::string type = "text";
  /// Text payload for text blocks.
  std::string text;
  /// Base64 payload for image/audio blocks or extension data for custom blocks.
  Json data = Json::object();
  /// MIME type for image/audio blocks.
  std::string mime_type;
  /// Embedded resource contents for `resource` blocks.
  std::optional<ResourceContents> resource;
  /// Resource descriptor for `resource_link` blocks.
  std::optional<Resource> resource_link;
  /// Optional annotations for model or client presentation.
  Json annotations = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;

  /// @brief Creates a text content block.
  static ContentBlock text_content(std::string value) {
    ContentBlock block;
    block.type = "text";
    block.text = std::move(value);
    return block;
  }

  /// @brief Creates an image content block with base64 data.
  static ContentBlock image(std::string base64_data, std::string mime_type) {
    ContentBlock block;
    block.type = "image";
    block.data = std::move(base64_data);
    block.mime_type = std::move(mime_type);
    return block;
  }

  /// @brief Creates an audio content block with base64 data.
  static ContentBlock audio(std::string base64_data, std::string mime_type) {
    ContentBlock block;
    block.type = "audio";
    block.data = std::move(base64_data);
    block.mime_type = std::move(mime_type);
    return block;
  }

  /// @brief Creates an embedded resource content block.
  static ContentBlock embedded_resource(ResourceContents value) {
    ContentBlock block;
    block.type = "resource";
    block.resource = std::move(value);
    return block;
  }

  /// @brief Creates a resource link content block.
  static ContentBlock resource_link_content(Resource value) {
    ContentBlock block;
    block.type = "resource_link";
    block.resource_link = std::move(value);
    return block;
  }

  /// @brief Returns true when this block is a text block.
  bool is_text() const noexcept { return type == "text"; }

  /// @brief Returns true when this block is an image block.
  bool is_image() const noexcept { return type == "image"; }

  /// @brief Returns true when this block is an audio block.
  bool is_audio() const noexcept { return type == "audio"; }

  /// @brief Returns true when this block embeds resource contents.
  bool is_embedded_resource() const noexcept { return type == "resource"; }

  /// @brief Returns true when this block links to a resource descriptor.
  bool is_resource_link() const noexcept { return type == "resource_link"; }

  /// @brief Returns the text payload when this is a text block.
  std::optional<std::string_view> as_text() const noexcept {
    if (!is_text()) {
      return std::nullopt;
    }
    return std::string_view(text);
  }

  /// @brief Returns base64 image data when this is an image block.
  std::optional<std::string_view> as_image_data() const {
    if (!is_image() || !data.is_string()) {
      return std::nullopt;
    }
    return std::string_view(data.get_ref<const std::string&>());
  }

  /// @brief Returns base64 audio data when this is an audio block.
  std::optional<std::string_view> as_audio_data() const {
    if (!is_audio() || !data.is_string()) {
      return std::nullopt;
    }
    return std::string_view(data.get_ref<const std::string&>());
  }

  /// @brief Returns embedded resource contents when present.
  const ResourceContents* as_embedded_resource() const noexcept {
    if (!is_embedded_resource() || !resource.has_value()) {
      return nullptr;
    }
    return &*resource;
  }

  /// @brief Returns the linked resource descriptor when present.
  const Resource* as_resource_link() const noexcept {
    if (!is_resource_link() || !resource_link.has_value()) {
      return nullptr;
    }
    return &*resource_link;
  }
};

/// @brief Metadata describing a callable MCP tool.
struct ToolDefinition {
  /// Optional human-readable display title.
  std::string title;
  /// Stable protocol identifier used by `tools/call`.
  std::string name;
  /// Human-readable tool description.
  std::string description;
  /// JSON Schema object describing accepted arguments.
  Json input_schema = Json::object();
  /// Optional JSON Schema object describing structured result content.
  Json output_schema = Json::object();
  /// Whether the tool may stream partial results outside a single response.
  bool streaming = false;
  /// Optional icon descriptors for client presentation.
  std::vector<Icon> icons;
  /// Optional execution configuration including task support mode.
  std::optional<ToolExecution> execution;
  /// Optional annotations for model or client presentation.
  Json annotations = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;

  /// @brief Returns the effective task support mode for this tool.
  TaskSupport task_support() const noexcept {
    if (!execution.has_value() || !execution->task_support.has_value()) {
      return TaskSupport::Forbidden;
    }
    return *execution->task_support;
  }
};

/// @brief Fluent builder for advertised MCP tool metadata.
class ToolDefinitionBuilder {
 public:
  explicit ToolDefinitionBuilder(std::string name) {
    definition_.name = std::move(name);
  }

  ToolDefinitionBuilder& title(std::string value) {
    definition_.title = std::move(value);
    return *this;
  }

  ToolDefinitionBuilder& description(std::string value) {
    definition_.description = std::move(value);
    return *this;
  }

  ToolDefinitionBuilder& input_schema(Json schema) {
    definition_.input_schema = std::move(schema);
    return *this;
  }

  ToolDefinitionBuilder& output_schema(Json schema) {
    definition_.output_schema = std::move(schema);
    return *this;
  }

  ToolDefinitionBuilder& streaming(bool value = true) {
    definition_.streaming = value;
    return *this;
  }

  ToolDefinitionBuilder& icon(Icon value) {
    definition_.icons.push_back(std::move(value));
    return *this;
  }

  ToolDefinitionBuilder& task_support(TaskSupport value) {
    if (!definition_.execution.has_value()) {
      definition_.execution = ToolExecution{};
    }
    definition_.execution->task_support = value;
    return *this;
  }

  ToolDefinitionBuilder& annotations(Json value) {
    definition_.annotations = std::move(value);
    return *this;
  }

  ToolDefinitionBuilder& meta(Json value) {
    definition_.meta = std::move(value);
    return *this;
  }

  ToolDefinition build() const& { return definition_; }

  ToolDefinition build() && { return std::move(definition_); }

 private:
  ToolDefinition definition_;
};

/// @brief Creates a fluent builder for advertised MCP tool metadata.
inline ToolDefinitionBuilder tool_definition(std::string name) {
  return ToolDefinitionBuilder(std::move(name));
}

/// @brief Parameters for `tools/call`.
struct ToolCall {
  /// Tool name matching a ToolDefinition.
  std::string name;
  /// JSON argument object validated against the tool input schema.
  Json arguments = Json::object();
  /// Optional task request parameters when asynchronous execution is desired.
  std::optional<TaskRequestParameters> task;
};

/// @brief Result object for `tools/list`.
struct ToolsListResult {
  /// Tools available to the caller.
  std::vector<ToolDefinition> tools;
  /// Optional cursor for retrieving the next page.
  std::optional<std::string> next_cursor;
};

/// @brief Result object for `tools/call`.
struct ToolResult {
  /// Ordered user/model-visible content returned by the tool.
  std::vector<ContentBlock> content;
  /// Optional machine-readable result matching the tool output schema.
  std::optional<Json> structured_content;
  /// True when the tool call completed with a domain-level error result.
  bool is_error = false;
};

/// @brief Builds an InvalidRequest error for tool JSON validation failures.
/// @param message Validation diagnostic.
/// @return Core error carrying the MCP invalid-request code.
inline core::Error tool_json_error(std::string message) {
  return core::Error{
      static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

/// @brief Converts a task support mode to the lowercase wire value.
inline std::string_view task_support_to_string(TaskSupport support) noexcept {
  switch (support) {
    case TaskSupport::Forbidden:
      return "forbidden";
    case TaskSupport::Optional:
      return "optional";
    case TaskSupport::Required:
      return "required";
  }
  return "forbidden";
}

/// @brief Parses a lowercase task support wire value.
inline std::optional<TaskSupport> task_support_from_string(
    std::string_view value) noexcept {
  if (value == "forbidden") {
    return TaskSupport::Forbidden;
  }
  if (value == "optional") {
    return TaskSupport::Optional;
  }
  if (value == "required") {
    return TaskSupport::Required;
  }
  return std::nullopt;
}

/// @brief Serializes tool execution configuration.
inline Json tool_execution_to_json(const ToolExecution& execution) {
  Json json = Json::object();
  if (execution.task_support.has_value()) {
    json["taskSupport"] = task_support_to_string(*execution.task_support);
  }
  return json;
}

/// @brief Parses tool execution configuration.
/// @return Parsed execution configuration or validation error.
inline core::Result<ToolExecution> tool_execution_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(tool_json_error("tool execution must be an object"));
  }

  ToolExecution execution;
  if (json.contains("taskSupport")) {
    if (!json.at("taskSupport").is_string()) {
      return std::unexpected(
          tool_json_error("tool execution taskSupport must be a string"));
    }
    const auto task_support = task_support_from_string(
        json.at("taskSupport").get_ref<const std::string&>());
    if (!task_support.has_value()) {
      return std::unexpected(
          tool_json_error("tool execution taskSupport is invalid"));
    }
    execution.task_support = *task_support;
  }
  return execution;
}

/// @brief Serializes a content block.
inline Json content_block_to_json(const ContentBlock& block) {
  Json json = Json::object();
  json["type"] = block.type;
  if (block.type == "resource" && block.resource.has_value()) {
    json["resource"] = resource_contents_to_json(*block.resource);
  } else if (block.type == "resource_link" && block.resource_link.has_value()) {
    json = resource_to_json(*block.resource_link);
    json["type"] = block.type;
  } else {
    if (!block.text.empty() || block.type == "text") {
      json["text"] = block.text;
    }
    if (!block.data.empty()) {
      json["data"] = block.data;
    }
    if (!block.mime_type.empty()) {
      json["mimeType"] = block.mime_type;
    }
  }
  if (!block.annotations.empty()) {
    json["annotations"] = block.annotations;
  }
  if (block.meta.has_value()) {
    json["_meta"] = *block.meta;
  }
  return json;
}

/// @brief Reads a required string member from a content object.
inline core::Result<std::string> required_content_string(
    const Json& json, std::string_view member, std::string message) {
  const std::string key(member);
  if (!json.contains(key) || !json.at(key).is_string()) {
    return std::unexpected(tool_json_error(std::move(message)));
  }
  return json.at(key).get<std::string>();
}

/// @brief Parses a content block from JSON.
/// @return Parsed block or validation error.
inline core::Result<ContentBlock> content_block_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(tool_json_error("content block must be an object"));
  }

  ContentBlock block;
  if (json.contains("type")) {
    if (!json.at("type").is_string()) {
      return std::unexpected(
          tool_json_error("content block type must be a string"));
    }
    block.type = json.at("type").get<std::string>();
  }

  if (block.type == "image" || block.type == "audio") {
    const auto data = required_content_string(
        json, "data", block.type + " content block requires string data");
    if (!data) {
      return std::unexpected(data.error());
    }
    const auto mime_type = required_content_string(
        json, "mimeType",
        block.type + " content block requires string mimeType");
    if (!mime_type) {
      return std::unexpected(mime_type.error());
    }
    block.data = *data;
    block.mime_type = *mime_type;
  } else if (block.type == "resource") {
    if (!json.contains("resource")) {
      return std::unexpected(
          tool_json_error("resource content block requires resource"));
    }
    const auto resource = resource_contents_from_json(json.at("resource"));
    if (!resource) {
      return std::unexpected(resource.error());
    }
    block.resource = *resource;
  } else if (block.type == "resource_link") {
    const auto resource = resource_from_json(json);
    if (!resource) {
      return std::unexpected(resource.error());
    }
    block.resource_link = *resource;
  } else {
    if (json.contains("text")) {
      if (!json.at("text").is_string()) {
        return std::unexpected(
            tool_json_error("content block text must be a string"));
      }
      block.text = json.at("text").get<std::string>();
    }

    if (json.contains("data")) {
      block.data = json.at("data");
    }

    if (json.contains("mimeType")) {
      if (!json.at("mimeType").is_string()) {
        return std::unexpected(
            tool_json_error("content block mimeType must be a string"));
      }
      block.mime_type = json.at("mimeType").get<std::string>();
    }
  }

  if (json.contains("annotations")) {
    block.annotations = json.at("annotations");
  }
  if (json.contains("_meta")) {
    block.meta = json.at("_meta");
  }

  return block;
}

/// @brief Serializes a tool definition as returned by tool discovery.
inline Json tool_definition_to_json(const ToolDefinition& definition) {
  Json json = Json::object();
  if (!definition.title.empty()) {
    json["title"] = definition.title;
  }
  json["name"] = definition.name;
  json["description"] = definition.description;
  json["inputSchema"] = definition.input_schema;
  if (!definition.output_schema.empty()) {
    json["outputSchema"] = definition.output_schema;
  }
  json["streaming"] = definition.streaming;
  if (!definition.icons.empty()) {
    json["icons"] = Json::array();
    for (const auto& icon : definition.icons) {
      json["icons"].push_back(icon_to_json(icon));
    }
  }
  if (definition.execution.has_value()) {
    json["execution"] = tool_execution_to_json(*definition.execution);
  }
  if (!definition.annotations.empty()) {
    json["annotations"] = definition.annotations;
  }
  if (definition.meta.has_value()) {
    json["_meta"] = *definition.meta;
  }
  return json;
}

/// @brief Parses a tool definition from JSON.
/// @return Parsed definition or validation error.
inline core::Result<ToolDefinition> tool_definition_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        tool_json_error("tool definition must be an object"));
  }

  ToolDefinition definition;
  if (json.contains("title")) {
    if (!json.at("title").is_string()) {
      return std::unexpected(
          tool_json_error("tool definition title must be a string"));
    }
    definition.title = json.at("title").get<std::string>();
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return std::unexpected(
        tool_json_error("tool definition requires a string name"));
  }
  definition.name = json.at("name").get<std::string>();

  if (json.contains("description")) {
    if (!json.at("description").is_string()) {
      return std::unexpected(
          tool_json_error("tool definition description must be a string"));
    }
    definition.description = json.at("description").get<std::string>();
  }

  if (json.contains("inputSchema")) {
    definition.input_schema = json.at("inputSchema");
  }

  if (json.contains("outputSchema")) {
    definition.output_schema = json.at("outputSchema");
  }

  if (json.contains("streaming")) {
    if (!json.at("streaming").is_boolean()) {
      return std::unexpected(
          tool_json_error("tool definition streaming must be a boolean"));
    }
    definition.streaming = json.at("streaming").get<bool>();
  }

  if (json.contains("icons")) {
    if (!json.at("icons").is_array()) {
      return std::unexpected(
          tool_json_error("tool definition icons must be an array"));
    }
    for (const auto& item : json.at("icons")) {
      const auto icon = icon_from_json(item);
      if (!icon.has_value()) {
        return std::unexpected(
            tool_json_error("tool definition icon is invalid"));
      }
      definition.icons.push_back(*icon);
    }
  }

  if (json.contains("execution")) {
    const auto execution = tool_execution_from_json(json.at("execution"));
    if (!execution) {
      return std::unexpected(execution.error());
    }
    definition.execution = *execution;
  }

  if (json.contains("annotations")) {
    definition.annotations = json.at("annotations");
  }

  if (json.contains("_meta")) {
    definition.meta = json.at("_meta");
  }

  return definition;
}

/// @brief Serializes a `tools/list` result.
inline Json tools_list_result_to_json(const ToolsListResult& result) {
  Json json = Json::object();
  json["tools"] = Json::array();
  for (const auto& tool : result.tools) {
    json["tools"].push_back(tool_definition_to_json(tool));
  }
  if (result.next_cursor.has_value()) {
    json["nextCursor"] = *result.next_cursor;
  }
  return json;
}

/// @brief Parses a `tools/list` result.
/// @return Parsed result or validation error.
inline core::Result<ToolsListResult> tools_list_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        tool_json_error("tools/list result must be an object"));
  }
  if (!json.contains("tools") || !json.at("tools").is_array()) {
    return std::unexpected(
        tool_json_error("tools/list result requires a tools array"));
  }

  ToolsListResult result;
  for (const auto& item : json.at("tools")) {
    const auto tool = tool_definition_from_json(item);
    if (!tool) {
      return std::unexpected(tool.error());
    }
    result.tools.push_back(*tool);
  }
  if (json.contains("nextCursor")) {
    if (!json.at("nextCursor").is_string()) {
      return std::unexpected(
          tool_json_error("tools/list nextCursor must be a string"));
    }
    result.next_cursor = json.at("nextCursor").get<std::string>();
  }
  return result;
}

/// @brief Serializes `tools/call` params.
inline Json tool_call_to_json(const ToolCall& call) {
  Json json = Json::object();
  json["name"] = call.name;
  if (!call.arguments.empty()) {
    json["arguments"] = call.arguments;
  }
  if (call.task.has_value()) {
    json["task"] = task_request_parameters_to_json(*call.task);
  }
  return json;
}

/// @brief Parses `tools/call` params.
/// @return Parsed call or validation error.
inline core::Result<ToolCall> tool_call_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        tool_json_error("tools/call params must be an object"));
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return std::unexpected(
        tool_json_error("tools/call params require a string name"));
  }

  ToolCall call;
  call.name = json.at("name").get<std::string>();
  if (json.contains("arguments")) {
    if (!json.at("arguments").is_object()) {
      return std::unexpected(
          tool_json_error("tools/call arguments must be an object"));
    }
    call.arguments = json.at("arguments");
  }
  if (json.contains("task")) {
    const auto task = task_request_parameters_from_json(json.at("task"));
    if (!task) {
      return std::unexpected(task.error());
    }
    call.task = *task;
  }
  return call;
}

/// @brief Serializes a tool result.
inline Json tool_result_to_json(const ToolResult& result) {
  Json json = Json::object();
  json["content"] = Json::array();
  for (const auto& block : result.content) {
    json["content"].push_back(content_block_to_json(block));
  }
  if (result.structured_content.has_value()) {
    json["structuredContent"] = *result.structured_content;
  }
  if (result.is_error) {
    json["isError"] = true;
  }
  return json;
}

/// @brief Parses a tool result.
/// @return Parsed result or validation error.
inline core::Result<ToolResult> tool_result_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(tool_json_error("tool result must be an object"));
  }

  ToolResult result;
  if (json.contains("content")) {
    if (!json.at("content").is_array()) {
      return std::unexpected(
          tool_json_error("tool result content must be an array"));
    }
    for (const auto& item : json.at("content")) {
      const auto block = content_block_from_json(item);
      if (!block) {
        return std::unexpected(block.error());
      }
      result.content.push_back(*block);
    }
  }

  if (json.contains("structuredContent")) {
    result.structured_content = json.at("structuredContent");
  }

  if (json.contains("isError")) {
    if (!json.at("isError").is_boolean()) {
      return std::unexpected(
          tool_json_error("tool result isError must be a boolean"));
    }
    result.is_error = json.at("isError").get<bool>();
  }

  return result;
}

}  // namespace mcp::protocol
