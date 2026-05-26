// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/task.hpp
/// @brief Asynchronous task status and task-management payloads.
///
/// Task payloads let long-running MCP requests return a task handle and report
/// progress through task status methods or notifications. Feature requests such
/// as tools, sampling, and elicitation may opt into task handling when the peer
/// advertises the corresponding task capability.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Lifecycle status for an asynchronous MCP task.
enum class TaskStatus {
  /// Task is still running.
  Working,
  /// Task is blocked waiting for additional input.
  InputRequired,
  /// Task completed successfully.
  Completed,
  /// Task failed.
  Failed,
  /// Task was cancelled.
  Cancelled,
};

/// @brief Converts a task status to its MCP string value.
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

/// @brief Parses a task status string.
/// @return Parsed status, or nullopt for unsupported strings.
inline std::optional<TaskStatus> task_status_from_string(
    const std::string& value) {
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

/// @brief Optional task parameters embedded in feature requests.
struct TaskRequestParameters {
  /// Optional requested time-to-live for the created task.
  std::optional<std::int64_t> ttl;
  /// Vendor or future task options preserved for forward compatibility.
  Json extensions = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
};

/// @brief Snapshot of an asynchronous task.
struct Task {
  /// Stable task identifier used by task management methods.
  std::string task_id;
  /// Current lifecycle status.
  TaskStatus status = TaskStatus::Working;
  /// Optional human-readable status detail.
  std::optional<std::string> status_message;
  /// Creation timestamp as a protocol string.
  std::string created_at;
  /// Task time-to-live. Monostate serializes as JSON null.
  std::variant<std::monostate, std::int64_t> ttl = std::monostate{};
  /// Optional recommended polling interval.
  std::optional<std::int64_t> poll_interval;
  /// Last update timestamp as a protocol string.
  std::string last_updated_at;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for `tasks/list`.
struct TaskListParams {
  /// Optional pagination cursor from a prior `tasks/list` result.
  std::optional<std::string> cursor;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for `tasks/get`.
struct TaskGetParams {
  /// Task identifier to retrieve.
  std::string task_id;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Parameters for `tasks/result`.
using TaskResultParams = TaskGetParams;

/// @brief Parameters for `tasks/cancel`.
struct TaskCancelParams {
  /// Task identifier to cancel.
  std::string task_id;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result object for `tasks/list`.
struct TaskListResult {
  /// Page of tasks.
  std::vector<Task> tasks;
  /// Optional cursor for retrieving the next page.
  std::optional<std::string> next_cursor;
  /// Optional total number of tasks known to the peer.
  std::optional<std::int64_t> total;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result wrapper returned when a feature request creates a task.
struct CreateTaskResult {
  /// Created task snapshot.
  Task task;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result object for `tasks/get` with task fields flattened.
struct TaskGetResult {
  /// Retrieved task snapshot.
  Task task;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Result object for `tasks/cancel` with task fields flattened.
struct TaskCancelResult {
  /// Cancelled task snapshot.
  Task task;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

/// @brief Builds an InvalidRequest error for task JSON validation failures.
inline core::Error task_json_error(std::string message) {
  return core::Error{
      static_cast<int>(ErrorCode::InvalidRequest), std::move(message), {}};
}

/// @brief Serializes optional task request parameters.
inline Json task_request_parameters_to_json(
    const TaskRequestParameters& parameters) {
  Json json = Json::object();
  if (parameters.extensions.is_object()) {
    for (const auto& [key, value] : parameters.extensions.items()) {
      if (key != "ttl" && key != "_meta") {
        json[key] = value;
      }
    }
  }
  if (parameters.ttl.has_value()) {
    json["ttl"] = *parameters.ttl;
  }
  if (parameters.meta.has_value()) {
    json["_meta"] = *parameters.meta;
  }
  return json;
}

/// @brief Parses optional task request parameters.
/// @return Parsed parameters or validation error.
inline core::Result<TaskRequestParameters> task_request_parameters_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        task_json_error("task parameters must be an object"));
  }

  TaskRequestParameters parameters;
  if (json.contains("ttl")) {
    if (!json.at("ttl").is_number_integer()) {
      return std::unexpected(task_json_error("task ttl must be an integer"));
    }
    parameters.ttl = json.at("ttl").get<std::int64_t>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          task_json_error("task parameters _meta must be an object"));
    }
    parameters.meta = json.at("_meta");
  }
  for (const auto& [key, value] : json.items()) {
    if (key != "ttl" && key != "_meta") {
      parameters.extensions[key] = value;
    }
  }
  return parameters;
}

/// @brief Serializes a task snapshot.
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
  append_json_extensions(json, task.extensions);
  return json;
}

/// @brief Parses a task snapshot.
/// @return Parsed task or validation error.
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
  if (!json.contains("lastUpdatedAt") ||
      !json.at("lastUpdatedAt").is_string()) {
    return std::unexpected(task_json_error("task requires lastUpdatedAt"));
  }

  const auto status =
      task_status_from_string(json.at("status").get<std::string>());
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
      return std::unexpected(
          task_json_error("task statusMessage must be a string"));
    }
    task.status_message = json.at("statusMessage").get<std::string>();
  }
  if (json.contains("ttl")) {
    if (json.at("ttl").is_number_integer()) {
      task.ttl = json.at("ttl").get<std::int64_t>();
    } else if (!json.at("ttl").is_null()) {
      return std::unexpected(
          task_json_error("task ttl must be an integer or null"));
    }
  }
  if (json.contains("pollInterval")) {
    if (!json.at("pollInterval").is_number_integer()) {
      return std::unexpected(
          task_json_error("task pollInterval must be an integer"));
    }
    task.poll_interval = json.at("pollInterval").get<std::int64_t>();
  }
  task.extensions = collect_json_extensions(
      json, {"taskId", "status", "statusMessage", "createdAt", "ttl",
             "pollInterval", "lastUpdatedAt"});
  return task;
}

/// @brief Serializes a flattened task result wrapper.
inline Json task_operation_result_to_json(const Task& task,
                                          const std::optional<Json>& meta,
                                          const Json& extensions) {
  Json json = task_to_json(task);
  if (meta.has_value()) {
    json["_meta"] = *meta;
  }
  append_json_extensions(json, extensions);
  return json;
}

/// @brief Parses a flattened task result wrapper.
inline core::Result<TaskGetResult> task_operation_result_from_json(
    const Json& json, std::string_view context) {
  if (!json.is_object()) {
    return std::unexpected(
        task_json_error(std::string(context) + " result must be an object"));
  }
  auto task = task_from_json(json);
  if (!task) {
    return std::unexpected(task.error());
  }
  task->extensions.erase("_meta");

  TaskGetResult result;
  result.task = *task;
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(task_json_error(
          std::string(context) + " result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions = collect_json_extensions(
      json, {"taskId", "status", "statusMessage", "createdAt", "ttl",
             "pollInterval", "lastUpdatedAt", "_meta"});
  return result;
}

/// @brief Serializes `tasks/list` params.
inline Json task_list_params_to_json(const TaskListParams& params) {
  Json json = Json::object();
  if (params.cursor.has_value()) {
    json["cursor"] = *params.cursor;
  }
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  append_json_extensions(json, params.extensions);
  return json;
}

/// @brief Parses `tasks/list` params.
/// @return Parsed params or validation error.
inline core::Result<TaskListParams> task_list_params_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        task_json_error("tasks/list params must be an object"));
  }
  TaskListParams params;
  if (json.contains("cursor")) {
    if (!json.at("cursor").is_string()) {
      return std::unexpected(
          task_json_error("tasks/list cursor must be a string"));
    }
    params.cursor = json.at("cursor").get<std::string>();
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          task_json_error("tasks/list _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  params.extensions = collect_json_extensions(json, {"cursor", "_meta"});
  return params;
}

/// @brief Serializes `tasks/get` params.
inline Json task_get_params_to_json(const TaskGetParams& params) {
  Json json = Json{{"taskId", params.task_id}};
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  append_json_extensions(json, params.extensions);
  return json;
}

/// @brief Parses `tasks/get` params.
/// @return Parsed params or validation error.
inline core::Result<TaskGetParams> task_get_params_from_json(const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        task_json_error("tasks/get params must be an object"));
  }
  if (!json.contains("taskId") || !json.at("taskId").is_string()) {
    return std::unexpected(task_json_error("tasks/get params require taskId"));
  }
  TaskGetParams params;
  params.task_id = json.at("taskId").get<std::string>();
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          task_json_error("tasks/get _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  params.extensions = collect_json_extensions(json, {"taskId", "_meta"});
  return params;
}

/// @brief Serializes `tasks/cancel` params.
inline Json task_cancel_params_to_json(const TaskCancelParams& params) {
  Json json = Json{{"taskId", params.task_id}};
  if (params.meta.has_value()) {
    json["_meta"] = *params.meta;
  }
  append_json_extensions(json, params.extensions);
  return json;
}

/// @brief Parses `tasks/cancel` params.
/// @return Parsed params or validation error.
inline core::Result<TaskCancelParams> task_cancel_params_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        task_json_error("tasks/cancel params must be an object"));
  }
  if (!json.contains("taskId") || !json.at("taskId").is_string()) {
    return std::unexpected(
        task_json_error("tasks/cancel params require taskId"));
  }
  TaskCancelParams params;
  params.task_id = json.at("taskId").get<std::string>();
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          task_json_error("tasks/cancel _meta must be an object"));
    }
    params.meta = json.at("_meta");
  }
  params.extensions = collect_json_extensions(json, {"taskId", "_meta"});
  return params;
}

/// @brief Serializes `tasks/result` params.
inline Json task_result_params_to_json(const TaskResultParams& params) {
  return task_get_params_to_json(params);
}

/// @brief Parses `tasks/result` params.
/// @return Parsed params or validation error.
inline core::Result<TaskResultParams> task_result_params_from_json(
    const Json& json) {
  return task_get_params_from_json(json);
}

/// @brief Serializes a `tasks/list` result.
inline Json task_list_result_to_json(const TaskListResult& result) {
  Json json = Json::object();
  json["tasks"] = Json::array();
  for (const auto& task : result.tasks) {
    json["tasks"].push_back(task_to_json(task));
  }
  if (result.next_cursor.has_value()) {
    json["nextCursor"] = *result.next_cursor;
  }
  if (result.total.has_value()) {
    json["total"] = *result.total;
  }
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a `tasks/list` result.
/// @return Parsed result or validation error.
inline core::Result<TaskListResult> task_list_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        task_json_error("tasks/list result must be an object"));
  }
  if (!json.contains("tasks") || !json.at("tasks").is_array()) {
    return std::unexpected(
        task_json_error("tasks/list result requires a tasks array"));
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
      return std::unexpected(
          task_json_error("tasks/list nextCursor must be a string"));
    }
    result.next_cursor = json.at("nextCursor").get<std::string>();
  }
  if (json.contains("total")) {
    if (!json.at("total").is_number_integer()) {
      return std::unexpected(
          task_json_error("tasks/list total must be an integer"));
    }
    const auto total = json.at("total").get<std::int64_t>();
    if (total < 0) {
      return std::unexpected(
          task_json_error("tasks/list total must be non-negative"));
    }
    result.total = total;
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return std::unexpected(
          task_json_error("tasks/list result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions =
      collect_json_extensions(json, {"tasks", "nextCursor", "total", "_meta"});
  return result;
}

/// @brief Serializes a `tasks/get` result with flattened task fields.
inline Json task_get_result_to_json(const TaskGetResult& result) {
  return task_operation_result_to_json(result.task, result.meta,
                                       result.extensions);
}

/// @brief Parses a `tasks/get` result with flattened task fields.
inline core::Result<TaskGetResult> task_get_result_from_json(const Json& json) {
  return task_operation_result_from_json(json, "tasks/get");
}

/// @brief Serializes a `tasks/cancel` result with flattened task fields.
inline Json task_cancel_result_to_json(const TaskCancelResult& result) {
  return task_operation_result_to_json(result.task, result.meta,
                                       result.extensions);
}

/// @brief Parses a `tasks/cancel` result with flattened task fields.
inline core::Result<TaskCancelResult> task_cancel_result_from_json(
    const Json& json) {
  const auto parsed = task_operation_result_from_json(json, "tasks/cancel");
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  TaskCancelResult result;
  result.task = parsed->task;
  result.meta = parsed->meta;
  result.extensions = parsed->extensions;
  return result;
}

/// @brief Serializes a task creation result wrapper.
inline Json create_task_result_to_json(const CreateTaskResult& result) {
  Json json = Json::object();
  json["task"] = task_to_json(result.task);
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a task creation result wrapper.
/// @return Parsed result or validation error.
inline core::Result<CreateTaskResult> create_task_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return std::unexpected(
        task_json_error("createTask result must be an object"));
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
  result.extensions = collect_json_extensions(json, {"task", "_meta"});
  return result;
}

}  // namespace mcp::protocol
