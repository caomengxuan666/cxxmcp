// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/core/executor.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/task.hpp"

namespace mcp::app {

/// @brief Options for the in-memory task manager.
struct TaskManagementOptions {
  std::size_t worker_count = 4;
  std::size_t queue_size = 64;
};

/// @brief Minimal in-memory task lifecycle manager for long-running jobs.
class TaskManagementService final {
 public:
  explicit TaskManagementService(TaskManagementOptions options = {});

  core::Result<protocol::Task> enqueue_task(
      std::function<core::Result<protocol::Json>()> job,
      std::optional<std::int64_t> ttl = std::nullopt,
      std::optional<std::int64_t> poll_interval = std::nullopt);

  std::vector<protocol::Task> list_tasks() const;

  core::Result<protocol::Task> get_task(std::string_view task_id) const;

  core::Result<protocol::Json> get_task_result(std::string_view task_id) const;

  core::Result<protocol::Task> cancel_task(std::string_view task_id);

 private:
  struct TaskRecord {
    protocol::Task task;
    std::optional<protocol::Json> result;
    std::optional<core::Error> failure;
  };

  static std::string now_timestamp();
  static std::string make_task_id(std::size_t index);

  std::vector<TaskRecord>::iterator find_task(std::string_view task_id);
  std::vector<TaskRecord>::const_iterator find_task(
      std::string_view task_id) const;

  core::BoundedExecutor executor_;
  std::size_t next_task_index_ = 1;
  mutable std::mutex mutex_;
  std::vector<TaskRecord> tasks_;
};

}  // namespace mcp::app
