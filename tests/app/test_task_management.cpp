// Copyright (c) 2025 [caomengxuan666]

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "cxxmcp/app/task_management.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void test_enqueue_task_completes_and_exposes_result() {
  mcp::app::TaskManagementService service;

  const auto created = service.enqueue_task([]() -> mcp::core::Result<Json> {
    return Json{{"value", 42}};
  });
  require(created.has_value(), "enqueue_task failed");

  for (int i = 0; i < 200; ++i) {
    const auto task = service.get_task(created->task_id);
    require(task.has_value(), "get_task should succeed");
    if (task->status == mcp::protocol::TaskStatus::Completed) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const auto task = service.get_task(created->task_id);
  require(task.has_value(), "completed task should exist");
  require(task->status == mcp::protocol::TaskStatus::Completed,
          "task should complete");

  const auto result = service.get_task_result(created->task_id);
  require(result.has_value(), "task result should be available");
  require(result->at("value") == 42, "task result payload mismatch");
}

void test_cancel_task_keeps_cancelled_status() {
  mcp::app::TaskManagementService service({
      .worker_count = 1,
      .queue_size = 8,
  });

  std::mutex mutex;
  std::condition_variable cv;
  bool started = false;
  bool release = false;

  const auto created = service.enqueue_task([&]() -> mcp::core::Result<Json> {
    std::unique_lock<std::mutex> lock(mutex);
    started = true;
    cv.notify_all();
    cv.wait(lock, [&]() { return release; });
    return Json{{"value", 1}};
  });
  require(created.has_value(), "enqueue_task should succeed");

  const auto cancelled = service.cancel_task(created->task_id);
  require(cancelled.has_value(), "cancel_task should succeed");
  require(cancelled->status == mcp::protocol::TaskStatus::Cancelled,
          "cancelled task status mismatch");

  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&]() { return started; });
    release = true;
  }
  cv.notify_all();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  const auto task = service.get_task(created->task_id);
  require(task.has_value(), "cancelled task should exist");
  require(task->status == mcp::protocol::TaskStatus::Cancelled,
          "cancelled task should stay cancelled");

  const auto result = service.get_task_result(created->task_id);
  require(!result.has_value(), "cancelled task should not expose result");
}

void test_enqueue_task_failure_cleans_up_record() {
  mcp::app::TaskManagementService service({
      .worker_count = 1,
      .queue_size = 0,
  });

  const auto created = service.enqueue_task([]() -> mcp::core::Result<Json> {
    return Json{{"value", 1}};
  });
  require(!created.has_value(), "enqueue_task should fail when queue is full");

  const auto tasks = service.list_tasks();
  require(tasks.empty(), "failed enqueue should not leave behind a task record");
}

}  // namespace

int main() {
  test_enqueue_task_completes_and_exposes_result();
  test_cancel_task_keeps_cancelled_status();
  test_enqueue_task_failure_cleans_up_record();
  return 0;
}
