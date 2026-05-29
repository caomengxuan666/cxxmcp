// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/tool.hpp
/// @brief Tool definition, call, and result payloads.
///
/// Tools are server-advertised operations invoked through `tools/call`.
/// Definitions are returned by listing or lookup methods, calls carry JSON
/// arguments, and results return content blocks plus optional structured data.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/annotations.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/schema.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/types.hpp"
#include "cxxmcp/protocol/types_reflect.hpp"

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

template <>
struct Reflect<ToolExecution> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(field("taskSupport", &ToolExecution::task_support));
  }
  static std::vector<std::string> known_keys() { return {"taskSupport"}; }
};
CXXMCP_REFLECT_CHECK(ToolExecution, 1);

template <>
struct JsonFieldTraits<ToolExecution> {
  static void serialize(Json& json, const char* key,
                        const ToolExecution& value) {
    json[key] = reflect_to_json(value);
  }
  static bool deserialize(const Json& json, const char* key,
                          ToolExecution& target) {
    if (!json.contains(key)) {
      return false;
    }
    auto result = reflect_from_json<ToolExecution>(json.at(key));
    if (!result) {
      return false;
    }
    target = std::move(*result);
    return true;
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
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();

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
  /// Whether output_schema was explicitly present on the wire or configured.
  bool output_schema_present = false;
  /// Whether the tool may stream partial results outside a single response.
  bool streaming = false;
  /// Optional icon descriptors for client presentation.
  std::vector<Icon> icons;
  /// Optional execution configuration including task support mode.
  std::optional<ToolExecution> execution;
  /// Optional typed tool annotations for model or client presentation.
  std::optional<ToolAnnotations> tool_annotations;
  /// Optional raw annotations preserved for forward-compatible round trips.
  Json annotations = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();

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

  template <class T>
  ToolDefinitionBuilder& input() {
    return input_schema(schema_for<T>());
  }

  ToolDefinitionBuilder& output_schema(Json schema) {
    definition_.output_schema = std::move(schema);
    definition_.output_schema_present = true;
    return *this;
  }

  template <class T>
  ToolDefinitionBuilder& output() {
    return output_schema(schema_for<T>());
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

  ToolDefinitionBuilder& tool_annotations(ToolAnnotations value) {
    definition_.tool_annotations = std::move(value);
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
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Opaque request state echoed back on MRTR retries (SEP-2322).
  std::optional<std::string> request_state;
  /// Client-provided responses to server input requests (MRTR).
  std::optional<Json> input_responses;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

template <>
struct Reflect<ToolCall> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(
        field("name", &ToolCall::name),
        field("arguments", &ToolCall::arguments),
        field("task", &ToolCall::task), field("_meta", &ToolCall::meta),
        field("requestState", &ToolCall::request_state),
        field("inputResponses", &ToolCall::input_responses),
        extensions_field(&ToolCall::extensions,
                         {"name", "arguments", "task", "_meta", "requestState",
                          "inputResponses"}));
  }
  static std::vector<std::string> known_keys() {
    return {"name",  "arguments",    "task",
            "_meta", "requestState", "inputResponses"};
  }
};

/// @brief Result object for `tools/list`.
struct ToolsListResult {
  /// Tools available to the caller.
  std::vector<ToolDefinition> tools;
  /// Optional cursor for retrieving the next page.
  std::optional<std::string> next_cursor;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
  /// Cache time-to-live hint in milliseconds (SEP-2549).
  std::optional<std::int64_t> ttl_ms;
  /// Cache scope hint: "public" or "private" (SEP-2549).
  std::optional<std::string> cache_scope;
};

/// @brief Result object for `tools/call`.
struct ToolResult {
  /// Ordered user/model-visible content returned by the tool.
  std::vector<ContentBlock> content;
  /// Optional machine-readable result matching the tool output schema.
  std::optional<Json> structured_content;
  /// Optional domain-level error signal. Missing means the peer did not state
  /// whether the tool call produced an error; explicit false is preserved.
  std::optional<bool> is_error;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Result type discriminator. "input_required" for MRTR (SEP-2322).
  /// Missing means the result is complete.
  std::optional<std::string> result_type;
  /// Server input requests for MRTR (SEP-2322).
  std::optional<Json> input_requests;
  /// Opaque request state for MRTR retries (SEP-2322).
  std::optional<std::string> request_state;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();

  /// @brief Convenience predicate treating a missing signal as success.
  bool is_error_result() const noexcept { return is_error.value_or(false); }

  /// @brief Creates a successful text-only tool result.
  static ToolResult text(std::string value) {
    ToolResult result;
    result.content.push_back(ContentBlock::text_content(std::move(value)));
    result.is_error = false;
    return result;
  }

  /// @brief Creates an error text-only tool result.
  static ToolResult error_text(std::string value) {
    ToolResult result = text(std::move(value));
    result.is_error = true;
    return result;
  }
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

template <>
struct JsonFieldTraits<TaskSupport> {
  static void serialize(Json& json, const char* key, TaskSupport value) {
    json[key] = std::string(task_support_to_string(value));
  }
  static bool deserialize(const Json& json, const char* key,
                          TaskSupport& target) {
    if (!json.contains(key) || !json.at(key).is_string()) {
      return false;
    }
    auto val =
        task_support_from_string(json.at(key).get_ref<const std::string&>());
    if (!val.has_value()) {
      return false;
    }
    target = *val;
    return true;
  }
};

/// @brief Returns true when value is canonical RFC 4648 base64 with padding.
inline bool is_valid_base64(std::string_view value) noexcept {
  if (value.empty()) {
    return true;
  }
  if (value.size() % 4 != 0) {
    return false;
  }

  std::size_t padding = 0;
  if (value.back() == '=') {
    ++padding;
    if (value.size() >= 2 && value[value.size() - 2] == '=') {
      ++padding;
    }
  }

  for (std::size_t i = 0; i < value.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    const bool is_base64_char = (c >= 'A' && c <= 'Z') ||
                                (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '+' || c == '/';
    if (is_base64_char) {
      continue;
    }
    if (c == '=' && i >= value.size() - padding) {
      continue;
    }
    return false;
  }

  return true;
}

/// @brief Serializes tool execution configuration.
inline Json tool_execution_to_json(const ToolExecution& execution) {
  return reflect_to_json(execution);
}

/// @brief Parses tool execution configuration.
/// @return Parsed execution configuration or validation error.
inline core::Result<ToolExecution> tool_execution_from_json(const Json& json) {
  return reflect_from_json<ToolExecution>(json);
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
    if (((block.type == "image" || block.type == "audio") &&
         block.data.is_string()) ||
        !block.data.empty()) {
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
  append_json_extensions(json, block.extensions);
  return json;
}

/// @brief Reads a required string member from a content object.
inline core::Result<std::string> required_content_string(
    const Json& json, std::string_view member, std::string message) {
  const std::string key(member);
  if (!json.contains(key) || !json.at(key).is_string()) {
    return mcp::core::unexpected(tool_json_error(std::move(message)));
  }
  return json.at(key).get<std::string>();
}

/// @brief Parses a content block from JSON.
/// @return Parsed block or validation error.
inline core::Result<ContentBlock> content_block_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        tool_json_error("content block must be an object"));
  }

  ContentBlock block;
  if (!json.contains("type") || !json.at("type").is_string()) {
    return mcp::core::unexpected(
        tool_json_error("content block requires a string type"));
  }
  block.type = json.at("type").get<std::string>();

  if (block.type == "image" || block.type == "audio") {
    const auto data = required_content_string(
        json, "data", block.type + " content block requires string data");
    if (!data) {
      return mcp::core::unexpected(data.error());
    }
    const auto mime_type = required_content_string(
        json, "mimeType",
        block.type + " content block requires string mimeType");
    if (!mime_type) {
      return mcp::core::unexpected(mime_type.error());
    }
    if (!is_valid_base64(*data)) {
      return mcp::core::unexpected(
          tool_json_error(block.type + " content block data must be base64"));
    }
    block.data = *data;
    block.mime_type = *mime_type;
  } else if (block.type == "resource") {
    if (!json.contains("resource")) {
      return mcp::core::unexpected(
          tool_json_error("resource content block requires resource"));
    }
    const auto resource = resource_contents_from_json(json.at("resource"));
    if (!resource) {
      return mcp::core::unexpected(resource.error());
    }
    block.resource = *resource;
  } else if (block.type == "resource_link") {
    const auto resource = resource_from_json(json);
    if (!resource) {
      return mcp::core::unexpected(resource.error());
    }
    block.resource_link = *resource;
  } else if (block.type == "text") {
    const auto text = required_content_string(
        json, "text", "text content block requires string text");
    if (!text) {
      return mcp::core::unexpected(text.error());
    }
    block.text = *text;
  } else {
    return mcp::core::unexpected(
        tool_json_error("content block type is not supported"));
  }

  if (json.contains("annotations")) {
    const auto parsed = annotations_from_json(json.at("annotations"));
    if (!parsed) {
      return mcp::core::unexpected(parsed.error());
    }
    block.annotations = annotations_to_json(*parsed);
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          tool_json_error("content block _meta must be an object"));
    }
    block.meta = json.at("_meta");
  }
  block.extensions = collect_json_extensions(
      json,
      {"type", "text", "data", "mimeType", "resource", "uri", "name", "title",
       "description", "mimeType", "size", "icons", "annotations", "_meta"});

  return block;
}

/// @brief Serializes a tool definition as returned by tool discovery.
inline Json tool_definition_to_json(const ToolDefinition& definition) {
  Json json = Json::object();
  if (!definition.title.empty()) {
    json["title"] = definition.title;
  }
  json["name"] = definition.name;
  if (!definition.description.empty()) {
    json["description"] = definition.description;
  }
  json["inputSchema"] = definition.input_schema;
  if (definition.output_schema_present || !definition.output_schema.empty()) {
    json["outputSchema"] = definition.output_schema;
  }
  if (definition.streaming) {
    json["streaming"] = definition.streaming;
  }
  if (!definition.icons.empty()) {
    json["icons"] = Json::array();
    for (const auto& icon : definition.icons) {
      json["icons"].push_back(icon_to_json(icon));
    }
  }
  if (definition.execution.has_value()) {
    json["execution"] = tool_execution_to_json(*definition.execution);
  }
  if (definition.tool_annotations.has_value()) {
    json["annotations"] =
        tool_annotations_to_json(*definition.tool_annotations);
  } else if (!definition.annotations.empty()) {
    json["annotations"] = definition.annotations;
  }
  if (definition.meta.has_value()) {
    json["_meta"] = *definition.meta;
  }
  append_json_extensions(json, definition.extensions);
  return json;
}

/// @brief Parses a tool definition from JSON.
/// @return Parsed definition or validation error.
inline core::Result<ToolDefinition> tool_definition_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        tool_json_error("tool definition must be an object"));
  }

  ToolDefinition definition;
  if (json.contains("title")) {
    if (!json.at("title").is_string()) {
      return mcp::core::unexpected(
          tool_json_error("tool definition title must be a string"));
    }
    definition.title = json.at("title").get<std::string>();
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return mcp::core::unexpected(
        tool_json_error("tool definition requires a string name"));
  }
  definition.name = json.at("name").get<std::string>();

  if (json.contains("description")) {
    if (!json.at("description").is_string()) {
      return mcp::core::unexpected(
          tool_json_error("tool definition description must be a string"));
    }
    definition.description = json.at("description").get<std::string>();
  }

  if (!json.contains("inputSchema") || !json.at("inputSchema").is_object()) {
    return mcp::core::unexpected(
        tool_json_error("tool definition requires object inputSchema"));
  }
  definition.input_schema = json.at("inputSchema");

  if (json.contains("outputSchema")) {
    if (!json.at("outputSchema").is_object()) {
      return mcp::core::unexpected(
          tool_json_error("tool definition outputSchema must be an object"));
    }
    definition.output_schema = json.at("outputSchema");
    definition.output_schema_present = true;
  }

  if (json.contains("streaming")) {
    if (!json.at("streaming").is_boolean()) {
      return mcp::core::unexpected(
          tool_json_error("tool definition streaming must be a boolean"));
    }
    definition.streaming = json.at("streaming").get<bool>();
  }

  if (json.contains("icons")) {
    if (!json.at("icons").is_array()) {
      return mcp::core::unexpected(
          tool_json_error("tool definition icons must be an array"));
    }
    for (const auto& item : json.at("icons")) {
      const auto icon = icon_from_json(item);
      if (!icon.has_value()) {
        return mcp::core::unexpected(
            tool_json_error("tool definition icon is invalid"));
      }
      definition.icons.push_back(*icon);
    }
  }

  if (json.contains("execution")) {
    const auto execution = tool_execution_from_json(json.at("execution"));
    if (!execution) {
      return mcp::core::unexpected(execution.error());
    }
    definition.execution = *execution;
  }

  if (json.contains("annotations")) {
    if (!json.at("annotations").is_object()) {
      return mcp::core::unexpected(
          tool_json_error("tool definition annotations must be an object"));
    }
    const auto parsed = tool_annotations_from_json(json.at("annotations"));
    if (parsed) {
      definition.tool_annotations = *parsed;
    }
    definition.annotations = json.at("annotations");
  }

  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          tool_json_error("tool definition _meta must be an object"));
    }
    definition.meta = json.at("_meta");
  }
  definition.extensions = collect_json_extensions(
      json, {"title", "name", "description", "inputSchema", "outputSchema",
             "streaming", "icons", "execution", "annotations", "_meta"});

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
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  if (result.ttl_ms.has_value()) {
    json["ttlMs"] = *result.ttl_ms;
  }
  if (result.cache_scope.has_value()) {
    json["cacheScope"] = *result.cache_scope;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a `tools/list` result.
/// @return Parsed result or validation error.
inline core::Result<ToolsListResult> tools_list_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        tool_json_error("tools/list result must be an object"));
  }
  if (!json.contains("tools") || !json.at("tools").is_array()) {
    return mcp::core::unexpected(
        tool_json_error("tools/list result requires a tools array"));
  }

  ToolsListResult result;
  for (const auto& item : json.at("tools")) {
    const auto tool = tool_definition_from_json(item);
    if (!tool) {
      return mcp::core::unexpected(tool.error());
    }
    result.tools.push_back(*tool);
  }
  if (json.contains("nextCursor")) {
    if (!json.at("nextCursor").is_string()) {
      return mcp::core::unexpected(
          tool_json_error("tools/list nextCursor must be a string"));
    }
    result.next_cursor = json.at("nextCursor").get<std::string>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          tool_json_error("tools/list result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions =
      collect_json_extensions(json, {"tools", "nextCursor", "_meta"});
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
  if (call.meta.has_value()) {
    json["_meta"] = *call.meta;
  }
  if (call.request_state.has_value()) {
    json["requestState"] = *call.request_state;
  }
  if (call.input_responses.has_value()) {
    json["inputResponses"] = *call.input_responses;
  }
  append_json_extensions(json, call.extensions);
  return json;
}

/// @brief Parses `tools/call` params.
/// @return Parsed call or validation error.
inline core::Result<ToolCall> tool_call_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        tool_json_error("tools/call params must be an object"));
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return mcp::core::unexpected(
        tool_json_error("tools/call params require a string name"));
  }

  ToolCall call;
  call.name = json.at("name").get<std::string>();
  if (json.contains("arguments")) {
    if (!json.at("arguments").is_object()) {
      return mcp::core::unexpected(
          tool_json_error("tools/call arguments must be an object"));
    }
    call.arguments = json.at("arguments");
  }
  if (json.contains("task")) {
    const auto task = task_request_parameters_from_json(json.at("task"));
    if (!task) {
      return mcp::core::unexpected(task.error());
    }
    call.task = *task;
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          tool_json_error("tools/call _meta must be an object"));
    }
    call.meta = json.at("_meta");
  }
  if (json.contains("requestState")) {
    call.request_state = json.at("requestState").get<std::string>();
  }
  if (json.contains("inputResponses")) {
    call.input_responses = json.at("inputResponses");
  }
  call.extensions = collect_json_extensions(
      json,
      {"name", "arguments", "task", "_meta", "requestState", "inputResponses"});
  return call;
}

/// @brief Serializes a tool result.
inline Json tool_result_to_json(const ToolResult& result) {
  Json json = Json::object();
  if (result.result_type.has_value()) {
    json["resultType"] = *result.result_type;
  }
  if (!result.content.empty()) {
    json["content"] = Json::array();
    for (const auto& block : result.content) {
      json["content"].push_back(content_block_to_json(block));
    }
  }
  if (result.structured_content.has_value()) {
    json["structuredContent"] = *result.structured_content;
  }
  if (result.is_error.has_value()) {
    json["isError"] = *result.is_error;
  }
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  if (result.input_requests.has_value()) {
    json["inputRequests"] = *result.input_requests;
  }
  if (result.request_state.has_value()) {
    json["requestState"] = *result.request_state;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a tool result.
/// @return Parsed result or validation error.
inline core::Result<ToolResult> tool_result_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        tool_json_error("tool result must be an object"));
  }
  if (!json.contains("content") && !json.contains("structuredContent") &&
      !json.contains("isError") && !json.contains("_meta") &&
      !json.contains("resultType") && !json.contains("inputRequests") &&
      !json.contains("requestState")) {
    return mcp::core::unexpected(tool_json_error(
        "tool result requires content, structuredContent, isError, _meta, "
        "resultType, inputRequests, or requestState"));
  }

  ToolResult result;
  if (json.contains("resultType")) {
    result.result_type = json.at("resultType").get<std::string>();
  }
  if (json.contains("content")) {
    if (!json.at("content").is_array()) {
      return mcp::core::unexpected(
          tool_json_error("tool result content must be an array"));
    }
    for (const auto& item : json.at("content")) {
      const auto block = content_block_from_json(item);
      if (!block) {
        return mcp::core::unexpected(block.error());
      }
      result.content.push_back(*block);
    }
  }

  if (json.contains("structuredContent")) {
    result.structured_content = json.at("structuredContent");
  }

  if (json.contains("isError")) {
    if (!json.at("isError").is_boolean()) {
      return mcp::core::unexpected(
          tool_json_error("tool result isError must be a boolean"));
    }
    result.is_error = json.at("isError").get<bool>();
  }

  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          tool_json_error("tool result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  if (json.contains("inputRequests")) {
    result.input_requests = json.at("inputRequests");
  }
  if (json.contains("requestState")) {
    result.request_state = json.at("requestState").get<std::string>();
  }
  result.extensions = collect_json_extensions(
      json, {"content", "structuredContent", "isError", "_meta", "resultType",
             "inputRequests", "requestState"});

  return result;
}

// ── SEP-2243: x-mcp-header support ──────────────────────────────────────────

/// @brief One x-mcp-header annotation extracted from a tool input schema.
struct XHeaderEntry {
  std::string header_name;  // x-mcp-header value, e.g. "Region"
  std::string param_name;   // property key, e.g. "region"
  std::string type;         // JSON Schema type: "string", "number", etc.
};

/// @brief Extracts x-mcp-header annotations from a tool inputSchema.
inline std::vector<XHeaderEntry> extract_x_mcp_headers(
    const Json& input_schema) {
  std::vector<XHeaderEntry> result;
  if (!input_schema.is_object() || !input_schema.contains("properties") ||
      !input_schema.at("properties").is_object()) {
    return result;
  }
  for (auto& [key, prop] : input_schema.at("properties").items()) {
    if (!prop.is_object() || !prop.contains("x-mcp-header") ||
        !prop.at("x-mcp-header").is_string()) {
      continue;
    }
    std::string type_str;
    if (prop.contains("type") && prop.at("type").is_string()) {
      type_str = prop.at("type").get<std::string>();
    }
    result.push_back({prop.at("x-mcp-header").get<std::string>(),
                      std::string(key), std::move(type_str)});
  }
  return result;
}

/// @brief Validates x-mcp-header annotations per SEP-2243 constraints.
/// Returns true if all headers are valid (tool should be kept).
inline bool validate_tool_x_headers(const std::vector<XHeaderEntry>& entries) {
  std::unordered_set<std::string> seen_lower;
  for (const auto& entry : entries) {
    if (entry.header_name.empty()) return false;
    for (char ch : entry.header_name) {
      const auto c = static_cast<unsigned char>(ch);
      if (c < 0x21 || c > 0x7E || c == ':') return false;
    }
    if (entry.type == "object" || entry.type == "array" ||
        entry.type == "null" || entry.type.empty()) {
      return false;
    }
    std::string lower = entry.header_name;
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!seen_lower.insert(lower).second) return false;
  }
  return true;
}

/// @brief Checks if a string value needs Base64 encoding per SEP-2243.
inline bool needs_base64_encoding(std::string_view value) {
  for (const char ch : value) {
    const auto c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c > 0x7E) return true;
  }
  if (!value.empty() && (value.front() == ' ' || value.back() == ' ')) {
    return true;
  }
  return false;
}

/// @brief Standard RFC 4648 base64 encoding.
inline std::string base64_encode(std::string_view input) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((input.size() + 2) / 3) * 4);
  const auto* data = reinterpret_cast<const unsigned char*>(input.data());
  std::size_t i = 0;
  while (i + 2 < input.size()) {
    result.push_back(kAlphabet[(data[i] >> 2) & 0x3F]);
    result.push_back(
        kAlphabet[((data[i] & 0x03) << 4) | ((data[i + 1] >> 4) & 0x0F)]);
    result.push_back(
        kAlphabet[((data[i + 1] & 0x0F) << 2) | ((data[i + 2] >> 6) & 0x03)]);
    result.push_back(kAlphabet[data[i + 2] & 0x3F]);
    i += 3;
  }
  if (i + 1 == input.size()) {
    result.push_back(kAlphabet[(data[i] >> 2) & 0x3F]);
    result.push_back(kAlphabet[(data[i] & 0x03) << 4]);
    result.push_back('=');
    result.push_back('=');
  } else if (i + 2 == input.size()) {
    result.push_back(kAlphabet[(data[i] >> 2) & 0x3F]);
    result.push_back(
        kAlphabet[((data[i] & 0x03) << 4) | ((data[i + 1] >> 4) & 0x0F)]);
    result.push_back(kAlphabet[(data[i + 1] & 0x0F) << 2]);
    result.push_back('=');
  }
  return result;
}

/// @brief Encodes a string value for an Mcp-Param-* header per SEP-2243.
inline std::string encode_header_value(std::string_view value) {
  if (needs_base64_encoding(value)) {
    return "=?base64?" + base64_encode(value) + "?=";
  }
  return std::string(value);
}

/// @brief Converts a JSON number to its string representation.
inline std::string number_to_header_string(const Json& value) {
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  return value.dump();
}

/// @brief Builds Mcp-Param-* transport headers from tool arguments and schema.
///
/// Inspects the tool inputSchema for x-mcp-header annotations and converts
/// matching argument values to HTTP header values per SEP-2243.
inline std::unordered_map<std::string, std::string> build_tool_param_headers(
    const Json& arguments, const std::vector<XHeaderEntry>& entries) {
  std::unordered_map<std::string, std::string> headers;
  for (const auto& entry : entries) {
    if (!arguments.contains(entry.param_name)) continue;
    const auto& value = arguments.at(entry.param_name);
    if (value.is_null()) continue;
    std::string header_key = "Mcp-Param-" + entry.header_name;
    if (value.is_boolean()) {
      headers[header_key] = value.get<bool>() ? "true" : "false";
    } else if (value.is_number()) {
      headers[header_key] = number_to_header_string(value);
    } else if (value.is_string()) {
      headers[header_key] = encode_header_value(value.get<std::string>());
    }
  }
  return headers;
}

}  // namespace mcp::protocol
