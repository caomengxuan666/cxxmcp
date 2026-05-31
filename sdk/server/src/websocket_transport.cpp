// Copyright (c) 2025 [arookieofc]

#include "cxxmcp/transport/websocket_transport.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
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

core::Error make_ws_server_error(protocol::ErrorCode code, std::string message,
                                 std::string detail = {}) {
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail), "transport"};
}

std::string request_id_to_string(const protocol::RequestId& request_id) {
  return std::visit(
      [](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return value;
        } else {
          return std::to_string(value);
        }
      },
      request_id);
}

}  // namespace

// ---------------------------------------------------------------------------
// WebSocketServerTransport::Impl
// ---------------------------------------------------------------------------

class WebSocketServerTransport::Impl {
 public:
  explicit Impl(WebSocketServerTransportOptions options)
      : options_(std::move(options)) {
    setup_server();
  }

  ~Impl() { (void)close(); }

  protocol::Json diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return protocol::Json{
        {"name", "websocket-server"},
        {"closed", closed_},
        {"ready", ready_},
        {"connections", connection_count_.load()},
        {"queued", inbound_.size()},
        {"pendingRequests", pending_client_requests_.size()},
        {"messagesReceived", messages_received_.load()},
        {"messagesSent", messages_sent_.load()},
    };
  }

  core::Result<core::Unit> send(protocol::JsonRpcMessage message) {
    if (auto* response = std::get_if<protocol::JsonRpcResponse>(&message)) {
      if (!response->id.has_value()) {
        return mcp::core::unexpected(make_ws_server_error(
            protocol::ErrorCode::InvalidRequest,
            "websocket server transport cannot send response without id"));
      }
      return complete_client_request(std::move(*response));
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return mcp::core::unexpected(
            make_ws_server_error(protocol::ErrorCode::InvalidRequest,
                                 "websocket server transport is closed"));
      }
    }

    if (auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      return send_notification_to_active(*notification);
    }

    auto* request = std::get_if<protocol::JsonRpcRequest>(&message);
    if (request == nullptr) {
      return mcp::core::unexpected(make_ws_server_error(
          protocol::ErrorCode::InvalidRequest,
          "websocket server transport cannot send unknown message"));
    }
    return send_request_to_active(std::move(*request));
  }

  core::Result<std::optional<protocol::JsonRpcMessage>> receive() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_ && inbound_.empty()) {
        return std::nullopt;
      }
    }

    std::unique_lock<std::mutex> lock(mutex_);
    receive_cv_.wait(lock, [this] {
      return closed_ || startup_error_ || !inbound_.empty();
    });
    if (startup_error_) {
      return mcp::core::unexpected(*startup_error_);
    }
    if (inbound_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(inbound_.front());
    inbound_.pop_front();
    return message;
  }

  void wait_until_ready() {
    std::unique_lock<std::mutex> lock(ready_mutex_);
    ready_cv_.wait(lock, [this] { return ready_ || closed_; });
  }

  void start() {
    const auto host = options_.listen_host;
    const auto port = options_.listen_port;

    server_thread_ = std::thread([this, host, port]() {
      try {
        bool bound = false;
        if (port > 0) {
          bound = server_.bind_to_port(host, port);
        } else {
          int assigned = server_.bind_to_any_port(host);
          bound = assigned > 0;
        }
        if (!bound) {
          {
            std::lock_guard<std::mutex> lock(mutex_);
            startup_error_ =
                make_ws_server_error(protocol::ErrorCode::InternalError,
                                     "websocket server failed to bind to " +
                                         host + ":" + std::to_string(port));
            closed_ = true;
          }
          mark_ready();
          receive_cv_.notify_all();
          return;
        }
        mark_ready();
        server_.listen_after_bind();
      } catch (const std::exception& ex) {
        {
          std::lock_guard<std::mutex> lock(mutex_);
          startup_error_ = make_ws_server_error(
              protocol::ErrorCode::InternalError,
              "websocket server failed to start", ex.what());
          closed_ = true;
        }
        mark_ready();
        receive_cv_.notify_all();
      }
    });

    wait_until_ready();
  }

  core::Result<core::Unit> close() {
    std::map<protocol::RequestId, std::shared_ptr<PendingClientRequest>>
        pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return core::Unit{};
      }
      closed_ = true;
      pending.swap(pending_client_requests_);
    }

    // Fail all pending requests
    for (auto& [_, req] : pending) {
      {
        std::lock_guard<std::mutex> req_lock(req->mutex);
        req->response = protocol::make_error_response(
            std::optional<protocol::RequestId>{req->id},
            protocol::make_error(protocol::ErrorCode::InternalError,
                                 "websocket server transport closed"));
      }
      req->cv.notify_all();
    }

    receive_cv_.notify_all();
    mark_ready();  // Unblock wait_until_ready

    // Stop the HTTP server
    server_.stop();

    // Join server thread
    if (server_thread_.joinable() &&
        server_thread_.get_id() != std::this_thread::get_id()) {
      server_thread_.join();
    }

    return core::Unit{};
  }

 private:
  struct PendingClientRequest {
    explicit PendingClientRequest(protocol::RequestId request_id)
        : id(std::move(request_id)) {}

    protocol::RequestId id;
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<protocol::JsonRpcResponse> response;
  };

  void setup_server() {
    server_.set_websocket_ping_interval(options_.ping_interval_sec);
    server_.set_websocket_max_missed_pongs(options_.max_missed_pongs);
    server_.WebSocket(options_.path, [this](const httplib::Request& req,
                                            httplib::ws::WebSocket& ws) {
      handle_connection(req, ws);
    });
  }

  void handle_connection(const httplib::Request& /*req*/,
                         httplib::ws::WebSocket& ws) {
    const auto conn_id = "conn-" + std::to_string(next_connection_id_++);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        ws.close();
        return;
      }
      if (options_.max_connections > 0 &&
          connections_.size() >= options_.max_connections) {
        ws.close(httplib::ws::CloseStatus::PolicyViolation,
                 "too many websocket connections");
        return;
      }
      active_connection_id_ = conn_id;
      connections_[conn_id] = &ws;
    }
    connection_count_++;

    while (true) {
      std::string raw;
      auto result = ws.read(raw);
      if (!result) {
        break;  // Connection closed or error
      }

      messages_received_++;

      auto parsed = protocol::parse_message(raw);
      if (!parsed) {
        continue;  // Invalid JSON-RPC
      }

      // If it's a request from the client, track the connection for routing
      // the response back
      if (auto* request = std::get_if<protocol::JsonRpcRequest>(&*parsed)) {
        auto pending = std::make_shared<PendingClientRequest>(request->id);
        {
          std::lock_guard<std::mutex> lock(mutex_);
          if (closed_) {
            break;
          }
          pending_client_requests_.emplace(request->id, pending);
          connection_for_request_[request->id] = conn_id;
        }
      }

      // Enqueue for receive()
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
          break;
        }
        inbound_.push_back(std::move(*parsed));
      }
      receive_cv_.notify_one();
    }

    // Connection closed — clean up
    connection_count_--;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (active_connection_id_ == conn_id) {
        active_connection_id_.clear();
      }
      connections_.erase(conn_id);

      // Fail any pending requests from this connection
      std::vector<protocol::RequestId> to_erase;
      for (auto& [req_id, conn] : connection_for_request_) {
        if (conn == conn_id) {
          to_erase.push_back(req_id);
        }
      }
      for (const auto& req_id : to_erase) {
        auto it = pending_client_requests_.find(req_id);
        if (it != pending_client_requests_.end()) {
          {
            std::lock_guard<std::mutex> req_lock(it->second->mutex);
            it->second->response = protocol::make_error_response(
                std::optional<protocol::RequestId>{req_id},
                protocol::make_error(protocol::ErrorCode::InternalError,
                                     "websocket client disconnected"));
          }
          it->second->cv.notify_all();
          pending_client_requests_.erase(it);
        }
        connection_for_request_.erase(req_id);
      }
    }
  }

  core::Result<core::Unit> complete_client_request(
      protocol::JsonRpcResponse response) {
    const auto id = *response.id;
    std::shared_ptr<PendingClientRequest> pending;
    std::string conn_id;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = pending_client_requests_.find(id);
      if (it == pending_client_requests_.end()) {
        return mcp::core::unexpected(make_ws_server_error(
            protocol::ErrorCode::InvalidRequest,
            "websocket server transport has no pending client request",
            request_id_to_string(id)));
      }
      pending = it->second;
      pending_client_requests_.erase(it);

      auto conn_it = connection_for_request_.find(id);
      if (conn_it != connection_for_request_.end()) {
        conn_id = conn_it->second;
        connection_for_request_.erase(conn_it);
      }
    }

    auto serialized = protocol::serialize_response(response);
    if (!serialized) {
      return mcp::core::unexpected(
          make_ws_server_error(protocol::ErrorCode::InternalError,
                               "failed to serialize websocket response"));
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto ws_it = connections_.find(conn_id);
      if (ws_it == connections_.end() || ws_it->second == nullptr) {
        return mcp::core::unexpected(make_ws_server_error(
            protocol::ErrorCode::InternalError,
            "websocket request connection is no longer active"));
      }
      if (!ws_it->second->send(*serialized)) {
        return mcp::core::unexpected(make_ws_server_error(
            protocol::ErrorCode::InternalError, "websocket send failed"));
      }
      messages_sent_++;
    }

    return core::Unit{};
  }

  core::Result<core::Unit> send_notification_to_active(
      const protocol::JsonRpcNotification& notification) {
    auto serialized = protocol::serialize_notification(notification);
    if (!serialized) {
      return mcp::core::unexpected(
          make_ws_server_error(protocol::ErrorCode::InternalError,
                               "failed to serialize websocket notification"));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto ws_it = connections_.find(active_connection_id_);
    if (ws_it == connections_.end() || ws_it->second == nullptr) {
      return mcp::core::unexpected(
          make_ws_server_error(protocol::ErrorCode::InternalError,
                               "no active websocket connection"));
    }
    if (!ws_it->second->send(*serialized)) {
      return mcp::core::unexpected(make_ws_server_error(
          protocol::ErrorCode::InternalError, "websocket send failed"));
    }
    messages_sent_++;
    return core::Unit{};
  }

  core::Result<core::Unit> send_request_to_active(
      protocol::JsonRpcRequest request) {
    auto pending = std::make_shared<PendingClientRequest>(request.id);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (connections_.find(active_connection_id_) == connections_.end()) {
        return mcp::core::unexpected(
            make_ws_server_error(protocol::ErrorCode::InternalError,
                                 "no active websocket connection"));
      }
      const auto [_, inserted] =
          pending_client_requests_.emplace(request.id, pending);
      if (!inserted) {
        return mcp::core::unexpected(
            make_ws_server_error(protocol::ErrorCode::InvalidRequest,
                                 "duplicate websocket server request id",
                                 request_id_to_string(request.id)));
      }
    }

    auto serialized = protocol::serialize_request(request);
    if (!serialized) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_client_requests_.erase(request.id);
      }
      return mcp::core::unexpected(
          make_ws_server_error(protocol::ErrorCode::InternalError,
                               "failed to serialize websocket request"));
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto ws_it = connections_.find(active_connection_id_);
      if (ws_it == connections_.end() || ws_it->second == nullptr ||
          !ws_it->second->send(*serialized)) {
        pending_client_requests_.erase(request.id);
        return mcp::core::unexpected(make_ws_server_error(
            protocol::ErrorCode::InternalError, "websocket send failed"));
      }
      messages_sent_++;
    }

    // Response will arrive through handle_connection and be routed back
    return core::Unit{};
  }

  void mark_ready() {
    {
      std::lock_guard<std::mutex> lock(ready_mutex_);
      ready_ = true;
    }
    ready_cv_.notify_all();
  }

  WebSocketServerTransportOptions options_;
  httplib::Server server_;
  std::thread server_thread_;
  mutable std::mutex mutex_;
  std::mutex ready_mutex_;
  std::condition_variable receive_cv_;
  std::condition_variable ready_cv_;
  std::deque<protocol::JsonRpcMessage> inbound_;
  std::map<protocol::RequestId, std::shared_ptr<PendingClientRequest>>
      pending_client_requests_;
  std::map<protocol::RequestId, std::string> connection_for_request_;
  std::map<std::string, httplib::ws::WebSocket*> connections_;
  std::string active_connection_id_;
  bool closed_ = false;
  bool ready_ = false;
  std::optional<core::Error> startup_error_;
  std::atomic<std::size_t> connection_count_{0};
  std::atomic<std::uint64_t> next_connection_id_{1};
  std::atomic<std::uint64_t> messages_received_{0};
  std::atomic<std::uint64_t> messages_sent_{0};
};

// ---------------------------------------------------------------------------
// WebSocketServerTransport forwarding methods
// ---------------------------------------------------------------------------

WebSocketServerTransport::WebSocketServerTransport(
    WebSocketServerTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {
  impl_->start();
}

WebSocketServerTransport::~WebSocketServerTransport() = default;

std::string_view WebSocketServerTransport::name() const noexcept {
  return "websocket-server";
}

protocol::Json WebSocketServerTransport::diagnostics() const {
  return impl_->diagnostics();
}

core::Result<core::Unit> WebSocketServerTransport::send(TxMessage message) {
  return impl_->send(std::move(message));
}

core::Result<std::optional<WebSocketServerTransport::RxMessage>>
WebSocketServerTransport::receive() {
  return impl_->receive();
}

core::Result<core::Unit> WebSocketServerTransport::close() {
  return impl_->close();
}

void WebSocketServerTransport::wait_until_ready() { impl_->wait_until_ready(); }

}  // namespace mcp::transport
