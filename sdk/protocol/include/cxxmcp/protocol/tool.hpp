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
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief A single content item returned by a tool or embedded in prompts.
struct ContentBlock {
  /// Content kind. `"text"` is the default SDK-supported kind.
  std::string type = "text";
  /// Text payload for text blocks.
  std::string text;
  /// Additional type-specific data for non-text or extended content.
  Json data = Json::object();
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
  /// Optional annotations for model or client presentation.
  Json annotations = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
};

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

/// @brief Serializes a content block.
inline Json content_block_to_json(const ContentBlock& block) {
  Json json = Json::object();
  json["type"] = block.type;
  json["text"] = block.text;
  if (!block.data.empty()) {
    json["data"] = block.data;
  }
  return json;
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
