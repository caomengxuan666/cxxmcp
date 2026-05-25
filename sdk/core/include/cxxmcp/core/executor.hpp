// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"

namespace mcp::core {

/// @brief Small bounded worker executor used by the SDK for background tasks.
class BoundedExecutor {
 public:
  explicit BoundedExecutor(std::size_t worker_count = 4,
                           std::size_t max_queue_size = 64)
      : max_queue_size_(max_queue_size) {
    worker_count = worker_count == 0 ? 1 : worker_count;
    for (std::size_t i = 0; i < worker_count; ++i) {
      workers_.emplace_back([this]() { worker_loop(); });
    }
  }

  BoundedExecutor(const BoundedExecutor&) = delete;
  BoundedExecutor& operator=(const BoundedExecutor&) = delete;

  ~BoundedExecutor() { stop(); }

  /// @brief Enqueue a task for background execution.
  core::Result<Unit> enqueue(std::function<void()> task) {
    if (!task) {
      return std::unexpected(Error{
          1,
          "executor task must be callable",
          {},
      });
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return std::unexpected(Error{
            1,
            "executor is stopped",
            {},
        });
      }
      if (tasks_.size() >= max_queue_size_) {
        return std::unexpected(Error{
            1,
            "executor queue is full",
            {},
        });
      }
      tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
    return Unit{};
  }

  /// @brief Stop accepting tasks and drain the worker threads.
  void stop() noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      stopping_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    workers_.clear();
  }

 private:
  void worker_loop() {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
        if (stopping_ && tasks_.empty()) {
          return;
        }
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      try {
        task();
      } catch (...) {
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
  std::vector<std::thread> workers_;
  std::size_t max_queue_size_;
  bool stopping_ = false;
};

}  // namespace mcp::core
