// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/capabilities.hpp
/// @brief MCP client and server capability declarations.
///
/// Capabilities are exchanged during the `initialize` lifecycle request and
/// response. They gate which feature methods and notifications a peer may use
/// after initialization, and they preserve unknown extension data for forward
/// compatibility.

#include <optional>
#include <string>
#include <utility>

#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Server capability flags for tool discovery and invocation.
struct ToolCapabilities {
  /// Whether the server supports tool discovery or invocation.
  bool enabled = false;
  /// Whether `notifications/tools/list_changed` may be emitted.
  bool list_changed = false;
  /// Whether `listChanged` was explicitly present on the wire.
  bool list_changed_present = false;
  /// Raw capability object preserved for future fields.
  Json raw = Json::object();
};

/// @brief Server capability flags for resources.
struct ResourceCapabilities {
  /// Whether the server supports resource listing or reading.
  bool enabled = false;
  /// Whether `notifications/resources/list_changed` may be emitted.
  bool list_changed = false;
  /// Whether `listChanged` was explicitly present on the wire.
  bool list_changed_present = false;
  /// Whether the server supports resource subscribe/unsubscribe methods.
  bool subscribe = false;
  /// Whether `subscribe` was explicitly present on the wire.
  bool subscribe_present = false;
  /// Raw capability object preserved for future fields.
  Json raw = Json::object();
};

/// @brief Server capability flags for prompts.
struct PromptCapabilities {
  /// Whether the server supports prompt listing or retrieval.
  bool enabled = false;
  /// Whether `notifications/prompts/list_changed` may be emitted.
  bool list_changed = false;
  /// Whether `listChanged` was explicitly present on the wire.
  bool list_changed_present = false;
  /// Raw capability object preserved for future fields.
  Json raw = Json::object();
};

/// @brief Server capability flags for logging.
struct LoggingCapabilities {
  /// Whether `logging/setLevel` and logging message notifications are
  /// supported.
  bool enabled = false;
  /// Raw capability object preserved for future fields.
  Json raw = Json::object();
};

/// @brief Client capability flags for sampling requests from the server.
struct SamplingCapabilities {
  /// Whether the client accepts `sampling/createMessage` requests.
  bool enabled = false;
  /// Whether sampling requests may use tool-related context.
  bool tools = false;
  /// Whether sampling requests may use broader context.
  bool context = false;
  /// Raw capability object preserved for future fields.
  Json raw = Json::object();
};

/// @brief Server capability flags for completion requests.
struct CompletionCapabilities {
  /// Whether `completion/complete` is supported.
  bool enabled = false;
  /// Raw capability object preserved for future fields.
  Json raw = Json::object();
};

/// @brief Client capability flags for roots.
struct RootCapabilities {
  /// Whether the client supports `roots/list`.
  bool enabled = false;
  /// Whether `notifications/roots/list_changed` may be emitted.
  bool list_changed = false;
  /// Whether `listChanged` was explicitly present on the wire.
  bool list_changed_present = false;
  /// Raw capability object preserved for future fields.
  Json raw = Json::object();
};

/// @brief Client capability flags for elicitation.
struct ElicitationCapabilities {
  /// Whether the `elicitation` capability family was explicitly advertised.
  bool present = false;
  /// Whether form-based `elicitation/create` requests are supported.
  bool form = false;
  /// Whether form elicitation can validate schemas locally.
  std::optional<bool> form_schema_validation;
  /// Whether URL-based elicitation requests are supported.
  bool url = false;
  /// Raw capability object preserved for future fields.
  Json raw = Json::object();

  /// @brief Returns true when any elicitation mode is supported.
  /// @return True if either `form` or `url` is enabled.
  bool enabled() const noexcept { return form || url; }
};

/// @brief Capability flags for asynchronous task support.
///
/// Task capabilities describe both the task management methods a peer supports
/// and which feature requests can opt into task creation.
struct TaskCapabilities {
  /// Whether `tasks/list` is supported.
  bool list = false;
  /// Whether `tasks/cancel` is supported.
  bool cancel = false;
  /// Whether `tools/call` requests may include task parameters.
  bool tools_call = false;
  /// Whether `sampling/createMessage` requests may include task parameters.
  bool sampling_create_message = false;
  /// Whether `elicitation/create` requests may include task parameters.
  bool elicitation_create = false;
  /// Raw capability object preserved for future fields.
  Json raw = Json::object();
};

inline Json capability_raw_object(const Json& raw) {
  return raw.is_object() ? raw : Json::object();
}

inline Json capability_member_object(const Json& object,
                                     const std::string& key) {
  if (object.is_object() && object.contains(key) &&
      object.at(key).is_object()) {
    return object.at(key);
  }
  return Json::object();
}

/// @brief Serializes task capability flags using object presence semantics.
/// @param capabilities Task capability flags.
/// @return JSON object suitable for the `tasks` capability member.
inline Json task_capabilities_to_json(const TaskCapabilities& capabilities) {
  Json tasks = capability_raw_object(capabilities.raw);
  if (capabilities.list) {
    tasks["list"] = capability_member_object(tasks, "list");
  }
  if (capabilities.cancel) {
    tasks["cancel"] = capability_member_object(tasks, "cancel");
  }

  Json requests = capability_member_object(tasks, "requests");
  if (capabilities.tools_call) {
    Json tools = capability_member_object(requests, "tools");
    tools["call"] = capability_member_object(tools, "call");
    requests["tools"] = std::move(tools);
  }
  if (capabilities.sampling_create_message) {
    Json sampling = capability_member_object(requests, "sampling");
    sampling["createMessage"] =
        capability_member_object(sampling, "createMessage");
    requests["sampling"] = std::move(sampling);
  }
  if (capabilities.elicitation_create) {
    Json elicitation = capability_member_object(requests, "elicitation");
    elicitation["create"] = capability_member_object(elicitation, "create");
    requests["elicitation"] = std::move(elicitation);
  }
  if (!requests.empty()) {
    tasks["requests"] = std::move(requests);
  }
  return tasks;
}

/// @brief Interprets either modern object presence or legacy boolean presence.
/// @param json Capability member value.
/// @return True when the capability is advertised.
inline bool capability_member_enabled(const Json& json) {
  return json.is_object() || (json.is_boolean() && json.get<bool>());
}

/// @brief Parses task capabilities from an MCP capability object.
/// @param tasks JSON object from a `tasks` capability member.
/// @return Parsed task capability flags.
inline TaskCapabilities task_capabilities_from_json(const Json& tasks) {
  TaskCapabilities task_capabilities;
  if (!tasks.is_object()) {
    return task_capabilities;
  }
  task_capabilities.raw = tasks;

  if (tasks.contains("list")) {
    task_capabilities.list = capability_member_enabled(tasks.at("list"));
  }
  if (tasks.contains("cancel")) {
    task_capabilities.cancel = capability_member_enabled(tasks.at("cancel"));
  }
  if (tasks.contains("requests") && tasks.at("requests").is_object()) {
    const auto& requests = tasks.at("requests");
    if (requests.contains("tools") && requests.at("tools").is_object()) {
      const auto& tools = requests.at("tools");
      if (tools.contains("call")) {
        task_capabilities.tools_call =
            capability_member_enabled(tools.at("call"));
      }
    }
    if (requests.contains("sampling") && requests.at("sampling").is_object()) {
      const auto& sampling = requests.at("sampling");
      if (sampling.contains("createMessage")) {
        task_capabilities.sampling_create_message =
            capability_member_enabled(sampling.at("createMessage"));
      }
    }
    if (requests.contains("elicitation") &&
        requests.at("elicitation").is_object()) {
      const auto& elicitation = requests.at("elicitation");
      if (elicitation.contains("create")) {
        task_capabilities.elicitation_create =
            capability_member_enabled(elicitation.at("create"));
      }
    }
  }
  return task_capabilities;
}

inline bool task_capability_member_valid(const Json& json) {
  return json.is_object() || json.is_boolean();
}

inline bool task_capabilities_are_valid(const Json& tasks) {
  if (!tasks.is_object()) {
    return false;
  }
  if (tasks.contains("list") &&
      !task_capability_member_valid(tasks.at("list"))) {
    return false;
  }
  if (tasks.contains("cancel") &&
      !task_capability_member_valid(tasks.at("cancel"))) {
    return false;
  }
  if (!tasks.contains("requests")) {
    return true;
  }
  if (!tasks.at("requests").is_object()) {
    return false;
  }
  const auto& requests = tasks.at("requests");
  if (requests.contains("tools")) {
    if (!requests.at("tools").is_object()) {
      return false;
    }
    const auto& tools = requests.at("tools");
    if (tools.contains("call") &&
        !task_capability_member_valid(tools.at("call"))) {
      return false;
    }
  }
  if (requests.contains("sampling")) {
    if (!requests.at("sampling").is_object()) {
      return false;
    }
    const auto& sampling = requests.at("sampling");
    if (sampling.contains("createMessage") &&
        !task_capability_member_valid(sampling.at("createMessage"))) {
      return false;
    }
  }
  if (requests.contains("elicitation")) {
    if (!requests.at("elicitation").is_object()) {
      return false;
    }
    const auto& elicitation = requests.at("elicitation");
    if (elicitation.contains("create") &&
        !task_capability_member_valid(elicitation.at("create"))) {
      return false;
    }
  }
  return true;
}

/// @brief Capabilities advertised by an MCP client during initialization.
struct ClientCapabilities {
  /// Roots feature support.
  RootCapabilities roots;
  /// Sampling feature support.
  SamplingCapabilities sampling;
  /// Elicitation feature support.
  ElicitationCapabilities elicitation;
  /// Optional task support. Omitted when the client does not advertise tasks.
  std::optional<TaskCapabilities> tasks;
  /// Experimental capability bag preserved as raw JSON.
  std::optional<Json> experimental;
  /// Vendor or SDK extension capability bag.
  Json extensions = Json::object();
};

/// @brief Fluent builder for client initialize capabilities.
class ClientCapabilitiesBuilder {
 public:
  ClientCapabilitiesBuilder& roots(bool list_changed = false) {
    capabilities_.roots.enabled = true;
    capabilities_.roots.list_changed = list_changed;
    capabilities_.roots.list_changed_present = list_changed;
    return *this;
  }

  ClientCapabilitiesBuilder& sampling(bool tools = false,
                                      bool context = false) {
    capabilities_.sampling.enabled = true;
    capabilities_.sampling.tools = tools;
    capabilities_.sampling.context = context;
    return *this;
  }

  ClientCapabilitiesBuilder& elicitation_form(
      std::optional<bool> schema_validation = std::nullopt) {
    capabilities_.elicitation.present = true;
    capabilities_.elicitation.form = true;
    capabilities_.elicitation.form_schema_validation = schema_validation;
    return *this;
  }

  ClientCapabilitiesBuilder& elicitation_url() {
    capabilities_.elicitation.present = true;
    capabilities_.elicitation.url = true;
    return *this;
  }

  ClientCapabilitiesBuilder& tasks(TaskCapabilities value) {
    capabilities_.tasks = std::move(value);
    return *this;
  }

  ClientCapabilitiesBuilder& task_list(bool value = true) {
    ensure_tasks().list = value;
    return *this;
  }

  ClientCapabilitiesBuilder& task_cancel(bool value = true) {
    ensure_tasks().cancel = value;
    return *this;
  }

  ClientCapabilitiesBuilder& task_tool_calls(bool value = true) {
    ensure_tasks().tools_call = value;
    return *this;
  }

  ClientCapabilitiesBuilder& task_sampling(bool value = true) {
    ensure_tasks().sampling_create_message = value;
    return *this;
  }

  ClientCapabilitiesBuilder& task_elicitation(bool value = true) {
    ensure_tasks().elicitation_create = value;
    return *this;
  }

  ClientCapabilitiesBuilder& experimental(Json value) {
    capabilities_.experimental = std::move(value);
    return *this;
  }

  ClientCapabilitiesBuilder& extension(std::string name, Json value) {
    capabilities_.extensions[std::move(name)] = std::move(value);
    return *this;
  }

  ClientCapabilities build() const { return capabilities_; }

 private:
  TaskCapabilities& ensure_tasks() {
    if (!capabilities_.tasks.has_value()) {
      capabilities_.tasks = TaskCapabilities{};
    }
    return *capabilities_.tasks;
  }

  ClientCapabilities capabilities_;
};

/// @brief Starts a fluent client capability builder.
inline ClientCapabilitiesBuilder client_capabilities() {
  return ClientCapabilitiesBuilder{};
}

/// @brief Serializes client capabilities to the MCP initialize payload shape.
/// @param capabilities Client capability flags and extension data.
/// @return JSON object suitable for `initialize.params.capabilities`.
inline Json client_capabilities_to_json(
    const ClientCapabilities& capabilities) {
  Json json = Json::object();

  Json roots = capability_raw_object(capabilities.roots.raw);
  if (capabilities.roots.list_changed ||
      capabilities.roots.list_changed_present) {
    roots["listChanged"] = capabilities.roots.list_changed;
  }
  if (capabilities.roots.enabled || !roots.empty()) {
    json["roots"] = std::move(roots);
  }

  if (capabilities.sampling.enabled) {
    Json sampling = capability_raw_object(capabilities.sampling.raw);
    if (capabilities.sampling.tools) {
      sampling["tools"] = capability_member_object(sampling, "tools");
    }
    if (capabilities.sampling.context) {
      sampling["context"] = capability_member_object(sampling, "context");
    }
    json["sampling"] = std::move(sampling);
  }

  Json elicitation = capability_raw_object(capabilities.elicitation.raw);
  if (capabilities.elicitation.form) {
    Json form = capability_member_object(elicitation, "form");
    if (capabilities.elicitation.form_schema_validation.has_value()) {
      form["schemaValidation"] =
          *capabilities.elicitation.form_schema_validation;
    }
    elicitation["form"] = std::move(form);
  }
  if (capabilities.elicitation.url) {
    elicitation["url"] = capability_member_object(elicitation, "url");
  }
  if (capabilities.elicitation.present || !elicitation.empty()) {
    json["elicitation"] = std::move(elicitation);
  }
  if (capabilities.experimental.has_value() &&
      capabilities.experimental->is_object()) {
    json["experimental"] = *capabilities.experimental;
  }
  if (capabilities.extensions.is_object() && !capabilities.extensions.empty()) {
    json["extensions"] = capabilities.extensions;
  }
  if (capabilities.tasks.has_value()) {
    Json tasks = task_capabilities_to_json(*capabilities.tasks);
    json["tasks"] = std::move(tasks);
  }
  return json;
}

/// @brief Capabilities advertised by an MCP server in the initialize result.
struct ServerCapabilities {
  /// Tool feature support.
  ToolCapabilities tools;
  /// Resource feature support.
  ResourceCapabilities resources;
  /// Prompt feature support.
  PromptCapabilities prompts;
  /// Logging feature support.
  LoggingCapabilities logging;
  /// Completion feature support.
  CompletionCapabilities completions;
  /// Optional task support. Omitted when the server does not advertise tasks.
  std::optional<TaskCapabilities> tasks;
  /// Experimental capability bag preserved as raw JSON.
  std::optional<Json> experimental;
  /// Vendor or SDK extension capability bag.
  Json extensions = Json::object();
};

/// @brief Fluent builder for server initialize result capabilities.
class ServerCapabilitiesBuilder {
 public:
  ServerCapabilitiesBuilder& tools(bool list_changed = false) {
    capabilities_.tools.enabled = true;
    capabilities_.tools.list_changed = list_changed;
    capabilities_.tools.list_changed_present = list_changed;
    return *this;
  }

  ServerCapabilitiesBuilder& resources(bool list_changed = false,
                                       bool subscribe = false) {
    capabilities_.resources.enabled = true;
    capabilities_.resources.list_changed = list_changed;
    capabilities_.resources.list_changed_present = list_changed;
    capabilities_.resources.subscribe = subscribe;
    capabilities_.resources.subscribe_present = subscribe;
    return *this;
  }

  ServerCapabilitiesBuilder& prompts(bool list_changed = false) {
    capabilities_.prompts.enabled = true;
    capabilities_.prompts.list_changed = list_changed;
    capabilities_.prompts.list_changed_present = list_changed;
    return *this;
  }

  ServerCapabilitiesBuilder& logging() {
    capabilities_.logging.enabled = true;
    return *this;
  }

  ServerCapabilitiesBuilder& completions() {
    capabilities_.completions.enabled = true;
    return *this;
  }

  ServerCapabilitiesBuilder& tasks(TaskCapabilities value) {
    capabilities_.tasks = std::move(value);
    return *this;
  }

  ServerCapabilitiesBuilder& task_list(bool value = true) {
    ensure_tasks().list = value;
    return *this;
  }

  ServerCapabilitiesBuilder& task_cancel(bool value = true) {
    ensure_tasks().cancel = value;
    return *this;
  }

  ServerCapabilitiesBuilder& task_tool_calls(bool value = true) {
    ensure_tasks().tools_call = value;
    return *this;
  }

  ServerCapabilitiesBuilder& task_sampling(bool value = true) {
    ensure_tasks().sampling_create_message = value;
    return *this;
  }

  ServerCapabilitiesBuilder& task_elicitation(bool value = true) {
    ensure_tasks().elicitation_create = value;
    return *this;
  }

  ServerCapabilitiesBuilder& experimental(Json value) {
    capabilities_.experimental = std::move(value);
    return *this;
  }

  ServerCapabilitiesBuilder& extension(std::string name, Json value) {
    capabilities_.extensions[std::move(name)] = std::move(value);
    return *this;
  }

  ServerCapabilities build() const { return capabilities_; }

 private:
  TaskCapabilities& ensure_tasks() {
    if (!capabilities_.tasks.has_value()) {
      capabilities_.tasks = TaskCapabilities{};
    }
    return *capabilities_.tasks;
  }

  ServerCapabilities capabilities_;
};

/// @brief Starts a fluent server capability builder.
inline ServerCapabilitiesBuilder server_capabilities() {
  return ServerCapabilitiesBuilder{};
}

/// @brief Serializes server capabilities to the MCP initialize result shape.
/// @param capabilities Server capability flags and extension data.
/// @return JSON object suitable for `initialize.result.capabilities`.
inline Json server_capabilities_to_json(
    const ServerCapabilities& capabilities) {
  Json json = Json::object();

  Json tools = capability_raw_object(capabilities.tools.raw);
  if (capabilities.tools.list_changed ||
      capabilities.tools.list_changed_present) {
    tools["listChanged"] = capabilities.tools.list_changed;
  }
  if (capabilities.tools.enabled || !tools.empty()) {
    json["tools"] = std::move(tools);
  }

  Json resources = capability_raw_object(capabilities.resources.raw);
  if (capabilities.resources.list_changed ||
      capabilities.resources.list_changed_present) {
    resources["listChanged"] = capabilities.resources.list_changed;
  }
  if (capabilities.resources.subscribe ||
      capabilities.resources.subscribe_present) {
    resources["subscribe"] = capabilities.resources.subscribe;
  }
  if (capabilities.resources.enabled || !resources.empty()) {
    json["resources"] = std::move(resources);
  }

  Json prompts = capability_raw_object(capabilities.prompts.raw);
  if (capabilities.prompts.list_changed ||
      capabilities.prompts.list_changed_present) {
    prompts["listChanged"] = capabilities.prompts.list_changed;
  }
  if (capabilities.prompts.enabled || !prompts.empty()) {
    json["prompts"] = std::move(prompts);
  }

  if (capabilities.logging.enabled || (capabilities.logging.raw.is_object() &&
                                       !capabilities.logging.raw.empty())) {
    json["logging"] = capability_raw_object(capabilities.logging.raw);
  }
  if (capabilities.completions.enabled ||
      (capabilities.completions.raw.is_object() &&
       !capabilities.completions.raw.empty())) {
    json["completions"] = capability_raw_object(capabilities.completions.raw);
  }
  if (capabilities.tasks.has_value()) {
    Json tasks = task_capabilities_to_json(*capabilities.tasks);
    json["tasks"] = std::move(tasks);
  }
  if (capabilities.experimental.has_value() &&
      capabilities.experimental->is_object()) {
    json["experimental"] = *capabilities.experimental;
  }
  if (capabilities.extensions.is_object() && !capabilities.extensions.empty()) {
    json["extensions"] = capabilities.extensions;
  }
  return json;
}

/// @brief Parses client capabilities from an initialize request.
/// @param json JSON object from `initialize.params.capabilities`.
/// @return Parsed capabilities, or nullopt if the top-level value is invalid.
inline std::optional<ClientCapabilities> client_capabilities_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }

  ClientCapabilities capabilities;
  if (json.contains("roots")) {
    if (!json.at("roots").is_object()) {
      return std::nullopt;
    }
    capabilities.roots.enabled = true;
    const auto& roots = json.at("roots");
    capabilities.roots.raw = roots;
    if (roots.contains("listChanged")) {
      if (!roots.at("listChanged").is_boolean()) {
        return std::nullopt;
      }
      capabilities.roots.list_changed = roots.at("listChanged").get<bool>();
      capabilities.roots.list_changed_present = true;
    }
  }
  if (json.contains("sampling")) {
    if (!json.at("sampling").is_object()) {
      return std::nullopt;
    }
    capabilities.sampling.enabled = true;
    const auto& sampling = json.at("sampling");
    capabilities.sampling.raw = sampling;
    if (sampling.contains("tools")) {
      if (!sampling.at("tools").is_object()) {
        return std::nullopt;
      }
      capabilities.sampling.tools = true;
    }
    if (sampling.contains("context")) {
      if (!sampling.at("context").is_object()) {
        return std::nullopt;
      }
      capabilities.sampling.context = true;
    }
  }
  if (json.contains("elicitation")) {
    const auto& elicitation = json.at("elicitation");
    if (!elicitation.is_object()) {
      return std::nullopt;
    }
    capabilities.elicitation.present = true;
    capabilities.elicitation.raw = elicitation;
    if (elicitation.contains("form")) {
      if (!elicitation.at("form").is_object()) {
        return std::nullopt;
      }
      capabilities.elicitation.form = true;
      const auto& form = elicitation.at("form");
      if (form.contains("schemaValidation")) {
        if (!form.at("schemaValidation").is_boolean()) {
          return std::nullopt;
        }
        capabilities.elicitation.form_schema_validation =
            form.at("schemaValidation").get<bool>();
      }
    }
    if (elicitation.contains("url")) {
      if (!elicitation.at("url").is_object()) {
        return std::nullopt;
      }
      capabilities.elicitation.url = true;
    }
  }
  if (json.contains("tasks")) {
    if (!task_capabilities_are_valid(json.at("tasks"))) {
      return std::nullopt;
    }
    capabilities.tasks = task_capabilities_from_json(json.at("tasks"));
  }
  if (json.contains("experimental")) {
    if (!json.at("experimental").is_object()) {
      return std::nullopt;
    }
    capabilities.experimental = json.at("experimental");
  }
  if (json.contains("extensions")) {
    if (!json.at("extensions").is_object()) {
      return std::nullopt;
    }
    capabilities.extensions = json.at("extensions");
  }
  return capabilities;
}

/// @brief Parses server capabilities from an initialize result.
/// @param json JSON object from `initialize.result.capabilities`.
/// @return Parsed capabilities, or nullopt if the top-level value is invalid.
inline std::optional<ServerCapabilities> server_capabilities_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }

  ServerCapabilities capabilities;
  if (json.contains("tools")) {
    if (!json.at("tools").is_object()) {
      return std::nullopt;
    }
    capabilities.tools.enabled = true;
    const auto& tools = json.at("tools");
    capabilities.tools.raw = tools;
    if (tools.contains("listChanged")) {
      if (!tools.at("listChanged").is_boolean()) {
        return std::nullopt;
      }
      capabilities.tools.list_changed = tools.at("listChanged").get<bool>();
      capabilities.tools.list_changed_present = true;
    }
  }

  if (json.contains("resources")) {
    if (!json.at("resources").is_object()) {
      return std::nullopt;
    }
    capabilities.resources.enabled = true;
    const auto& resources = json.at("resources");
    capabilities.resources.raw = resources;
    if (resources.contains("listChanged")) {
      if (!resources.at("listChanged").is_boolean()) {
        return std::nullopt;
      }
      capabilities.resources.list_changed =
          resources.at("listChanged").get<bool>();
      capabilities.resources.list_changed_present = true;
    }
    if (resources.contains("subscribe")) {
      if (!resources.at("subscribe").is_boolean()) {
        return std::nullopt;
      }
      capabilities.resources.subscribe = resources.at("subscribe").get<bool>();
      capabilities.resources.subscribe_present = true;
    }
  }

  if (json.contains("prompts")) {
    if (!json.at("prompts").is_object()) {
      return std::nullopt;
    }
    capabilities.prompts.enabled = true;
    const auto& prompts = json.at("prompts");
    capabilities.prompts.raw = prompts;
    if (prompts.contains("listChanged")) {
      if (!prompts.at("listChanged").is_boolean()) {
        return std::nullopt;
      }
      capabilities.prompts.list_changed = prompts.at("listChanged").get<bool>();
      capabilities.prompts.list_changed_present = true;
    }
  }

  if (json.contains("logging")) {
    if (!json.at("logging").is_object()) {
      return std::nullopt;
    }
    capabilities.logging.enabled = true;
    capabilities.logging.raw = json.at("logging");
  }
  if (json.contains("completions")) {
    if (!json.at("completions").is_object()) {
      return std::nullopt;
    }
    capabilities.completions.enabled = true;
    capabilities.completions.raw = json.at("completions");
  }
  if (json.contains("tasks")) {
    if (!task_capabilities_are_valid(json.at("tasks"))) {
      return std::nullopt;
    }
    capabilities.tasks = task_capabilities_from_json(json.at("tasks"));
  }
  if (json.contains("experimental")) {
    if (!json.at("experimental").is_object()) {
      return std::nullopt;
    }
    capabilities.experimental = json.at("experimental");
  }
  if (json.contains("extensions")) {
    if (!json.at("extensions").is_object()) {
      return std::nullopt;
    }
    capabilities.extensions = json.at("extensions");
  }
  return capabilities;
}

}  // namespace mcp::protocol
