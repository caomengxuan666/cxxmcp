// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"

namespace mcp::core {

/// @brief A thread-safe, CV-based future that replaces
/// std::promise/shared_future.
///
/// AsyncResult<T> provides condition-variable-based waiting (no polling),
/// cancellation support, and optional continuation chaining via then().
///
/// @tparam T The value type. The result is always stored as Result<T>.
template <class T>
class AsyncResult {
 public:
  using ResultType = Result<T>;

  AsyncResult() = default;

  AsyncResult(const AsyncResult&) = delete;
  AsyncResult& operator=(const AsyncResult&) = delete;

  /// @brief Set the result value. Notifies all waiters and fires continuations.
  /// Must be called at most once.
  void set_value(T value) {
    std::vector<std::function<void(ResultType)>> conts;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value_.has_value()) return;  // already set
      value_.emplace(std::move(value));
      conts = std::move(continuations_);
    }
    cv_.notify_all();
    fire_continuations(conts, *value_);
  }

  /// @brief Set an error result. Notifies all waiters and fires continuations.
  void set_error(Error error) {
    std::vector<std::function<void(ResultType)>> conts;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value_.has_value()) return;
      value_.emplace(unexpected(std::move(error)));
      conts = std::move(continuations_);
    }
    cv_.notify_all();
    fire_continuations(conts, *value_);
  }

  /// @brief Cancel the result with a cancellation error. Notifies all waiters.
  void cancel(std::string reason = {}) {
    std::vector<std::function<void(ResultType)>> conts;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value_.has_value()) return;
      value_.emplace(
          unexpected(Error{1,
                           reason.empty() ? "cancelled" : std::move(reason),
                           {},
                           "cancellation"}));
      conts = std::move(continuations_);
    }
    cv_.notify_all();
    fire_continuations(conts, *value_);
  }

  /// @brief Set the result from an exception_ptr.
  void set_exception(std::exception_ptr ex) {
    try {
      std::rethrow_exception(ex);
    } catch (const std::exception& e) {
      set_error(Error{1, e.what(), {}, "exception"});
    } catch (...) {
      set_error(Error{1, "unknown exception", {}, "exception"});
    }
  }

  /// @brief Block until the result is available. CV-based, no polling.
  ResultType wait() const {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return value_.has_value(); });
    return *value_;
  }

  /// @brief Block until the result is available or the timeout expires.
  ResultType wait_for(std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout,  // NOLINT(whitespace/newline)
                      [this]() { return value_.has_value(); })) {
      return unexpected(Error{1, "AsyncResult wait timed out", {}, "timeout"});
    }
    return *value_;
  }

  /// @brief Block until the result is available or the deadline is reached.
  ResultType wait_until(std::chrono::steady_clock::time_point deadline) const {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_until(lock, deadline,
                        [this]() { return value_.has_value(); })) {
      return unexpected(Error{1, "AsyncResult wait timed out", {}, "timeout"});
    }
    return *value_;
  }

  /// @brief Returns true if the result has been set.
  bool ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_.has_value();
  }

  /// @brief Chain a continuation that fires when the result is set.
  ///
  /// If the result is already set, the callback fires immediately on the
  /// calling thread. Otherwise it is stored and invoked on the thread that
  /// calls set_value/set_error/cancel.
  ///
  /// @tparam F Callable with signature R(Result<T>).
  /// @return A new AsyncResult holding the callback's return value.
  template <class F>
  auto then(F&& callback)
      -> std::shared_ptr<AsyncResult<std::invoke_result_t<F, ResultType>>> {
    using R = std::invoke_result_t<F, ResultType>;
    auto next = std::make_shared<AsyncResult<R>>();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value_.has_value()) {
        // Already resolved -- fire immediately
        try {
          next->set_value(callback(*value_));
        } catch (...) {
          next->set_exception(std::current_exception());
        }
        return next;
      }
      continuations_.emplace_back(
          [next, cb = std::forward<F>(callback)](ResultType result) mutable {
            try {
              next->set_value(cb(std::move(result)));
            } catch (...) {
              next->set_exception(std::current_exception());
            }
          });
    }
    return next;
  }

 private:
  static void fire_continuations(
      const std::vector<std::function<void(ResultType)>>& conts,
      const ResultType& value) {
    for (auto& cont : conts) {
      try {
        cont(value);
      } catch (...) {
        // continuation exceptions are swallowed; the result is already set
      }
    }
  }

  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  std::optional<ResultType> value_;
  std::vector<std::function<void(ResultType)>> continuations_;
};

/// @brief Void specialization of AsyncResult.
template <>
class AsyncResult<void> {
 public:
  using ResultType = Result<Unit>;

  AsyncResult() = default;

  AsyncResult(const AsyncResult&) = delete;
  AsyncResult& operator=(const AsyncResult&) = delete;

  void set_value() {
    std::vector<std::function<void(ResultType)>> conts;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value_.has_value()) return;
      value_.emplace(Unit{});
      conts = std::move(continuations_);
    }
    cv_.notify_all();
    for (auto& cont : conts) {
      try {
        cont(*value_);
      } catch (...) {
      }
    }
  }

  void set_error(Error error) {
    std::vector<std::function<void(ResultType)>> conts;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value_.has_value()) return;
      value_.emplace(unexpected(std::move(error)));
      conts = std::move(continuations_);
    }
    cv_.notify_all();
    for (auto& cont : conts) {
      try {
        cont(*value_);
      } catch (...) {
      }
    }
  }

  void cancel(std::string reason = {}) {
    std::vector<std::function<void(ResultType)>> conts;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value_.has_value()) return;
      value_.emplace(
          unexpected(Error{1,
                           reason.empty() ? "cancelled" : std::move(reason),
                           {},
                           "cancellation"}));
      conts = std::move(continuations_);
    }
    cv_.notify_all();
    for (auto& cont : conts) {
      try {
        cont(*value_);
      } catch (...) {
      }
    }
  }

  ResultType wait() const {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return value_.has_value(); });
    return *value_;
  }

  ResultType wait_for(std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout,  // NOLINT(whitespace/newline)
                      [this]() { return value_.has_value(); })) {
      return unexpected(Error{1, "AsyncResult wait timed out", {}, "timeout"});
    }
    return *value_;
  }

  ResultType wait_until(std::chrono::steady_clock::time_point deadline) const {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_until(lock, deadline,
                        [this]() { return value_.has_value(); })) {
      return unexpected(Error{1, "AsyncResult wait timed out", {}, "timeout"});
    }
    return *value_;
  }

  bool ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_.has_value();
  }

  template <class F>
  auto then(F&& callback)
      -> std::shared_ptr<AsyncResult<std::invoke_result_t<F, ResultType>>> {
    using R = std::invoke_result_t<F, ResultType>;
    auto next = std::make_shared<AsyncResult<R>>();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value_.has_value()) {
        try {
          next->set_value(callback(*value_));
        } catch (...) {
          next->set_exception(std::current_exception());
        }
        return next;
      }
      continuations_.emplace_back(
          [next, cb = std::forward<F>(callback)](ResultType result) mutable {
            try {
              next->set_value(cb(std::move(result)));
            } catch (...) {
              next->set_exception(std::current_exception());
            }
          });
    }
    return next;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  std::optional<ResultType> value_;
  std::vector<std::function<void(ResultType)>> continuations_;
};

}  // namespace mcp::core
