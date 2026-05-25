// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Service lifecycle facade for role-aware MCP peers.
///
/// This header provides a small synchronous lifecycle layer that mirrors RMCP's
/// service-oriented public shape without introducing an async runtime yet.

#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/peer.hpp"

namespace mcp {

namespace detail {

struct ServiceLifecycleState {
  mutable std::mutex mutex;
  std::condition_variable cv;
  CancellationSource cancellation;
  bool running = true;
  bool closing = false;
};

inline bool service_running(
    const std::shared_ptr<ServiceLifecycleState>& state) noexcept {
  if (!state) {
    return false;
  }
  std::lock_guard lock(state->mutex);
  return state->running;
}

inline CancellationToken service_cancellation_token(
    const std::shared_ptr<ServiceLifecycleState>& state) noexcept {
  return state ? state->cancellation.token() : CancellationToken{};
}

template <class Stop>
inline core::Result<core::Unit> stop_service(
    const std::shared_ptr<ServiceLifecycleState>& state, Stop stop) noexcept {
  if (!state) {
    return core::Unit{};
  }

  {
    std::unique_lock lock(state->mutex);
    if (!state->running) {
      return core::Unit{};
    }
    if (state->closing) {
      state->cv.wait(lock, [&] { return !state->running; });
      return core::Unit{};
    }
    state->closing = true;
    state->cancellation.cancel();
  }

  stop();

  {
    std::lock_guard lock(state->mutex);
    state->running = false;
    state->closing = false;
  }
  state->cv.notify_all();
  return core::Unit{};
}

inline core::Result<core::Unit> wait_service(
    const std::shared_ptr<ServiceLifecycleState>& state) noexcept {
  if (!state) {
    return core::Unit{};
  }
  std::unique_lock lock(state->mutex);
  state->cv.wait(lock, [&] { return !state->running; });
  return core::Unit{};
}

}  // namespace detail

/// @brief Role-specialized MCP service before it is running.
template <class Role>
class Service;

/// @brief Role-specialized running MCP service.
template <class Role>
class RunningService;

/// @brief Running client-side MCP service.
template <>
class RunningService<RoleClient> {
 public:
  explicit RunningService(ClientPeer peer) : peer_(std::move(peer)) {}

  RunningService(const RunningService&) = delete;
  RunningService& operator=(const RunningService&) = delete;
  RunningService(RunningService&& other) noexcept
      : peer_(std::move(other.peer_)), state_(std::move(other.state_)) {}
  RunningService& operator=(RunningService&& other) noexcept {
    if (this != &other) {
      (void)stop();
      peer_ = std::move(other.peer_);
      state_ = std::move(other.state_);
    }
    return *this;
  }

  ~RunningService() { (void)stop(); }

  ClientPeer& peer() noexcept { return peer_; }

  const ClientPeer& peer() const noexcept { return peer_; }

  bool running() const noexcept { return detail::service_running(state_); }

  /// @brief Returns the service cancellation token.
  CancellationToken cancellation_token() const noexcept {
    return detail::service_cancellation_token(state_);
  }

  /// @brief Explicitly closes the running service.
  core::Result<core::Unit> close() noexcept { return stop(); }

  /// @brief Waits for service shutdown.
  ///
  /// Blocks until close() or stop() completes.
  core::Result<core::Unit> wait() noexcept {
    return detail::wait_service(state_);
  }

  core::Result<core::Unit> stop() noexcept {
    return detail::stop_service(state_, [this] { peer_.client().stop(); });
  }

 private:
  ClientPeer peer_;
  std::shared_ptr<detail::ServiceLifecycleState> state_ =
      std::make_shared<detail::ServiceLifecycleState>();
};

/// @brief Running server-side MCP service.
template <>
class RunningService<RoleServer> {
 public:
  explicit RunningService(ServerPeer peer) : peer_(std::move(peer)) {}

  RunningService(const RunningService&) = delete;
  RunningService& operator=(const RunningService&) = delete;
  RunningService(RunningService&& other) noexcept
      : peer_(std::move(other.peer_)), state_(std::move(other.state_)) {}
  RunningService& operator=(RunningService&& other) noexcept {
    if (this != &other) {
      (void)stop();
      peer_ = std::move(other.peer_);
      state_ = std::move(other.state_);
    }
    return *this;
  }

  ~RunningService() { (void)stop(); }

  ServerPeer& peer() noexcept { return peer_; }

  const ServerPeer& peer() const noexcept { return peer_; }

  bool running() const noexcept { return detail::service_running(state_); }

  /// @brief Returns the service cancellation token.
  CancellationToken cancellation_token() const noexcept {
    return detail::service_cancellation_token(state_);
  }

  /// @brief Explicitly closes the running service.
  core::Result<core::Unit> close() noexcept { return stop(); }

  /// @brief Waits for service shutdown.
  ///
  /// Blocks until close() or stop() completes.
  core::Result<core::Unit> wait() noexcept {
    return detail::wait_service(state_);
  }

  core::Result<core::Unit> stop() noexcept {
    return detail::stop_service(state_, [this] { peer_.stop(); });
  }

 private:
  ServerPeer peer_;
  std::shared_ptr<detail::ServiceLifecycleState> state_ =
      std::make_shared<detail::ServiceLifecycleState>();
};

/// @brief Client-side MCP service ready to be served.
template <>
class Service<RoleClient> {
 public:
  explicit Service(ClientPeer peer) : peer_(std::move(peer)) {}

  ClientPeer& peer() noexcept { return peer_; }

  const ClientPeer& peer() const noexcept { return peer_; }

  core::Result<RunningService<RoleClient>> serve() && {
    return RunningService<RoleClient>(std::move(peer_));
  }

 private:
  ClientPeer peer_;
};

/// @brief Server-side MCP service ready to be served.
template <>
class Service<RoleServer> {
 public:
  explicit Service(ServerPeer peer) : peer_(std::move(peer)) {}

  ServerPeer& peer() noexcept { return peer_; }

  const ServerPeer& peer() const noexcept { return peer_; }

  core::Result<RunningService<RoleServer>> serve() && {
    const auto started = peer_.start();
    if (!started) {
      return std::unexpected(started.error());
    }
    return RunningService<RoleServer>(std::move(peer_));
  }

 private:
  ServerPeer peer_;
};

/// @brief Creates a client service from a role-aware peer.
inline Service<RoleClient> make_service(ClientPeer peer) {
  return Service<RoleClient>(std::move(peer));
}

/// @brief Creates a server service from a role-aware peer.
inline Service<RoleServer> make_service(ServerPeer peer) {
  return Service<RoleServer>(std::move(peer));
}

/// @brief Starts a client service and returns its running handle.
inline core::Result<RunningService<RoleClient>> serve(ClientPeer peer) {
  return make_service(std::move(peer)).serve();
}

/// @brief Starts a server service and returns its running handle.
inline core::Result<RunningService<RoleServer>> serve(ServerPeer peer) {
  return make_service(std::move(peer)).serve();
}

}  // namespace mcp
