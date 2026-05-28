// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace mcp::core {

/// @brief Internal entry for a scheduled timer task.
struct TimerEntry {
  std::chrono::steady_clock::time_point when;
  std::function<void()> task;
  std::atomic<bool> cancelled{false};
  std::uint64_t id = 0;
};

/// @brief Handle to a scheduled timer that can be cancelled before it fires.
///
/// The handle shares ownership of the underlying timer entry. Cancelling the
/// handle prevents the task from executing but does not remove it from the
/// executor's timer heap; the executor skips cancelled entries during dispatch.
class TimerHandle {
 public:
  TimerHandle() = default;

  explicit TimerHandle(std::shared_ptr<TimerEntry> entry)
      : entry_(std::move(entry)) {}

  /// @brief Returns true if this handle references a valid timer entry.
  bool valid() const noexcept { return entry_ != nullptr; }

  /// @brief Cancels the timer. The associated task will not execute.
  void cancel() {
    if (entry_) {
      entry_->cancelled.store(true, std::memory_order_release);
    }
  }

  /// @brief Returns true if the timer has been cancelled.
  bool cancelled() const noexcept {
    return !entry_ || entry_->cancelled.load(std::memory_order_acquire);
  }

 private:
  std::shared_ptr<TimerEntry> entry_;
};

}  // namespace mcp::core
