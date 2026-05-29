// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Request lifecycle helpers for cancellable SDK calls.

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/core/async_result.hpp"
#include "cxxmcp/core/executor.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp {

/// @brief Configuration for the background request executor.
///
/// The executor is used by RequestHandle::spawn() and by async request helpers
/// that wrap blocking request paths in a background task. Defaults are tuned
/// for local stdio/process-stdio MCP workloads.
struct RequestExecutorOptions {
  std::size_t worker_count = 4;
  std::size_t max_queue_size = 256;
};

namespace detail {

struct RequestExecutorState {
  std::mutex mutex;
  RequestExecutorOptions options;
  std::unique_ptr<core::Executor> executor;
};

inline RequestExecutorState& request_executor_state() {
  static RequestExecutorState state;
  return state;
}

inline core::Error request_executor_error(std::string message,
                                          std::string detail = {}) {
  return core::Error{static_cast<int>(protocol::ErrorCode::InternalError),
                     std::move(message), std::move(detail), "request"};
}

inline core::Executor& request_executor() {
  auto& state = request_executor_state();
  std::lock_guard lock(state.mutex);
  if (!state.executor) {
    state.executor = std::make_unique<core::Executor>(core::Executor::Options{
        state.options.worker_count, state.options.max_queue_size});
  }
  return *state.executor;
}

struct RequestHandleControl {
  mutable std::mutex mutex;
  std::optional<core::Error> terminal_error;
  bool cancel_sent = false;
};

}  // namespace detail

/// @brief Configures the process-wide background request executor.
///
/// This function must be called before the first RequestHandle::spawn() or
/// async request helper initializes the executor. Reconfiguring a live executor
/// is rejected so in-flight request semantics remain stable.
inline core::Result<core::Unit> configure_request_executor(
    RequestExecutorOptions options) {
  if (options.worker_count == 0) {
    return mcp::core::unexpected(detail::request_executor_error(
        "request executor worker_count must be greater than zero"));
  }
  if (options.max_queue_size == 0) {
    return mcp::core::unexpected(detail::request_executor_error(
        "request executor max_queue_size must be greater than zero"));
  }

  auto& state = detail::request_executor_state();
  std::lock_guard lock(state.mutex);
  if (state.executor) {
    return mcp::core::unexpected(detail::request_executor_error(
        "request executor is already initialized"));
  }
  state.options = options;
  return core::Unit{};
}

/// @brief Options for an outbound SDK request.
struct RequestOptions {
  /// Optional timeout applied when awaiting the response.
  std::optional<std::chrono::milliseconds> timeout;

  /// Optional protocol metadata attached to the request envelope.
  std::optional<protocol::Json> meta;

  /// Optional cooperative cancellation token observed while awaiting the
  /// response.
  std::optional<CancellationToken> cancellation_token;

  /// Per-request HTTP headers merged into the transport layer.
  std::unordered_map<std::string, std::string> headers;

  /// Optional override for the MCP-Protocol-Version header sent on this
  /// request.
  std::optional<std::string> protocol_version;
};

template <class T>
class RequestHandle {
 public:
  using ResultType = core::Result<T>;
  using CancelCallback =
      std::function<core::Result<core::Unit>(std::string reason)>;

  RequestHandle() = default;

  /// @brief Creates a handle whose result is already available.
  static RequestHandle ready(protocol::RequestId request_id,
                             ResultType result) {
    auto async_result = std::make_shared<core::AsyncResult<ResultType>>();
    async_result->set_value(std::move(result));
    return RequestHandle(std::move(request_id), std::nullopt, std::nullopt, {},
                         std::move(async_result));
  }

  static RequestHandle spawn(protocol::RequestId request_id,
                             std::optional<std::chrono::milliseconds> timeout,
                             std::optional<CancellationToken> cancellation,
                             CancelCallback cancel,
                             std::function<ResultType()> task) {
    auto async_result = std::make_shared<core::AsyncResult<ResultType>>();
    if (!task) {
      async_result->set_value(
          mcp::core::unexpected(errors::request_task_missing()));
      return RequestHandle(std::move(request_id), std::move(timeout),
                           std::move(cancellation), std::move(cancel),
                           std::move(async_result));
    }

    const auto queued = detail::request_executor().post(
        [async_result, task = std::move(task)]() mutable {
          try {
            async_result->set_value(task());
          } catch (const std::exception& ex) {
            async_result->set_value(mcp::core::unexpected(
                errors::request_worker_exception(ex.what())));
          } catch (...) {
            async_result->set_value(mcp::core::unexpected(
                errors::request_worker_unknown_exception()));
          }
        },
        core::TaskPriority::IO_BOUND);
    if (!queued) {
      async_result->set_value(mcp::core::unexpected(queued.error()));
    }

    auto handle = RequestHandle(std::move(request_id), std::move(timeout),
                                std::move(cancellation), std::move(cancel),
                                std::move(async_result));
    // Start cancellation watcher if a real cancellation source is configured.
    if (handle.cancellation_.has_value() &&
        handle.cancellation_->cancellable()) {
      handle.start_cancellation_watcher();
    }
    return handle;
  }

  const protocol::RequestId& request_id() const noexcept { return request_id_; }

  const std::optional<std::chrono::milliseconds>& timeout() const noexcept {
    return timeout_;
  }

  const std::optional<CancellationToken>& cancellation_token() const noexcept {
    return cancellation_;
  }

  /// @brief Waits for the request result. CV-based, no polling.
  ///
  /// If neither timeout nor cancellation was configured, blocks until the
  /// background task finishes. A configured cancellation token or timeout is
  /// observed: cancellation wakes the waiter immediately via a background
  /// watcher task that calls async_result_->cancel(). Timeout uses
  /// AsyncResult::wait_for() which is CV-based.
  core::Result<T> await_response() const {
    if (!async_result_) {
      return mcp::core::unexpected(errors::request_state_missing());
    }

    if (const auto terminal = terminal_error(); terminal.has_value()) {
      return mcp::core::unexpected(*terminal);
    }

    // Fast path: no timeout, no cancellation
    if (!timeout_.has_value() && !cancellation_.has_value()) {
      return read_from_async_result(async_result_->wait());
    }

    // Timeout-only path
    if (timeout_.has_value() && !cancellation_.has_value()) {
      auto result = async_result_->wait_for(*timeout_);
      if (const auto terminal = terminal_error(); terminal.has_value()) {
        return mcp::core::unexpected(*terminal);
      }
      if (result) {
        return read_from_async_result(*result);
      }
      return fail_terminal("request timeout",
                           errors::request_timed_out(*timeout_));
    }

    // Cancellation path (with optional timeout)
    // The cancellation watcher is already running and will set a terminal
    // error and cancel the async_result_ when the token fires.
    if (timeout_.has_value()) {
      auto result = async_result_->wait_for(*timeout_);
      if (const auto terminal = terminal_error(); terminal.has_value()) {
        return mcp::core::unexpected(*terminal);
      }
      if (result) {
        return read_from_async_result(*result);
      }
      // Check if cancellation fired
      if (cancellation_.has_value() && cancellation_->cancelled()) {
        return fail_terminal("request cancelled", errors::request_cancelled());
      }
      return fail_terminal("request timeout",
                           errors::request_timed_out(*timeout_));
    }

    // No timeout, just cancellation -- watcher delivers via async_result_
    auto result = async_result_->wait();
    if (const auto terminal = terminal_error(); terminal.has_value()) {
      return mcp::core::unexpected(*terminal);
    }
    return read_from_async_result(result);
  }

  core::Result<core::Unit> cancel(std::string reason = {}) const {
    if (!cancel_) {
      return core::Unit{};
    }
    return cancel_(std::move(reason));
  }

 private:
  RequestHandle(protocol::RequestId request_id,
                std::optional<std::chrono::milliseconds> timeout,
                std::optional<CancellationToken> cancellation,
                CancelCallback cancel,
                std::shared_ptr<core::AsyncResult<ResultType>> async_result)
      : request_id_(std::move(request_id)),
        timeout_(std::move(timeout)),
        cancellation_(std::move(cancellation)),
        cancel_(std::move(cancel)),
        async_result_(std::move(async_result)),
        control_(std::make_shared<detail::RequestHandleControl>()) {}

  /// @brief Start a background watcher that blocks on the cancellation
  /// token's CV and cancels the async_result when the token fires.
  void start_cancellation_watcher() const {
    if (!cancellation_.has_value() || !async_result_) return;

    auto control = control_;
    auto async_result = async_result_;
    auto cancel_token = *cancellation_;
    auto cancel_cb = cancel_;

    (void)detail::request_executor().post(
        [control, async_result, cancel_token, cancel_cb]() {
          // Block until cancelled (CV-based, zero CPU)
          cancel_token.wait_for_cancel();
          // Set terminal error
          bool send_cancel = false;
          {
            std::lock_guard lock(control->mutex);
            if (!control->terminal_error.has_value()) {
              control->terminal_error = errors::request_cancelled();
              control->cancel_sent = true;
              send_cancel = true;
            }
          }
          // Cancel the async result to wake up await_response() waiters
          async_result->cancel("request cancelled");
          // Send cancel callback to peer
          if (send_cancel && cancel_cb) {
            (void)cancel_cb("request cancelled");
          }
        },
        core::TaskPriority::BACKGROUND);
  }

  std::optional<core::Error> terminal_error() const {
    if (!control_) {
      return std::nullopt;
    }
    std::lock_guard lock(control_->mutex);
    return control_->terminal_error;
  }

  core::Result<T> read_from_async_result(
      const core::Result<ResultType>& outer) const noexcept {
    if (!outer) {
      return mcp::core::unexpected(outer.error());
    }
    return *outer;
  }

  core::Result<T> read_from_async_result(
      const ResultType& inner) const noexcept {
    return inner;
  }

  core::Result<T> fail_terminal(std::string reason, core::Error error) const {
    bool send_cancel = false;
    core::Error terminal = error;
    if (control_) {
      std::lock_guard lock(control_->mutex);
      if (!control_->terminal_error.has_value()) {
        control_->terminal_error = std::move(error);
      }
      terminal = *control_->terminal_error;
      if (!control_->cancel_sent) {
        control_->cancel_sent = true;
        send_cancel = true;
      }
    } else {
      send_cancel = true;
    }

    if (send_cancel) {
      (void)cancel(std::move(reason));
    }
    return mcp::core::unexpected(std::move(terminal));
  }

  protocol::RequestId request_id_;
  std::optional<std::chrono::milliseconds> timeout_;
  std::optional<CancellationToken> cancellation_;
  CancelCallback cancel_;
  std::shared_ptr<core::AsyncResult<ResultType>> async_result_;
  std::shared_ptr<detail::RequestHandleControl> control_;
};

}  // namespace mcp
