// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Request lifecycle helpers for cancellable SDK calls.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/core/executor.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp {

/// @brief Configuration for the bounded background request executor.
///
/// The executor is used by RequestHandle::spawn() and by async request helpers
/// that wrap blocking request paths in a background task. Defaults are tuned
/// for local stdio/process-stdio MCP workloads.
struct RequestExecutorOptions {
  std::size_t worker_count = 4;
  std::size_t max_queue_size = 64;
};

namespace detail {

struct RequestExecutorState {
  std::mutex mutex;
  RequestExecutorOptions options;
  std::unique_ptr<core::BoundedExecutor> executor;
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

inline core::BoundedExecutor& request_executor() {
  auto& state = request_executor_state();
  std::lock_guard lock(state.mutex);
  if (!state.executor) {
    state.executor = std::make_unique<core::BoundedExecutor>(
        state.options.worker_count, state.options.max_queue_size);
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
    auto promise = std::make_shared<std::promise<ResultType>>();
    auto future = promise->get_future().share();
    promise->set_value(std::move(result));
    return RequestHandle(std::move(request_id), std::nullopt, std::nullopt, {},
                         std::move(future));
  }

  static RequestHandle spawn(protocol::RequestId request_id,
                             std::optional<std::chrono::milliseconds> timeout,
                             std::optional<CancellationToken> cancellation,
                             CancelCallback cancel,
                             std::function<ResultType()> task) {
    auto promise = std::make_shared<std::promise<ResultType>>();
    auto future = promise->get_future().share();
    if (!task) {
      promise->set_value(mcp::core::unexpected(errors::request_task_missing()));
      return RequestHandle(std::move(request_id), std::move(timeout),
                           std::move(cancellation), std::move(cancel),
                           std::move(future));
    }

    const auto queued = detail::request_executor().enqueue(
        [promise, task = std::move(task)]() mutable {
          try {
            promise->set_value(task());
          } catch (const std::exception& ex) {
            promise->set_value(mcp::core::unexpected(
                errors::request_worker_exception(ex.what())));
          } catch (...) {
            promise->set_value(mcp::core::unexpected(
                errors::request_worker_unknown_exception()));
          }
        });
    if (!queued) {
      promise->set_value(mcp::core::unexpected(queued.error()));
    }
    return RequestHandle(std::move(request_id), std::move(timeout),
                         std::move(cancellation), std::move(cancel),
                         std::move(future));
  }

  const protocol::RequestId& request_id() const noexcept { return request_id_; }

  const std::optional<std::chrono::milliseconds>& timeout() const noexcept {
    return timeout_;
  }

  const std::optional<CancellationToken>& cancellation_token() const noexcept {
    return cancellation_;
  }

  /// @brief Waits for the request result and returns a copy of the shared
  /// result state.
  ///
  /// RequestHandle is multi-await: await_response() may be called more than
  /// once on the same handle or on handle copies. If neither timeout nor
  /// cancellation was configured, await_response() blocks until the background
  /// task finishes and returns its result.
  ///
  /// A configured cancellation token or timeout is observed by all copies of
  /// the handle. If the token is already cancelled before await_response(), or
  /// becomes cancelled while waiting, the handle sends the cancel callback with
  /// "request cancelled" and stores a terminal cancellation error. If the
  /// timeout expires while waiting, it sends "request timeout" and stores a
  /// terminal timeout error. The cancel callback is sent at most once per
  /// request handle state. These operations are cooperative: they notify the
  /// peer but do not forcibly stop a request task that is already running.
  /// Late task results are ignored by future await_response() calls after a
  /// terminal timeout or cancellation has been observed.
  core::Result<T> await_response() const {
    if (!response_.valid()) {
      return mcp::core::unexpected(errors::request_state_missing());
    }

    if (const auto terminal = terminal_error(); terminal.has_value()) {
      return mcp::core::unexpected(*terminal);
    }

    if (!timeout_.has_value() && !cancellation_.has_value()) {
      return read_response_result();
    }

    const auto started_at = std::chrono::steady_clock::now();
    if (timeout_.has_value() && !cancellation_.has_value()) {
      const auto deadline = started_at + *timeout_;
      if (response_.wait_until(deadline) == std::future_status::ready) {
        if (const auto terminal = terminal_error(); terminal.has_value()) {
          return mcp::core::unexpected(*terminal);
        }
        return read_response_result();
      }
      return fail_terminal("request timeout",
                           errors::request_timed_out(*timeout_));
    }

    while (true) {
      if (const auto terminal = terminal_error(); terminal.has_value()) {
        return mcp::core::unexpected(*terminal);
      }

      if (cancellation_.has_value() && cancellation_->cancelled()) {
        return fail_terminal("request cancelled", errors::request_cancelled());
      }

      if (timeout_.has_value() &&
          std::chrono::steady_clock::now() - started_at >= *timeout_) {
        return fail_terminal("request timeout",
                             errors::request_timed_out(*timeout_));
      }

      const auto wait_slice =
          timeout_.has_value()
              ? std::min(std::chrono::milliseconds(10), *timeout_)
              : std::chrono::milliseconds(10);
      if (response_.wait_for(wait_slice) == std::future_status::ready) {
        if (const auto terminal = terminal_error(); terminal.has_value()) {
          return mcp::core::unexpected(*terminal);
        }
        return read_response_result();
      }
    }
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
                CancelCallback cancel, std::shared_future<ResultType> response)
      : request_id_(std::move(request_id)),
        timeout_(std::move(timeout)),
        cancellation_(std::move(cancellation)),
        cancel_(std::move(cancel)),
        response_(std::move(response)),
        control_(std::make_shared<detail::RequestHandleControl>()) {}

  std::optional<core::Error> terminal_error() const {
    if (!control_) {
      return std::nullopt;
    }
    std::lock_guard lock(control_->mutex);
    return control_->terminal_error;
  }

  core::Result<T> read_response_result() const noexcept {
    try {
      return response_.get();
    } catch (const std::exception& ex) {
      return mcp::core::unexpected(errors::request_worker_exception(ex.what()));
    } catch (...) {
      return mcp::core::unexpected(errors::request_worker_unknown_exception());
    }
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
  std::shared_future<ResultType> response_;
  std::shared_ptr<detail::RequestHandleControl> control_;
};

}  // namespace mcp
