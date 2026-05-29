// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/core/timer_handle.hpp"

namespace mcp::core {

/// @brief Task priority levels for the executor's priority queues.
///
/// IO_BOUND tasks are dispatched first and are intended for blocking transport
/// calls. DEFAULT is for normal request processing. BACKGROUND is for
/// low-priority work such as cancellation watchers and cleanup.
enum class TaskPriority { IO_BOUND = 0, DEFAULT = 1, BACKGROUND = 2 };

class Executor;

/// @brief Options for the Executor class.
struct ExecutorOptions {
  std::size_t worker_count = 4;
  std::size_t max_queue_size = 256;
  std::function<void(std::exception_ptr)> exception_handler;
};

/// @brief Thread pool executor with priority queues and a timer wheel.
///
/// The executor owns a fixed number of worker threads and a dedicated timer
/// thread. Worker threads dequeue tasks from three priority-ordered queues.
/// The timer thread sleeps until the next scheduled timer fires and moves due
/// tasks into the appropriate priority queue.
///
/// @code
/// Executor exec(ExecutorOptions{4, 256});
/// exec.post([]{ do_work(); }, TaskPriority::DEFAULT);
/// exec.post_after(100ms, []{ delayed(); });
/// exec.shutdown(); // drain and join
/// @endcode
class Executor {
 public:
  using ExceptionHandler = std::function<void(std::exception_ptr)>;
  using Options = ExecutorOptions;

  explicit Executor(Options options = {}) : options_(std::move(options)) {
    if (options_.worker_count == 0) {
      options_.worker_count = 1;
    }
    for (std::size_t i = 0; i < options_.worker_count; ++i) {
      workers_.emplace_back([this]() { worker_loop(); });
    }
    timer_thread_ = std::thread([this]() { timer_loop(); });
  }

  /// @brief Backward-compatible constructor matching BoundedExecutor's
  /// signature.
  Executor(std::size_t worker_count, std::size_t max_queue_size)
      : Executor(worker_count, max_queue_size, ExceptionHandler{}) {}

  /// @brief Backward-compatible constructor matching BoundedExecutor's
  /// signature.
  Executor(std::size_t worker_count, std::size_t max_queue_size,
           ExceptionHandler exception_handler) {
    options_.worker_count = worker_count;
    options_.max_queue_size = max_queue_size;
    options_.exception_handler = std::move(exception_handler);
    if (options_.worker_count == 0) {
      options_.worker_count = 1;
    }
    for (std::size_t i = 0; i < options_.worker_count; ++i) {
      workers_.emplace_back([this]() { worker_loop(); });
    }
    timer_thread_ = std::thread([this]() { timer_loop(); });
  }

  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  ~Executor() { shutdown(); }

  /// @brief Backward-compatible enqueue (equivalent to post with DEFAULT
  /// priority).
  core::Result<Unit> enqueue(std::function<void()> task) {
    return post(std::move(task), TaskPriority::DEFAULT);
  }

  /// @brief Post a task for execution at the given priority.
  core::Result<Unit> post(std::function<void()> task,
                          TaskPriority prio = TaskPriority::DEFAULT) {
    if (!task) {
      return core::unexpected(
          Error{1, "executor task must be callable", {}, "executor"});
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return core::unexpected(
            Error{1, "executor is stopped", {}, "executor"});
      }
      const auto idx = static_cast<int>(prio);
      if (queues_[idx].size() >= options_.max_queue_size) {
        return core::unexpected(
            Error{1, "executor queue is full", {}, "executor"});
      }
      queues_[idx].push_back(std::move(task));
    }
    work_cv_.notify_one();
    return Unit{};
  }

  /// @brief Schedule a task to run after a delay.
  TimerHandle post_after(std::chrono::milliseconds delay,
                         std::function<void()> task,
                         TaskPriority prio = TaskPriority::DEFAULT) {
    if (!task || delay.count() <= 0) {
      // Fire immediately if no delay
      if (task) {
        (void)post(std::move(task), prio);
      }
      return TimerHandle{};
    }
    return post_at(std::chrono::steady_clock::now() + delay, std::move(task),
                   prio);
  }

  /// @brief Schedule a task to run at an absolute time point.
  TimerHandle post_at(std::chrono::steady_clock::time_point when,
                      std::function<void()> task,
                      TaskPriority prio = TaskPriority::DEFAULT) {
    if (!task) {
      return TimerHandle{};
    }
    auto entry = std::make_shared<TimerEntry>();
    entry->when = when;
    entry->task = std::move(task);
    entry->id = next_timer_id_++;
    TimerHandle handle(entry);
    {
      std::lock_guard<std::mutex> lock(timer_mutex_);
      timers_.push(entry);
    }
    timer_cv_.notify_one();
    return handle;
  }

  /// @brief Graceful shutdown: complete all queued tasks, then join threads.
  void shutdown() noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      stopping_ = true;
      drain_mode_ = true;
    }
    // Wake workers to drain remaining tasks
    work_cv_.notify_all();
    {
      std::lock_guard<std::mutex> lock(timer_mutex_);
      // Clear pending timers so timer thread can exit without dispatching
      // stale tasks into a shutting-down executor.
      decltype(timers_) empty;
      timers_.swap(empty);
    }
    timer_cv_.notify_all();

    for (auto& w : workers_) {
      if (w.joinable()) {
        w.join();
      }
    }
    workers_.clear();
    if (timer_thread_.joinable()) {
      timer_thread_.join();
    }
  }

  /// @brief Backward-compatible stop (equivalent to shutdown).
  void stop() { shutdown(); }

  /// @brief Cancel pending tasks and stop immediately.
  void cancel_and_stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      stopping_ = true;
      for (auto& q : queues_) {
        q.clear();
      }
    }
    work_cv_.notify_all();
    {
      std::lock_guard<std::mutex> lock(timer_mutex_);
      // Pop all pending timers so the timer thread wakes and exits.
      while (!timers_.empty()) {
        timers_.pop();
      }
    }
    timer_cv_.notify_all();

    for (auto& w : workers_) {
      if (w.joinable()) {
        w.join();
      }
    }
    workers_.clear();
    if (timer_thread_.joinable()) {
      timer_thread_.join();
    }
  }

 private:
  void worker_loop() {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        work_cv_.wait(lock, [this]() {
          return stopping_ || drain_mode_ || has_any_task();
        });
        if (stopping_ && !has_any_task()) {
          return;
        }
        if (drain_mode_ && !has_any_task()) {
          return;
        }
        task = dequeue_highest_priority();
      }
      try {
        task();
      } catch (...) {
        if (options_.exception_handler) {
          options_.exception_handler(std::current_exception());
        }
      }
    }
  }

  void timer_loop() {
    while (true) {
      std::shared_ptr<TimerEntry> next;
      {
        std::unique_lock<std::mutex> lock(timer_mutex_);
        // Purge cancelled entries from the top
        while (!timers_.empty() &&
               timers_.top()->cancelled.load(std::memory_order_acquire)) {
          timers_.pop();
        }
        if (timers_.empty()) {
          timer_cv_.wait(lock,
                         [this]() { return stopping_ || !timers_.empty(); });
          if (stopping_) {
            return;
          }
        } else {
          auto deadline = timers_.top()->when;
          timer_cv_.wait_until(lock, deadline, [this, deadline]() {
            return stopping_ ||
                   (timers_.empty() ? true : timers_.top()->when != deadline);
          });
          if (stopping_) {
            return;
          }
        }
        // Purge cancelled entries again after waking
        while (!timers_.empty() &&
               timers_.top()->cancelled.load(std::memory_order_acquire)) {
          timers_.pop();
        }
        if (timers_.empty()) {
          continue;
        }
        auto top = timers_.top();
        if (top->when > std::chrono::steady_clock::now()) {
          continue;  // Not yet due, loop back to wait_until
        }
        timers_.pop();
        next = std::move(top);
      }
      // Dispatch the timer task into the appropriate priority queue
      if (next && next->task) {
        (void)post(std::move(next->task), TaskPriority::DEFAULT);
      }
    }
  }

  bool has_any_task() const {
    for (const auto& q : queues_) {
      if (!q.empty()) return true;
    }
    return false;
  }

  std::function<void()> dequeue_highest_priority() {
    for (auto& q : queues_) {
      if (!q.empty()) {
        auto task = std::move(q.front());
        q.pop_front();
        return task;
      }
    }
    return nullptr;
  }

  struct TimerEntryCompare {
    bool operator()(const std::shared_ptr<TimerEntry>& a,
                    const std::shared_ptr<TimerEntry>& b) const {
      return a->when > b->when;  // min-heap: earliest first
    }
  };

  Options options_;
  std::mutex mutex_;
  std::condition_variable work_cv_;
  std::deque<std::function<void()>> queues_[3];
  std::vector<std::thread> workers_;
  std::atomic<bool> stopping_ = false;
  std::atomic<bool> drain_mode_ = false;

  std::mutex timer_mutex_;
  std::condition_variable timer_cv_;
  std::priority_queue<std::shared_ptr<TimerEntry>,
                      std::vector<std::shared_ptr<TimerEntry>>,
                      TimerEntryCompare>
      timers_;
  std::thread timer_thread_;
  std::uint64_t next_timer_id_ = 1;
};

/// @brief Deprecated alias for backward compatibility.
using BoundedExecutor = Executor;

}  // namespace mcp::core
