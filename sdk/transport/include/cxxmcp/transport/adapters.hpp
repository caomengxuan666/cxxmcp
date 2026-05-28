// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Small adapter helpers for custom role-generic transports.

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp::transport {

inline core::Error transport_adapter_error(protocol::ErrorCode code,
                                           std::string message) {
  return core::Error{
      static_cast<int>(code),
      std::move(message),
      {},
      "transport",
  };
}

/// @brief Function-backed implementation of Transport<Role>.
template <class Role>
struct FunctionTransportOptions {
  using TxMessage = typename Transport<Role>::TxMessage;
  using RxMessage = typename Transport<Role>::RxMessage;

  /// Human-readable transport name.
  std::string name = "function";
  /// Sends one message. Required.
  std::function<core::Result<core::Unit>(TxMessage)> send;
  /// Receives the next message or end-of-stream. Required.
  std::function<core::Result<std::optional<RxMessage>>()> receive;
  /// Closes the underlying transport. Optional.
  std::function<core::Result<core::Unit>()> close;
  /// Returns structured diagnostics. Optional.
  std::function<protocol::Json()> diagnostics;
};

/// @brief Transport adapter for applications that already have send/receive
/// callables.
template <class Role>
class FunctionTransport final : public Transport<Role> {
 public:
  using TxMessage = typename Transport<Role>::TxMessage;
  using RxMessage = typename Transport<Role>::RxMessage;

  explicit FunctionTransport(FunctionTransportOptions<Role> options)
      : options_(std::move(options)) {}

  std::string_view name() const noexcept override { return options_.name; }

  protocol::Json diagnostics() const override {
    if (options_.diagnostics) {
      return options_.diagnostics();
    }
    return protocol::Json{{"name", options_.name}};
  }

  core::Result<core::Unit> send(TxMessage message) override {
    if (!options_.send) {
      return mcp::core::unexpected(transport_adapter_error(
          protocol::ErrorCode::InvalidRequest,
          "function transport send callback is not configured"));
    }
    return options_.send(std::move(message));
  }

  core::Result<std::optional<RxMessage>> receive() override {
    if (!options_.receive) {
      return mcp::core::unexpected(transport_adapter_error(
          protocol::ErrorCode::InvalidRequest,
          "function transport receive callback is not configured"));
    }
    return options_.receive();
  }

  core::Result<core::Unit> close() override {
    if (!options_.close) {
      return core::Unit{};
    }
    return options_.close();
  }

 private:
  FunctionTransportOptions<Role> options_;
};

/// @brief Creates a function-backed role-generic transport.
template <class Role>
std::unique_ptr<Transport<Role>> make_function_transport(
    FunctionTransportOptions<Role> options) {
  return std::make_unique<FunctionTransport<Role>>(std::move(options));
}

/// @brief Line-oriented JSON-RPC source/sink adapter options.
template <class Role>
struct JsonLineTransportOptions {
  using RxLine = core::Result<std::optional<std::string>>;

  /// Human-readable transport name.
  std::string name = "json-line";
  /// Writes one serialized JSON-RPC document without a trailing newline.
  std::function<core::Result<core::Unit>(std::string)> write_line;
  /// Reads one serialized JSON-RPC document or end-of-stream.
  std::function<RxLine()> read_line;
  /// Closes the underlying source/sink. Optional.
  std::function<core::Result<core::Unit>()> close;
  /// Returns structured diagnostics. Optional.
  std::function<protocol::Json()> diagnostics;
};

/// @brief Adapter for transports that expose newline-delimited JSON strings.
template <class Role>
class JsonLineTransport final : public Transport<Role> {
 public:
  using TxMessage = typename Transport<Role>::TxMessage;
  using RxMessage = typename Transport<Role>::RxMessage;

  explicit JsonLineTransport(JsonLineTransportOptions<Role> options)
      : options_(std::move(options)) {}

  std::string_view name() const noexcept override { return options_.name; }

  protocol::Json diagnostics() const override {
    if (options_.diagnostics) {
      return options_.diagnostics();
    }
    return protocol::Json{{"name", options_.name}};
  }

  core::Result<core::Unit> send(TxMessage message) override {
    if (!options_.write_line) {
      return mcp::core::unexpected(transport_adapter_error(
          protocol::ErrorCode::InvalidRequest,
          "json-line transport write callback is not configured"));
    }
    const auto serialized = protocol::serialize_message(message);
    if (!serialized) {
      return mcp::core::unexpected(serialized.error());
    }
    return options_.write_line(*serialized);
  }

  core::Result<std::optional<RxMessage>> receive() override {
    if (!options_.read_line) {
      return mcp::core::unexpected(transport_adapter_error(
          protocol::ErrorCode::InvalidRequest,
          "json-line transport read callback is not configured"));
    }
    auto line = options_.read_line();
    if (!line) {
      return mcp::core::unexpected(line.error());
    }
    if (!line->has_value()) {
      return std::nullopt;
    }
    if (line->value().empty()) {
      return mcp::core::unexpected(transport_adapter_error(
          protocol::ErrorCode::ParseError, "empty transport message"));
    }
    const auto parsed = protocol::parse_message(line->value());
    if (!parsed) {
      return mcp::core::unexpected(parsed.error());
    }
    return RxMessage{*parsed};
  }

  core::Result<core::Unit> close() override {
    if (!options_.close) {
      return core::Unit{};
    }
    return options_.close();
  }

 private:
  JsonLineTransportOptions<Role> options_;
};

/// @brief Creates a line-oriented JSON-RPC transport.
template <class Role>
std::unique_ptr<Transport<Role>> make_json_line_transport(
    JsonLineTransportOptions<Role> options) {
  return std::make_unique<JsonLineTransport<Role>>(std::move(options));
}

/// @brief Options for an in-memory queue-backed transport.
template <class Role>
struct QueueTransportOptions {
  /// Human-readable transport name.
  std::string name = "queue";
  /// Maximum inbound queue size. Zero means unbounded.
  std::size_t max_inbound = 0;
  /// Maximum outbound queue size. Zero means unbounded.
  std::size_t max_outbound = 0;
};

/// @brief Thread-safe queue transport for worker/queue integrations and tests.
template <class Role>
class QueueTransport final : public Transport<Role> {
 public:
  using TxMessage = typename Transport<Role>::TxMessage;
  using RxMessage = typename Transport<Role>::RxMessage;

  explicit QueueTransport(QueueTransportOptions<Role> options = {})
      : options_(std::move(options)) {}

  std::string_view name() const noexcept override { return options_.name; }

  protocol::Json diagnostics() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return protocol::Json{
        {"name", options_.name},
        {"closed", closed_},
        {"inbound", inbound_.size()},
        {"outbound", outbound_.size()},
    };
  }

  core::Result<core::Unit> send(TxMessage message) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return mcp::core::unexpected(transport_adapter_error(
          protocol::ErrorCode::InvalidRequest, "transport is closed"));
    }
    if (options_.max_outbound != 0 &&
        outbound_.size() >= options_.max_outbound) {
      return mcp::core::unexpected(
          transport_adapter_error(protocol::ErrorCode::InternalError,
                                  "transport outbound queue is full"));
    }
    outbound_.push_back(std::move(message));
    outbound_cv_.notify_one();
    return core::Unit{};
  }

  core::Result<std::optional<RxMessage>> receive() override {
    std::unique_lock<std::mutex> lock(mutex_);
    inbound_cv_.wait(lock, [&] { return closed_ || !inbound_.empty(); });
    if (inbound_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(inbound_.front());
    inbound_.pop_front();
    return message;
  }

  core::Result<core::Unit> close() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    inbound_cv_.notify_all();
    outbound_cv_.notify_all();
    return core::Unit{};
  }

  /// @brief Queues a message that receive() will return.
  core::Result<core::Unit> push_inbound(RxMessage message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return mcp::core::unexpected(transport_adapter_error(
          protocol::ErrorCode::InvalidRequest, "transport is closed"));
    }
    if (options_.max_inbound != 0 && inbound_.size() >= options_.max_inbound) {
      return mcp::core::unexpected(
          transport_adapter_error(protocol::ErrorCode::InternalError,
                                  "transport inbound queue is full"));
    }
    inbound_.push_back(std::move(message));
    inbound_cv_.notify_one();
    return core::Unit{};
  }

  /// @brief Returns one message captured by send(), if available.
  std::optional<TxMessage> pop_outbound() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (outbound_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(outbound_.front());
    outbound_.pop_front();
    return message;
  }

 private:
  QueueTransportOptions<Role> options_;
  mutable std::mutex mutex_;
  std::condition_variable inbound_cv_;
  std::condition_variable outbound_cv_;
  std::deque<RxMessage> inbound_;
  std::deque<TxMessage> outbound_;
  bool closed_ = false;
};

using ClientFunctionTransport = FunctionTransport<RoleClient>;
using ServerFunctionTransport = FunctionTransport<RoleServer>;
using ClientJsonLineTransport = JsonLineTransport<RoleClient>;
using ServerJsonLineTransport = JsonLineTransport<RoleServer>;
using ClientQueueTransport = QueueTransport<RoleClient>;
using ServerQueueTransport = QueueTransport<RoleServer>;

}  // namespace mcp::transport
