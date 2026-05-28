// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Cooperative cancellation primitives shared by SDK lifecycle APIs.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

namespace mcp {

/// @brief Shared state backing a cancellation token/source pair.
///
/// The atomic flag provides a fast-path check for cancelled(). The mutex and
/// condition variable are only used when wait_for_cancel() is called. This
/// keeps the common path (polling cancelled()) allocation-free after the
/// initial CancellationState construction.
struct CancellationState {
  std::atomic_bool cancelled{false};
  std::mutex mutex;
  std::condition_variable cv;
};

/// @brief Copyable token observed by cancellation-aware SDK operations.
class CancellationToken {
 public:
  /// @brief Constructs a token with its own private cancellation state.
  CancellationToken() : state_(std::make_shared<CancellationState>()) {}

  /// @brief Returns true after the associated source has requested
  /// cancellation.
  bool cancelled() const noexcept {
    return state_ != nullptr &&
           state_->cancelled.load(std::memory_order_acquire);
  }

  /// @brief Block until cancellation is requested. Returns true when
  /// cancelled. CV-based, no polling.
  bool wait_for_cancel() const {
    if (!state_) return true;
    // Fast path: already cancelled
    if (state_->cancelled.load(std::memory_order_acquire)) return true;
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->cv.wait(lock, [this]() {
      return state_->cancelled.load(std::memory_order_acquire);
    });
    return true;
  }

  /// @brief Block until cancellation is requested or timeout expires.
  /// Returns true if cancelled, false if timeout.
  bool wait_for_cancel(std::chrono::milliseconds timeout) const {
    if (!state_) return true;
    if (state_->cancelled.load(std::memory_order_acquire)) return true;
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (state_->cv.wait_for(lock, timeout, [this]() {
          return state_->cancelled.load(std::memory_order_acquire);
        })) {
      return true;
    }
    return false;
  }

  /// @brief Block until cancellation is requested or deadline is reached.
  /// Returns true if cancelled, false if deadline passed.
  bool wait_for_cancel(std::chrono::steady_clock::time_point deadline) const {
    if (!state_) return true;
    if (state_->cancelled.load(std::memory_order_acquire)) return true;
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (state_->cv.wait_until(lock, deadline, [this]() {
          return state_->cancelled.load(std::memory_order_acquire);
        })) {
      return true;
    }
    return false;
  }

 private:
  friend class CancellationSource;

  explicit CancellationToken(std::shared_ptr<CancellationState> state)
      : state_(std::move(state)) {}

  std::shared_ptr<CancellationState> state_;
};

/// @brief Owner side of a cooperative cancellation token.
class CancellationSource {
 public:
  CancellationSource() : state_(std::make_shared<CancellationState>()) {}

  /// @brief Returns a token sharing this source's cancellation state.
  CancellationToken token() const noexcept { return CancellationToken(state_); }

  /// @brief Requests cancellation for all tokens created from this source.
  /// Sets the atomic flag and notifies all threads blocked in
  /// wait_for_cancel().
  void cancel() const noexcept {
    if (state_ != nullptr) {
      state_->cancelled.store(true, std::memory_order_release);
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->cv.notify_all();
    }
  }

  /// @brief Returns true when cancellation has been requested.
  bool cancelled() const noexcept {
    return state_ != nullptr &&
           state_->cancelled.load(std::memory_order_acquire);
  }

 private:
  std::shared_ptr<CancellationState> state_;
};

}  // namespace mcp
