// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/prompt.hpp
/// @brief Prompt discovery and rendering payloads.
///
/// Prompts are server-defined templates retrieved through `prompts/get`.
/// Listing returns prompt metadata and argument definitions; getting a prompt
/// returns concrete messages that can be supplied to a model.

#include <optional>
#include <string>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Argument accepted by a prompt template.
struct PromptArgument {
  /// Optional human-readable display title.
  std::string title;
  /// Stable argument name used as a key in `prompts/get` arguments.
  std::string name;
  /// Optional human-readable description.
  std::string description;
  /// Whether the caller must provide this argument.
  bool required = false;
  /// Optional annotations for model or client presentation.
  Json annotations = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
};

/// @brief Prompt descriptor returned by `prompts/list`.
struct Prompt {
  /// Optional human-readable display title.
  std::string title;
  /// Stable prompt name used by `prompts/get`.
  std::string name;
  /// Optional human-readable description.
  std::string description;
  /// Prompt arguments accepted by this prompt.
  std::vector<PromptArgument> arguments;
  /// Optional icon descriptors for client presentation.
  std::vector<Icon> icons;
  /// Optional annotations for model or client presentation.
  Json annotations = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
};

/// @brief Result object for `prompts/list`.
struct PromptsListResult {
  /// Prompts available to the caller.
  std::vector<Prompt> prompts;
  /// Optional cursor for retrieving the next page.
  std::optional<std::string> next_cursor;
};

/// @brief Parameters for `prompts/get`.
struct PromptsGetParams {
  /// Prompt name matching a Prompt descriptor.
  std::string name;
  /// JSON object keyed by prompt argument name.
  Json arguments = Json::object();
};

/// @brief One rendered prompt message.
struct PromptMessage {
  /// Message role understood by the receiving model or peer.
  std::string role;
  /// Message content block.
  ContentBlock content;
};

/// @brief Result object for `prompts/get`.
struct PromptsGetResult {
  /// Optional rendered prompt description.
  std::string description;
  /// Ordered messages produced by the prompt.
  std::vector<PromptMessage> messages;
};

/// @brief Builds an InvalidRequest error for prompt JSON validation failures.
inline core::Error prompt_json_error(std::string message) {
  return core::Error{
      static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

/// @brief Serializes a prompt argument descriptor.
inline Json prompt_argument_to_json(const PromptArgument& argument) {
  Json json = Json::object();
  if (!argument.title.empty()) {
    json["title"] = argument.title;
  }
  json["name"] = argument.name;
  if (!argument.description.empty()) {
    json["description"] = argument.description;
  }
  if (argument.required) {
    json["required"] = true;
  }
  if (!argument.annotations.empty()) {
    json["annotations"] = argument.annotations;
  }
  if (argument.meta.has_value()) {
    json["_meta"] = *argument.meta;
  }
  return json;
}

/// @brief Parses a prompt argument descriptor.
/// @return Parsed argument or validation error.
inline core::Result<PromptArgument> prompt_argument_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        prompt_json_error("prompt argument must be an object"));
  }
  if (json.contains("title")) {
    if (!json.at("title").is_string()) {
      return std::unexpected(
          prompt_json_error("prompt argument title must be a string"));
    }
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return std::unexpected(
        prompt_json_error("prompt argument requires a string name"));
  }

  PromptArgument argument;
  if (json.contains("title")) {
    argument.title = json.at("title").get<std::string>();
  }
  argument.name = json.at("name").get<std::string>();
  if (json.contains("description")) {
    if (!json.at("description").is_string()) {
      return std::unexpected(
          prompt_json_error("prompt argument description must be a string"));
    }
    argument.description = json.at("description").get<std::string>();
  }
  if (json.contains("required")) {
    if (!json.at("required").is_boolean()) {
      return std::unexpected(
          prompt_json_error("prompt argument required must be a boolean"));
    }
    argument.required = json.at("required").get<bool>();
  }
  if (json.contains("annotations")) {
    argument.annotations = json.at("annotations");
  }
  if (json.contains("_meta")) {
    argument.meta = json.at("_meta");
  }
  return argument;
}

/// @brief Serializes a prompt descriptor.
inline Json prompt_to_json(const Prompt& prompt) {
  Json json = Json::object();
  if (!prompt.title.empty()) {
    json["title"] = prompt.title;
  }
  json["name"] = prompt.name;
  if (!prompt.description.empty()) {
    json["description"] = prompt.description;
  }
  if (!prompt.arguments.empty()) {
    json["arguments"] = Json::array();
    for (const auto& argument : prompt.arguments) {
      json["arguments"].push_back(prompt_argument_to_json(argument));
    }
  }
  if (!prompt.icons.empty()) {
    json["icons"] = Json::array();
    for (const auto& icon : prompt.icons) {
      json["icons"].push_back(icon_to_json(icon));
    }
  }
  if (!prompt.annotations.empty()) {
    json["annotations"] = prompt.annotations;
  }
  if (prompt.meta.has_value()) {
    json["_meta"] = *prompt.meta;
  }
  return json;
}

/// @brief Parses a prompt descriptor.
/// @return Parsed prompt or validation error.
inline core::Result<Prompt> prompt_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(prompt_json_error("prompt must be an object"));
  }
  if (json.contains("title")) {
    if (!json.at("title").is_string()) {
      return std::unexpected(
          prompt_json_error("prompt title must be a string"));
    }
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return std::unexpected(prompt_json_error("prompt requires a string name"));
  }

  Prompt prompt;
  if (json.contains("title")) {
    prompt.title = json.at("title").get<std::string>();
  }
  prompt.name = json.at("name").get<std::string>();
  if (json.contains("description")) {
    if (!json.at("description").is_string()) {
      return std::unexpected(
          prompt_json_error("prompt description must be a string"));
    }
    prompt.description = json.at("description").get<std::string>();
  }
  if (json.contains("arguments")) {
    if (!json.at("arguments").is_array()) {
      return std::unexpected(
          prompt_json_error("prompt arguments must be an array"));
    }
    for (const auto& item : json.at("arguments")) {
      const auto argument = prompt_argument_from_json(item);
      if (!argument) {
        return std::unexpected(argument.error());
      }
      prompt.arguments.push_back(*argument);
    }
  }
  if (json.contains("icons")) {
    if (!json.at("icons").is_array()) {
      return std::unexpected(
          prompt_json_error("prompt icons must be an array"));
    }
    for (const auto& item : json.at("icons")) {
      const auto icon = icon_from_json(item);
      if (!icon.has_value()) {
        return std::unexpected(prompt_json_error("prompt icon is invalid"));
      }
      prompt.icons.push_back(*icon);
    }
  }
  if (json.contains("annotations")) {
    prompt.annotations = json.at("annotations");
  }
  if (json.contains("_meta")) {
    prompt.meta = json.at("_meta");
  }
  return prompt;
}

/// @brief Serializes a `prompts/list` result.
inline Json prompts_list_result_to_json(const PromptsListResult& result) {
  Json json = Json::object();
  json["prompts"] = Json::array();
  for (const auto& prompt : result.prompts) {
    json["prompts"].push_back(prompt_to_json(prompt));
  }
  if (result.next_cursor.has_value()) {
    json["nextCursor"] = *result.next_cursor;
  }
  return json;
}

/// @brief Parses a `prompts/list` result.
/// @return Parsed result or validation error.
inline core::Result<PromptsListResult> prompts_list_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        prompt_json_error("prompts/list result must be an object"));
  }
  if (!json.contains("prompts") || !json.at("prompts").is_array()) {
    return std::unexpected(
        prompt_json_error("prompts/list result requires a prompts array"));
  }

  PromptsListResult result;
  for (const auto& item : json.at("prompts")) {
    const auto prompt = prompt_from_json(item);
    if (!prompt) {
      return std::unexpected(prompt.error());
    }
    result.prompts.push_back(*prompt);
  }
  if (json.contains("nextCursor")) {
    if (!json.at("nextCursor").is_string()) {
      return std::unexpected(
          prompt_json_error("prompts/list nextCursor must be a string"));
    }
    result.next_cursor = json.at("nextCursor").get<std::string>();
  }
  return result;
}

/// @brief Serializes `prompts/get` params.
inline Json prompts_get_params_to_json(const PromptsGetParams& params) {
  Json json = Json::object();
  json["name"] = params.name;
  if (!params.arguments.empty()) {
    json["arguments"] = params.arguments;
  }
  return json;
}

/// @brief Parses `prompts/get` params.
/// @return Parsed params or validation error.
inline core::Result<PromptsGetParams> prompts_get_params_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        prompt_json_error("prompts/get params must be an object"));
  }
  if (!json.contains("name") || !json.at("name").is_string()) {
    return std::unexpected(
        prompt_json_error("prompts/get params require a string name"));
  }

  PromptsGetParams params;
  params.name = json.at("name").get<std::string>();
  if (json.contains("arguments")) {
    if (!json.at("arguments").is_object()) {
      return std::unexpected(
          prompt_json_error("prompts/get arguments must be an object"));
    }
    params.arguments = json.at("arguments");
  }
  return params;
}

/// @brief Serializes one rendered prompt message.
inline Json prompt_message_to_json(const PromptMessage& message) {
  Json json = Json::object();
  json["role"] = message.role;
  json["content"] = content_block_to_json(message.content);
  return json;
}

/// @brief Parses one rendered prompt message.
/// @return Parsed message or validation error.
inline core::Result<PromptMessage> prompt_message_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        prompt_json_error("prompt message must be an object"));
  }
  if (!json.contains("role") || !json.at("role").is_string()) {
    return std::unexpected(
        prompt_json_error("prompt message requires a string role"));
  }
  if (!json.contains("content")) {
    return std::unexpected(
        prompt_json_error("prompt message requires content"));
  }

  const auto content = content_block_from_json(json.at("content"));
  if (!content) {
    return std::unexpected(content.error());
  }

  PromptMessage message;
  message.role = json.at("role").get<std::string>();
  message.content = *content;
  return message;
}

/// @brief Serializes a `prompts/get` result.
inline Json prompts_get_result_to_json(const PromptsGetResult& result) {
  Json json = Json::object();
  if (!result.description.empty()) {
    json["description"] = result.description;
  }
  json["messages"] = Json::array();
  for (const auto& message : result.messages) {
    json["messages"].push_back(prompt_message_to_json(message));
  }
  return json;
}

/// @brief Parses a `prompts/get` result.
/// @return Parsed result or validation error.
inline core::Result<PromptsGetResult> prompts_get_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        prompt_json_error("prompts/get result must be an object"));
  }
  if (!json.contains("messages") || !json.at("messages").is_array()) {
    return std::unexpected(
        prompt_json_error("prompts/get result requires a messages array"));
  }

  PromptsGetResult result;
  if (json.contains("description")) {
    if (!json.at("description").is_string()) {
      return std::unexpected(
          prompt_json_error("prompts/get description must be a string"));
    }
    result.description = json.at("description").get<std::string>();
  }
  for (const auto& item : json.at("messages")) {
    const auto message = prompt_message_from_json(item);
    if (!message) {
      return std::unexpected(message.error());
    }
    result.messages.push_back(*message);
  }
  return result;
}

}  // namespace mcp::protocol
