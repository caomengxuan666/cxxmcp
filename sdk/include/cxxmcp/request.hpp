// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Request lifecycle helpers for cancellable SDK calls.

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/core/executor.hpp"
#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp {

namespace detail {

inline core::BoundedExecutor& request_executor() {
  static core::BoundedExecutor executor;
  return executor;
}

}  // namespace detail

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

  static RequestHandle spawn(protocol::RequestId request_id,
                             std::optional<std::chrono::milliseconds> timeout,
                             std::optional<CancellationToken> cancellation,
                             CancelCallback cancel,
                             std::function<ResultType()> task) {
    auto promise = std::make_shared<std::promise<ResultType>>();
    auto future = promise->get_future().share();
    const auto queued = detail::request_executor().enqueue(
        [promise, task = std::move(task)]() mutable {
          try {
            promise->set_value(task());
          } catch (const std::exception& ex) {
            promise->set_value(std::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::InternalError),
                "request worker threw an exception",
                ex.what(),
            }));
          } catch (...) {
            promise->set_value(std::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::InternalError),
                "request worker threw an unknown exception",
                {},
            }));
          }
        });
    if (!queued) {
      promise->set_value(std::unexpected(queued.error()));
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

  core::Result<T> await_response() const {
    if (!timeout_.has_value() && !cancellation_.has_value()) {
      return response_.get();
    }

    const auto started_at = std::chrono::steady_clock::now();
    while (true) {
      if (cancellation_.has_value() && cancellation_->cancelled()) {
        (void)cancel("request cancelled");
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::InternalError),
            "request cancelled",
            {},
        });
      }

      if (timeout_.has_value() &&
          std::chrono::steady_clock::now() - started_at >= *timeout_) {
        (void)cancel("request timeout");
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::InternalError),
            "request timed out",
            std::to_string(timeout_->count()) + "ms",
        });
      }

      const auto wait_slice =
          timeout_.has_value()
              ? std::min(std::chrono::milliseconds(10), *timeout_)
              : std::chrono::milliseconds(10);
      if (response_.wait_for(wait_slice) == std::future_status::ready) {
        return response_.get();
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
        response_(std::move(response)) {}

  protocol::RequestId request_id_;
  std::optional<std::chrono::milliseconds> timeout_;
  std::optional<CancellationToken> cancellation_;
  CancelCallback cancel_;
  std::shared_future<ResultType> response_;
};

}  // namespace mcp
