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
  /// Maximum retained terminal task records.
  std::size_t max_completed_tasks = 128;
};

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

  /// @brief Submit a `tools/call` request for background execution.
  core::Result<protocol::CreateTaskResult> submit_tool_call(
      const ToolRegistry& tools, protocol::ToolCall call,
      const SessionContext& context);

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
    std::chrono::steady_clock::time_point started_at;
    std::optional<std::chrono::seconds> timeout;
    CancellationToken cancellation;
    std::optional<protocol::Json> result;
    std::optional<core::Error> failure;
  };

  static std::string now_timestamp();
  static bool is_terminal(protocol::TaskStatus status) noexcept;

  std::string make_task_id();
  core::Error task_not_found_error(std::string_view task_id) const;
  TaskRecord* find_task_locked(std::string_view task_id);
  const TaskRecord* find_task_locked(std::string_view task_id) const;
  void finish_task(std::string task_id, core::Result<protocol::Json> result);
  void refresh_locked();
  void trim_completed_locked();

  TaskOperationProcessorOptions options_;
  core::BoundedExecutor executor_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, TaskRecord> tasks_;
  std::vector<std::string> order_;
  std::uint64_t next_task_index_ = 1;
};

}  // namespace mcp::server
