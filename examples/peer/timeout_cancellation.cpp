// Copyright (c) 2025 [caomengxuan666]
//
// Canonical Peer/Service example: demonstrates request timeout and cooperative
// cancellation on a role-generic client transport.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/service.hpp"
#include "cxxmcp/transport.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class BlockingClientTransport final : public mcp::transport::ClientTransport {
 public:
  std::string_view name() const noexcept override {
    return "timeout-cancellation-loopback";
  }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (const auto* request =
              std::get_if<mcp::protocol::JsonRpcRequest>(&message)) {
        request_ = *request;
        request_started_ = true;
      } else if (const auto* notification =
                     std::get_if<mcp::protocol::JsonRpcNotification>(
                         &message)) {
        notifications_.push_back(*notification);
      }
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return closed_ || release_response_; });
    if (closed_ || !request_.has_value()) {
      return std::nullopt;
    }

    auto request = *request_;
    request_finished_ = true;
    cv_.notify_all();
    return mcp::protocol::JsonRpcMessage{make_response(request)};
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

  void release_response() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      release_response_ = true;
    }
    cv_.notify_all();
  }

  void wait_until_request_started() {
    std::unique_lock<std::mutex> lock(mutex_);
    require(cv_.wait_for(lock, std::chrono::seconds(1),
                         [&] { return request_started_; }),
            "request did not start");
  }

  void wait_until_request_finished() {
    std::unique_lock<std::mutex> lock(mutex_);
    require(cv_.wait_for(lock, std::chrono::seconds(1),
                         [&] { return request_finished_; }),
            "request did not finish");
  }

  std::vector<mcp::protocol::JsonRpcNotification> wait_until_notifications(
      std::size_t count) {
    std::unique_lock<std::mutex> lock(mutex_);
    require(cv_.wait_for(lock, std::chrono::seconds(1),
                         [&] { return notifications_.size() >= count; }),
            "cancellation notification was not sent");
    return notifications_;
  }

 private:
  static mcp::protocol::JsonRpcResponse make_response(
      const mcp::protocol::JsonRpcRequest& request) {
    if (request.method == mcp::protocol::ToolsListMethod) {
      return mcp::protocol::JsonRpcResponse{
          request.id,
          Json{{"tools",
                Json::array({Json{{"name", "slow.echo"},
                                  {"description", "Slow echo tool"},
                                  {"inputSchema", Json{{"type", "object"}}},
                                  {"streaming", false}}})}},
          std::nullopt};
    }
    return mcp::protocol::JsonRpcResponse{request.id, Json::object(),
                                          std::nullopt};
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<mcp::protocol::JsonRpcRequest> request_;
  std::vector<mcp::protocol::JsonRpcNotification> notifications_;
  bool request_started_ = false;
  bool request_finished_ = false;
  bool release_response_ = false;
  bool closed_ = false;
};

void require_cancelled_notification(
    const std::vector<mcp::protocol::JsonRpcNotification>& notifications,
    const mcp::protocol::RequestId& request_id, std::string_view reason) {
  require(!notifications.empty(), "cancelled notification missing");
  const auto& notification = notifications.front();
  require(notification.method == mcp::protocol::CancelledNotificationMethod,
          "unexpected cancellation notification method");

  const auto params = mcp::protocol::cancelled_notification_params_from_json(
      notification.params);
  require(params.has_value(), "cancelled notification params did not parse");
  require(params->reason == reason, "cancelled notification reason mismatch");
  require(params->request_id == request_id,
          "cancelled notification request id mismatch");
}

void demonstrate_timeout() {
  auto transport = std::make_unique<BlockingClientTransport>();
  auto* transport_ptr = transport.get();
  mcp::ClientPeer peer(std::move(transport));
  auto running = mcp::serve(std::move(peer));
  require(running.has_value(), "client peer service failed to start");

  mcp::RequestOptions options;
  options.timeout = std::chrono::milliseconds(5);
  auto handle = running->peer().list_tools_async(options);

  transport_ptr->wait_until_request_started();
  const auto response = handle.await_response();
  require(!response.has_value(), "timed request should fail");
  require(response.error().message == "request timed out",
          "timeout error message mismatch");

  const auto notifications = transport_ptr->wait_until_notifications(1);
  require_cancelled_notification(notifications, handle.request_id(),
                                 "request timeout");

  transport_ptr->release_response();
  transport_ptr->wait_until_request_finished();
  require(running->stop().has_value(), "timeout peer stop failed");
}

void demonstrate_cancellation() {
  auto transport = std::make_unique<BlockingClientTransport>();
  auto* transport_ptr = transport.get();
  mcp::ClientPeer peer(std::move(transport));
  auto running = mcp::serve(std::move(peer));
  require(running.has_value(), "client peer service failed to start");

  mcp::CancellationSource cancellation;
  mcp::RequestOptions options;
  options.cancellation_token = cancellation.token();
  auto handle = running->peer().list_tools_async(options);
  require(handle.cancellation_token().has_value(),
          "request handle should expose cancellation token");

  transport_ptr->wait_until_request_started();
  cancellation.cancel();
  const auto response = handle.await_response();
  require(!response.has_value(), "cancelled request should fail");
  require(response.error().message == "request cancelled",
          "cancellation error message mismatch");

  const auto notifications = transport_ptr->wait_until_notifications(1);
  require_cancelled_notification(notifications, handle.request_id(),
                                 "request cancelled");

  transport_ptr->release_response();
  transport_ptr->wait_until_request_finished();
  require(running->stop().has_value(), "cancelled peer stop failed");
}

}  // namespace

int main() {
  try {
    demonstrate_timeout();
    demonstrate_cancellation();
    std::cout << "timeout/cancellation example passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "timeout/cancellation example failed: " << ex.what() << '\n';
    return 1;
  }
}
