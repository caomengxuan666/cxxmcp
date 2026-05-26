// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Cooperative cancellation primitives shared by SDK lifecycle APIs.

#include <atomic>
#include <memory>
#include <utility>

namespace mcp {

/// @brief Copyable token observed by cancellation-aware SDK operations.
class CancellationToken {
 public:
  CancellationToken() : cancelled_(std::make_shared<std::atomic_bool>(false)) {}

  /// @brief Returns true after the associated source has requested
  /// cancellation.
  bool cancelled() const noexcept {
    return cancelled_ != nullptr && cancelled_->load();
  }

 private:
  friend class CancellationSource;

  explicit CancellationToken(std::shared_ptr<std::atomic_bool> cancelled)
      : cancelled_(std::move(cancelled)) {}

  std::shared_ptr<std::atomic_bool> cancelled_;
};

/// @brief Owner side of a cooperative cancellation token.
class CancellationSource {
 public:
  CancellationSource()
      : cancelled_(std::make_shared<std::atomic_bool>(false)) {}

  /// @brief Returns a token sharing this source's cancellation state.
  CancellationToken token() const noexcept {
    return CancellationToken(cancelled_);
  }

  /// @brief Requests cancellation for all tokens created from this source.
  void cancel() const noexcept {
    if (cancelled_ != nullptr) {
      cancelled_->store(true);
    }
  }

  /// @brief Returns true when cancellation has been requested.
  bool cancelled() const noexcept {
    return cancelled_ != nullptr && cancelled_->load();
  }

 private:
  std::shared_ptr<std::atomic_bool> cancelled_;
};

}  // namespace mcp
