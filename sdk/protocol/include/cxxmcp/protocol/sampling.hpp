// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/sampling.hpp
/// @brief Client-side model sampling request and response payloads.
///
/// Sampling lets a server ask a capable client to create a model message
/// through `sampling/createMessage`. The request carries chat-like messages,
/// optional model preferences, and generation controls; the response carries
/// one generated message and the model that produced it.

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Context inclusion mode for sampling requests.
enum class ContextInclusion {
  /// Include context from all servers.
  AllServers,
  /// Do not include any server context.
  None,
  /// Include context only from the requesting server.
  ThisServer,
};

/// @brief Message role for sampling messages.
enum class Role {
  /// The human/user participant.
  User,
  /// The model/assistant participant.
  Assistant,
};

/// @brief Converts a Role to its lowercase wire representation.
inline std::string_view to_string(Role role) noexcept {
  switch (role) {
    case Role::User:
      return "user";
    case Role::Assistant:
      return "assistant";
  }
  return "user";
}

/// @brief Parses a Role from its wire string.
/// @return Parsed role, or nullopt for unrecognized strings.
inline std::optional<Role> from_string_role(std::string_view value) noexcept {
  if (value == "user") {
    return Role::User;
  }
  if (value == "assistant") {
    return Role::Assistant;
  }
  return std::nullopt;
}

/// @brief Tool selection mode for SEP-1577 sampling requests.
enum class ToolChoiceMode {
  /// Let the model decide whether to use tools.
  Auto,
  /// Require at least one tool use.
  Required,
  /// Do not allow tool use.
  None,
};

/// @brief Tool selection behavior supplied to `sampling/createMessage`.
struct ToolChoice {
  /// Optional selection mode. Missing preserves an empty object if present.
  std::optional<ToolChoiceMode> mode;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();

  /// @brief Lets the model decide whether to use tools.
  static ToolChoice auto_choice() { return ToolChoice{ToolChoiceMode::Auto}; }

  /// @brief Requires the model to use a tool.
  static ToolChoice required() { return ToolChoice{ToolChoiceMode::Required}; }

  /// @brief Prevents model tool use.
  static ToolChoice none() { return ToolChoice{ToolChoiceMode::None}; }
};

/// @brief Assistant-side tool call request embedded in sampling content.
struct ToolUseContent {
  /// Unique id for this requested tool call.
  std::string id;
  /// Tool name to invoke.
  std::string name;
  /// JSON object containing tool arguments.
  Json input = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief User-side result for a prior sampling tool call.
struct ToolResultContent {
  /// Id of the corresponding tool use.
  std::string tool_use_id;
  /// Tool-visible content blocks returned by the tool.
  std::vector<ContentBlock> content;
  /// Optional structured result object.
  std::optional<Json> structured_content;
  /// Optional error marker for failed tool execution.
  std::optional<bool> is_error;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief One sampling message content item.
///
/// Text, image, and audio content reuse ContentBlock. SEP-1577 tool-use and
/// tool-result items are modeled explicitly because their wire shape is not the
/// same as ordinary tool-call results.
struct SamplingMessageContent {
  /// Ordinary content block for text, image, audio, or extension content.
  ContentBlock content = ContentBlock::text_content("");
  /// Assistant-only tool-use content.
  std::optional<ToolUseContent> tool_use;
  /// User-only tool-result content.
  std::optional<ToolResultContent> tool_result;

  /// @brief Creates a text sampling content item.
  static SamplingMessageContent text(std::string value) {
    SamplingMessageContent item;
    item.content = ContentBlock::text_content(std::move(value));
    return item;
  }

  /// @brief Wraps a regular content block.
  static SamplingMessageContent from_content(ContentBlock value) {
    SamplingMessageContent item;
    item.content = std::move(value);
    return item;
  }

  /// @brief Creates an assistant tool-use content item.
  static SamplingMessageContent tool_use_content(ToolUseContent value) {
    SamplingMessageContent item;
    item.content.type = "tool_use";
    item.tool_use = std::move(value);
    return item;
  }

  /// @brief Creates a user tool-result content item.
  static SamplingMessageContent tool_result_content(ToolResultContent value) {
    SamplingMessageContent item;
    item.content.type = "tool_result";
    item.tool_result = std::move(value);
    return item;
  }

  /// @brief Returns true when this item carries tool-use content.
  bool is_tool_use() const noexcept { return tool_use.has_value(); }

  /// @brief Returns true when this item carries tool-result content.
  bool is_tool_result() const noexcept { return tool_result.has_value(); }
};

/// @brief Input message supplied to a sampling request.
struct SamplingMessage {
  /// Message role understood by the sampling client.
  std::string role;
  /// First or only message content block kept for simple callers.
  ContentBlock content;
  /// Optional full content list. Empty means `content` is serialized.
  std::vector<SamplingMessageContent> contents;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();

  /// @brief Creates a text-only sampling message for the given role.
  static SamplingMessage text(std::string role, std::string value) {
    SamplingMessage message;
    message.role = std::move(role);
    message.content = ContentBlock::text_content(std::move(value));
    return message;
  }
};

/// @brief Soft model name hint for sampling.
struct ModelHint {
  /// Model family, name, or alias preferred by the requester.
  std::string name;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Preferences used by the client when choosing a model.
struct ModelPreferences {
  /// Ordered or unordered model hints supplied by the requester.
  std::vector<ModelHint> hints;
  /// Optional priority for low cost, typically normalized by the peer.
  std::optional<double> cost_priority;
  /// Optional priority for low latency, typically normalized by the peer.
  std::optional<double> speed_priority;
  /// Optional priority for model capability, typically normalized by the peer.
  std::optional<double> intelligence_priority;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for `sampling/createMessage`.
struct CreateMessageParams {
  /// Conversation messages to sample from.
  std::vector<SamplingMessage> messages;
  /// Optional model selection preferences.
  std::optional<ModelPreferences> model_preferences;
  /// Optional system prompt supplied outside the message array.
  std::optional<std::string> system_prompt;
  /// Optional instruction for whether client context should be included.
  std::optional<std::string> include_context;
  /// Optional sampling temperature. Explicit 0.0 is serialized.
  std::optional<double> temperature;
  /// Maximum tokens the client may generate.
  int max_tokens = 0;
  /// Optional stop sequences.
  std::vector<std::string> stop_sequences;
  /// Optional metadata object carried through the sampling request.
  Json metadata = Json::object();
  /// Optional task request parameters for asynchronous sampling.
  std::optional<TaskRequestParameters> task;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Optional tools available to the sampled model.
  std::vector<ToolDefinition> tools;
  /// Optional tool selection behavior.
  std::optional<ToolChoice> tool_choice;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result object for `sampling/createMessage`.
struct CreateMessageResult {
  /// Role assigned to the generated message.
  std::string role;
  /// First or only generated content kept for simple callers.
  ContentBlock content;
  /// Optional full generated content list. Empty means `content` is serialized.
  std::vector<SamplingMessageContent> contents;
  /// Model identifier selected by the client.
  std::string model;
  /// Optional reason generation stopped.
  std::string stop_reason;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();

  /// @brief Creates a text-only sampling result for the given role and model.
  static CreateMessageResult text(std::string role, std::string value,
                                  std::string model = {}) {
    CreateMessageResult result;
    result.role = std::move(role);
    result.content = ContentBlock::text_content(std::move(value));
    result.model = std::move(model);
    return result;
  }
};

/// @brief Builds an InvalidRequest error for sampling JSON validation failures.
inline core::Error sampling_json_error(std::string message) {
  return core::Error{
      static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

/// @brief Returns true for the MCP sampling message roles.
inline bool sampling_role_is_valid(std::string_view role) noexcept {
  return from_string_role(role).has_value();
}

/// @brief Returns true for the MCP sampling message roles (enum overload).
inline bool sampling_role_is_valid(Role /*role*/) noexcept { return true; }

/// @brief Converts a context-inclusion mode to the camelCase wire value.
inline std::string_view context_inclusion_to_string(ContextInclusion mode) {
  switch (mode) {
    case ContextInclusion::AllServers:
      return "allServers";
    case ContextInclusion::None:
      return "none";
    case ContextInclusion::ThisServer:
      return "thisServer";
  }
  return "none";
}

/// @brief Parses a context-inclusion mode string.
inline std::optional<ContextInclusion> context_inclusion_from_string(
    std::string_view value) {
  if (value == "allServers") {
    return ContextInclusion::AllServers;
  }
  if (value == "none") {
    return ContextInclusion::None;
  }
  if (value == "thisServer") {
    return ContextInclusion::ThisServer;
  }
  return std::nullopt;
}

/// @brief Returns true for the MCP sampling includeContext values.
inline bool sampling_include_context_is_valid(std::string_view value) noexcept {
  return context_inclusion_from_string(value).has_value();
}

/// @brief Returns true for finite JSON numbers accepted by sampling.
inline bool sampling_number_is_finite(double value) noexcept {
  return std::isfinite(value);
}

/// @brief Validates SEP-1577 tool-use/tool-result placement for one message.
inline core::Result<core::Unit> validate_sampling_message_content_roles(
    const SamplingMessage& message) {
  bool has_tool_result = false;
  bool has_non_tool_result = false;
  for (const auto& content : message.contents) {
    if (content.is_tool_use() && message.role != "assistant") {
      return mcp::core::unexpected(sampling_json_error(
          "sampling tool_use content is only allowed in assistant messages"));
    }
    if (content.is_tool_result() && message.role != "user") {
      return mcp::core::unexpected(sampling_json_error(
          "sampling tool_result content is only allowed in user messages"));
    }
    has_tool_result = has_tool_result || content.is_tool_result();
    has_non_tool_result = has_non_tool_result || !content.is_tool_result();
  }
  if (has_tool_result && has_non_tool_result) {
    return mcp::core::unexpected(sampling_json_error(
        "sampling tool_result messages must not mix other content types"));
  }
  return core::Unit{};
}

/// @brief Validates that every sampling tool result answers a prior tool use.
inline core::Result<core::Unit> validate_sampling_tool_use_result_balance(
    const std::vector<SamplingMessage>& messages) {
  std::vector<std::string> pending_tool_use_ids;
  for (const auto& message : messages) {
    if (message.role == "assistant") {
      for (const auto& content : message.contents) {
        if (content.tool_use.has_value()) {
          pending_tool_use_ids.push_back(content.tool_use->id);
        }
      }
      continue;
    }
    if (message.role != "user") {
      continue;
    }
    for (const auto& content : message.contents) {
      if (!content.tool_result.has_value()) {
        continue;
      }
      const auto found =
          std::find(pending_tool_use_ids.begin(), pending_tool_use_ids.end(),
                    content.tool_result->tool_use_id);
      if (found == pending_tool_use_ids.end()) {
        return mcp::core::unexpected(sampling_json_error(
            "sampling tool_result content has no matching tool_use"));
      }
      pending_tool_use_ids.erase(found);
    }
  }
  if (!pending_tool_use_ids.empty()) {
    return mcp::core::unexpected(sampling_json_error(
        "sampling tool_use content is missing a matching tool_result"));
  }
  return core::Unit{};
}

/// @brief Converts a tool-choice mode to the lowercase wire value.
inline std::string tool_choice_mode_to_string(ToolChoiceMode mode) {
  switch (mode) {
    case ToolChoiceMode::Auto:
      return "auto";
    case ToolChoiceMode::Required:
      return "required";
    case ToolChoiceMode::None:
      return "none";
  }
  return "auto";
}

/// @brief Parses a tool-choice mode string.
inline std::optional<ToolChoiceMode> tool_choice_mode_from_string(
    const std::string& value) {
  if (value == "auto") {
    return ToolChoiceMode::Auto;
  }
  if (value == "required") {
    return ToolChoiceMode::Required;
  }
  if (value == "none") {
    return ToolChoiceMode::None;
  }
  return std::nullopt;
}

/// @brief Serializes tool-choice behavior.
inline Json tool_choice_to_json(const ToolChoice& choice) {
  Json json = Json::object();
  if (choice.mode.has_value()) {
    json["mode"] = tool_choice_mode_to_string(*choice.mode);
  }
  append_json_extensions(json, choice.extensions);
  return json;
}

/// @brief Parses tool-choice behavior.
inline core::Result<ToolChoice> tool_choice_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        sampling_json_error("toolChoice must be an object"));
  }
  ToolChoice choice;
  if (json.contains("mode")) {
    if (!json.at("mode").is_string()) {
      return mcp::core::unexpected(
          sampling_json_error("toolChoice mode must be a string"));
    }
    const auto mode =
        tool_choice_mode_from_string(json.at("mode").get<std::string>());
    if (!mode.has_value()) {
      return mcp::core::unexpected(
          sampling_json_error("toolChoice mode is not supported"));
    }
    choice.mode = *mode;
  }
  choice.extensions = collect_json_extensions(json, {"mode"});
  return choice;
}

/// @brief Serializes assistant-side sampling tool use content.
inline Json tool_use_content_to_json(const ToolUseContent& content) {
  Json json = Json::object();
  json["type"] = "tool_use";
  json["id"] = content.id;
  json["name"] = content.name;
  json["input"] = content.input;
  if (content.meta.has_value()) {
    json["_meta"] = *content.meta;
  }
  append_json_extensions(json, content.extensions);
  return json;
}

/// @brief Parses assistant-side sampling tool use content.
inline core::Result<ToolUseContent> tool_use_content_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        sampling_json_error("tool_use content must be an object"));
  }
  if (!json.contains("id") || !json.at("id").is_string()) {
    return mcp::core::unexpected(
        sampling_json_error("tool_use content requires a string id"));
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return mcp::core::unexpected(
        sampling_json_error("tool_use content requires a string name"));
  }
  if (!json.contains("input") || !json.at("input").is_object()) {
    return mcp::core::unexpected(
        sampling_json_error("tool_use content requires object input"));
  }
  ToolUseContent content;
  content.id = json.at("id").get<std::string>();
  content.name = json.at("name").get<std::string>();
  content.input = json.at("input");
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          sampling_json_error("tool_use content _meta must be an object"));
    }
    content.meta = json.at("_meta");
  }
  content.extensions =
      collect_json_extensions(json, {"type", "id", "name", "input", "_meta"});
  return content;
}

/// @brief Serializes user-side sampling tool result content.
inline Json tool_result_content_to_json(const ToolResultContent& content) {
  Json json = Json::object();
  json["type"] = "tool_result";
  json["toolUseId"] = content.tool_use_id;
  if (!content.content.empty()) {
    json["content"] = Json::array();
    for (const auto& block : content.content) {
      json["content"].push_back(content_block_to_json(block));
    }
  }
  if (content.structured_content.has_value()) {
    json["structuredContent"] = *content.structured_content;
  }
  if (content.is_error.has_value()) {
    json["isError"] = *content.is_error;
  }
  if (content.meta.has_value()) {
    json["_meta"] = *content.meta;
  }
  append_json_extensions(json, content.extensions);
  return json;
}

/// @brief Parses user-side sampling tool result content.
inline core::Result<ToolResultContent> tool_result_content_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        sampling_json_error("tool_result content must be an object"));
  }
  if (!json.contains("toolUseId") || !json.at("toolUseId").is_string()) {
    return mcp::core::unexpected(
        sampling_json_error("tool_result content requires toolUseId"));
  }
  ToolResultContent content;
  content.tool_use_id = json.at("toolUseId").get<std::string>();
  if (json.contains("content")) {
    if (!json.at("content").is_array()) {
      return mcp::core::unexpected(
          sampling_json_error("tool_result content must be an array"));
    }
    for (const auto& item : json.at("content")) {
      const auto block = content_block_from_json(item);
      if (!block) {
        return mcp::core::unexpected(block.error());
      }
      content.content.push_back(*block);
    }
  }
  if (json.contains("structuredContent")) {
    if (!json.at("structuredContent").is_object()) {
      return mcp::core::unexpected(sampling_json_error(
          "tool_result structuredContent must be an object"));
    }
    content.structured_content = json.at("structuredContent");
  }
  if (json.contains("isError")) {
    if (!json.at("isError").is_boolean()) {
      return mcp::core::unexpected(
          sampling_json_error("tool_result isError must be a boolean"));
    }
    content.is_error = json.at("isError").get<bool>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          sampling_json_error("tool_result _meta must be an object"));
    }
    content.meta = json.at("_meta");
  }
  content.extensions =
      collect_json_extensions(json, {"type", "toolUseId", "content",
                                     "structuredContent", "isError", "_meta"});
  return content;
}

/// @brief Serializes one sampling message content item.
inline Json sampling_message_content_to_json(
    const SamplingMessageContent& content) {
  if (content.tool_use.has_value()) {
    return tool_use_content_to_json(*content.tool_use);
  }
  if (content.tool_result.has_value()) {
    return tool_result_content_to_json(*content.tool_result);
  }
  return content_block_to_json(content.content);
}

/// @brief Parses one sampling message content item.
inline core::Result<SamplingMessageContent> sampling_message_content_from_json(
    const Json& json) {
  if (json.is_object() && json.contains("type") &&
      json.at("type").is_string()) {
    const auto type = json.at("type").get<std::string>();
    if (type == "tool_use") {
      const auto tool_use = tool_use_content_from_json(json);
      if (!tool_use) {
        return mcp::core::unexpected(tool_use.error());
      }
      return SamplingMessageContent::tool_use_content(*tool_use);
    }
    if (type == "tool_result") {
      const auto tool_result = tool_result_content_from_json(json);
      if (!tool_result) {
        return mcp::core::unexpected(tool_result.error());
      }
      return SamplingMessageContent::tool_result_content(*tool_result);
    }
  }

  const auto block = content_block_from_json(json);
  if (!block) {
    return mcp::core::unexpected(block.error());
  }
  if (block->type == "resource" || block->type == "resource_link") {
    return mcp::core::unexpected(sampling_json_error(
        "sampling message content does not support resource content"));
  }
  return SamplingMessageContent::from_content(*block);
}

/// @brief Serializes a sampling message.
inline Json sampling_message_to_json(const SamplingMessage& message) {
  Json json = Json{{"role", message.role}};
  if (!message.contents.empty()) {
    if (message.contents.size() == 1) {
      json["content"] =
          sampling_message_content_to_json(message.contents.front());
    } else {
      json["content"] = Json::array();
      for (const auto& content : message.contents) {
        json["content"].push_back(sampling_message_content_to_json(content));
      }
    }
  } else {
    json["content"] = content_block_to_json(message.content);
  }
  if (message.meta.has_value()) {
    json["_meta"] = *message.meta;
  }
  append_json_extensions(json, message.extensions);
  return json;
}

/// @brief Parses a sampling message.
/// @return Parsed message or validation error.
inline core::Result<SamplingMessage> sampling_message_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        sampling_json_error("sampling message must be an object"));
  }
  if (!json.contains("role") || !json.at("role").is_string()) {
    return mcp::core::unexpected(
        sampling_json_error("sampling message requires a string role"));
  }
  if (!json.contains("content")) {
    return mcp::core::unexpected(
        sampling_json_error("sampling message requires content"));
  }
  SamplingMessage message;
  message.role = json.at("role").get<std::string>();
  if (!sampling_role_is_valid(message.role)) {
    return mcp::core::unexpected(
        sampling_json_error("sampling message role is not supported"));
  }
  if (json.at("content").is_array()) {
    for (const auto& item : json.at("content")) {
      const auto content = sampling_message_content_from_json(item);
      if (!content) {
        return mcp::core::unexpected(content.error());
      }
      message.contents.push_back(*content);
    }
  } else {
    const auto content = sampling_message_content_from_json(json.at("content"));
    if (!content) {
      return mcp::core::unexpected(content.error());
    }
    message.contents.push_back(*content);
  }
  if (!message.contents.empty()) {
    message.content = message.contents.front().content;
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          sampling_json_error("sampling message _meta must be an object"));
    }
    message.meta = json.at("_meta");
  }
  message.extensions =
      collect_json_extensions(json, {"role", "content", "_meta"});
  if (const auto valid = validate_sampling_message_content_roles(message);
      !valid) {
    return mcp::core::unexpected(valid.error());
  }
  return message;
}

/// @brief Serializes a model hint.
inline Json model_hint_to_json(const ModelHint& hint) {
  Json json = Json::object();
  if (!hint.name.empty()) {
    json["name"] = hint.name;
  }
  append_json_extensions(json, hint.extensions);
  return json;
}

/// @brief Parses a model hint.
/// @return Parsed hint or validation error.
inline core::Result<ModelHint> model_hint_from_json(const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        sampling_json_error("model hint must be an object"));
  }
  ModelHint hint;
  if (json.contains("name")) {
    if (!json.at("name").is_string()) {
      return mcp::core::unexpected(
          sampling_json_error("model hint name must be a string"));
    }
    hint.name = json.at("name").get<std::string>();
  }
  hint.extensions = collect_json_extensions(json, {"name"});
  return hint;
}

/// @brief Serializes model preferences.
inline Json model_preferences_to_json(const ModelPreferences& preferences) {
  Json json = Json::object();
  if (!preferences.hints.empty()) {
    json["hints"] = Json::array();
    for (const auto& hint : preferences.hints) {
      json["hints"].push_back(model_hint_to_json(hint));
    }
  }
  if (preferences.cost_priority.has_value()) {
    json["costPriority"] = *preferences.cost_priority;
  }
  if (preferences.speed_priority.has_value()) {
    json["speedPriority"] = *preferences.speed_priority;
  }
  if (preferences.intelligence_priority.has_value()) {
    json["intelligencePriority"] = *preferences.intelligence_priority;
  }
  append_json_extensions(json, preferences.extensions);
  return json;
}

/// @brief Parses model preferences.
/// @return Parsed preferences or validation error.
inline core::Result<ModelPreferences> model_preferences_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        sampling_json_error("model preferences must be an object"));
  }
  ModelPreferences preferences;
  if (json.contains("hints")) {
    if (!json.at("hints").is_array()) {
      return mcp::core::unexpected(
          sampling_json_error("model preferences hints must be an array"));
    }
    for (const auto& item : json.at("hints")) {
      const auto hint = model_hint_from_json(item);
      if (!hint) {
        return mcp::core::unexpected(hint.error());
      }
      preferences.hints.push_back(*hint);
    }
  }
  const auto read_priority =
      [&](const char* key,
          std::optional<double>& target) -> core::Result<core::Unit> {
    if (!json.contains(key)) {
      return core::Unit{};
    }
    if (!json.at(key).is_number()) {
      return mcp::core::unexpected(sampling_json_error(
          std::string("model preferences ") + key + " must be a number"));
    }
    const auto value = json.at(key).get<double>();
    if (!sampling_number_is_finite(value)) {
      return mcp::core::unexpected(sampling_json_error(
          std::string("model preferences ") + key + " must be finite"));
    }
    target = value;
    return core::Unit{};
  };
  if (const auto ok = read_priority("costPriority", preferences.cost_priority);
      !ok) {
    return mcp::core::unexpected(ok.error());
  }
  if (const auto ok =
          read_priority("speedPriority", preferences.speed_priority);
      !ok) {
    return mcp::core::unexpected(ok.error());
  }
  if (const auto ok = read_priority("intelligencePriority",
                                    preferences.intelligence_priority);
      !ok) {
    return mcp::core::unexpected(ok.error());
  }
  preferences.extensions = collect_json_extensions(
      json, {"hints", "costPriority", "speedPriority", "intelligencePriority"});
  return preferences;
}

/// @brief Serializes `sampling/createMessage` params.
inline Json create_message_params_to_json(const CreateMessageParams& params) {
  Json json = Json::object();
  json["messages"] = Json::array();
  for (const auto& message : params.messages) {
    json["messages"].push_back(sampling_message_to_json(message));
  }
  if (params.model_preferences.has_value()) {
    json["modelPreferences"] =
        model_preferences_to_json(*params.model_preferences);
  }
  if (params.system_prompt.has_value()) {
    json["systemPrompt"] = *params.system_prompt;
  }
  if (params.include_context.has_value()) {
    json["includeContext"] = *params.include_context;
  }
  if (params.temperature.has_value()) {
    json["temperature"] = *params.temperature;
  }
  json["maxTokens"] = params.max_tokens;
  if (!params.stop_sequences.empty()) {
    json["stopSequences"] = params.stop_sequences;
  }
  if (!params.metadata.empty()) {
    json["metadata"] = params.metadata;
  }
  if (params.task.has_value()) {
    json["task"] = task_request_parameters_to_json(*params.task);
  }
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  if (!params.tools.empty()) {
    json["tools"] = Json::array();
    for (const auto& tool : params.tools) {
      json["tools"].push_back(tool_definition_to_json(tool));
    }
  }
  if (params.tool_choice.has_value()) {
    json["toolChoice"] = tool_choice_to_json(*params.tool_choice);
  }
  append_json_extensions(json, params.extensions);
  return json;
}

/// @brief Parses `sampling/createMessage` params.
/// @return Parsed params or validation error.
inline core::Result<CreateMessageParams> create_message_params_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        sampling_json_error("sampling/createMessage params must be an object"));
  }
  if (!json.contains("messages") || !json.at("messages").is_array()) {
    return mcp::core::unexpected(sampling_json_error(
        "sampling/createMessage params require a messages array"));
  }
  if (!json.contains("maxTokens") ||
      !json.at("maxTokens").is_number_integer()) {
    return mcp::core::unexpected(sampling_json_error(
        "sampling/createMessage params require integer maxTokens"));
  }

  CreateMessageParams params;
  for (const auto& item : json.at("messages")) {
    const auto message = sampling_message_from_json(item);
    if (!message) {
      return mcp::core::unexpected(message.error());
    }
    params.messages.push_back(*message);
  }
  if (json.contains("modelPreferences")) {
    const auto preferences =
        model_preferences_from_json(json.at("modelPreferences"));
    if (!preferences) {
      return mcp::core::unexpected(preferences.error());
    }
    params.model_preferences = *preferences;
  }
  if (json.contains("systemPrompt")) {
    if (!json.at("systemPrompt").is_string()) {
      return mcp::core::unexpected(
          sampling_json_error("sampling systemPrompt must be a string"));
    }
    params.system_prompt = json.at("systemPrompt").get<std::string>();
  }
  if (json.contains("includeContext")) {
    if (!json.at("includeContext").is_string()) {
      return mcp::core::unexpected(
          sampling_json_error("sampling includeContext must be a string"));
    }
    params.include_context = json.at("includeContext").get<std::string>();
    if (!sampling_include_context_is_valid(*params.include_context)) {
      return mcp::core::unexpected(
          sampling_json_error("sampling includeContext is not supported"));
    }
  }
  if (json.contains("temperature")) {
    if (!json.at("temperature").is_number()) {
      return mcp::core::unexpected(
          sampling_json_error("sampling temperature must be a number"));
    }
    const auto temperature = json.at("temperature").get<double>();
    if (!sampling_number_is_finite(temperature)) {
      return mcp::core::unexpected(
          sampling_json_error("sampling temperature must be finite"));
    }
    params.temperature = temperature;
  }
  params.max_tokens = json.at("maxTokens").get<int>();
  if (json.contains("stopSequences")) {
    if (!json.at("stopSequences").is_array()) {
      return mcp::core::unexpected(
          sampling_json_error("sampling stopSequences must be an array"));
    }
    for (const auto& item : json.at("stopSequences")) {
      if (!item.is_string()) {
        return mcp::core::unexpected(
            sampling_json_error("sampling stopSequences must contain strings"));
      }
      params.stop_sequences.push_back(item.get<std::string>());
    }
  }
  if (json.contains("metadata")) {
    if (!json.at("metadata").is_object()) {
      return mcp::core::unexpected(
          sampling_json_error("sampling metadata must be an object"));
    }
    params.metadata = json.at("metadata");
  }
  if (json.contains("task")) {
    const auto task = task_request_parameters_from_json(json.at("task"));
    if (!task) {
      return mcp::core::unexpected(task.error());
    }
    params.task = *task;
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(sampling_json_error(
          "sampling/createMessage _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  if (json.contains("tools")) {
    if (!json.at("tools").is_array()) {
      return mcp::core::unexpected(
          sampling_json_error("sampling tools must be an array"));
    }
    for (const auto& item : json.at("tools")) {
      const auto tool = tool_definition_from_json(item);
      if (!tool) {
        return mcp::core::unexpected(tool.error());
      }
      params.tools.push_back(*tool);
    }
  }
  if (json.contains("toolChoice")) {
    const auto tool_choice = tool_choice_from_json(json.at("toolChoice"));
    if (!tool_choice) {
      return mcp::core::unexpected(tool_choice.error());
    }
    params.tool_choice = *tool_choice;
  }
  params.extensions = collect_json_extensions(
      json, {"messages", "modelPreferences", "systemPrompt", "includeContext",
             "temperature", "maxTokens", "stopSequences", "metadata", "task",
             "_meta", "tools", "toolChoice"});
  if (const auto valid =
          validate_sampling_tool_use_result_balance(params.messages);
      !valid) {
    return mcp::core::unexpected(valid.error());
  }
  return params;
}

/// @brief Serializes a `sampling/createMessage` result.
inline Json create_message_result_to_json(const CreateMessageResult& result) {
  Json json = Json::object();
  json["role"] = result.role;
  if (!result.contents.empty()) {
    if (result.contents.size() == 1) {
      json["content"] =
          sampling_message_content_to_json(result.contents.front());
    } else {
      json["content"] = Json::array();
      for (const auto& content : result.contents) {
        json["content"].push_back(sampling_message_content_to_json(content));
      }
    }
  } else {
    json["content"] = content_block_to_json(result.content);
  }
  json["model"] = result.model;
  if (!result.stop_reason.empty()) {
    json["stopReason"] = result.stop_reason;
  }
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a `sampling/createMessage` result.
/// @return Parsed result or validation error.
inline core::Result<CreateMessageResult> create_message_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        sampling_json_error("sampling/createMessage result must be an object"));
  }
  if (!json.contains("role") || !json.at("role").is_string()) {
    return mcp::core::unexpected(sampling_json_error(
        "sampling/createMessage result requires a string role"));
  }
  if (!json.contains("content")) {
    return mcp::core::unexpected(
        sampling_json_error("sampling/createMessage result requires content"));
  }
  if (!json.contains("model") || !json.at("model").is_string()) {
    return mcp::core::unexpected(sampling_json_error(
        "sampling/createMessage result requires a string model"));
  }
  CreateMessageResult result;
  result.role = json.at("role").get<std::string>();
  if (result.role != "assistant") {
    return mcp::core::unexpected(sampling_json_error(
        "sampling/createMessage result role must be assistant"));
  }
  if (json.at("content").is_array()) {
    for (const auto& item : json.at("content")) {
      const auto content = sampling_message_content_from_json(item);
      if (!content) {
        return mcp::core::unexpected(content.error());
      }
      result.contents.push_back(*content);
    }
  } else {
    const auto content = sampling_message_content_from_json(json.at("content"));
    if (!content) {
      return mcp::core::unexpected(content.error());
    }
    result.contents.push_back(*content);
  }
  if (!result.contents.empty()) {
    result.content = result.contents.front().content;
  }
  result.model = json.at("model").get<std::string>();
  if (json.contains("stopReason")) {
    if (!json.at("stopReason").is_string()) {
      return mcp::core::unexpected(
          sampling_json_error("sampling stopReason must be a string"));
    }
    result.stop_reason = json.at("stopReason").get<std::string>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(sampling_json_error(
          "sampling/createMessage result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions = collect_json_extensions(
      json, {"role", "content", "model", "stopReason", "_meta"});
  SamplingMessage result_message;
  result_message.role = result.role;
  result_message.contents = result.contents;
  if (const auto valid =
          validate_sampling_message_content_roles(result_message);
      !valid) {
    return mcp::core::unexpected(valid.error());
  }
  return result;
}

}  // namespace mcp::protocol
