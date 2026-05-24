#pragma once

#include "mcp/protocol/task.hpp"
#include "mcp/protocol/types.hpp"

#include <optional>
#include <utility>

namespace mcp::protocol {

struct ToolCapabilities {
    bool list_changed = false;
};

struct ResourceCapabilities {
    bool list_changed = false;
    bool subscribe = false;
};

struct PromptCapabilities {
    bool list_changed = false;
};

struct LoggingCapabilities {
    bool enabled = false;
};

struct SamplingCapabilities {
    bool enabled = false;
};

struct CompletionCapabilities {
    bool enabled = false;
};

struct RootCapabilities {
    bool list_changed = false;
};

struct ElicitationCapabilities {
    bool form = false;
    bool url = false;

    bool enabled() const noexcept {
        return form || url;
    }
};

struct TaskCapabilities {
    bool list = false;
    bool cancel = false;
    bool tools_call = false;
    bool sampling_create_message = false;
    bool elicitation_create = false;
};

struct ClientCapabilities {
    RootCapabilities roots;
    SamplingCapabilities sampling;
    ElicitationCapabilities elicitation;
    std::optional<TaskCapabilities> tasks;
};

inline Json client_capabilities_to_json(const ClientCapabilities& capabilities) {
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

struct ServerCapabilities {
    ToolCapabilities tools;
    ResourceCapabilities resources;
    PromptCapabilities prompts;
    LoggingCapabilities logging;
    CompletionCapabilities completions;
    std::optional<TaskCapabilities> tasks;
};

inline std::optional<ClientCapabilities> client_capabilities_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::nullopt;
    }

    ClientCapabilities capabilities;
    if (json.contains("roots") && json.at("roots").is_object()) {
        const auto& roots = json.at("roots");
        if (roots.contains("listChanged") && roots.at("listChanged").is_boolean()) {
            capabilities.roots.list_changed = roots.at("listChanged").get<bool>();
        }
    }
    if (json.contains("sampling")) {
        capabilities.sampling.enabled = true;
    }
    if (json.contains("elicitation")) {
        const auto& elicitation = json.at("elicitation");
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
        const auto& tasks = json.at("tasks");
        TaskCapabilities task_capabilities;
        if (tasks.contains("list") && tasks.at("list").is_boolean()) {
            task_capabilities.list = tasks.at("list").get<bool>();
        }
        if (tasks.contains("cancel") && tasks.at("cancel").is_boolean()) {
            task_capabilities.cancel = tasks.at("cancel").get<bool>();
        }
        if (tasks.contains("requests") && tasks.at("requests").is_object()) {
            const auto& requests = tasks.at("requests");
            if (requests.contains("tools") && requests.at("tools").is_object()) {
                const auto& tools = requests.at("tools");
                if (tools.contains("call") && tools.at("call").is_boolean()) {
                    task_capabilities.tools_call = tools.at("call").get<bool>();
                }
            }
            if (requests.contains("sampling") && requests.at("sampling").is_object()) {
                const auto& sampling = requests.at("sampling");
                if (sampling.contains("createMessage") && sampling.at("createMessage").is_boolean()) {
                    task_capabilities.sampling_create_message = sampling.at("createMessage").get<bool>();
                }
            }
            if (requests.contains("elicitation") && requests.at("elicitation").is_object()) {
                const auto& elicitation = requests.at("elicitation");
                if (elicitation.contains("create") && elicitation.at("create").is_boolean()) {
                    task_capabilities.elicitation_create = elicitation.at("create").get<bool>();
                }
            }
        }
        capabilities.tasks = std::move(task_capabilities);
    }
    return capabilities;
}

} // namespace mcp::protocol
