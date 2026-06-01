// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Cooperative cancellation primitives shared by SDK lifecycle APIs.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace mcp {

class CancellationToken;

namespace detail {

class CancellationRegistration;

CancellationRegistration register_cancellation_callback(
    CancellationToken token, std::function<void()> callback);

}  // namespace detail

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
  std::size_t next_callback_id = 1;
  std::vector<std::pair<std::size_t, std::function<void()>>> callbacks;
};

namespace detail {

class CancellationRegistration {
 public:
  CancellationRegistration() = default;
  CancellationRegistration(const CancellationRegistration&) = delete;
  CancellationRegistration& operator=(const CancellationRegistration&) = delete;

  CancellationRegistration(CancellationRegistration&& other) noexcept
      : state_(std::move(other.state_)), id_(other.id_) {
    other.id_ = 0;
  }

  CancellationRegistration& operator=(
      CancellationRegistration&& other) noexcept {
    if (this != &other) {
      unregister();
      state_ = std::move(other.state_);
      id_ = other.id_;
      other.id_ = 0;
    }
    return *this;
  }

  ~CancellationRegistration() { unregister(); }

  bool active() const noexcept { return state_ != nullptr && id_ != 0; }

 private:
  friend CancellationRegistration register_cancellation_callback(
      CancellationToken token, std::function<void()> callback);

  CancellationRegistration(std::shared_ptr<CancellationState> state,
                           std::size_t id)
      : state_(std::move(state)), id_(id) {}

  void unregister() noexcept {
    if (!state_ || id_ == 0) {
      return;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (auto iter = state_->callbacks.begin(); iter != state_->callbacks.end();
         ++iter) {
      if (iter->first == id_) {
        state_->callbacks.erase(iter);
        break;
      }
    }
    id_ = 0;
    state_.reset();
  }

  std::shared_ptr<CancellationState> state_;
  std::size_t id_ = 0;
};

}  // namespace detail

/// @brief Copyable token observed by cancellation-aware SDK operations.
class CancellationToken {
 public:
  /// @brief Constructs a detached token that is never cancelled.
  ///
  /// Use this when no cancellation source is needed. The token's cancelled()
  /// always returns false and wait_for_cancel() returns false immediately.
  static CancellationToken none() { return CancellationToken(nullptr); }

  /// @brief Constructs a detached token (same as none()).
  CancellationToken() : CancellationToken(nullptr) {}

  /// @brief Returns true after the associated source has requested
  /// cancellation.
  bool cancelled() const noexcept {
    return state_ != nullptr &&
           state_->cancelled.load(std::memory_order_acquire);
  }

  /// @brief Returns true when this token is attached to a cancellation source.
  bool cancellable() const noexcept { return state_ != nullptr; }

  /// @brief Block until cancellation is requested. Returns true when
  /// cancelled. CV-based, no polling.
  bool wait_for_cancel() const {
    if (!state_) return false;
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
    if (!state_) return false;
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
    if (!state_) return false;
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
  friend detail::CancellationRegistration
  detail::register_cancellation_callback(CancellationToken token,
                                         std::function<void()> callback);

  explicit CancellationToken(std::shared_ptr<CancellationState> state)
      : state_(std::move(state)) {}

  std::shared_ptr<CancellationState> state_;
};

namespace detail {

inline CancellationRegistration register_cancellation_callback(
    CancellationToken token, std::function<void()> callback) {
  if (!callback || !token.state_) {
    return CancellationRegistration{};
  }

  bool run_now = false;
  std::size_t id = 0;
  {
    std::lock_guard<std::mutex> lock(token.state_->mutex);
    if (token.state_->cancelled.load(std::memory_order_acquire)) {
      run_now = true;
    } else {
      id = token.state_->next_callback_id++;
      token.state_->callbacks.emplace_back(id, std::move(callback));
    }
  }

  if (run_now) {
    try {
      callback();
    } catch (...) {
    }
    return CancellationRegistration{};
  }

  return CancellationRegistration{std::move(token.state_), id};
}

}  // namespace detail

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
      const bool already_cancelled =
          state_->cancelled.exchange(true, std::memory_order_acq_rel);
      std::vector<std::function<void()>> callbacks;
      if (!already_cancelled) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        for (auto& callback : state_->callbacks) {
          callbacks.push_back(std::move(callback.second));
        }
        state_->callbacks.clear();
      }
      state_->cv.notify_all();
      for (auto& callback : callbacks) {
        try {
          callback();
        } catch (...) {
        }
      }
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
