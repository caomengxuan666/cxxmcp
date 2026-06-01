// Copyright (c) 2025 [arookieofc]

#include "cxxmcp/transport/websocket_transport.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "httplib.h"

namespace mcp::transport {

namespace {

core::Error make_ws_error(protocol::ErrorCode code, std::string message,
                          std::string detail = {}) {
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail), "transport"};
}

/// @brief Parsed WebSocket URI components.
struct ParsedWsUri {
  std::string host;
  int port = 80;
  std::string path = "/mcp";
  bool ssl = false;
};

ParsedWsUri parse_ws_uri(const std::string& uri) {
  ParsedWsUri result;
  std::string remaining = uri;

  // Strip scheme
  if (remaining.rfind("wss://", 0) == 0) {
    result.ssl = true;
    result.port = 443;
    remaining = remaining.substr(6);
  } else if (remaining.rfind("ws://", 0) == 0) {
    remaining = remaining.substr(5);
  }

  // Split host:port/path
  auto slash_pos = remaining.find('/');
  std::string host_port;
  if (slash_pos != std::string::npos) {
    host_port = remaining.substr(0, slash_pos);
    result.path = remaining.substr(slash_pos);
  } else {
    host_port = remaining;
    result.path = "/";
  }

  auto colon_pos = host_port.find(':');
  if (colon_pos != std::string::npos) {
    result.host = host_port.substr(0, colon_pos);
    result.port = std::stoi(host_port.substr(colon_pos + 1));
  } else {
    result.host = host_port;
  }

  return result;
}

httplib::Headers build_headers(
    const std::unordered_map<std::string, std::string>& headers,
    const std::optional<std::string>& auth_header) {
  httplib::Headers result;
  for (const auto& [key, value] : headers) {
    result.emplace(key, value);
  }
  if (auth_header.has_value() && !auth_header->empty()) {
    result.emplace("Authorization", *auth_header);
  }
  return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// WebSocketClientTransport::Impl
// ---------------------------------------------------------------------------

class WebSocketClientTransport::Impl {
 public:
  explicit Impl(WebSocketClientTransportOptions options)
      : options_(std::move(options)),
        current_backoff_(options_.reconnect_initial_delay) {}

  ~Impl() { (void)close(); }

  protocol::Json diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return protocol::Json{
        {"name", "websocket-client"},
        {"closed", closed_},
        {"connected", connected_},
        {"queued", inbound_.size()},
        {"pendingRequests", 0},
        {"reconnectCount", reconnect_count_.load()},
        {"messagesSent", messages_sent_.load()},
        {"messagesReceived", messages_received_.load()},
    };
  }

  core::Result<core::Unit> send(protocol::JsonRpcMessage message) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return mcp::core::unexpected(
            make_ws_error(protocol::ErrorCode::InvalidRequest,
                          "websocket client transport is closed"));
      }
    }

    // Ensure connection is established
    auto connected = ensure_connected();
    if (!connected) {
      return mcp::core::unexpected(connected.error());
    }

    if (auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      return send_request(std::move(*request));
    }

    if (auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      return send_notification(*notification);
    }

    auto* response = std::get_if<protocol::JsonRpcResponse>(&message);
    if (response == nullptr || !response->id.has_value()) {
      return mcp::core::unexpected(make_ws_error(
          protocol::ErrorCode::InvalidRequest,
          "websocket client transport cannot send response without id"));
    }
    return complete_server_request(std::move(*response));
  }

  core::Result<std::optional<protocol::JsonRpcMessage>> receive() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_ && inbound_.empty()) {
        return std::nullopt;
      }
    }

    auto connected = ensure_connected();
    if (!connected) {
      return mcp::core::unexpected(connected.error());
    }

    std::unique_lock<std::mutex> lock(mutex_);
    receive_cv_.wait(lock, [this] { return closed_ || !inbound_.empty(); });
    if (inbound_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(inbound_.front());
    inbound_.pop_front();
    return message;
  }

  core::Result<core::Unit> close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return core::Unit{};
      }
      closed_ = true;
    }

    receive_cv_.notify_all();
    shutdown_active_socket();

    // Join reader thread
    if (reader_thread_.joinable() &&
        reader_thread_.get_id() != std::this_thread::get_id()) {
      reader_thread_.join();
    }

    {
      std::lock_guard<std::mutex> ws_lock(ws_mutex_);
      if (ws_) {
        try {
          ws_->close();
        } catch (...) {
          // Best-effort close
        }
        ws_.reset();
      }
      set_active_socket(INVALID_SOCKET);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      connected_ = false;
    }

    return core::Unit{};
  }

 private:
  core::Result<core::Unit> ensure_connected() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return mcp::core::unexpected(
          make_ws_error(protocol::ErrorCode::InvalidRequest,
                        "websocket client transport is closed"));
    }
    if (connected_) {
      return core::Unit{};
    }
    return connect_locked(StartReaderThread::Yes);
  }

  enum class StartReaderThread { No, Yes };

  core::Result<core::Unit> connect_locked(StartReaderThread start_reader) {
    // Parse URI or use host/port/path
    ParsedWsUri parsed;
    if (!options_.uri.empty()) {
      parsed = parse_ws_uri(options_.uri);
    } else {
      parsed.host = options_.host;
      parsed.port = options_.port;
      parsed.path = options_.path;
    }

    try {
      auto headers = build_headers(options_.headers, options_.auth_header);
      const auto endpoint = options_.uri.empty()
                                ? std::string(parsed.ssl ? "wss://" : "ws://") +
                                      parsed.host + ":" +
                                      std::to_string(parsed.port) + parsed.path
                                : options_.uri;
      ws_ = std::make_unique<httplib::ws::WebSocketClient>(endpoint, headers);

      if (!ws_->is_valid()) {
        return mcp::core::unexpected(
            make_ws_error(protocol::ErrorCode::InternalError,
                          "failed to create websocket client"));
      }

      // Configure timeouts
      const auto timeout_sec =
          std::chrono::duration_cast<std::chrono::seconds>(options_.timeout);
      ws_->set_connection_timeout(timeout_sec.count());
      ws_->set_read_timeout(timeout_sec.count());
      ws_->set_write_timeout(timeout_sec.count());

      // Configure ping/pong
      if (options_.ping_interval_sec > 0) {
        ws_->set_websocket_ping_interval(options_.ping_interval_sec);
        ws_->set_websocket_max_missed_pongs(options_.max_missed_pongs);
      }

      ws_->set_socket_options(
          [this](socket_t sock) { set_active_socket(sock); });

      if (!ws_->connect()) {
        set_active_socket(INVALID_SOCKET);
        return mcp::core::unexpected(make_ws_error(
            protocol::ErrorCode::InternalError, "websocket connection failed"));
      }

      connected_ = true;
      consecutive_failures_ = 0;
      current_backoff_ = options_.reconnect_initial_delay;

      if (start_reader == StartReaderThread::Yes) {
        if (reader_thread_.joinable() &&
            reader_thread_.get_id() != std::this_thread::get_id()) {
          reader_thread_.join();
        }
        reader_thread_ = std::thread([this] { reader_loop(); });
      }

      return core::Unit{};
    } catch (const std::exception& ex) {
      return mcp::core::unexpected(
          make_ws_error(protocol::ErrorCode::InternalError,
                        "websocket connection threw exception", ex.what()));
    }
  }

  void reader_loop() {
    while (true) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
          return;
        }
      }

      std::string raw;
      auto result = ws_->read(raw);
      if (!result) {
        // Read failed — attempt reconnect or close
        if (!try_reconnect()) {
          enqueue_error_and_close();
          return;
        }
        continue;
      }

      messages_received_++;

      // Parse the message
      auto parsed = protocol::parse_message(raw);
      if (!parsed) {
        // Invalid JSON-RPC, skip
        continue;
      }

      route_message(std::move(*parsed));
    }
  }

  bool try_reconnect() {
    if (!options_.auto_reconnect) {
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return false;
      }
    }

    consecutive_failures_++;
    if (options_.max_reconnect_attempts > 0 &&
        consecutive_failures_ > options_.max_reconnect_attempts) {
      return false;
    }

    // Close old connection. Hold mutex_ first (consistent with send()),
    // then ws_mutex_ for the actual WebSocket operations.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::lock_guard<std::mutex> ws_lock(ws_mutex_);
      if (ws_) {
        try {
          ws_->close();
        } catch (...) {
        }
        ws_.reset();
      }
      set_active_socket(INVALID_SOCKET);
      connected_ = false;

      // Wait with backoff (holding both locks so send() blocks)
      std::this_thread::sleep_for(current_backoff_);
      current_backoff_ = std::min(
          std::chrono::milliseconds(
              static_cast<std::int64_t>(current_backoff_.count() *
                                        options_.reconnect_backoff_multiplier)),
          options_.reconnect_max_delay);

      // Attempt reconnect
      auto result = connect_locked(StartReaderThread::No);
      if (result) {
        reconnect_count_++;
        return true;
      }
      return false;
    }
  }

  void route_message(protocol::JsonRpcMessage message) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return;
      }
      inbound_.push_back(std::move(message));
    }
    receive_cv_.notify_one();
  }

  void enqueue_error_and_close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return;
      }
      closed_ = true;
    }

    receive_cv_.notify_all();
  }

  void set_active_socket(socket_t sock) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    active_socket_ = sock;
  }

  void shutdown_active_socket() {
    socket_t sock = INVALID_SOCKET;
    {
      std::lock_guard<std::mutex> lock(socket_mutex_);
      sock = active_socket_;
    }
    if (sock == INVALID_SOCKET) {
      return;
    }
#ifdef _WIN32
    (void)::shutdown(sock, SD_BOTH);
#else
    (void)::shutdown(sock, SHUT_RDWR);
#endif
  }

  core::Result<core::Unit> send_request(protocol::JsonRpcRequest request) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return mcp::core::unexpected(
            make_ws_error(protocol::ErrorCode::InvalidRequest,
                          "websocket client transport is closed"));
      }
    }

    auto serialized = protocol::serialize_request(request);
    if (!serialized) {
      return mcp::core::unexpected(
          make_ws_error(protocol::ErrorCode::InternalError,
                        "failed to serialize websocket request"));
    }

    {
      std::lock_guard<std::mutex> ws_lock(ws_mutex_);
      if (!ws_ || !ws_->send(*serialized)) {
        return mcp::core::unexpected(make_ws_error(
            protocol::ErrorCode::InternalError, "websocket send failed"));
      }
    }
    messages_sent_++;

    return core::Unit{};
  }

  core::Result<core::Unit> send_notification(
      const protocol::JsonRpcNotification& notification) {
    auto serialized = protocol::serialize_notification(notification);
    if (!serialized) {
      return mcp::core::unexpected(
          make_ws_error(protocol::ErrorCode::InternalError,
                        "failed to serialize websocket notification"));
    }

    std::lock_guard<std::mutex> ws_lock(ws_mutex_);
    if (!ws_ || !ws_->send(*serialized)) {
      return mcp::core::unexpected(make_ws_error(
          protocol::ErrorCode::InternalError, "websocket send failed"));
    }
    messages_sent_++;
    return core::Unit{};
  }

  core::Result<core::Unit> complete_server_request(
      protocol::JsonRpcResponse response) {
    auto serialized = protocol::serialize_response(response);
    if (!serialized) {
      return mcp::core::unexpected(
          make_ws_error(protocol::ErrorCode::InternalError,
                        "failed to serialize websocket response"));
    }

    std::lock_guard<std::mutex> ws_lock(ws_mutex_);
    if (!ws_ || !ws_->send(*serialized)) {
      return mcp::core::unexpected(make_ws_error(
          protocol::ErrorCode::InternalError, "websocket send failed"));
    }
    messages_sent_++;
    return core::Unit{};
  }

  WebSocketClientTransportOptions options_;
  mutable std::mutex mutex_;
  std::mutex ws_mutex_;  // Guards writes and WebSocketClient destruction.
  std::mutex socket_mutex_;
  std::condition_variable receive_cv_;
  std::deque<protocol::JsonRpcMessage> inbound_;
  std::unique_ptr<httplib::ws::WebSocketClient> ws_;
  std::thread reader_thread_;
  socket_t active_socket_ = INVALID_SOCKET;
  bool closed_ = false;
  bool connected_ = false;

  // Reconnect state
  int consecutive_failures_ = 0;
  std::chrono::milliseconds current_backoff_;

  // Stats
  std::atomic<std::uint64_t> reconnect_count_{0};
  std::atomic<std::uint64_t> messages_sent_{0};
  std::atomic<std::uint64_t> messages_received_{0};
};

// ---------------------------------------------------------------------------
// WebSocketClientTransport forwarding methods
// ---------------------------------------------------------------------------

WebSocketClientTransport::WebSocketClientTransport(
    WebSocketClientTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

WebSocketClientTransport::~WebSocketClientTransport() = default;

std::string_view WebSocketClientTransport::name() const noexcept {
  return "websocket-client";
}

protocol::Json WebSocketClientTransport::diagnostics() const {
  return impl_->diagnostics();
}

core::Result<core::Unit> WebSocketClientTransport::send(TxMessage message) {
  return impl_->send(std::move(message));
}

core::Result<std::optional<WebSocketClientTransport::RxMessage>>
WebSocketClientTransport::receive() {
  return impl_->receive();
}

core::Result<core::Unit> WebSocketClientTransport::close() {
  return impl_->close();
}

}  // namespace mcp::transport
