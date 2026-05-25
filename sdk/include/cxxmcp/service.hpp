// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Service lifecycle facade for role-aware MCP peers.
///
/// This header provides a small synchronous lifecycle layer that mirrors RMCP's
/// service-oriented public shape without introducing an async runtime yet.

#include <utility>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/peer.hpp"

namespace mcp {

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
      : peer_(std::move(other.peer_)),
        cancellation_(std::move(other.cancellation_)),
        running_(other.running_) {
    other.running_ = false;
  }
  RunningService& operator=(RunningService&& other) noexcept {
    if (this != &other) {
      (void)stop();
      peer_ = std::move(other.peer_);
      cancellation_ = std::move(other.cancellation_);
      running_ = other.running_;
      other.running_ = false;
    }
    return *this;
  }

  ~RunningService() { (void)stop(); }

  ClientPeer& peer() noexcept { return peer_; }

  const ClientPeer& peer() const noexcept { return peer_; }

  bool running() const noexcept { return running_; }

  /// @brief Returns the service cancellation token.
  CancellationToken cancellation_token() const noexcept {
    return cancellation_.token();
  }

  /// @brief Explicitly closes the running service.
  core::Result<core::Unit> close() noexcept { return stop(); }

  /// @brief Waits for service shutdown.
  ///
  /// The current C++ service facade is synchronous, so there is no background
  /// driver to join. The method is still part of the public lifecycle shape so
  /// callers can write code against close/wait semantics.
  core::Result<core::Unit> wait() noexcept { return core::Unit{}; }

  core::Result<core::Unit> stop() noexcept {
    if (running_) {
      cancellation_.cancel();
      peer_.client().stop();
      running_ = false;
    }
    return core::Unit{};
  }

 private:
  ClientPeer peer_;
  CancellationSource cancellation_;
  bool running_ = true;
};

/// @brief Running server-side MCP service.
template <>
class RunningService<RoleServer> {
 public:
  explicit RunningService(ServerPeer peer) : peer_(std::move(peer)) {}

  RunningService(const RunningService&) = delete;
  RunningService& operator=(const RunningService&) = delete;
  RunningService(RunningService&& other) noexcept
      : peer_(std::move(other.peer_)),
        cancellation_(std::move(other.cancellation_)),
        running_(other.running_) {
    other.running_ = false;
  }
  RunningService& operator=(RunningService&& other) noexcept {
    if (this != &other) {
      (void)stop();
      peer_ = std::move(other.peer_);
      cancellation_ = std::move(other.cancellation_);
      running_ = other.running_;
      other.running_ = false;
    }
    return *this;
  }

  ~RunningService() { (void)stop(); }

  ServerPeer& peer() noexcept { return peer_; }

  const ServerPeer& peer() const noexcept { return peer_; }

  bool running() const noexcept { return running_; }

  /// @brief Returns the service cancellation token.
  CancellationToken cancellation_token() const noexcept {
    return cancellation_.token();
  }

  /// @brief Explicitly closes the running service.
  core::Result<core::Unit> close() noexcept { return stop(); }

  /// @brief Waits for service shutdown.
  ///
  /// Server transports are started by serve(); once serve() returns, the
  /// synchronous start path has already completed or failed. This wait hook
  /// keeps the lifecycle facade aligned with peer/service SDKs that expose a
  /// separate shutdown wait step.
  core::Result<core::Unit> wait() noexcept { return core::Unit{}; }

  core::Result<core::Unit> stop() noexcept {
    if (running_) {
      cancellation_.cancel();
      peer_.stop();
      running_ = false;
    }
    return core::Unit{};
  }

 private:
  ServerPeer peer_;
  CancellationSource cancellation_;
  bool running_ = true;
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
