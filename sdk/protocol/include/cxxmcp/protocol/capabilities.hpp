#pragma once

/// @file cxxmcp/protocol/capabilities.hpp
/// @brief MCP client and server capability declarations.
///
/// Capabilities are exchanged during the `initialize` lifecycle request and
/// response. They gate which feature methods and notifications a peer may use
/// after initialization, and they preserve unknown extension data for forward
/// compatibility.

#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/types.hpp"

#include <optional>
#include <utility>

namespace mcp::protocol {

    /// @brief Server capability flags for tool discovery and invocation.
    struct ToolCapabilities {
        /// Whether `notifications/tools/list_changed` may be emitted.
        bool list_changed = false;
    };

    /// @brief Server capability flags for resources.
    struct ResourceCapabilities {
        /// Whether `notifications/resources/list_changed` may be emitted.
        bool list_changed = false;
        /// Whether the server supports resource subscribe/unsubscribe methods.
        bool subscribe = false;
    };

    /// @brief Server capability flags for prompts.
    struct PromptCapabilities {
        /// Whether `notifications/prompts/list_changed` may be emitted.
        bool list_changed = false;
    };

    /// @brief Server capability flags for logging.
    struct LoggingCapabilities {
        /// Whether `logging/setLevel` and logging message notifications are supported.
        bool enabled = false;
    };

    /// @brief Client capability flags for sampling requests from the server.
    struct SamplingCapabilities {
        /// Whether the client accepts `sampling/createMessage` requests.
        bool enabled = false;
    };

    /// @brief Server capability flags for completion requests.
    struct CompletionCapabilities {
        /// Whether `completion/complete` is supported.
        bool enabled = false;
    };

    /// @brief Client capability flags for roots.
    struct RootCapabilities {
        /// Whether `notifications/roots/list_changed` may be emitted.
        bool list_changed = false;
    };

    /// @brief Client capability flags for elicitation.
    struct ElicitationCapabilities {
        /// Whether form-based `elicitation/create` requests are supported.
        bool form = false;
        /// Whether URL-based elicitation requests are supported.
        bool url = false;

        /// @brief Returns true when any elicitation mode is supported.
        /// @return True if either `form` or `url` is enabled.
        bool enabled() const noexcept {
            return form || url;
        }
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
    };

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

    /// @brief Serializes client capabilities to the MCP initialize payload shape.
    /// @param capabilities Client capability flags and extension data.
    /// @return JSON object suitable for `initialize.params.capabilities`.
    inline Json client_capabilities_to_json(const ClientCapabilities &capabilities) {
        Json json = Json::object();
        json["roots"] = Json{{"listChanged", capabilities.roots.list_changed}};
        json["sampling"] = Json::object();

        Json elicitation = Json::object();
        if (capabilities.elicitation.form) {
            elicitation["form"] = Json::object();
        }
        if (capabilities.elicitation.url) {
            elicitation["url"] = Json::object();
        }
        if (!elicitation.empty()) {
            json["elicitation"] = std::move(elicitation);
        }
        if (capabilities.experimental.has_value()) {
            json["experimental"] = *capabilities.experimental;
        }
        if (!capabilities.extensions.empty()) {
            json["extensions"] = capabilities.extensions;
        }
        if (capabilities.tasks.has_value()) {
            Json tasks = Json::object();
            if (capabilities.tasks->list) {
                tasks["list"] = true;
            }
            if (capabilities.tasks->cancel) {
                tasks["cancel"] = true;
            }
            Json requests = Json::object();
            if (capabilities.tasks->tools_call) {
                requests["tools"] = Json{{"call", true}};
            }
            if (capabilities.tasks->sampling_create_message) {
                requests["sampling"] = Json{{"createMessage", true}};
            }
            if (capabilities.tasks->elicitation_create) {
                requests["elicitation"] = Json{{"create", true}};
            }
            if (!requests.empty()) {
                tasks["requests"] = std::move(requests);
            }
            if (!tasks.empty()) {
                json["tasks"] = std::move(tasks);
            }
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

    /// @brief Parses client capabilities from an initialize request.
    /// @param json JSON object from `initialize.params.capabilities`.
    /// @return Parsed capabilities, or nullopt if the top-level value is invalid.
    inline std::optional<ClientCapabilities> client_capabilities_from_json(const Json &json) {
        if (!json.is_object()) {
            return std::nullopt;
        }

        ClientCapabilities capabilities;
        if (json.contains("roots") && json.at("roots").is_object()) {
            const auto &roots = json.at("roots");
            if (roots.contains("listChanged") && roots.at("listChanged").is_boolean()) {
                capabilities.roots.list_changed = roots.at("listChanged").get<bool>();
            }
        }
        if (json.contains("sampling")) {
            capabilities.sampling.enabled = true;
        }
        if (json.contains("elicitation")) {
            const auto &elicitation = json.at("elicitation");
            if (elicitation.is_object()) {
                if (elicitation.contains("form") && elicitation.at("form").is_object()) {
                    capabilities.elicitation.form = true;
                }
                if (elicitation.contains("url") && elicitation.at("url").is_object()) {
                    capabilities.elicitation.url = true;
                }
                if (!capabilities.elicitation.enabled()) {
                    capabilities.elicitation.form = true;
                }
            } else {
                capabilities.elicitation.form = true;
            }
        }
        if (json.contains("tasks") && json.at("tasks").is_object()) {
            const auto &tasks = json.at("tasks");
            TaskCapabilities task_capabilities;
            if (tasks.contains("list") && tasks.at("list").is_boolean()) {
                task_capabilities.list = tasks.at("list").get<bool>();
            }
            if (tasks.contains("cancel") && tasks.at("cancel").is_boolean()) {
                task_capabilities.cancel = tasks.at("cancel").get<bool>();
            }
            if (tasks.contains("requests") && tasks.at("requests").is_object()) {
                const auto &requests = tasks.at("requests");
                if (requests.contains("tools") && requests.at("tools").is_object()) {
                    const auto &tools = requests.at("tools");
                    if (tools.contains("call") && tools.at("call").is_boolean()) {
                        task_capabilities.tools_call = tools.at("call").get<bool>();
                    }
                }
                if (requests.contains("sampling") && requests.at("sampling").is_object()) {
                    const auto &sampling = requests.at("sampling");
                    if (sampling.contains("createMessage") && sampling.at("createMessage").is_boolean()) {
                        task_capabilities.sampling_create_message = sampling.at("createMessage").get<bool>();
                    }
                }
                if (requests.contains("elicitation") && requests.at("elicitation").is_object()) {
                    const auto &elicitation = requests.at("elicitation");
                    if (elicitation.contains("create") && elicitation.at("create").is_boolean()) {
                        task_capabilities.elicitation_create = elicitation.at("create").get<bool>();
                    }
                }
            }
            capabilities.tasks = std::move(task_capabilities);
        }
        if (json.contains("experimental")) {
            capabilities.experimental = json.at("experimental");
        }
        if (json.contains("extensions")) {
            capabilities.extensions = json.at("extensions");
        }
        return capabilities;
    }

}// namespace mcp::protocol
