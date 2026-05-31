// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/service.hpp"
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

std::string serialize_initialize_response(
    const mcp::protocol::RequestId& request_id) {
  mcp::protocol::InitializeResult result;
  result.server_info.name = "websocket-test-server";
  result.server_info.version = "1.0.0";
  const auto response = mcp::protocol::JsonRpcResponse{
      .id = request_id,
      .result = mcp::protocol::initialize_result_to_json(result),
  };
  return serialize_test_response(response);
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

class HeaderCheckingWebSocketServer {
 public:
  HeaderCheckingWebSocketServer() {
    server_.WebSocket("/mcp", [this](const httplib::Request& req,
                                     httplib::ws::WebSocket& ws) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (req.has_header("Authorization")) {
          authorization_ = req.get_header_value("Authorization");
        }
      }
      std::string msg;
      while (ws.read(msg)) {
        auto parsed = mcp::protocol::parse_message(msg);
        if (!parsed) {
          continue;
        }
        if (auto* request =
                std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed)) {
          ws.send(serialize_initialize_response(request->id));
        }
      }
    });

    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      throw std::runtime_error("failed to bind header websocket test server");
    }
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  ~HeaderCheckingWebSocketServer() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  int port() const { return port_; }

  std::optional<std::string> authorization() {
    std::lock_guard<std::mutex> lock(mutex_);
    return authorization_;
  }

 private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  std::mutex mutex_;
  std::optional<std::string> authorization_;
};

class ReconnectingWebSocketServer {
 public:
  ReconnectingWebSocketServer() {
    server_.WebSocket("/mcp", [this](const httplib::Request& /*req*/,
                                     httplib::ws::WebSocket& ws) {
      const auto connection_index = ++connections_;
      cv_.notify_all();

      std::string msg;
      while (ws.read(msg)) {
        auto parsed = mcp::protocol::parse_message(msg);
        if (!parsed) {
          continue;
        }
        if (auto* request =
                std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed)) {
          ws.send(serialize_test_response(mcp::protocol::JsonRpcResponse{
              .id = request->id,
              .result = Json::object(),
          }));
          if (connection_index == 1) {
            ws.close();
            return;
          }
        }
      }
    });

    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      throw std::runtime_error(
          "failed to bind reconnect websocket test server");
    }
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  ~ReconnectingWebSocketServer() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  int port() const { return port_; }

  void wait_for_connections(int expected) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    cv_.wait_until(lock, deadline, [this, expected] {
      return connections_.load() >= expected;
    });
    require(connections_.load() >= expected,
            "websocket client should reconnect");
  }

 private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<int> connections_{0};
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
// Test: Client transport reconnects on dropped connection
// ---------------------------------------------------------------------------
void test_websocket_client_transport_reconnects_after_drop() {
  std::cout << "  test_websocket_client_transport_reconnects_after_drop... ";

  ReconnectingWebSocketServer server;

  mcp::transport::WebSocketClientTransportOptions options;
  options.uri = "ws://127.0.0.1:" + std::to_string(server.port()) + "/mcp";
  options.auto_reconnect = true;
  options.reconnect_initial_delay = std::chrono::milliseconds(10);
  options.reconnect_max_delay = std::chrono::milliseconds(20);
  options.ping_interval_sec = 0;
  mcp::transport::WebSocketClientTransport transport(std::move(options));

  auto first = mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{1},
  };
  auto sent_first =
      transport.send(mcp::protocol::JsonRpcMessage{std::move(first)});
  require(sent_first.has_value(), "first websocket send should succeed");

  auto first_response = transport.receive();
  require(first_response.has_value(), "first websocket receive should succeed");
  require(first_response->has_value(), "first receive should have a message");

  server.wait_for_connections(2);

  auto second = mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::PingMethod,
      .params = Json::object(),
      .id = std::int64_t{2},
  };
  auto sent_second =
      transport.send(mcp::protocol::JsonRpcMessage{std::move(second)});
  require(sent_second.has_value(), "second websocket send should succeed");

  auto second_response = transport.receive();
  require(second_response.has_value(),
          "second websocket receive should succeed");
  require(second_response->has_value(), "second receive should have a message");

  auto closed = transport.close();
  require(closed.has_value(), "close should succeed");

  std::cout << "OK\n";
}

// ---------------------------------------------------------------------------
// Test: Client peer builder preserves bearer auth semantics
// ---------------------------------------------------------------------------
void test_websocket_builder_bearer_token_uses_authorization_scheme() {
  std::cout << "  test_websocket_builder_bearer_token_uses_authorization_scheme"
               "... ";

  HeaderCheckingWebSocketServer server;

  auto peer =
      mcp::ClientPeer::builder()
          .websocket("ws://127.0.0.1:" + std::to_string(server.port()) + "/mcp")
          .bearer_token("test-token")
          .build();
  require(peer.has_value(), "websocket client peer should build");

  auto running = mcp::serve(std::move(*peer));
  require(running.has_value(), "websocket client service should start");

  auto initialized = running->peer().initialize("ws-client", "1.0.0");
  require(initialized.has_value(), "websocket initialize should succeed");

  auto authorization = server.authorization();
  require(authorization.has_value(), "websocket request should send auth");
  require(*authorization == "Bearer test-token",
          "websocket bearer helper should send Authorization: Bearer token");

  auto stopped = running->stop();
  require(stopped.has_value(), "websocket client service should stop");

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

// ---------------------------------------------------------------------------
// Test: Server responses route to the connection that sent the request
// ---------------------------------------------------------------------------
void test_websocket_server_transport_routes_response_to_original_connection() {
  std::cout << "  test_websocket_server_transport_routes_response_to_original_"
               "connection... ";

  mcp::transport::WebSocketServerTransportOptions options;
  options.listen_host = "127.0.0.1";
  options.listen_port = 18767;
  options.path = "/mcp";
  mcp::transport::WebSocketServerTransport transport(std::move(options));

  httplib::ws::WebSocketClient first_client("ws://127.0.0.1:18767/mcp");
  first_client.set_read_timeout(2);
  require(first_client.connect(), "first websocket client should connect");

  auto request = mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{101},
  };
  require(first_client.send(serialize_test_request(request)),
          "first websocket client should send request");

  auto received = transport.receive();
  require(received.has_value(), "server receive should succeed");
  require(received->has_value(), "server should receive request");
  auto* original_request =
      std::get_if<mcp::protocol::JsonRpcRequest>(&**received);
  require(original_request != nullptr,
          "server should receive JSON-RPC request");

  httplib::ws::WebSocketClient second_client("ws://127.0.0.1:18767/mcp");
  second_client.set_read_timeout(1);
  require(second_client.connect(), "second websocket client should connect");

  auto response = mcp::protocol::JsonRpcResponse{
      .id = original_request->id,
      .result = Json::object(),
  };
  auto sent =
      transport.send(mcp::protocol::JsonRpcMessage{std::move(response)});
  require(sent.has_value(), "server response send should succeed");

  std::string first_message;
  require(first_client.read(first_message),
          "original websocket client should receive response");
  auto parsed = mcp::protocol::parse_message(first_message);
  require(parsed.has_value(), "original client response should parse");
  auto* parsed_response = std::get_if<mcp::protocol::JsonRpcResponse>(&*parsed);
  require(parsed_response != nullptr,
          "original client should receive response");
  require(parsed_response->id == original_request->id,
          "response id should match original request");

  first_client.close();
  second_client.close();
  (void)transport.close();

  std::cout << "OK\n";
}

// ---------------------------------------------------------------------------
// Test: Server transport enforces maximum active connections
// ---------------------------------------------------------------------------
void test_websocket_server_transport_limits_active_connections() {
  std::cout
      << "  test_websocket_server_transport_limits_active_connections... ";

  mcp::transport::WebSocketServerTransportOptions options;
  options.listen_host = "127.0.0.1";
  options.listen_port = 18768;
  options.path = "/mcp";
  options.max_connections = 1;
  options.ping_interval_sec = 0;
  options.max_missed_pongs = 0;
  mcp::transport::WebSocketServerTransport transport(std::move(options));

  httplib::ws::WebSocketClient first_client("ws://127.0.0.1:18768/mcp");
  require(first_client.connect(), "first websocket client should connect");

  httplib::ws::WebSocketClient second_client("ws://127.0.0.1:18768/mcp");
  second_client.set_read_timeout(1);
  const auto second_connected = second_client.connect();
  if (second_connected) {
    std::string ignored;
    require(!second_client.read(ignored),
            "second websocket client should be closed by connection limit");
  }

  auto diag = transport.diagnostics();
  require(diag["connections"].get<std::size_t>() <= 1,
          "server should enforce websocket connection limit");

  first_client.close();
  second_client.close();
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
    test_websocket_client_transport_reconnects_after_drop();
    test_websocket_builder_bearer_token_uses_authorization_scheme();
    test_websocket_server_transport_accepts_connection();
    test_websocket_server_transport_diagnostics();
    test_websocket_server_transport_routes_response_to_original_connection();
    test_websocket_server_transport_limits_active_connections();
  } catch (const std::exception& ex) {
    std::cerr << "FAIL: " << ex.what() << "\n";
    return 1;
  }
  std::cout << "All WebSocket transport tests passed.\n";
  return 0;
}
