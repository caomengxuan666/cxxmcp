// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Server-side task operation processor for asynchronous MCP requests.

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cxxmcp/core/executor.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/server/registry.hpp"
#include "cxxmcp/server/transport.hpp"

namespace mcp::server {

/// @brief Observer called after a task snapshot changes.
using TaskStatusNotificationHook = std::function<void(const protocol::Task&)>;

/// @brief Observer called when a task state change can be mapped to progress.
using TaskProgressNotificationHook =
    std::function<void(const protocol::ProgressNotificationParams&)>;

/// @brief Options for the SDK server task processor.
struct TaskOperationProcessorOptions {
  /// Worker threads used to execute background operations.
  std::size_t worker_count = 4;
  /// Maximum queued background operations.
  std::size_t queue_size = 64;
  /// Default timeout for task execution when a request omits ttl.
  std::chrono::seconds default_timeout{300};
  /// Recommended polling interval exposed in created task snapshots.
  std::optional<std::int64_t> poll_interval;
  /// Optional age limit for terminal task records and stored results.
  std::optional<std::chrono::milliseconds> completed_task_ttl;
  /// Maximum retained terminal task records.
  std::size_t max_completed_tasks = 128;
  /// Maximum tasks returned by one `tasks/list` page. Zero means unbounded.
  std::size_t list_page_size = 0;
  /// Optional hook for automatic `notifications/tasks/status` emission.
  TaskStatusNotificationHook task_status_hook;
  /// Optional hook for automatic `notifications/progress` emission.
  TaskProgressNotificationHook task_progress_hook;
  /// Emit progress updates from task state changes when a progress token
  /// exists.
  bool emit_progress_for_task_state_changes = true;
};

/// @brief Metadata for a background operation managed as an MCP task.
struct TaskOperationDescriptor {
  /// Human-readable operation name used for diagnostics.
  std::string name;
  /// Optional task request metadata supplied by the peer.
  std::optional<protocol::TaskRequestParameters> task;
  /// Optional progress token from request `_meta.progressToken`.
  std::optional<protocol::ProgressToken> progress_token;
};

/// @brief Callable executed by the task processor.
using TaskOperationHandler =
    std::function<core::Result<protocol::Json>(const CancellationToken&)>;

/// @brief RMCP-style operation processor for server-side task execution.
///
/// The processor keeps task state in SDK/server, executes requested tool calls
/// on a bounded executor, and exposes task-management methods. It intentionally
/// has no dependency on runtime/app gateway policy.
class TaskOperationProcessor {
 public:
  explicit TaskOperationProcessor(TaskOperationProcessorOptions options = {});

  TaskOperationProcessor(const TaskOperationProcessor&) = delete;
  TaskOperationProcessor& operator=(const TaskOperationProcessor&) = delete;

  /// @brief Stops accepting background work and waits for workers to finish.
  void stop() noexcept;

  /// @brief Submit a generic background operation.
  core::Result<protocol::CreateTaskResult> submit_operation(
      TaskOperationDescriptor descriptor, TaskOperationHandler operation);

  /// @brief Submit a `tools/call` request for background execution.
  core::Result<protocol::CreateTaskResult> submit_tool_call(
      const ToolRegistry& tools, protocol::ToolCall call,
      const SessionContext& context,
      const JsonSchemaValidator* schema_validator = nullptr);

  /// @brief List retained task snapshots.
  core::Result<protocol::TaskListResult> list_tasks(
      const protocol::TaskListParams& params = {});

  /// @brief Get one task snapshot.
  core::Result<protocol::Task> get_task(const protocol::TaskGetParams& params);

  /// @brief Cancel a running task or return an existing terminal snapshot.
  core::Result<protocol::Task> cancel_task(
      const protocol::TaskCancelParams& params);

  /// @brief Return the stored result JSON for a completed task.
  core::Result<protocol::Json> task_result(
      const protocol::TaskResultParams& params);

 private:
  struct TaskRecord {
    protocol::Task task;
    std::optional<protocol::ProgressToken> progress_token;
    std::chrono::steady_clock::time_point started_at;
    std::optional<std::chrono::steady_clock::time_point> terminal_at;
    std::optional<std::chrono::seconds> timeout;
    CancellationSource cancellation;
    std::optional<protocol::Json> result;
    std::optional<core::Error> failure;
  };

  struct TaskUpdate {
    protocol::Task task;
    std::optional<protocol::ProgressToken> progress_token;
  };

  static std::string now_timestamp();
  static bool is_terminal(protocol::TaskStatus status) noexcept;

  std::string make_task_id();
  core::Error task_not_found_error(std::string_view task_id) const;
  TaskRecord* find_task_locked(std::string_view task_id);
  const TaskRecord* find_task_locked(std::string_view task_id) const;
  void finish_task(std::string task_id, core::Result<protocol::Json> result);
  void refresh_locked(std::vector<TaskUpdate>* updates = nullptr);
  void trim_completed_locked();
  void append_update_locked(std::vector<TaskUpdate>* updates,
                            const TaskRecord& record) const;
  void emit_updates(const std::vector<TaskUpdate>& updates) const;

  TaskOperationProcessorOptions options_;
  core::Executor executor_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, TaskRecord> tasks_;
  std::vector<std::string> order_;
  std::uint64_t next_task_index_ = 1;
};

}  // namespace mcp::server
