// Copyright (c) 2025 [caomengxuan666]

#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/transport/websocket_transport.hpp"
#include "httplib.h"

namespace {

using mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string serialize_test_response(
    const mcp::protocol::JsonRpcResponse& response) {
  const auto serialized = mcp::protocol::serialize_response(response);
  require(serialized.has_value(), "response should serialize");
  return *serialized;
}

std::string serialize_test_request(
    const mcp::protocol::JsonRpcRequest& request) {
  const auto serialized = mcp::protocol::serialize_request(request);
  require(serialized.has_value(), "request should serialize");
  return *serialized;
}

std::string serialize_test_notification(
    const mcp::protocol::JsonRpcNotification& notification) {
  const auto serialized = mcp::protocol::serialize_notification(notification);
  require(serialized.has_value(), "notification should serialize");
  return *serialized;
}

/// @brief Mock WebSocket server that echoes requests back as responses.
class MockWebSocketServer {
 public:
  MockWebSocketServer() {
    server_.WebSocket("/mcp", [this](const httplib::Request& /*req*/,
                                     httplib::ws::WebSocket& ws) {
      std::string msg;
      while (ws.read(msg)) {
        std::lock_guard<std::mutex> lock(mutex_);
        received_messages_.push_back(msg);

        // Try to parse as a request and echo a response
        auto parsed = mcp::protocol::parse_message(msg);
        if (parsed) {
          if (auto* req =
                  std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed)) {
            auto response = mcp::protocol::JsonRpcResponse{
                .id = req->id,
                .result = Json::object(),
            };
            ws.send(serialize_test_response(response));
          } else if (auto* notif =
                         std::get_if<mcp::protocol::JsonRpcNotification>(
                             &*parsed)) {
            // Store notifications
          }
        }
      }
    });

    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      throw std::runtime_error("failed to bind websocket test server");
    }
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  ~MockWebSocketServer() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  int port() const { return port_; }

  std::vector<std::string> received_messages() {
    std::lock_guard<std::mutex> lock(mutex_);
    return received_messages_;
  }

 private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  std::mutex mutex_;
  std::vector<std::string> received_messages_;
};

// ---------------------------------------------------------------------------
// Test: Client transport send and receive roundtrip
// ---------------------------------------------------------------------------
void test_websocket_client_transport_sends_and_receives() {
  std::cout << "  test_websocket_client_transport_sends_and_receives... ";

  MockWebSocketServer mock;

  mcp::transport::WebSocketClientTransportOptions options;
  options.uri = "ws://127.0.0.1:" + std::to_string(mock.port()) + "/mcp";
  options.auto_reconnect = false;
  mcp::transport::WebSocketClientTransport transport(std::move(options));

  // Send an initialize request
  mcp::protocol::JsonRpcRequest request{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{1},
  };
  auto sent = transport.send(mcp::protocol::JsonRpcMessage{std::move(request)});
  require(sent.has_value(), "send should succeed");

  // Receive the response
  auto received = transport.receive();
  require(received.has_value(), "receive should succeed");
  require(received->has_value(), "should have a message");

  auto* response = std::get_if<mcp::protocol::JsonRpcResponse>(&**received);
  require(response != nullptr, "message should be a response");
  require(response->id.has_value(), "response should have an id");

  auto closed = transport.close();
  require(closed.has_value(), "close should succeed");

  std::cout << "OK\n";
}

// ---------------------------------------------------------------------------
// Test: Client transport close unblocks receive
// ---------------------------------------------------------------------------
void test_websocket_client_transport_close_unblocks_receive() {
  std::cout << "  test_websocket_client_transport_close_unblocks_receive... ";

  MockWebSocketServer mock;

  mcp::transport::WebSocketClientTransportOptions options;
  options.uri = "ws://127.0.0.1:" + std::to_string(mock.port()) + "/mcp";
  options.auto_reconnect = false;
  mcp::transport::WebSocketClientTransport transport(std::move(options));

  // Start receive in a background thread
  std::optional<mcp::core::Result<std::optional<mcp::protocol::JsonRpcMessage>>>
      result;
  std::thread reader([&]() { result = transport.receive(); });

  // Give the reader time to block
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Close should unblock receive
  auto closed = transport.close();
  require(closed.has_value(), "close should succeed");

  reader.join();
  require(result.has_value(), "receive should have returned");
  require(result->has_value(), "receive should succeed");
  require(!(*result)->has_value(), "receive should return nullopt on close");

  std::cout << "OK\n";
}

// ---------------------------------------------------------------------------
// Test: Client transport diagnostics
// ---------------------------------------------------------------------------
void test_websocket_client_transport_diagnostics() {
  std::cout << "  test_websocket_client_transport_diagnostics... ";

  MockWebSocketServer mock;

  mcp::transport::WebSocketClientTransportOptions options;
  options.uri = "ws://127.0.0.1:" + std::to_string(mock.port()) + "/mcp";
  options.auto_reconnect = false;
  mcp::transport::WebSocketClientTransport transport(std::move(options));

  auto diag = transport.diagnostics();
  require(diag.is_object(), "diagnostics should be an object");
  require(diag.contains("name"), "diagnostics should have name");
  require(diag["name"] == "websocket-client",
          "name should be websocket-client");
  require(diag.contains("closed"), "diagnostics should have closed");
  require(diag.contains("connected"), "diagnostics should have connected");

  auto closed = transport.close();
  require(closed.has_value(), "close should succeed");

  std::cout << "OK\n";
}

// ---------------------------------------------------------------------------
// Test: Server transport accepts connections and receives messages
// ---------------------------------------------------------------------------
void test_websocket_server_transport_accepts_connection() {
  std::cout << "  test_websocket_server_transport_accepts_connection... ";

  mcp::transport::WebSocketServerTransportOptions options;
  options.listen_host = "127.0.0.1";
  options.listen_port = 0;  // Let the OS pick a port
  options.path = "/mcp";
  mcp::transport::WebSocketServerTransport transport(std::move(options));

  // The server is now listening. We need to get the port somehow.
  // Since we used port 0, the OS assigned a port. We can get it from
  // diagnostics.
  // For this test, we'll use a fixed port approach instead.
  // Let's recreate with a specific port.
  (void)transport.close();

  // Use a specific port for testing
  mcp::transport::WebSocketServerTransportOptions options2;
  options2.listen_host = "127.0.0.1";
  options2.listen_port = 18765;
  options2.path = "/mcp";
  mcp::transport::WebSocketServerTransport transport2(std::move(options2));

  // Connect a raw WebSocket client
  httplib::ws::WebSocketClient ws_client("ws://127.0.0.1:18765/mcp");
  require(ws_client.connect(), "ws client should connect");

  // Send an initialize request
  mcp::protocol::JsonRpcRequest request{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{1},
  };
  ws_client.send(serialize_test_request(request));

  // Receive from the server transport
  auto received = transport2.receive();
  require(received.has_value(), "server receive should succeed");
  require(received->has_value(), "should have a message");

  auto* req = std::get_if<mcp::protocol::JsonRpcRequest>(&**received);
  require(req != nullptr, "message should be a request");

  // Send a response back through the transport
  auto response = mcp::protocol::JsonRpcResponse{
      .id = req->id,
      .result = Json{{"serverInfo", Json{{"name", "test"}, {"version", "1.0"}}},
                     {"capabilities", Json::object()}},
  };
  auto sent =
      transport2.send(mcp::protocol::JsonRpcMessage{std::move(response)});
  require(sent.has_value(), "server send should succeed");

  // Client should receive the response
  std::string client_msg;
  require(ws_client.read(client_msg), "ws client should read response");

  ws_client.close();
  (void)transport2.close();

  std::cout << "OK\n";
}

// ---------------------------------------------------------------------------
// Test: Server transport diagnostics
// ---------------------------------------------------------------------------
void test_websocket_server_transport_diagnostics() {
  std::cout << "  test_websocket_server_transport_diagnostics... ";

  mcp::transport::WebSocketServerTransportOptions options;
  options.listen_host = "127.0.0.1";
  options.listen_port = 18766;
  options.path = "/mcp";
  mcp::transport::WebSocketServerTransport transport(std::move(options));

  auto diag = transport.diagnostics();
  require(diag.is_object(), "diagnostics should be an object");
  require(diag.contains("name"), "diagnostics should have name");
  require(diag["name"] == "websocket-server",
          "name should be websocket-server");
  require(diag.contains("closed"), "diagnostics should have closed");
  require(diag.contains("connections"), "diagnostics should have connections");

  (void)transport.close();

  std::cout << "OK\n";
}

}  // namespace

int main() {
  std::cout << "WebSocket transport tests:\n";
  try {
    test_websocket_client_transport_sends_and_receives();
    test_websocket_client_transport_close_unblocks_receive();
    test_websocket_client_transport_diagnostics();
    test_websocket_server_transport_accepts_connection();
    test_websocket_server_transport_diagnostics();
  } catch (const std::exception& ex) {
    std::cerr << "FAIL: " << ex.what() << "\n";
    return 1;
  }
  std::cout << "All WebSocket transport tests passed.\n";
  return 0;
}
