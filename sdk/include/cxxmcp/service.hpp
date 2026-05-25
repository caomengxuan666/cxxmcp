// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Service lifecycle facade for role-aware MCP peers.
///
/// This header provides a small synchronous lifecycle layer that mirrors RMCP's
/// service-oriented public shape without introducing an async runtime yet.
///
/// Lifecycle contract:
/// - serve(peer) transfers peer ownership into a RunningService.
/// - Server services own a background service loop that calls
///   ServerPeer::start(), or ServerPeer::serve_transport() when constructed
///   with a role-generic server transport. Client services do not create an
///   additional universal receive loop because the current built-in client
///   transports already drive inbound callbacks through the concrete Client
///   transport model.
/// - close() and stop() are equivalent, idempotent, noexcept-safe shutdown
///   entry points. They cancel the service token, stop the underlying peer, and
///   unblock wait().
/// - wait() blocks until close(), stop(), or destruction completes shutdown.
/// - The destructor performs best-effort stop().
/// - Moved-from RunningService objects are inert: running() is false and
///   close(), stop(), and wait() return success without touching the moved-to
///   peer.

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/peer.hpp"

namespace mcp {

namespace detail {

struct ServiceLifecycleState {
  mutable std::mutex mutex;
  std::mutex join_mutex;
  std::condition_variable cv;
  CancellationSource cancellation;
  bool running = true;
  bool closing = false;
  std::optional<core::Error> failure;
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

inline void finish_service(const std::shared_ptr<ServiceLifecycleState>& state,
                           std::optional<core::Error> failure = {}) noexcept {
  if (!state) {
    return;
  }
  {
    std::lock_guard lock(state->mutex);
    state->running = false;
    state->closing = false;
    state->failure = std::move(failure);
  }
  state->cv.notify_all();
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
  explicit RunningService(ServerPeer peer)
      : peer_(std::make_shared<ServerPeer>(std::move(peer))) {
    start_loop();
  }

  RunningService(ServerPeer peer,
                 std::unique_ptr<transport::ServerTransport> transport,
                 server::SessionContext context = {})
      : peer_(std::make_shared<ServerPeer>(std::move(peer))),
        transport_(std::move(transport)),
        context_(std::move(context)) {
    start_loop();
  }

  RunningService(const RunningService&) = delete;
  RunningService& operator=(const RunningService&) = delete;
  RunningService(RunningService&& other) noexcept
      : peer_(std::move(other.peer_)),
        transport_(std::move(other.transport_)),
        context_(std::move(other.context_)),
        state_(std::move(other.state_)),
        loop_(std::move(other.loop_)) {}
  RunningService& operator=(RunningService&& other) noexcept {
    if (this != &other) {
      (void)stop();
      peer_ = std::move(other.peer_);
      transport_ = std::move(other.transport_);
      context_ = std::move(other.context_);
      state_ = std::move(other.state_);
      loop_ = std::move(other.loop_);
    }
    return *this;
  }

  ~RunningService() { (void)stop(); }

  ServerPeer& peer() noexcept { return *peer_; }

  const ServerPeer& peer() const noexcept { return *peer_; }

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
    const auto waited = detail::wait_service(state_);
    join_loop();
    if (!waited) {
      return std::unexpected(waited.error());
    }
    if (state_) {
      std::lock_guard lock(state_->mutex);
      if (state_->failure.has_value()) {
        return std::unexpected(*state_->failure);
      }
    }
    return core::Unit{};
  }

  core::Result<core::Unit> stop() noexcept {
    if (!state_) {
      return core::Unit{};
    }
    {
      std::unique_lock lock(state_->mutex);
      if (!state_->running) {
        lock.unlock();
        return wait();
      }
      if (state_->closing) {
        lock.unlock();
        return wait();
      }
      state_->closing = true;
      state_->cancellation.cancel();
    }
    if (transport_) {
      (void)transport_->close();
    }
    if (peer_) {
      peer_->stop();
    }
    return wait();
  }

 private:
  void start_loop() {
    auto state = state_;
    auto peer = peer_;
    auto transport = transport_;
    auto context = context_;
    loop_ = std::thread([state, peer, transport, context]() noexcept {
      const auto started = transport
                               ? peer->serve_transport(*transport, context)
                               : peer->start();
      if (!started) {
        detail::finish_service(state, started.error());
      } else {
        detail::finish_service(state);
      }
    });
  }

  void join_loop() noexcept {
    if (!state_) {
      return;
    }
    std::lock_guard lock(state_->join_mutex);
    if (loop_.joinable() && loop_.get_id() != std::this_thread::get_id()) {
      loop_.join();
    }
  }

  std::shared_ptr<ServerPeer> peer_;
  std::shared_ptr<transport::ServerTransport> transport_;
  server::SessionContext context_;
  std::shared_ptr<detail::ServiceLifecycleState> state_ =
      std::make_shared<detail::ServiceLifecycleState>();
  std::thread loop_;
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

  Service(ServerPeer peer,
          std::unique_ptr<transport::ServerTransport> transport,
          server::SessionContext context = {})
      : peer_(std::move(peer)),
        transport_(std::move(transport)),
        context_(std::move(context)) {}

  ServerPeer& peer() noexcept { return peer_; }

  const ServerPeer& peer() const noexcept { return peer_; }

  core::Result<RunningService<RoleServer>> serve() && {
    if (transport_) {
      return RunningService<RoleServer>(std::move(peer_), std::move(transport_),
                                        std::move(context_));
    }
    return RunningService<RoleServer>(std::move(peer_));
  }

 private:
  ServerPeer peer_;
  std::unique_ptr<transport::ServerTransport> transport_;
  server::SessionContext context_;
};

/// @brief Creates a client service from a role-aware peer.
inline Service<RoleClient> make_service(ClientPeer peer) {
  return Service<RoleClient>(std::move(peer));
}

/// @brief Creates a server service from a role-aware peer.
inline Service<RoleServer> make_service(ServerPeer peer) {
  return Service<RoleServer>(std::move(peer));
}

/// @brief Creates a server service that is driven by a role-generic transport.
inline Service<RoleServer> make_service(
    ServerPeer peer, std::unique_ptr<transport::ServerTransport> transport,
    server::SessionContext context = {}) {
  return Service<RoleServer>(std::move(peer), std::move(transport),
                             std::move(context));
}

/// @brief Starts a client service and returns its running handle.
inline core::Result<RunningService<RoleClient>> serve(ClientPeer peer) {
  return make_service(std::move(peer)).serve();
}

/// @brief Starts a server service and returns its running handle.
inline core::Result<RunningService<RoleServer>> serve(ServerPeer peer) {
  return make_service(std::move(peer)).serve();
}

/// @brief Starts a server service over a role-generic transport.
inline core::Result<RunningService<RoleServer>> serve(
    ServerPeer peer, std::unique_ptr<transport::ServerTransport> transport,
    server::SessionContext context = {}) {
  return make_service(std::move(peer), std::move(transport), std::move(context))
      .serve();
}

}  // namespace mcp
