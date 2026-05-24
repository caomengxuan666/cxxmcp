// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "cxxmcp/client/http_transport.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server/http_transport.hpp"
#include "cxxmcp/server/peer.hpp"
#include "httplib.h"

namespace {

using mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string sse_event(const std::string& json) {
  return "data: " + json + "\n\n";
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

class HttpServerFixture {
 public:
  HttpServerFixture() {
    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      throw std::runtime_error("failed to bind http test server");
    }
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  ~HttpServerFixture() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  httplib::Server& server() { return server_; }

  int port() const { return port_; }

 private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
};

class RunningServerTransportFixture {
 public:
  explicit RunningServerTransportFixture(
      std::unique_ptr<mcp::server::HttpTransport> transport,
      mcp::server::RequestHandler handler,
      mcp::server::NotificationHandler notification_handler = {})
      : transport_(std::move(transport)) {
    thread_ = std::thread(
        [this, handler = std::move(handler),
         notification_handler = std::move(notification_handler)]() mutable {
          const auto started = transport_->start(
              std::move(handler), std::move(notification_handler));
          if (!started) {
            std::lock_guard lock(mutex_);
            start_error_ = started.error();
          }
        });
  }

  ~RunningServerTransportFixture() {
    if (transport_) {
      transport_->stop();
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  mcp::server::HttpTransport& transport() { return *transport_; }

  std::optional<mcp::core::Error> start_error() const {
    std::lock_guard lock(mutex_);
    return start_error_;
  }

 private:
  std::unique_ptr<mcp::server::HttpTransport> transport_;
  std::thread thread_;
  mutable std::mutex mutex_;
  std::optional<mcp::core::Error> start_error_;
};

bool wait_for_http_initialize(int port, const std::string& path) {
  const auto initialize_request =
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::InitializeMethod),
          .params = Json::object(),
          .id = std::int64_t{1},
      });
  const httplib::Headers initialize_headers{
      {"Accept", "application/json"},
      {"Content-Type", "application/json"},
      {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
  };

  for (int attempt = 0; attempt < 100; ++attempt) {
    httplib::Client client("127.0.0.1", port);
    const auto response = client.Post(path, initialize_headers,
                                      initialize_request, "application/json");
    if (response && response->status >= 200 && response->status < 300) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

bool wait_for(std::function<bool()> predicate,
              std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

void test_http_transport_decodes_sse_post_and_answers_interleaved_request() {
  std::atomic<bool> client_response_seen{false};
  std::atomic<bool> interleaved_request_handled{false};

  HttpServerFixture fixture;
  fixture.server().Post("/mcp", [&](const httplib::Request& request,
                                    httplib::Response& response) {
    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "server should parse client message");

    if (const auto* rpc_response =
            std::get_if<mcp::protocol::JsonRpcResponse>(&*parsed)) {
      require(rpc_response->id.has_value(), "posted response should have id");
      require(std::get<std::string>(*rpc_response->id) == "srv-1",
              "posted response should answer server request");
      require(rpc_response->result.has_value(),
              "posted response should contain result");
      require(rpc_response->result->at("model") == "test-model",
              "posted response payload mismatch");
      client_response_seen.store(true);
      response.status = 202;
      return;
    }

    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "server should receive a request");
    require(rpc_request->method == "tools/list",
            "server should receive tools/list");

    const auto server_request =
        serialize_test_request(mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::SamplingCreateMessageMethod),
            .params =
                Json{
                    {"messages", Json::array()},
                    {"maxTokens", 16},
                },
            .id = std::string("srv-1"),
        });
    const auto final_response =
        serialize_test_response(mcp::protocol::JsonRpcResponse{
            .id = rpc_request->id,
            .result = Json{{"tools", Json::array()}},
        });

    response.set_content(sse_event(server_request) + sse_event(final_response),
                         "text/event-stream");
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = fixture.port(),
      .path = "/mcp",
      .timeout = std::chrono::milliseconds(2000),
  });
  const auto started =
      transport.start([&](const mcp::protocol::JsonRpcRequest& request)
                          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        interleaved_request_handled.store(true);
        require(request.method == mcp::protocol::SamplingCreateMessageMethod,
                "client should receive sampling request");
        return mcp::protocol::make_response(request.id,
                                            Json{{"model", "test-model"}});
      });
  require(started.has_value(), "http transport should start");

  const auto actual = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = Json::object(),
      .id = std::int64_t{9},
  });
  require(actual.has_value(), "sse post should return final response");
  require(actual->result.has_value(), "final response should contain result");
  require(actual->result->at("tools").is_array(),
          "final response payload mismatch");
  require(interleaved_request_handled.load(),
          "interleaved server request should be handled");
  require(wait_for([&]() { return client_response_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "client should post response for interleaved request");
}

void test_http_transport_opens_get_sse_after_session_and_dispatches_notification() {
  std::atomic<bool> get_seen{false};
  std::atomic<bool> notification_seen{false};

  HttpServerFixture fixture;
  fixture.server().Post(
      "/mcp", [](const httplib::Request& request, httplib::Response& response) {
        const auto parsed = mcp::protocol::parse_message(request.body);
        require(parsed.has_value(), "server should parse initialize request");
        const auto* rpc_request =
            std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
        require(rpc_request != nullptr, "server should receive request");
        require(rpc_request->method == mcp::protocol::InitializeMethod,
                "server should receive initialize");
        require(!request.has_header("MCP-Protocol-Version"),
                "initialize should not include MCP-Protocol-Version");

        response.set_header("Mcp-Session-Id", "test-session");
        response.set_content(
            serialize_test_response(mcp::protocol::JsonRpcResponse{
                .id = rpc_request->id,
                .result =
                    Json{
                        {"protocolVersion",
                         std::string(mcp::protocol::McpProtocolVersion)},
                        {"capabilities", Json::object()},
                        {"serverInfo",
                         Json{{"name", "sse-test"}, {"version", "1"}}},
                    },
            }),
            "application/json");
      });
  fixture.server().Get("/mcp", [&](const httplib::Request& request,
                                   httplib::Response& response) {
    require(request.has_header("Mcp-Session-Id"),
            "sse get should include session id");
    require(request.get_header_value("Mcp-Session-Id") == "test-session",
            "sse get session id mismatch");
    require(request.has_header("MCP-Protocol-Version"),
            "sse get should include protocol version");
    require(request.get_header_value("MCP-Protocol-Version") ==
                std::string(mcp::protocol::McpProtocolVersion),
            "sse get protocol version mismatch");
    get_seen.store(true);

    const auto notification =
        serialize_test_notification(mcp::protocol::JsonRpcNotification{
            .method =
                std::string(mcp::protocol::ToolsListChangedNotificationMethod),
            .params = Json::object(),
        });
    response.set_chunked_content_provider(
        "text/event-stream",
        [sent = false, notification](size_t, httplib::DataSink& sink) mutable {
          if (!sent) {
            const auto event = sse_event(notification);
            sent = true;
            return sink.write(event.data(), event.size());
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
          if (!sink.is_writable()) {
            sink.done();
            return false;
          }
          return true;
        });
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = fixture.port(),
      .path = "/mcp",
      .timeout = std::chrono::milliseconds(2000),
  });
  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return std::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected request",
        });
      },
      [&](const mcp::protocol::JsonRpcNotification& notification)
          -> mcp::core::Result<mcp::core::Unit> {
        if (notification.method ==
            mcp::protocol::ToolsListChangedNotificationMethod) {
          notification_seen.store(true);
        }
        return mcp::core::Unit{};
      });
  require(started.has_value(), "http transport should start");

  const auto initialized = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(initialized.has_value(), "initialize should complete");
  require(wait_for([&]() { return get_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "http transport should open an SSE GET stream");
  require(wait_for([&]() { return notification_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "http transport should dispatch SSE notification");

  transport.stop();
}

void test_http_transport_sets_method_and_name_headers() {
  std::atomic<bool> get_seen{false};
  std::atomic<bool> call_seen{false};

  HttpServerFixture fixture;
  fixture.server().Post("/mcp", [&](const httplib::Request& request,
                                    httplib::Response& response) {
    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "server should parse client message");
    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "server should receive request");

    require(request.has_header("Mcp-Method"), "post should include Mcp-Method");
    require(request.get_header_value("Mcp-Method") == rpc_request->method,
            "post method header mismatch");

    if (rpc_request->method == mcp::protocol::InitializeMethod) {
      require(!request.has_header("Mcp-Name"),
              "initialize should not include Mcp-Name");
      require(!request.has_header("MCP-Protocol-Version"),
              "initialize should not include protocol version header");
      response.set_header("Mcp-Session-Id", "header-session");
      response.set_content(
          serialize_test_response(mcp::protocol::JsonRpcResponse{
              .id = rpc_request->id,
              .result =
                  Json{
                      {"protocolVersion",
                       std::string(mcp::protocol::McpProtocolVersion)},
                      {"capabilities", Json::object()},
                      {"serverInfo",
                       Json{{"name", "headers-test"}, {"version", "1"}}},
                  },
          }),
          "application/json");
      return;
    }

    require(rpc_request->method == mcp::protocol::ToolsCallMethod,
            "server should receive tools/call");
    require(request.has_header("Mcp-Name"),
            "tools/call should include Mcp-Name");
    require(request.get_header_value("Mcp-Name") == "echo",
            "tools/call name header mismatch");
    call_seen.store(true);
    response.set_content(serialize_test_response(mcp::protocol::JsonRpcResponse{
                             .id = rpc_request->id,
                             .result =
                                 Json{
                                     {"content", Json::array()},
                                     {"isError", false},
                                 },
                         }),
                         "application/json");
  });
  fixture.server().Get("/mcp", [&](const httplib::Request& request,
                                   httplib::Response& response) {
    if (!request.has_header("Mcp-Session-Id") ||
        request.get_header_value("Mcp-Session-Id") != "header-session") {
      response.status = 404;
      return;
    }
    get_seen.store(true);
    response.set_chunked_content_provider(
        "text/event-stream", [](size_t, httplib::DataSink& sink) {
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
          if (!sink.is_writable()) {
            sink.done();
            return false;
          }
          return true;
        });
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = fixture.port(),
      .path = "/mcp",
      .timeout = std::chrono::milliseconds(2000),
  });
  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return std::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected request",
        });
      },
      [](const mcp::protocol::JsonRpcNotification&)
          -> mcp::core::Result<mcp::core::Unit> { return mcp::core::Unit{}; });
  require(started.has_value(), "http transport should start");

  const auto initialized = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(initialized.has_value(), "initialize should succeed");
  require(wait_for([&]() { return get_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "client should open a stream after initialize");

  const auto call = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ToolsCallMethod),
      .params = Json{{"name", "echo"}, {"arguments", Json::object()}},
      .id = std::int64_t{2},
  });
  require(call.has_value(), "tools/call should succeed");
  require(call->result.has_value(), "tools/call should return a result");
  require(call_seen.load(), "server should observe tools/call header");

  transport.stop();
}

void test_server_http_transport_delivers_outbound_notification() {
  std::atomic<bool> notification_seen{false};
  constexpr int kPort = 40173;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion",
                       std::string(mcp::protocol::McpProtocolVersion)},
                      {"capabilities", Json::object()},
                      {"serverInfo",
                       Json{{"name", "server-http-test"}, {"version", "1"}}},
                  },
          };
        }
        return mcp::protocol::make_error_response(
            std::optional<mcp::protocol::RequestId>{request.id},
            mcp::protocol::make_error(mcp::protocol::ErrorCode::MethodNotFound,
                                      "unexpected request"));
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");

  const auto initialize_request =
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::InitializeMethod),
          .params = Json::object(),
          .id = std::int64_t{2},
      });
  require(!initialize_request.empty(), "initialize request should serialize");

  httplib::Client http_client("127.0.0.1", kPort);
  const auto initialize_response = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
      },
      initialize_request, "application/json");
  require(static_cast<bool>(initialize_response),
          "server initialize should succeed");
  require(initialize_response->has_header("Mcp-Session-Id"),
          "initialize should return a session id");
  const auto session_id =
      initialize_response->get_header_value("Mcp-Session-Id");

  httplib::Client sse_client("127.0.0.1", kPort);
  httplib::sse::SSEClient sse(
      sse_client, kPath,
      httplib::Headers{
          {"Mcp-Session-Id", session_id},
          {"Accept", "text/event-stream"},
          {"MCP-Protocol-Version",
           std::string(mcp::protocol::McpProtocolVersion)},
      });
  sse.on_message([&](const httplib::sse::SSEMessage& message) {
    const auto parsed = mcp::protocol::parse_notification(message.data);
    if (parsed &&
        parsed->method == mcp::protocol::ToolsListChangedNotificationMethod) {
      notification_seen.store(true);
    }
  });
  sse.on_error([](httplib::Error) {});
  sse.start_async();

  const auto sent = server_transport.transport().send_notification(
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ToolsListChangedNotificationMethod),
          .params = Json::object(),
      });
  const std::string sent_error =
      sent ? std::string{}
           : std::string("server outbound notification should succeed: ") +
                 sent.error().message +
                 (sent.error().detail.empty()
                      ? std::string{}
                      : std::string(" | ") + sent.error().detail);
  require(sent.has_value(), sent_error.empty()
                                ? "server outbound notification should succeed"
                                : sent_error);
  require(wait_for([&]() { return notification_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "client should observe server outbound notification");
  const auto first_event_id = sse.last_event_id();
  require(!first_event_id.empty(), "sse client should track last event id");
  server_transport.transport().stop();
  sse.stop();
}

void test_server_http_transport_emits_sse_retry_priming() {
  constexpr int kPort = 40179;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .sse_retry = std::chrono::milliseconds(3000),
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion",
                       std::string(mcp::protocol::McpProtocolVersion)},
                      {"capabilities", Json::object()},
                      {"serverInfo",
                       Json{{"name", "server-http-test"}, {"version", "1"}}},
                  },
          };
        }
        return mcp::protocol::make_error_response(
            std::optional<mcp::protocol::RequestId>{request.id},
            mcp::protocol::make_error(mcp::protocol::ErrorCode::MethodNotFound,
                                      "unexpected request"));
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");

  httplib::Client http_client("127.0.0.1", kPort);
  const auto initialize_response = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
      },
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::InitializeMethod),
          .params = Json::object(),
          .id = std::int64_t{1},
      }),
      "application/json");
  require(static_cast<bool>(initialize_response), "initialize should succeed");
  require(initialize_response->has_header("Mcp-Session-Id"),
          "initialize should return a session id");
  const auto session_id =
      initialize_response->get_header_value("Mcp-Session-Id");

  std::string body;
  const auto stream = http_client.Get(
      kPath,
      httplib::Headers{
          {"Mcp-Session-Id", session_id},
          {"Accept", "text/event-stream"},
          {"MCP-Protocol-Version",
           std::string(mcp::protocol::McpProtocolVersion)},
      },
      [&](const char* data, size_t len) {
        body.append(data, len);
        return body.find("retry: 3000") == std::string::npos;
      });
  (void)stream;

  require(body.find("retry: 3000") != std::string::npos,
          "server should emit sse retry priming");
  server_transport.transport().stop();
}

void test_server_http_transport_accepts_client_notification_with_202() {
  constexpr int kPort = 40176;
  const std::string kPath = "/mcp";
  std::string observed_session_id;

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion",
                       std::string(mcp::protocol::McpProtocolVersion)},
                      {"capabilities", Json::object()},
                      {"serverInfo",
                       Json{{"name", "server-http-test"}, {"version", "1"}}},
                  },
          };
        }
        return mcp::protocol::make_error_response(
            std::optional<mcp::protocol::RequestId>{request.id},
            mcp::protocol::make_error(mcp::protocol::ErrorCode::MethodNotFound,
                                      "unexpected request"));
      },
      [&](const mcp::protocol::JsonRpcNotification&,
          const mcp::server::SessionContext& context) {
        observed_session_id = context.session_id;
        return mcp::core::Unit{};
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");

  httplib::Client http_client("127.0.0.1", kPort);
  const auto rejected_without_session = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version",
           std::string(mcp::protocol::McpProtocolVersion)},
          {"Mcp-Method",
           std::string(mcp::protocol::ToolsListChangedNotificationMethod)},
      },
      serialize_test_notification(mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ToolsListChangedNotificationMethod),
          .params = Json::object(),
      }),
      "application/json");
  require(rejected_without_session != nullptr,
          "notification without session should return a response");
  require(rejected_without_session->status == 404,
          "notification without session should be rejected");

  const auto initialize_response = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
      },
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::InitializeMethod),
          .params = Json::object(),
          .id = std::int64_t{1},
      }),
      "application/json");
  require(static_cast<bool>(initialize_response), "initialize should succeed");
  require(initialize_response->has_header("Mcp-Session-Id"),
          "initialize should return a session id");
  const auto session_id =
      initialize_response->get_header_value("Mcp-Session-Id");

  const auto notification_response = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version",
           std::string(mcp::protocol::McpProtocolVersion)},
          {"Mcp-Session-Id", session_id},
          {"Mcp-Method",
           std::string(mcp::protocol::ToolsListChangedNotificationMethod)},
      },
      serialize_test_notification(mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ToolsListChangedNotificationMethod),
          .params = Json::object(),
      }),
      "application/json");
  require(static_cast<bool>(notification_response),
          "notification should be accepted");
  require(notification_response->status == 202,
          "notification status should be 202 Accepted");
  require(observed_session_id == session_id,
          "notification session id should match negotiated session");

  const auto rejected =
      http_client.Delete(kPath, httplib::Headers{
                                    {"Accept", "application/json"},
                                    {"MCP-Protocol-Version", "0.0.0"},
                                    {"Mcp-Session-Id", session_id},
                                });
  require(rejected != nullptr,
          "delete with wrong protocol version should return a response");
  require(rejected->status == 400,
          "delete with wrong protocol version should be rejected");

  server_transport.transport().stop();
}

void test_server_http_transport_rejects_mismatched_origin_when_allowlisted() {
  constexpr int kPort = 40177;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .allowed_origins = {"https://trusted.example"},
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion",
                       std::string(mcp::protocol::McpProtocolVersion)},
                      {"capabilities", Json::object()},
                      {"serverInfo",
                       Json{{"name", "server-http-test"}, {"version", "1"}}},
                  },
          };
        }
        return mcp::protocol::make_error_response(
            std::optional<mcp::protocol::RequestId>{request.id},
            mcp::protocol::make_error(mcp::protocol::ErrorCode::MethodNotFound,
                                      "unexpected request"));
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");

  httplib::Client http_client("127.0.0.1", kPort);
  const auto rejected = http_client.Delete(
      kPath, httplib::Headers{
                 {"Accept", "application/json"},
                 {"Origin", "https://evil.example"},
                 {"MCP-Protocol-Version",
                  std::string(mcp::protocol::McpProtocolVersion)},
                 {"Mcp-Session-Id", "http-session"},
             });
  require(rejected != nullptr,
          "delete with mismatched origin should return a response");
  require(rejected->status == 400,
          "delete with mismatched origin should be rejected");

  server_transport.transport().stop();
}

void test_server_http_transport_can_request_client() {
  std::atomic<int> roots_seen{0};
  std::atomic<int> sampling_seen{0};
  std::atomic<int> elicitation_seen{0};
  std::atomic<int> elicitation_complete_seen{0};
  std::string elicitation_complete_id;
  constexpr int kPort = 40174;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion",
                       std::string(mcp::protocol::McpProtocolVersion)},
                      {"capabilities", Json::object()},
                      {"serverInfo",
                       Json{{"name", "server-http-test"}, {"version", "1"}}},
                  },
          };
        }
        return mcp::protocol::make_error_response(
            std::optional<mcp::protocol::RequestId>{request.id},
            mcp::protocol::make_error(mcp::protocol::ErrorCode::MethodNotFound,
                                      "unexpected request"));
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");

  mcp::client::HttpTransport client_transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = kPort,
      .path = kPath,
      .timeout = std::chrono::milliseconds(2000),
  });
  const auto started = client_transport.start(
      [&](const mcp::protocol::JsonRpcRequest& request)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        if (request.method == mcp::protocol::RootsListMethod) {
          roots_seen.fetch_add(1);
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result = mcp::protocol::roots_list_result_to_json(
                  mcp::protocol::RootsListResult{
                      .roots = {mcp::protocol::Root{
                          .uri = "file:///workspace",
                          .name = "workspace",
                      }},
                  }),
          };
        }
        if (request.method == mcp::protocol::SamplingCreateMessageMethod) {
          require(request.params.at("maxTokens") == 16,
                  "sampling request should preserve maxTokens");
          sampling_seen.fetch_add(1);
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result = mcp::protocol::create_message_result_to_json(
                  mcp::protocol::CreateMessageResult{
                      .role = "assistant",
                      .content =
                          mcp::protocol::ContentBlock{
                              .type = "text",
                              .text = "sampled from client",
                          },
                      .model = "test-model",
                  }),
          };
        }
        if (request.method == mcp::protocol::ElicitationCreateMethod) {
          require(request.params.at("message") == "open",
                  "elicitation request should preserve message");
          require(request.params.at("mode") == "url",
                  "elicitation request should preserve mode");
          require(request.params.at("elicitationId") == "elicitation-1",
                  "elicitation request should preserve elicitationId");
          require(
              request.params.at("url") == "https://example.test/elicitation/1",
              "elicitation request should preserve url");
          elicitation_seen.fetch_add(1);
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result = mcp::protocol::create_elicitation_result_to_json(
                  mcp::protocol::CreateElicitationResult{
                      .action = mcp::protocol::ElicitationAction::Accept,
                  }),
          };
        }

        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .error =
                mcp::protocol::ErrorObject{
                    .code = static_cast<int>(
                        mcp::protocol::ErrorCode::MethodNotFound),
                    .message = "unexpected server request",
                },
        };
      },
      [&](const mcp::protocol::JsonRpcNotification& notification)
          -> mcp::core::Result<mcp::core::Unit> {
        if (notification.method ==
            mcp::protocol::ElicitationCompleteNotificationMethod) {
          const auto parsed =
              mcp::protocol::elicitation_complete_notification_params_from_json(
                  notification.params);
          require(parsed.has_value(),
                  "elicitation completion notification should parse");
          elicitation_complete_id = parsed->elicitation_id;
          elicitation_complete_seen.fetch_add(1);
        }
        return mcp::core::Unit{};
      });
  require(started.has_value(), "client transport should start");

  Json initialize_params = Json::object();
  initialize_params["protocolVersion"] =
      std::string(mcp::protocol::McpProtocolVersion);
  initialize_params["capabilities"] = Json{
      {"roots", Json{{"listChanged", true}}},
      {"sampling", Json::object()},
      {"elicitation", Json{{"form", Json::object()}, {"url", Json::object()}}},
  };
  initialize_params["clientInfo"] =
      Json{{"name", "http-transport-test"}, {"version", "1"}};

  const auto initialized = client_transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params = initialize_params,
      .id = std::int64_t{1},
  });
  require(initialized.has_value(), "client initialize should succeed");

  mcp::server::SessionContext peer_context{
      .session_id = "server-session",
      .remote_address = "127.0.0.1",
      .transport = &server_transport.transport(),
  };
  auto peer = peer_context.client();
  require(peer.available(), "client peer should be available");
  require(peer.supports_roots(), "client peer should report roots support");
  require(peer.supports_sampling_tools(),
          "client peer should report sampling support");
  require(peer.supports_elicitation(),
          "client peer should report elicitation support");
  require(peer.supports_elicitation_url(),
          "client peer should report url elicitation support");

  const auto roots = peer.list_roots();
  require(roots.has_value(), "client peer list_roots should succeed");
  require(roots->roots.size() == 1, "client peer roots count mismatch");
  require(roots->roots.front().uri == "file:///workspace",
          "client peer root uri mismatch");

  const auto response = peer.create_message(mcp::protocol::CreateMessageParams{
      .messages = {mcp::protocol::SamplingMessage{
          .role = "user",
          .content =
              mcp::protocol::ContentBlock{
                  .type = "text",
                  .text = "sample",
              },
      }},
      .max_tokens = 16,
  });
  require(response.has_value(), "client peer create_message should succeed");
  require(response->model == "test-model",
          "client peer create_message model mismatch");
  require(response->content.text == "sampled from client",
          "client peer create_message text mismatch");

  const auto schema = mcp::protocol::ElicitationSchema::Builder()
                          .required_bool("accepted")
                          .build();
  require(schema.has_value(), "elicitation schema should build");
  const auto elicitation =
      peer.create_elicitation(mcp::protocol::CreateElicitationRequestParam{
          .message = "open",
          .mode = mcp::protocol::ElicitationMode::Url,
          .elicitation_id = "elicitation-1",
          .url = "https://example.test/elicitation/1",
      });
  require(elicitation.has_value(),
          "client peer create_elicitation should succeed");
  require(elicitation->action == mcp::protocol::ElicitationAction::Accept,
          "client peer elicitation action mismatch");
  require(!elicitation->content.has_value(),
          "client peer url elicitation should omit content");

  const auto completion_sent =
      peer.notify_elicitation_complete("elicitation-1");
  require(completion_sent.has_value(),
          "client peer notify_elicitation_complete should succeed");

  require(roots_seen.load() == 1,
          "client should observe one roots/list request");
  require(sampling_seen.load() == 1,
          "client should observe one sampling request");
  require(elicitation_seen.load() == 1,
          "client should observe one elicitation request");
  require(wait_for([&]() { return elicitation_complete_seen.load() == 1; },
                   std::chrono::milliseconds(1000)),
          "client should observe one elicitation completion notification");
  require(elicitation_complete_id == "elicitation-1",
          "elicitation completion id mismatch");

  const std::string session_id = "mcp-session-1";
  httplib::Client admin_client("127.0.0.1", kPort);
  httplib::Headers delete_headers{
      {"Accept", "application/json"},
      {"MCP-Protocol-Version", std::string(mcp::protocol::McpProtocolVersion)},
      {"Mcp-Session-Id", session_id},
  };
  const auto deleted = admin_client.Delete(kPath, delete_headers);
  require(deleted != nullptr, "session delete request should succeed");
  require(deleted->status == 204, "session delete status mismatch");
  require(!server_transport.transport().client_capabilities().has_value(),
          "server should clear client capabilities after session delete");

  const auto reinitialized =
      client_transport.send(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::InitializeMethod),
          .params = initialize_params,
          .id = std::int64_t{2},
      });
  require(reinitialized.has_value(),
          "client transport should reinitialize after delete");
  require(reinitialized->result.has_value(),
          "reinitialize response should contain a result");
  require(reinitialized->result->at("protocolVersion") ==
              std::string(mcp::protocol::McpProtocolVersion),
          "reinitialize protocol version mismatch");

  server_transport.transport().stop();
  client_transport.stop();
}

void test_client_http_transport_uses_uri_and_auth_header() {
  HttpServerFixture fixture;
  std::atomic<bool> request_seen{false};
  std::string observed_path;
  std::string observed_authorization;

  fixture.server().Post("/api/mcp", [&](const httplib::Request& request,
                                        httplib::Response& response) {
    observed_path = request.path;
    if (request.has_header("Authorization")) {
      observed_authorization = request.get_header_value("Authorization");
    }

    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "uri transport request should parse");
    const auto* rpc_request = std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "uri transport should send a request");
    require(rpc_request->method == mcp::protocol::PingMethod,
            "uri transport should send ping");

    response.set_content(
        serialize_test_response(mcp::protocol::JsonRpcResponse{
            .id = rpc_request->id,
            .result = Json::object(),
        }),
        "application/json");
    request_seen.store(true);
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .uri = "http://127.0.0.1:" + std::to_string(fixture.port()) + "/api/mcp",
      .headers = {{"X-Test", "1"}},
      .auth_header = "token-123",
      .timeout = std::chrono::milliseconds(2000),
  });

  const auto response = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PingMethod),
      .params = Json::object(),
      .id = std::int64_t{9},
  });

  require(response.has_value(), "uri transport request should succeed");
  require(request_seen.load(), "uri transport should reach server");
  require(observed_path == "/api/mcp", "uri transport should use uri path");
  require(observed_authorization == "Bearer token-123",
          "uri transport should inject authorization header");
}

void test_client_connect_streamable_http_accepts_uri_string() {
  HttpServerFixture fixture;
  std::atomic<int> request_count{0};

  fixture.server().Post("/uri-mcp", [&](const httplib::Request& request,
                                        httplib::Response& response) {
    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "uri client request should parse");
    const auto* rpc_request = std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "uri client should send a request");
    request_count.fetch_add(1);

    if (rpc_request->method == mcp::protocol::InitializeMethod) {
      response.set_content(
          serialize_test_response(mcp::protocol::JsonRpcResponse{
              .id = rpc_request->id,
              .result =
                  Json{
                      {"protocolVersion",
                       std::string(mcp::protocol::McpProtocolVersion)},
                      {"capabilities", Json::object()},
                      {"serverInfo", Json{{"name", "uri-test"},
                                          {"version", "1"}}},
                  },
          }),
          "application/json");
      return;
    }

    require(rpc_request->method == mcp::protocol::PingMethod,
            "uri client should send ping");
    response.set_content(
        serialize_test_response(mcp::protocol::JsonRpcResponse{
            .id = rpc_request->id,
            .result = Json::object(),
        }),
        "application/json");
  });

  const auto uri = "http://127.0.0.1:" + std::to_string(fixture.port()) +
                   "/uri-mcp";
  auto client = mcp::client::Client::connect_streamable_http(uri);
  const auto pong = client.ping();

  require(pong.has_value(), "uri string client should ping successfully");
  require(request_count.load() == 2,
          "uri string client should initialize before ping");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, void (*)()>> tests = {
      {"http transport decodes sse post and answers interleaved request",
       test_http_transport_decodes_sse_post_and_answers_interleaved_request},
      {"http transport opens get sse after session and dispatches notification",
       test_http_transport_opens_get_sse_after_session_and_dispatches_notification},
      {"http transport sets method and name headers",
       test_http_transport_sets_method_and_name_headers},
      {"server http transport delivers outbound notification",
       test_server_http_transport_delivers_outbound_notification},
      {"server http transport emits sse retry priming",
       test_server_http_transport_emits_sse_retry_priming},
      {"server http transport accepts client notification with 202",
       test_server_http_transport_accepts_client_notification_with_202},
      {"server http transport rejects mismatched origin when allowlisted",
       test_server_http_transport_rejects_mismatched_origin_when_allowlisted},
      {"server http transport can request client",
       test_server_http_transport_can_request_client},
      {"client http transport uses uri and auth header",
       test_client_http_transport_uses_uri_and_auth_header},
      {"client connect_streamable_http accepts uri string",
       test_client_connect_streamable_http_accepts_uri_string},
  };

  std::size_t failures = 0;
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "[PASS] " << name << '\n';
    } catch (const std::exception& ex) {
      ++failures;
      std::cerr << "[FAIL] " << name << ": " << ex.what() << '\n';
    }
  }

  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }

  std::cout << tests.size() << " test(s) passed\n";
  return 0;
}
