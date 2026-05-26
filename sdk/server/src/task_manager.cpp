// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/server/task_manager.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <iomanip>
#include <sstream>
#include <utility>

namespace mcp::server {

namespace {

core::Error make_task_error(protocol::ErrorCode code, std::string message,
                            std::string detail = {}) {
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail)};
}

}  // namespace

TaskOperationProcessor::TaskOperationProcessor(
    TaskOperationProcessorOptions options)
    : options_(std::move(options)),
      executor_(options_.worker_count, options_.queue_size) {}

void TaskOperationProcessor::stop() noexcept { executor_.stop(); }

std::string TaskOperationProcessor::now_timestamp() {
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

bool TaskOperationProcessor::is_terminal(protocol::TaskStatus status) noexcept {
  return status == protocol::TaskStatus::Completed ||
         status == protocol::TaskStatus::Failed ||
         status == protocol::TaskStatus::Cancelled;
}

std::string TaskOperationProcessor::make_task_id() {
  return "task-" + std::to_string(next_task_index_++);
}

core::Error TaskOperationProcessor::task_not_found_error(
    std::string_view task_id) const {
  return make_task_error(protocol::ErrorCode::InvalidRequest, "task not found",
                         std::string(task_id));
}

TaskOperationProcessor::TaskRecord* TaskOperationProcessor::find_task_locked(
    std::string_view task_id) {
  const auto it = tasks_.find(std::string(task_id));
  if (it == tasks_.end()) {
    return nullptr;
  }
  return &it->second;
}

const TaskOperationProcessor::TaskRecord*
TaskOperationProcessor::find_task_locked(std::string_view task_id) const {
  const auto it = tasks_.find(std::string(task_id));
  if (it == tasks_.end()) {
    return nullptr;
  }
  return &it->second;
}

void TaskOperationProcessor::refresh_locked() {
  const auto now = std::chrono::steady_clock::now();
  const auto timestamp = now_timestamp();
  for (auto& [task_id, record] : tasks_) {
    (void)task_id;
    if (record.task.status != protocol::TaskStatus::Working ||
        !record.timeout.has_value()) {
      continue;
    }
    if (now - record.started_at >= *record.timeout) {
      record.cancellation.cancel();
      record.task.status = protocol::TaskStatus::Failed;
      record.task.status_message = "Operation timed out";
      record.task.last_updated_at = timestamp;
      record.terminal_at = now;
      record.failure =
          make_task_error(protocol::ErrorCode::InternalError,
                          "Operation timed out", record.task.task_id);
      record.result.reset();
    }
  }
  trim_completed_locked();
}

void TaskOperationProcessor::trim_completed_locked() {
  const auto now = std::chrono::steady_clock::now();
  if (options_.completed_task_ttl.has_value()) {
    auto it = order_.begin();
    while (it != order_.end()) {
      auto record_it = tasks_.find(*it);
      if (record_it != tasks_.end() &&
          is_terminal(record_it->second.task.status) &&
          record_it->second.terminal_at.has_value() &&
          now - *record_it->second.terminal_at >=
              *options_.completed_task_ttl) {
        tasks_.erase(record_it);
        it = order_.erase(it);
      } else {
        ++it;
      }
    }
  }

  std::size_t terminal_count = 0;
  for (const auto& task_id : order_) {
    const auto* record = find_task_locked(task_id);
    if (record != nullptr && is_terminal(record->task.status)) {
      ++terminal_count;
    }
  }

  auto it = order_.begin();
  while (terminal_count > options_.max_completed_tasks && it != order_.end()) {
    auto record_it = tasks_.find(*it);
    if (record_it != tasks_.end() &&
        is_terminal(record_it->second.task.status)) {
      tasks_.erase(record_it);
      it = order_.erase(it);
      --terminal_count;
    } else {
      ++it;
    }
  }
}

void TaskOperationProcessor::finish_task(std::string task_id,
                                         core::Result<protocol::Json> result) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* record = find_task_locked(task_id);
  if (record == nullptr ||
      record->task.status != protocol::TaskStatus::Working) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  record->task.last_updated_at = now_timestamp();
  record->terminal_at = now;
  if (result) {
    record->task.status = protocol::TaskStatus::Completed;
    record->task.status_message.reset();
    record->result = *result;
    record->failure.reset();
  } else {
    record->task.status = protocol::TaskStatus::Failed;
    record->task.status_message = result.error().message;
    record->failure = result.error();
    record->result.reset();
  }
  trim_completed_locked();
}

core::Result<protocol::CreateTaskResult>
TaskOperationProcessor::submit_operation(TaskOperationDescriptor descriptor,
                                         TaskOperationHandler operation) {
  if (!operation) {
    return std::unexpected(make_task_error(
        protocol::ErrorCode::InvalidRequest,
        "task operation handler must be callable", descriptor.name));
  }

  protocol::Task task;
  task.status = protocol::TaskStatus::Working;
  task.created_at = now_timestamp();
  task.last_updated_at = task.created_at;
  task.poll_interval = options_.poll_interval;
  std::optional<std::chrono::seconds> timeout = options_.default_timeout;
  if (descriptor.task.has_value() && descriptor.task->ttl.has_value()) {
    if (*descriptor.task->ttl < 0) {
      return std::unexpected(
          make_task_error(protocol::ErrorCode::InvalidRequest,
                          "task ttl must be non-negative", descriptor.name));
    }
    task.ttl = *descriptor.task->ttl;
    timeout = std::chrono::seconds(*descriptor.task->ttl);
  } else {
    task.ttl = options_.default_timeout.count();
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    task.task_id = make_task_id();
    CancellationSource cancellation;
    TaskRecord record;
    record.task = task;
    record.started_at = std::chrono::steady_clock::now();
    record.terminal_at = std::nullopt;
    record.timeout = timeout;
    record.cancellation = cancellation;
    tasks_.emplace(task.task_id, std::move(record));
    order_.push_back(task.task_id);
  }

  const auto task_id = task.task_id;
  const auto cancellation = [&]() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* record = find_task_locked(task_id);
    return record == nullptr ? CancellationToken{}
                             : record->cancellation.token();
  }();
  const auto queued = executor_.enqueue([this, operation = std::move(operation),
                                         task_id, cancellation]() mutable {
    try {
      auto outcome = operation(cancellation);
      finish_task(task_id, std::move(outcome));
    } catch (const std::exception& exception) {
      finish_task(task_id,
                  std::unexpected(make_task_error(
                      protocol::ErrorCode::InternalError,
                      "task operation threw an exception", exception.what())));
    } catch (...) {
      finish_task(task_id, std::unexpected(make_task_error(
                               protocol::ErrorCode::InternalError,
                               "task operation threw an unknown exception")));
    }
  });
  if (!queued) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.erase(task.task_id);
    order_.erase(std::remove(order_.begin(), order_.end(), task.task_id),
                 order_.end());
    return std::unexpected(queued.error());
  }

  protocol::CreateTaskResult result;
  result.task = std::move(task);
  return result;
}

core::Result<protocol::CreateTaskResult>
TaskOperationProcessor::submit_tool_call(const ToolRegistry& tools,
                                         protocol::ToolCall call,
                                         const SessionContext& context) {
  TaskOperationDescriptor descriptor;
  descriptor.name = call.name;
  descriptor.task = call.task;
  return submit_operation(
      std::move(descriptor),
      [&tools, call = std::move(call),
       context](const CancellationToken& cancellation) mutable
          -> core::Result<protocol::Json> {
        auto result = tools.call(std::move(call), context, cancellation);
        if (!result) {
          return std::unexpected(result.error());
        }
        return protocol::tool_result_to_json(*result);
      });
}

core::Result<protocol::TaskListResult> TaskOperationProcessor::list_tasks(
    const protocol::TaskListParams& params) {
  (void)params;
  std::lock_guard<std::mutex> lock(mutex_);
  refresh_locked();

  protocol::TaskListResult result;
  result.tasks.reserve(order_.size());
  for (const auto& task_id : order_) {
    const auto* record = find_task_locked(task_id);
    if (record != nullptr) {
      result.tasks.push_back(record->task);
    }
  }
  result.total = static_cast<std::int64_t>(result.tasks.size());
  return result;
}

core::Result<protocol::Task> TaskOperationProcessor::get_task(
    const protocol::TaskGetParams& params) {
  std::lock_guard<std::mutex> lock(mutex_);
  refresh_locked();
  const auto* record = find_task_locked(params.task_id);
  if (record == nullptr) {
    return std::unexpected(task_not_found_error(params.task_id));
  }
  return record->task;
}

core::Result<protocol::Task> TaskOperationProcessor::cancel_task(
    const protocol::TaskCancelParams& params) {
  std::lock_guard<std::mutex> lock(mutex_);
  refresh_locked();
  auto* record = find_task_locked(params.task_id);
  if (record == nullptr) {
    return std::unexpected(task_not_found_error(params.task_id));
  }
  if (is_terminal(record->task.status)) {
    return record->task;
  }
  record->cancellation.cancel();
  record->task.status = protocol::TaskStatus::Cancelled;
  record->task.status_message = "Operation cancelled";
  record->task.last_updated_at = now_timestamp();
  record->terminal_at = std::chrono::steady_clock::now();
  record->failure = make_task_error(protocol::ErrorCode::InternalError,
                                    "Operation cancelled", params.task_id);
  record->result.reset();
  trim_completed_locked();
  return record->task;
}

core::Result<protocol::Json> TaskOperationProcessor::task_result(
    const protocol::TaskResultParams& params) {
  std::lock_guard<std::mutex> lock(mutex_);
  refresh_locked();
  const auto* record = find_task_locked(params.task_id);
  if (record == nullptr) {
    return std::unexpected(task_not_found_error(params.task_id));
  }
  if (record->result.has_value()) {
    return *record->result;
  }
  if (record->failure.has_value()) {
    return std::unexpected(*record->failure);
  }
  return std::unexpected(make_task_error(protocol::ErrorCode::InvalidRequest,
                                         "task result is not available",
                                         params.task_id));
}

}  // namespace mcp::server
