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
#include "cxxmcp/protocol/reflect.hpp"
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

/// @brief JsonFieldTraits specialization for TaskStatus enum.
template <>
struct JsonFieldTraits<TaskStatus> {
  static void serialize(Json& json, const char* key, TaskStatus value) {
    json[key] = task_status_to_string(value);
  }
  static bool deserialize(const Json& json, const char* key,
                          TaskStatus& target) {
    if (!json.contains(key) || !json.at(key).is_string()) {
      return false;
    }
    auto val = task_status_from_string(json.at(key).get<std::string>());
    if (!val.has_value()) {
      return false;
    }
    target = *val;
    return true;
  }
};

/// @brief Optional task parameters embedded in feature requests.
struct TaskRequestParameters {
  /// Optional requested time-to-live for the created task.
  std::optional<std::int64_t> ttl;
  /// Vendor or future task options preserved for forward compatibility.
  Json extensions = Json::object();
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
};

template <>
struct Reflect<TaskRequestParameters> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(
        field("ttl", &TaskRequestParameters::ttl),
        field("_meta", &TaskRequestParameters::meta),
        extensions_field(&TaskRequestParameters::extensions, {"ttl", "_meta"}));
  }
  static std::vector<std::string> known_keys() { return {"ttl", "_meta"}; }
};
CXXMCP_REFLECT_CHECK(TaskRequestParameters, 3);

template <>
struct JsonFieldTraits<TaskRequestParameters> {
  static void serialize(Json& json, const char* key,
                        const TaskRequestParameters& value) {
    json[key] = reflect_to_json(value);
  }
  static bool deserialize(const Json& json, const char* key,
                          TaskRequestParameters& target) {
    if (!json.contains(key)) {
      return false;
    }
    auto result = reflect_from_json<TaskRequestParameters>(json.at(key));
    if (!result) {
      return false;
    }
    target = std::move(*result);
    return true;
  }
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

/// @brief Reflection trait for Task.
template <>
struct Reflect<Task> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(
        field("taskId", &Task::task_id), field("status", &Task::status),
        field("statusMessage", &Task::status_message),
        field("createdAt", &Task::created_at),
        nullable_field("ttl", &Task::ttl),
        field("pollInterval", &Task::poll_interval),
        field("lastUpdatedAt", &Task::last_updated_at),
        extensions_field(&Task::extensions,
                         {"taskId", "status", "statusMessage", "createdAt",
                          "ttl", "pollInterval", "lastUpdatedAt"}));
  }
  static std::vector<std::string> known_keys() {
    return {"taskId", "status",       "statusMessage", "createdAt",
            "ttl",    "pollInterval", "lastUpdatedAt"};
  }
};
CXXMCP_REFLECT_CHECK(Task, 8);

/// @brief Parameters for `tasks/list`.
struct TaskListParams {
  /// Optional pagination cursor from a prior `tasks/list` result.
  std::optional<std::string> cursor;
  /// Optional `_meta` extension object preserved on the wire.
  std::optional<Json> meta;
  /// Unknown JSON members preserved for forward-compatible round trips.
  Json extensions = Json::object();
};

template <>
struct Reflect<TaskListParams> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(
        field("cursor", &TaskListParams::cursor),
        field("_meta", &TaskListParams::meta),
        extensions_field(&TaskListParams::extensions, {"cursor", "_meta"}));
  }
  static std::vector<std::string> known_keys() { return {"cursor", "_meta"}; }
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

template <>
struct Reflect<TaskGetParams> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(
        field("taskId", &TaskGetParams::task_id),
        field("_meta", &TaskGetParams::meta),
        extensions_field(&TaskGetParams::extensions, {"taskId", "_meta"}));
  }
  static std::vector<std::string> known_keys() { return {"taskId", "_meta"}; }
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

template <>
struct Reflect<TaskCancelParams> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(
        field("taskId", &TaskCancelParams::task_id),
        field("_meta", &TaskCancelParams::meta),
        extensions_field(&TaskCancelParams::extensions, {"taskId", "_meta"}));
  }
  static std::vector<std::string> known_keys() { return {"taskId", "_meta"}; }
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

/// @brief Result object for `tasks/result` (GetTaskPayloadResult equivalent).
///
/// Carries the original request's polymorphic result payload. On the wire the
/// payload fields are flattened alongside `_meta` and any future extensions.
struct TaskGetPayloadResult {
  /// The original request's result (polymorphic -- may be any JSON value).
  Json payload;
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
  return reflect_to_json(parameters);
}

/// @brief Parses optional task request parameters.
/// @return Parsed parameters or validation error.
inline core::Result<TaskRequestParameters> task_request_parameters_from_json(
    const Json& json) {
  return reflect_from_json<TaskRequestParameters>(json);
}

/// @brief Serializes a task snapshot.
inline Json task_to_json(const Task& task) { return reflect_to_json(task); }

/// @brief Parses a task snapshot.
/// @return Parsed task or validation error.
inline core::Result<Task> task_from_json(const Json& json) {
  return reflect_from_json<Task>(json);
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
    return mcp::core::unexpected(
        task_json_error(std::string(context) + " result must be an object"));
  }
  auto task = task_from_json(json);
  if (!task) {
    return mcp::core::unexpected(task.error());
  }
  task->extensions.erase("_meta");

  TaskGetResult result;
  result.task = *task;
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(task_json_error(
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
  return reflect_to_json(params);
}

/// @brief Parses `tasks/list` params.
/// @return Parsed params or validation error.
inline core::Result<TaskListParams> task_list_params_from_json(
    const Json& json) {
  return reflect_from_json<TaskListParams>(json);
}

/// @brief Serializes `tasks/get` params.
inline Json task_get_params_to_json(const TaskGetParams& params) {
  return reflect_to_json(params);
}

/// @brief Parses `tasks/get` params.
/// @return Parsed params or validation error.
inline core::Result<TaskGetParams> task_get_params_from_json(const Json& json) {
  return reflect_from_json<TaskGetParams>(json);
}

/// @brief Serializes `tasks/cancel` params.
inline Json task_cancel_params_to_json(const TaskCancelParams& params) {
  return reflect_to_json(params);
}

/// @brief Parses `tasks/cancel` params.
/// @return Parsed params or validation error.
inline core::Result<TaskCancelParams> task_cancel_params_from_json(
    const Json& json) {
  return reflect_from_json<TaskCancelParams>(json);
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
    return mcp::core::unexpected(
        task_json_error("tasks/list result must be an object"));
  }
  if (!json.contains("tasks") || !json.at("tasks").is_array()) {
    return mcp::core::unexpected(
        task_json_error("tasks/list result requires a tasks array"));
  }

  TaskListResult result;
  for (const auto& item : json.at("tasks")) {
    const auto task = task_from_json(item);
    if (!task) {
      return mcp::core::unexpected(task.error());
    }
    result.tasks.push_back(*task);
  }
  if (json.contains("nextCursor")) {
    if (!json.at("nextCursor").is_string()) {
      return mcp::core::unexpected(
          task_json_error("tasks/list nextCursor must be a string"));
    }
    result.next_cursor = json.at("nextCursor").get<std::string>();
  }
  if (json.contains("total")) {
    if (!json.at("total").is_number_integer()) {
      return mcp::core::unexpected(
          task_json_error("tasks/list total must be an integer"));
    }
    const auto total = json.at("total").get<std::int64_t>();
    if (total < 0) {
      return mcp::core::unexpected(
          task_json_error("tasks/list total must be non-negative"));
    }
    result.total = total;
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
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
    return mcp::core::unexpected(parsed.error());
  }
  TaskCancelResult result;
  result.task = parsed->task;
  result.meta = parsed->meta;
  result.extensions = parsed->extensions;
  return result;
}

/// @brief Serializes a `tasks/result` payload result.
///
/// The wire format flattens the payload fields at the top level alongside
/// `_meta` and any future extension keys.
inline Json task_get_payload_result_to_json(
    const TaskGetPayloadResult& result) {
  Json json = Json::object();
  if (result.payload.is_object()) {
    for (const auto& [key, value] : result.payload.items()) {
      json[key] = value;
    }
  }
  if (result.meta.has_value()) {
    json["_meta"] = *result.meta;
  }
  append_json_extensions(json, result.extensions);
  return json;
}

/// @brief Parses a `tasks/result` payload result.
/// @return Parsed result or validation error.
inline core::Result<TaskGetPayloadResult> task_get_payload_result_from_json(
    const Json& json) {
  if (!json.is_object()) {
    return mcp::core::unexpected(
        task_json_error("tasks/result result must be an object"));
  }

  TaskGetPayloadResult result;
  result.payload = Json::object();
  for (const auto& [key, value] : json.items()) {
    if (key != "_meta") {
      result.payload[key] = value;
    }
  }
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          task_json_error("tasks/result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
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
    return mcp::core::unexpected(
        task_json_error("createTask result must be an object"));
  }
  if (!json.contains("task")) {
    return mcp::core::unexpected(
        task_json_error("createTask result requires task"));
  }

  const auto task = task_from_json(json.at("task"));
  if (!task) {
    return mcp::core::unexpected(task.error());
  }

  CreateTaskResult result;
  result.task = *task;
  if (json.contains("_meta")) {
    if (!json.at("_meta").is_object()) {
      return mcp::core::unexpected(
          task_json_error("createTask result _meta must be an object"));
    }
    result.meta = json.at("_meta");
  }
  result.extensions = collect_json_extensions(json, {"task", "_meta"});
  return result;
}

}  // namespace mcp::protocol
