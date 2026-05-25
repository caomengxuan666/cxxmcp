// Copyright (c) 2025 [caomengxuan666]

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "cxxmcp/handler.hpp"
#include "cxxmcp/service.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class BlockingTransport final : public mcp::server::Transport {
 public:
  mcp::core::Result<mcp::core::Unit> start(
      mcp::server::RequestHandler,
      mcp::server::NotificationHandler = {}) override {
    std::unique_lock<std::mutex> lock(mutex_);
    started_ = true;
    cv_.notify_all();
    cv_.wait(lock, [this] { return stopped_; });
    return mcp::core::Unit{};
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification&) override {
    return mcp::core::Unit{};
  }

  void stop() noexcept override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    cv_.notify_all();
  }

  std::string_view name() const noexcept override { return "blocking"; }

  void wait_until_started() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return started_; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool started_ = false;
  bool stopped_ = false;
};

void test_handler_and_service_headers_are_linkable() {
  mcp::server::ServerBuilder builder;
  builder.name("public-target-test").version("1.0.0");

  auto server = builder.build();
  require(server.has_value(), "server builder failed");
  auto transport = std::make_unique<BlockingTransport>();
  auto* transport_ptr = transport.get();
  require((*server)->add_transport(std::move(transport)).has_value(),
          "server transport add failed");

  auto running = mcp::serve(mcp::ServerPeer(std::move(*server)));
  require(running.has_value(), "service target did not expose serve()");
  transport_ptr->wait_until_started();
  require(running->running(), "running service should report running");
  require(running->stop().has_value(), "running service stop failed");
}

}  // namespace

int main() {
  try {
    test_handler_and_service_headers_are_linkable();
    std::cout << "public target compile/link test passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "public target compile/link test failed: " << ex.what()
              << '\n';
    return 1;
  }
}
