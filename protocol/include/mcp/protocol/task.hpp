#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mcp::protocol {

enum class TaskStatus {
    Working,
    InputRequired,
    Completed,
    Failed,
    Cancelled,
};

inline std::string task_status_to_string(TaskStatus status) {
    switch (status) {
    case TaskStatus::Working:
        return "working";
    case TaskStatus::InputRequired:
        return "input_required";
    case TaskStatus::Completed:
        return "completed";
    case TaskStatus::Failed:
        return "failed";
    case TaskStatus::Cancelled:
        return "cancelled";
    }
    return "working";
}

inline std::optional<TaskStatus> task_status_from_string(const std::string& value) {
    if (value == "working") {
        return TaskStatus::Working;
    }
    if (value == "input_required") {
        return TaskStatus::InputRequired;
    }
    if (value == "completed") {
        return TaskStatus::Completed;
    }
    if (value == "failed") {
        return TaskStatus::Failed;
    }
    if (value == "cancelled") {
        return TaskStatus::Cancelled;
    }
    return std::nullopt;
}

struct TaskRequestParameters {
    std::optional<std::int64_t> ttl;
};

struct Task {
    std::string task_id;
    TaskStatus status = TaskStatus::Working;
    std::optional<std::string> status_message;
    std::string created_at;
    std::variant<std::monostate, std::int64_t> ttl = std::monostate{};
    std::optional<std::int64_t> poll_interval;
    std::string last_updated_at;
};

struct TaskListParams {
    std::optional<std::string> cursor;
};

struct TaskGetParams {
    std::string task_id;
};

using TaskResultParams = TaskGetParams;

struct TaskCancelParams {
    std::string task_id;
};

struct TaskListResult {
    std::vector<Task> tasks;
    std::optional<std::string> next_cursor;
};

struct CreateTaskResult {
    Task task;
    std::optional<Json> meta;
};

inline core::Error task_json_error(std::string message) {
    return core::Error{static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

inline Json task_request_parameters_to_json(const TaskRequestParameters& parameters) {
    Json json = Json::object();
    if (parameters.ttl.has_value()) {
        json["ttl"] = *parameters.ttl;
    }
    return json;
}

inline core::Result<TaskRequestParameters> task_request_parameters_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(task_json_error("task parameters must be an object"));
    }

    TaskRequestParameters parameters;
    if (json.contains("ttl")) {
        if (!json.at("ttl").is_number_integer()) {
            return std::unexpected(task_json_error("task ttl must be an integer"));
        }
        parameters.ttl = json.at("ttl").get<std::int64_t>();
    }
    return parameters;
}

inline Json task_to_json(const Task& task) {
    Json json = Json::object();
    json["taskId"] = task.task_id;
    json["status"] = task_status_to_string(task.status);
    json["createdAt"] = task.created_at;
    json["lastUpdatedAt"] = task.last_updated_at;
    if (task.status_message.has_value()) {
        json["statusMessage"] = *task.status_message;
    }
    if (std::holds_alternative<std::int64_t>(task.ttl)) {
        json["ttl"] = std::get<std::int64_t>(task.ttl);
    } else {
        json["ttl"] = nullptr;
    }
    if (task.poll_interval.has_value()) {
        json["pollInterval"] = *task.poll_interval;
    }
    return json;
}

inline core::Result<Task> task_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(task_json_error("task must be an object"));
    }
    if (!json.contains("taskId") || !json.at("taskId").is_string()) {
        return std::unexpected(task_json_error("task requires a string taskId"));
    }
    if (!json.contains("status") || !json.at("status").is_string()) {
        return std::unexpected(task_json_error("task requires a string status"));
    }
    if (!json.contains("createdAt") || !json.at("createdAt").is_string()) {
        return std::unexpected(task_json_error("task requires createdAt"));
    }
    if (!json.contains("lastUpdatedAt") || !json.at("lastUpdatedAt").is_string()) {
        return std::unexpected(task_json_error("task requires lastUpdatedAt"));
    }

    const auto status = task_status_from_string(json.at("status").get<std::string>());
    if (!status.has_value()) {
        return std::unexpected(task_json_error("task status is invalid"));
    }

    Task task;
    task.task_id = json.at("taskId").get<std::string>();
    task.status = *status;
    task.created_at = json.at("createdAt").get<std::string>();
    task.last_updated_at = json.at("lastUpdatedAt").get<std::string>();
    if (json.contains("statusMessage")) {
        if (!json.at("statusMessage").is_string()) {
            return std::unexpected(task_json_error("task statusMessage must be a string"));
        }
        task.status_message = json.at("statusMessage").get<std::string>();
    }
    if (json.contains("ttl")) {
        if (json.at("ttl").is_number_integer()) {
            task.ttl = json.at("ttl").get<std::int64_t>();
        } else if (!json.at("ttl").is_null()) {
            return std::unexpected(task_json_error("task ttl must be an integer or null"));
        }
    }
    if (json.contains("pollInterval")) {
        if (!json.at("pollInterval").is_number_integer()) {
            return std::unexpected(task_json_error("task pollInterval must be an integer"));
        }
        task.poll_interval = json.at("pollInterval").get<std::int64_t>();
    }
    return task;
}

inline Json task_list_params_to_json(const TaskListParams& params) {
    Json json = Json::object();
    if (params.cursor.has_value()) {
        json["cursor"] = *params.cursor;
    }
    return json;
}

inline core::Result<TaskListParams> task_list_params_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(task_json_error("tasks/list params must be an object"));
    }
    TaskListParams params;
    if (json.contains("cursor")) {
        if (!json.at("cursor").is_string()) {
            return std::unexpected(task_json_error("tasks/list cursor must be a string"));
        }
        params.cursor = json.at("cursor").get<std::string>();
    }
    return params;
}

inline Json task_get_params_to_json(const TaskGetParams& params) {
    return Json{{"taskId", params.task_id}};
}

inline core::Result<TaskGetParams> task_get_params_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(task_json_error("tasks/get params must be an object"));
    }
    if (!json.contains("taskId") || !json.at("taskId").is_string()) {
        return std::unexpected(task_json_error("tasks/get params require taskId"));
    }
    return TaskGetParams{json.at("taskId").get<std::string>()};
}

inline Json task_cancel_params_to_json(const TaskCancelParams& params) {
    return Json{{"taskId", params.task_id}};
}

inline core::Result<TaskCancelParams> task_cancel_params_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(task_json_error("tasks/cancel params must be an object"));
    }
    if (!json.contains("taskId") || !json.at("taskId").is_string()) {
        return std::unexpected(task_json_error("tasks/cancel params require taskId"));
    }
    return TaskCancelParams{json.at("taskId").get<std::string>()};
}

inline Json task_list_result_to_json(const TaskListResult& result) {
    Json json = Json::object();
    json["tasks"] = Json::array();
    for (const auto& task : result.tasks) {
        json["tasks"].push_back(task_to_json(task));
    }
    if (result.next_cursor.has_value()) {
        json["nextCursor"] = *result.next_cursor;
    }
    return json;
}

inline core::Result<TaskListResult> task_list_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(task_json_error("tasks/list result must be an object"));
    }
    if (!json.contains("tasks") || !json.at("tasks").is_array()) {
        return std::unexpected(task_json_error("tasks/list result requires a tasks array"));
    }

    TaskListResult result;
    for (const auto& item : json.at("tasks")) {
        const auto task = task_from_json(item);
        if (!task) {
            return std::unexpected(task.error());
        }
        result.tasks.push_back(*task);
    }
    if (json.contains("nextCursor")) {
        if (!json.at("nextCursor").is_string()) {
            return std::unexpected(task_json_error("tasks/list nextCursor must be a string"));
        }
        result.next_cursor = json.at("nextCursor").get<std::string>();
    }
    return result;
}

inline Json create_task_result_to_json(const CreateTaskResult& result) {
    Json json = Json::object();
    json["task"] = task_to_json(result.task);
    if (result.meta.has_value()) {
        json["_meta"] = *result.meta;
    }
    return json;
}

inline core::Result<CreateTaskResult> create_task_result_from_json(const Json& json) {
    if (!json.is_object()) {
        return std::unexpected(task_json_error("createTask result must be an object"));
    }
    if (!json.contains("task")) {
        return std::unexpected(task_json_error("createTask result requires task"));
    }

    const auto task = task_from_json(json.at("task"));
    if (!task) {
        return std::unexpected(task.error());
    }

    CreateTaskResult result;
    result.task = *task;
    if (json.contains("_meta")) {
        result.meta = json.at("_meta");
    }
    return result;
}

} // namespace mcp::protocol
