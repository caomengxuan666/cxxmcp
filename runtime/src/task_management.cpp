// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/app/task_management.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace mcp::app {

namespace {

core::Error make_task_error(std::string message, std::string detail = {}) {
  return core::Error{1, std::move(message), std::move(detail)};
}

}  // namespace

TaskManagementService::TaskManagementService(TaskManagementOptions options)
    : executor_(options.worker_count, options.queue_size) {}

std::string TaskManagementService::now_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::string TaskManagementService::make_task_id(std::size_t index) {
  return "task-" + std::to_string(index);
}

std::vector<TaskManagementService::TaskRecord>::iterator
TaskManagementService::find_task(std::string_view task_id) {
  return std::find_if(
      tasks_.begin(), tasks_.end(),
      [&](const TaskRecord& task) { return task.task.task_id == task_id; });
}

std::vector<TaskManagementService::TaskRecord>::const_iterator
TaskManagementService::find_task(std::string_view task_id) const {
  return std::find_if(
      tasks_.begin(), tasks_.end(),
      [&](const TaskRecord& task) { return task.task.task_id == task_id; });
}

core::Result<protocol::Task> TaskManagementService::enqueue_task(
    std::function<core::Result<protocol::Json>()> job,
    std::optional<std::int64_t> ttl,
    std::optional<std::int64_t> poll_interval) {
  protocol::Task task;
  task.status = protocol::TaskStatus::Working;
  task.created_at = now_timestamp();
  task.last_updated_at = task.created_at;
  if (ttl.has_value()) {
    task.ttl = *ttl;
  }
  task.poll_interval = poll_interval;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    task.task_id = make_task_id(next_task_index_++);
    tasks_.push_back(TaskRecord{.task = task});
  }

  const auto queue_result = executor_.enqueue(
      [this, task_id = task.task_id, job = std::move(job)]() mutable {
        const auto outcome = job();
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = find_task(task_id);
        if (it == tasks_.end()) {
          return;
        }
        if (it->task.status == protocol::TaskStatus::Cancelled) {
          return;
        }
        it->task.last_updated_at = now_timestamp();
        if (outcome) {
          it->task.status = protocol::TaskStatus::Completed;
          it->task.status_message.reset();
          it->result = *outcome;
          it->failure.reset();
        } else {
          it->task.status = protocol::TaskStatus::Failed;
          it->task.status_message = outcome.error().message;
          it->failure = outcome.error();
          it->result.reset();
        }
      });
  if (!queue_result) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                                [&](const TaskRecord& record) {
                                  return record.task.task_id == task.task_id;
                                }),
                 tasks_.end());
    return std::unexpected(queue_result.error());
  }

  return task;
}

std::vector<protocol::Task> TaskManagementService::list_tasks() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<protocol::Task> tasks;
  tasks.reserve(tasks_.size());
  for (const auto& record : tasks_) {
    tasks.push_back(record.task);
  }
  return tasks;
}

core::Result<protocol::Task> TaskManagementService::get_task(
    std::string_view task_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = find_task(task_id);
  if (it == tasks_.end()) {
    return std::unexpected(
        make_task_error("task not found", std::string(task_id)));
  }
  return it->task;
}

core::Result<protocol::Json> TaskManagementService::get_task_result(
    std::string_view task_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = find_task(task_id);
  if (it == tasks_.end()) {
    return std::unexpected(
        make_task_error("task not found", std::string(task_id)));
  }
  if (it->result.has_value()) {
    return *it->result;
  }
  if (it->failure.has_value()) {
    return std::unexpected(*it->failure);
  }
  return std::unexpected(
      make_task_error("task result is not available", std::string(task_id)));
}

core::Result<protocol::Task> TaskManagementService::cancel_task(
    std::string_view task_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = find_task(task_id);
  if (it == tasks_.end()) {
    return std::unexpected(
        make_task_error("task not found", std::string(task_id)));
  }
  if (it->task.status == protocol::TaskStatus::Completed ||
      it->task.status == protocol::TaskStatus::Failed ||
      it->task.status == protocol::TaskStatus::Cancelled) {
    return it->task;
  }
  it->task.status = protocol::TaskStatus::Cancelled;
  it->task.status_message = "cancelled";
  it->task.last_updated_at = now_timestamp();
  it->result.reset();
  it->failure.reset();
  return it->task;
}

}  // namespace mcp::app
