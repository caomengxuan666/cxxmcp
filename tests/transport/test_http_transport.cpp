// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

#include "cxxmcp/client/http_transport.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server/http_transport.hpp"
#include "cxxmcp/server/peer.hpp"
#include "cxxmcp/transport/http_transport.hpp"
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

std::string initialize_http_session(int port, const std::string& path,
                                    std::int64_t id = 1) {
  httplib::Client http_client("127.0.0.1", port);
  const auto initialize_response = http_client.Post(
      path,
      httplib::Headers{
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
      },
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::InitializeMethod),
          .params = Json::object(),
          .id = id,
      }),
      "application/json");
  require(static_cast<bool>(initialize_response), "initialize should succeed");
  require(
      initialize_response->status >= 200 && initialize_response->status < 300,
      "initialize should return a success status");
  require(initialize_response->has_header("Mcp-Session-Id"),
          "initialize should return a session id");
  return initialize_response->get_header_value("Mcp-Session-Id");
}

httplib::Headers sse_headers(const std::string& session_id) {
  return httplib::Headers{
      {"Mcp-Session-Id", session_id},
      {"Accept", "text/event-stream"},
      {"MCP-Protocol-Version", std::string(mcp::protocol::McpProtocolVersion)},
  };
}

std::string request_id_json(const mcp::protocol::RequestId& request_id) {
  return mcp::protocol::request_id_to_json(request_id).dump();
}

std::string read_sse_until(int port, const std::string& path,
                           const std::string& session_id,
                           std::function<bool(const std::string&)> done) {
  httplib::Client http_client("127.0.0.1", port);
  http_client.set_read_timeout(std::chrono::seconds(1));
  std::string body;
  const auto stream = http_client.Get(path, sse_headers(session_id),
                                      [&](const char* data, size_t len) {
                                        body.append(data, len);
                                        return !done(body);
                                      });
  (void)stream;
  return body;
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

void test_http_transport_posts_error_for_throwing_interleaved_request_handler() {
  std::atomic<bool> handler_error_seen{false};

  HttpServerFixture fixture;
  fixture.server().Post("/mcp", [&](const httplib::Request& request,
                                    httplib::Response& response) {
    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "server should parse client message");

    if (const auto* rpc_response =
            std::get_if<mcp::protocol::JsonRpcResponse>(&*parsed)) {
      require(rpc_response->error.has_value(),
              "posted handler response should contain error");
      require(rpc_response->error->message == "handler failed",
              "posted handler response message mismatch");
      require(rpc_response->error->data.has_value() &&
                  *rpc_response->error->data == "client http handler threw",
              "posted handler response detail mismatch");
      handler_error_seen.store(true);
      response.status = 202;
      return;
    }

    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "server should receive request");

    const auto server_request =
        serialize_test_request(mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::SamplingCreateMessageMethod),
            .params = Json{{"messages", Json::array()}, {"maxTokens", 16}},
            .id = std::string("srv-throw"),
        });
    const auto final_response =
        serialize_test_response(mcp::protocol::JsonRpcResponse{
            .id = rpc_request->id, .result = Json{{"tools", Json::array()}}});

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
      transport.start([](const mcp::protocol::JsonRpcRequest&)
                          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        throw std::runtime_error("client http handler threw");
      });
  require(started.has_value(), "http transport should start");

  const auto actual = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = Json::object(),
      .id = std::int64_t{19},
  });
  require(actual.has_value(),
          "http transport should keep waiting after handler error response");
  require(wait_for([&]() { return handler_error_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "server should receive posted handler error response");
}

void test_http_transport_opens_get_sse_after_session_and_dispatches_notification() {
  std::atomic<bool> get_seen{false};
  std::atomic<bool> notification_seen{false};

  HttpServerFixture fixture;
  fixture.server().Post("/mcp", [](const httplib::Request& request,
                                   httplib::Response& response) {
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
                     std::string(mcp::protocol::McpProtocolVersion2025_06_18)},
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
                std::string(mcp::protocol::McpProtocolVersion2025_06_18),
            "sse get should use negotiated protocol version");
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

void test_server_http_transport_writes_error_for_throwing_request_handler() {
  constexpr int kPort = 40214;
  const std::string kPath = "/throwing-handler";
  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [](const mcp::protocol::JsonRpcRequest&,
         const mcp::server::SessionContext&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        throw std::runtime_error("server http handler threw");
      });

  std::optional<httplib::Result> posted;
  const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params = Json::object(),
      .id = std::int64_t{29},
  });
  for (int attempt = 0; attempt < 100; ++attempt) {
    httplib::Client client("127.0.0.1", kPort);
    auto response = client.Post(
        kPath,
        httplib::Headers{
            {"Accept", "application/json"},
            {"Content-Type", "application/json"},
            {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
        },
        body, "application/json");
    if (response) {
      posted = std::move(response);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  require(posted.has_value(),
          "server http throwing handler request should return");
  require((*posted)->status == 200,
          "server http throwing handler status mismatch");
  const auto parsed = mcp::protocol::parse_response((*posted)->body);
  require(parsed.has_value(),
          "server http throwing handler response should parse");
  require(parsed->error.has_value(),
          "server http throwing handler should return error response");
  require(parsed->error->message == "handler failed",
          "server http throwing handler error message mismatch");
  require(parsed->error->data.has_value() &&
              *parsed->error->data == "server http handler threw",
          "server http throwing handler error detail mismatch");

  server_transport.transport().stop();
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

void test_server_http_transport_replays_after_last_event_id() {
  constexpr int kPort = 40180;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .max_sse_replay_events = 8,
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
  const auto session_id = initialize_http_session(kPort, kPath, 2);

  const auto sent = server_transport.transport().send_notification(
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ToolsListChangedNotificationMethod),
          .params = Json::object(),
      });
  require(sent.has_value(), "first notification should queue");

  httplib::Client first_client("127.0.0.1", kPort);
  std::string first_body;
  const auto first_stream = first_client.Get(
      kPath, sse_headers(session_id), [&](const char* data, size_t len) {
        first_body.append(data, len);
        return first_body.find("id: 1") == std::string::npos;
      });
  (void)first_stream;
  require(first_body.find("id: 1") != std::string::npos,
          "first stream should receive event id 1");

  httplib::Headers replay_headers = sse_headers(session_id);
  replay_headers.emplace("Last-Event-ID", "0");
  httplib::Client replay_client("127.0.0.1", kPort);
  std::string replay_body;
  const auto replay_stream = replay_client.Get(
      kPath, replay_headers, [&](const char* data, size_t len) {
        replay_body.append(data, len);
        return replay_body.find("id: 1") == std::string::npos;
      });
  (void)replay_stream;
  require(replay_body.find("id: 1") != std::string::npos,
          "reconnect should replay event id 1 after Last-Event-ID 0");
  require(replay_body.find("tools/list_changed") != std::string::npos,
          "replayed event should preserve notification payload");

  server_transport.transport().stop();
}

void test_server_http_transport_rejects_expired_last_event_id() {
  constexpr int kPort = 40181;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .max_sse_replay_events = 1,
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
  const auto session_id = initialize_http_session(kPort, kPath, 2);

  for (int i = 0; i < 2; ++i) {
    const auto sent = server_transport.transport().send_notification(
        mcp::protocol::JsonRpcNotification{
            .method =
                std::string(mcp::protocol::ToolsListChangedNotificationMethod),
            .params = Json{{"index", i}},
        });
    require(sent.has_value(), "notification should queue");
  }

  httplib::Client stream_client("127.0.0.1", kPort);
  std::string stream_body;
  const auto stream = stream_client.Get(
      kPath, sse_headers(session_id), [&](const char* data, size_t len) {
        stream_body.append(data, len);
        return stream_body.find("id: 2") == std::string::npos;
      });
  (void)stream;
  require(stream_body.find("id: 2") != std::string::npos,
          "stream should drain through event id 2");

  httplib::Headers stale_headers = sse_headers(session_id);
  stale_headers.emplace("Last-Event-ID", "0");
  httplib::Client stale_client("127.0.0.1", kPort);
  const auto stale = stale_client.Get(kPath, stale_headers);
  require(stale != nullptr, "stale replay request should return a response");
  require(stale->status == 409,
          "stale replay request should be rejected as conflict");

  server_transport.transport().stop();
}

void test_server_http_transport_applies_outbound_backpressure() {
  constexpr int kPort = 40182;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .max_pending_sse_events = 1,
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
  (void)initialize_http_session(kPort, kPath, 2);

  const auto first = server_transport.transport().send_notification(
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ToolsListChangedNotificationMethod),
          .params = Json{{"index", 1}},
      });
  require(first.has_value(), "first queued notification should succeed");

  const auto second = server_transport.transport().send_notification(
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ToolsListChangedNotificationMethod),
          .params = Json{{"index", 2}},
      });
  require(!second.has_value(),
          "second queued notification should hit backpressure");
  require(second.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::RateLimited),
          "backpressure should map to rate limited");

  server_transport.transport().stop();
}

void test_server_http_transport_keeps_pending_request_across_reconnect() {
  constexpr int kPort = 40183;
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
  const auto session_id = initialize_http_session(kPort, kPath, 2);

  std::atomic<bool> request_finished{false};
  std::optional<mcp::core::Error> request_error;
  std::thread request_thread([&]() {
    const auto response =
        server_transport.transport().send_request(mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::RootsListMethod),
            .params = Json::object(),
            .id = std::string("server-request-1"),
        });
    if (!response) {
      request_error = response.error();
    } else if (!response->result.has_value()) {
      request_error = mcp::core::Error{
          static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "missing response result",
      };
    }
    request_finished.store(true);
  });

  httplib::Client stream_client("127.0.0.1", kPort);
  std::string stream_body;
  const auto stream = stream_client.Get(
      kPath, sse_headers(session_id), [&](const char* data, size_t len) {
        stream_body.append(data, len);
        return stream_body.find("server-request-1") == std::string::npos;
      });
  (void)stream;
  require(stream_body.find("server-request-1") != std::string::npos,
          "server request should be delivered on first SSE stream");
  require(!request_finished.load(),
          "SSE reconnect should not fail the pending request");

  httplib::Client response_client("127.0.0.1", kPort);
  const auto posted_response = response_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version",
           std::string(mcp::protocol::McpProtocolVersion)},
          {"Mcp-Session-Id", session_id},
      },
      serialize_test_response(mcp::protocol::JsonRpcResponse{
          .id = std::string("server-request-1"),
          .result = Json{{"roots", Json::array()}},
      }),
      "application/json");
  require(posted_response != nullptr, "posted response should return");
  require(posted_response->status == 202,
          "posted response should complete pending request");

  const bool completed = wait_for([&]() { return request_finished.load(); },
                                  std::chrono::milliseconds(1000));
  if (!completed) {
    server_transport.transport().stop();
  }
  if (request_thread.joinable()) {
    request_thread.join();
  }
  require(completed, "server request should complete");
  require(!request_error.has_value(), "server request should not fail");

  server_transport.transport().stop();
}

void test_server_http_transport_routes_notifications_by_session() {
  constexpr int kPort = 40190;
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
  const auto first_session = initialize_http_session(kPort, kPath, 2);
  const auto second_session = initialize_http_session(kPort, kPath, 3);
  require(first_session != second_session,
          "independent initialize requests should create distinct sessions");

  std::atomic<bool> first_seen{false};
  std::atomic<bool> second_seen{false};
  httplib::Client first_client("127.0.0.1", kPort);
  httplib::Client second_client("127.0.0.1", kPort);
  httplib::sse::SSEClient first_sse(first_client, kPath,
                                    sse_headers(first_session));
  httplib::sse::SSEClient second_sse(second_client, kPath,
                                     sse_headers(second_session));
  first_sse.on_message([&](const httplib::sse::SSEMessage& message) {
    const auto parsed = mcp::protocol::parse_notification(message.data);
    if (parsed && parsed->method == mcp::protocol::ProgressNotificationMethod) {
      first_seen.store(true);
    }
  });
  second_sse.on_message([&](const httplib::sse::SSEMessage& message) {
    const auto parsed = mcp::protocol::parse_notification(message.data);
    if (!parsed ||
        parsed->method != mcp::protocol::ProgressNotificationMethod) {
      return;
    }
    const auto params =
        mcp::protocol::progress_notification_params_from_json(parsed->params);
    require(params.has_value(), "progress notification should parse");
    require(std::get<std::string>(params->progress_token) == "upload-1",
            "progress token should be preserved");
    require(params->progress == 4.0, "progress value should be preserved");
    require(params->total.has_value() && *params->total == 10.0,
            "progress total should be preserved");
    second_seen.store(true);
  });
  first_sse.on_error([](httplib::Error) {});
  second_sse.on_error([](httplib::Error) {});
  first_sse.start_async();
  second_sse.start_async();

  const auto sent = server_transport.transport().send_notification_to_session(
      second_session,
      mcp::protocol::JsonRpcNotification{
          .method = std::string(mcp::protocol::ProgressNotificationMethod),
          .params = mcp::protocol::progress_notification_params_to_json(
              mcp::protocol::ProgressNotificationParams{
                  .progress_token = std::string("upload-1"),
                  .progress = 4.0,
                  .total = 10.0,
              }),
      });
  require(sent.has_value(), "session-targeted notification should succeed");
  require(wait_for([&]() { return second_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "target session should receive progress notification");
  require(!first_seen.load(),
          "other sessions should not receive targeted notification");

  server_transport.transport().stop();
  first_sse.stop();
  second_sse.stop();
}

void test_server_http_transport_rejects_concurrent_sse_streams() {
  constexpr int kPort = 40191;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .sse_retry = std::chrono::milliseconds(25),
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
  const auto session_id = initialize_http_session(kPort, kPath, 2);

  std::atomic<bool> first_stream_ready{false};
  std::thread first_stream([&]() {
    httplib::Client client("127.0.0.1", kPort);
    std::string body;
    (void)client.Get(kPath, sse_headers(session_id),
                     [&](const char* data, size_t len) {
                       body.append(data, len);
                       if (body.find("retry: 25") != std::string::npos) {
                         first_stream_ready.store(true);
                       }
                       return true;
                     });
  });

  require(wait_for([&]() { return first_stream_ready.load(); },
                   std::chrono::milliseconds(1000)),
          "first SSE stream should become active");

  httplib::Client second_client("127.0.0.1", kPort);
  const auto second = second_client.Get(kPath, sse_headers(session_id));
  require(second != nullptr, "second stream should return a response");
  require(second->status == 409,
          "second concurrent SSE stream should be rejected");

  server_transport.transport().stop();
  if (first_stream.joinable()) {
    first_stream.join();
  }
}

void test_server_http_transport_rejects_duplicate_inflight_response() {
  constexpr int kPort = 40192;
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
  const auto session_id = initialize_http_session(kPort, kPath, 2);

  std::atomic<bool> request_finished{false};
  std::thread request_thread([&]() {
    const auto response =
        server_transport.transport().send_request(mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::RootsListMethod),
            .params = Json::object(),
            .id = std::string("server-request-dup"),
        });
    require(response.has_value(), "server request should complete");
    request_finished.store(true);
  });

  httplib::Client stream_client("127.0.0.1", kPort);
  std::string stream_body;
  (void)stream_client.Get(
      kPath, sse_headers(session_id), [&](const char* data, size_t len) {
        stream_body.append(data, len);
        return stream_body.find("server-request-dup") == std::string::npos;
      });
  require(stream_body.find("server-request-dup") != std::string::npos,
          "server request should be delivered");

  httplib::Client response_client("127.0.0.1", kPort);
  const httplib::Headers headers{
      {"Accept", "application/json"},
      {"Content-Type", "application/json"},
      {"MCP-Protocol-Version", std::string(mcp::protocol::McpProtocolVersion)},
      {"Mcp-Session-Id", session_id},
  };
  const auto response_body =
      serialize_test_response(mcp::protocol::JsonRpcResponse{
          .id = std::string("server-request-dup"),
          .result = Json{{"roots", Json::array()}},
      });
  const auto first =
      response_client.Post(kPath, headers, response_body, "application/json");
  require(first != nullptr, "first response post should return");
  require(first->status == 202, "first response post should be accepted");
  require(wait_for([&]() { return request_finished.load(); },
                   std::chrono::milliseconds(1000)),
          "server request should finish after first response");
  if (request_thread.joinable()) {
    request_thread.join();
  }

  const auto duplicate =
      response_client.Post(kPath, headers, response_body, "application/json");
  require(duplicate != nullptr, "duplicate response post should return");
  require(duplicate->status == 400,
          "duplicate response post should be rejected");
  require(duplicate->body.find("unexpected response") != std::string::npos,
          "duplicate response should return a stable error");

  server_transport.transport().stop();
}

void test_server_http_transport_rejects_stale_session_after_delete() {
  constexpr int kPort = 40184;
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
  const auto session_id = initialize_http_session(kPort, kPath, 2);

  httplib::Client http_client("127.0.0.1", kPort);
  const auto deleted = http_client.Delete(
      kPath, httplib::Headers{
                 {"Accept", "application/json"},
                 {"MCP-Protocol-Version",
                  std::string(mcp::protocol::McpProtocolVersion)},
                 {"Mcp-Session-Id", session_id},
             });
  require(deleted != nullptr, "session delete should return");
  require(deleted->status == 204, "session delete should terminate session");

  const auto stale_post = http_client.Post(
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
  require(stale_post != nullptr, "stale post should return");
  require(stale_post->status == 404, "stale post should be rejected");

  const auto stale_get = http_client.Get(kPath, sse_headers(session_id));
  require(stale_get != nullptr, "stale get should return");
  require(stale_get->status == 404, "stale get should be rejected");

  server_transport.transport().stop();
}

void test_server_http_transport_rejects_initialize_protocol_version_mismatch() {
  constexpr int kPort = 40185;
  const std::string kPath = "/mcp";
  std::atomic<bool> handler_called{false};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        handler_called.store(true);
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result = Json{{"protocolVersion",
                            std::string(mcp::protocol::McpProtocolVersion)},
                           {"capabilities", Json::object()},
                           {"serverInfo", Json{{"name", "server-http-test"},
                                               {"version", "1"}}}},
        };
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");
  handler_called.store(false);

  Json initialize_params = Json::object();
  initialize_params["protocolVersion"] =
      std::string(mcp::protocol::McpProtocolVersion);
  initialize_params["capabilities"] = Json::object();
  initialize_params["clientInfo"] =
      Json{{"name", "version-test"}, {"version", "1"}};

  httplib::Client http_client("127.0.0.1", kPort);
  const auto mismatch = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
          {"MCP-Protocol-Version", "2024-11-05"},
      },
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::InitializeMethod),
          .params = initialize_params,
          .id = std::int64_t{9},
      }),
      "application/json");
  require(mismatch != nullptr, "mismatched initialize should return");
  require(mismatch->status == 400,
          "mismatched initialize protocol version should be rejected");
  require(!handler_called.load(),
          "mismatched initialize should not reach handler");

  initialize_params["protocolVersion"] = "1900-01-01";
  const auto unsupported = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
          {"MCP-Protocol-Version", "1900-01-01"},
      },
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::InitializeMethod),
          .params = initialize_params,
          .id = std::int64_t{10},
      }),
      "application/json");
  require(unsupported != nullptr, "unsupported initialize should return");
  require(unsupported->status == 400,
          "unsupported initialize protocol version should be rejected");
  require(!handler_called.load(),
          "unsupported initialize should not reach handler");

  server_transport.transport().stop();
}

void test_server_http_transport_rejects_malformed_post_body() {
  constexpr int kPort = 40186;
  const std::string kPath = "/mcp";
  std::atomic<bool> handler_called{false};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        handler_called.store(true);
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
  handler_called.store(false);

  httplib::Client http_client("127.0.0.1", kPort);
  const auto malformed =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json"},
                           {"Content-Type", "application/json"},
                       },
                       "{not-json}", "application/json");
  require(malformed != nullptr, "malformed post should return");
  require(malformed->status == 400, "malformed post should be rejected");
  const auto parsed = mcp::protocol::parse_response(malformed->body);
  require(parsed.has_value(), "malformed post error response should parse");
  require(parsed->error.has_value(),
          "malformed post response should contain error");
  require(parsed->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::ParseError),
          "malformed post error code mismatch");
  require(!parsed->id.has_value(), "malformed post response id should be null");
  require(!handler_called.load(), "malformed post should not reach handler");

  server_transport.transport().stop();
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
  const auto stream =
      http_client.Get(kPath,
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
  std::mutex session_mutex;
  std::string initialized_session_id;
  constexpr int kPort = 40174;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext& context) {
        if (request.method == mcp::protocol::InitializeMethod) {
          std::lock_guard lock(session_mutex);
          initialized_session_id = context.session_id;
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
  std::string session_id;
  {
    std::lock_guard lock(session_mutex);
    session_id = initialized_session_id;
  }
  require(!session_id.empty(), "initialize should record a session id");

  mcp::server::SessionContext peer_context{
      .session_id = session_id,
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

void test_server_http_transport_request_timeout_sends_cancelled_over_sse() {
  constexpr int kPort = 40191;
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
  const auto session_id = initialize_http_session(kPort, kPath, 2);

  mcp::server::SessionContext peer_context{
      .session_id = session_id,
      .remote_address = "127.0.0.1",
      .transport = &server_transport.transport(),
  };
  auto peer = peer_context.client();

  mcp::RequestOptions options;
  options.timeout = std::chrono::milliseconds(50);
  auto handle = peer.request_async(std::string(mcp::protocol::RootsListMethod),
                                   Json::object(), options);

  const auto response = handle.await_response();
  require(!response.has_value(), "server request handle should time out");
  require(response.error().message == "request timed out",
          "server request handle timeout message mismatch");

  const auto expected_id = request_id_json(handle.request_id());
  const auto body = read_sse_until(
      kPort, kPath, session_id, [&](const std::string& candidate) {
        return candidate.find(std::string(mcp::protocol::RootsListMethod)) !=
                   std::string::npos &&
               candidate.find(
                   std::string(mcp::protocol::CancelledNotificationMethod)) !=
                   std::string::npos &&
               candidate.find("\"requestId\":" + expected_id) !=
                   std::string::npos;
      });
  require(body.find(std::string(mcp::protocol::RootsListMethod)) !=
              std::string::npos,
          "SSE stream should deliver the timed-out server request");
  require(body.find(std::string(mcp::protocol::CancelledNotificationMethod)) !=
              std::string::npos,
          "SSE stream should deliver cancellation notification");
  require(body.find("\"requestId\":" + expected_id) != std::string::npos,
          "cancelled notification request id mismatch");
  require(body.find("\"reason\":\"request timeout\"") != std::string::npos,
          "cancelled notification timeout reason mismatch");

  server_transport.transport().stop();
}

void test_server_http_transport_explicit_cancel_sends_cancelled_over_sse() {
  constexpr int kPort = 40192;
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
  const auto session_id = initialize_http_session(kPort, kPath, 2);

  mcp::server::SessionContext peer_context{
      .session_id = session_id,
      .remote_address = "127.0.0.1",
      .transport = &server_transport.transport(),
  };
  auto peer = peer_context.client();

  auto handle = peer.request_async(std::string(mcp::protocol::RootsListMethod),
                                   Json::object());
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  const auto cancelled = handle.cancel("request cancelled");
  require(cancelled.has_value(), "server request handle cancel should send");

  const auto expected_id = request_id_json(handle.request_id());
  const auto body = read_sse_until(
      kPort, kPath, session_id, [&](const std::string& candidate) {
        return candidate.find(std::string(mcp::protocol::RootsListMethod)) !=
                   std::string::npos &&
               candidate.find(
                   std::string(mcp::protocol::CancelledNotificationMethod)) !=
                   std::string::npos &&
               candidate.find("\"requestId\":" + expected_id) !=
                   std::string::npos;
      });
  require(body.find(std::string(mcp::protocol::RootsListMethod)) !=
              std::string::npos,
          "SSE stream should deliver the cancelled server request");
  require(body.find(std::string(mcp::protocol::CancelledNotificationMethod)) !=
              std::string::npos,
          "SSE stream should deliver cancellation notification");
  require(body.find("\"requestId\":" + expected_id) != std::string::npos,
          "cancelled notification request id mismatch");
  require(body.find("\"reason\":\"request cancelled\"") != std::string::npos,
          "cancelled notification reason mismatch");

  server_transport.transport().stop();
}

void test_client_http_transport_times_out_initialize() {
  HttpServerFixture fixture;

  fixture.server().Post(
      "/mcp", [](const httplib::Request& request, httplib::Response& response) {
        const auto parsed = mcp::protocol::parse_message(request.body);
        require(parsed.has_value(), "initialize request should parse");
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const auto* rpc_request =
            std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
        require(rpc_request != nullptr, "server should receive a request");
        response.set_header("Mcp-Session-Id", "timeout-session");
        response.set_content(
            serialize_test_response(mcp::protocol::JsonRpcResponse{
                .id = rpc_request->id,
                .result =
                    Json{
                        {"protocolVersion",
                         std::string(mcp::protocol::McpProtocolVersion)},
                        {"capabilities", Json::object()},
                        {"serverInfo",
                         Json{{"name", "timeout-test"}, {"version", "1"}}},
                    },
            }),
            "application/json");
      });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = fixture.port(),
      .path = "/mcp",
      .timeout = std::chrono::milliseconds(50),
  });

  const auto response = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(!response.has_value(), "initialize should honor HTTP timeout");
}

void test_client_http_transport_rejects_unexpected_response_id() {
  HttpServerFixture fixture;

  fixture.server().Post("/mcp", [](const httplib::Request&,
                                   httplib::Response& response) {
    response.set_content(serialize_test_response(mcp::protocol::JsonRpcResponse{
                             .id = std::int64_t{8},
                             .result = Json{{"ok", true}},
                         }),
                         "application/json");
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = fixture.port(),
      .path = "/mcp",
      .timeout = std::chrono::milliseconds(2000),
  });

  const auto response = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PingMethod),
      .params = Json::object(),
      .id = std::int64_t{7},
  });
  require(!response.has_value(),
          "http transport should reject unexpected response ids");
  require(response.error().message ==
              "http transport received an unexpected response",
          "http unexpected response message mismatch");
  require(response.error().detail == "8",
          "http unexpected response detail mismatch");
  require(response.error().category == "transport",
          "http unexpected response category mismatch");
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
    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "uri transport should send a request");
    require(rpc_request->method == mcp::protocol::PingMethod,
            "uri transport should send ping");

    response.set_content(serialize_test_response(mcp::protocol::JsonRpcResponse{
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
    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
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
                      {"serverInfo",
                       Json{{"name", "uri-test"}, {"version", "1"}}},
                  },
          }),
          "application/json");
      return;
    }

    require(rpc_request->method == mcp::protocol::PingMethod,
            "uri client should send ping");
    response.set_content(serialize_test_response(mcp::protocol::JsonRpcResponse{
                             .id = rpc_request->id,
                             .result = Json::object(),
                         }),
                         "application/json");
  });

  const auto uri =
      "http://127.0.0.1:" + std::to_string(fixture.port()) + "/uri-mcp";
  auto client = mcp::client::Client::connect_streamable_http(uri);
  const auto pong = client.ping();

  require(pong.has_value(), "uri string client should ping successfully");
  require(request_count.load() == 2,
          "uri string client should initialize before ping");
}

void test_native_streamable_http_transport_exposes_client_contract() {
  static_assert(
      std::is_base_of_v<mcp::transport::ClientTransport,
                        mcp::transport::StreamableHttpClientTransport>);

  constexpr int kPort = 40210;
  const std::string kPath = "/native-mcp";
  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::make_response(
              request.id, Json{
                              {"protocolVersion",
                               std::string(mcp::protocol::McpProtocolVersion)},
                              {"capabilities", Json::object()},
                              {"serverInfo", Json{{"name", "native-http-test"},
                                                  {"version", "1"}}},
                          });
        }
        if (request.method == mcp::protocol::ToolsListMethod) {
          return mcp::protocol::make_response(
              request.id,
              Json{{"tools", Json::array({Json{
                                 {"name", "echo"},
                                 {"description", "Echo"},
                                 {"inputSchema", Json{{"type", "object"}}},
                             }})}});
        }
        return mcp::protocol::make_error_response(
            std::optional<mcp::protocol::RequestId>{request.id},
            mcp::protocol::make_error(mcp::protocol::ErrorCode::MethodNotFound,
                                      "unexpected method"));
      });
  require(wait_for_http_initialize(kPort, kPath),
          "native http server should start");

  mcp::transport::StreamableHttpClientTransport transport(
      mcp::transport::StreamableHttpClientTransportOptions{
          .host = "127.0.0.1",
          .port = kPort,
          .path = kPath,
          .timeout = std::chrono::milliseconds(2000),
      });

  require(transport.name() == "streamable-http",
          "native http transport name mismatch");
  require(transport.diagnostics().at("name") == "streamable-http",
          "native http diagnostics name mismatch");

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params =
          Json{
              {"protocolVersion",
               std::string(mcp::protocol::McpProtocolVersion)},
              {"capabilities", Json::object()},
              {"clientInfo", Json{{"name", "native"}, {"version", "1"}}},
          },
      .id = std::int64_t{1},
  });
  require(sent.has_value(), "native http initialize send should succeed");

  auto received = transport.receive();
  require(received.has_value(), "native http initialize receive failed");
  require(received->has_value(), "native http initialize response missing");
  const auto* initialize_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(initialize_response != nullptr,
          "native http initialize should receive response");
  require(initialize_response->result.has_value(),
          "native http initialize should have result");

  sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ToolsListMethod),
      .params = Json::object(),
      .id = std::int64_t{2},
  });
  require(sent.has_value(), "native http tools/list send should succeed");

  received = transport.receive();
  require(received.has_value(), "native http tools/list receive failed");
  require(received->has_value(), "native http tools/list response missing");
  const auto* tools_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(tools_response != nullptr,
          "native http tools/list should receive response");
  require(tools_response->result.has_value(),
          "native http tools/list should have result");
  require(tools_response->result->at("tools").size() == 1,
          "native http tools/list count mismatch");
  const auto diagnostics = transport.diagnostics();
  require(diagnostics.at("activeRequestWorkers").get<std::size_t>() == 0,
          "native http request workers should be inactive");
  require(diagnostics.at("completedRequestWorkers").get<std::size_t>() >= 2,
          "native http completed request worker count mismatch");
  require(diagnostics.at("failedRequestWorkers").get<std::size_t>() == 0,
          "native http failed request worker count mismatch");
  require(diagnostics.at("timedOutRequestWorkers").get<std::size_t>() == 0,
          "native http timeout request worker count mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native http close should succeed");
  server_transport.transport().stop();
}

void test_native_streamable_http_transport_diagnostics_timeout_cleanup() {
  HttpServerFixture fixture;

  fixture.server().Post(
      "/mcp", [](const httplib::Request& request, httplib::Response& response) {
        const auto parsed = mcp::protocol::parse_message(request.body);
        require(parsed.has_value(), "native timeout request should parse");
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const auto* rpc_request =
            std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
        require(rpc_request != nullptr,
                "native timeout server should receive a request");
        response.set_header("Mcp-Session-Id", "native-timeout-session");
        response.set_content(
            serialize_test_response(mcp::protocol::JsonRpcResponse{
                .id = rpc_request->id,
                .result =
                    Json{
                        {"protocolVersion",
                         std::string(mcp::protocol::McpProtocolVersion)},
                        {"capabilities", Json::object()},
                        {"serverInfo",
                         Json{{"name", "native-timeout"}, {"version", "1"}}},
                    },
            }),
            "application/json");
      });

  mcp::transport::StreamableHttpClientTransport transport(
      mcp::transport::StreamableHttpClientTransportOptions{
          .host = "127.0.0.1",
          .port = fixture.port(),
          .path = "/mcp",
          .timeout = std::chrono::milliseconds(50),
      });

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params = Json::object(),
      .id = std::int64_t{601},
  });
  require(sent.has_value(), "native http timeout request should send");

  auto received = transport.receive();
  require(received.has_value(), "native http timeout receive failed");
  require(received->has_value(), "native http timeout response missing");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(response != nullptr,
          "native http timeout should receive response message");
  require(response->error.has_value(),
          "native http timeout should surface an error response");

  const auto diagnostics = transport.diagnostics();
  require(diagnostics.at("pendingServerRequests").get<std::size_t>() == 0,
          "native http timeout should not leave pending server requests");
  require(diagnostics.at("activeRequestWorkers").get<std::size_t>() == 0,
          "native http timeout should not leave active request workers");
  require(diagnostics.at("completedRequestWorkers").get<std::size_t>() >= 1,
          "native http timeout completed worker count mismatch");
  require(diagnostics.at("failedRequestWorkers").get<std::size_t>() >= 1,
          "native http timeout failed worker count mismatch");
  require(diagnostics.at("timedOutRequestWorkers").get<std::size_t>() >= 1,
          "native http timeout worker count mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native http timeout close should succeed");
}

void test_native_streamable_http_transport_rejects_unknown_server_response_id() {
  mcp::transport::StreamableHttpClientTransport transport(
      mcp::transport::StreamableHttpClientTransportOptions{
          .host = "127.0.0.1",
          .port = 40215,
          .path = "/native-client-unknown-response",
      });

  const auto sent = transport.send(mcp::protocol::JsonRpcResponse{
      .id = mcp::protocol::RequestId{std::string("unknown-server-request")},
      .result = Json::object(),
  });
  require(!sent.has_value(),
          "native http should reject response without pending server request");
  require(sent.error().message ==
              "streamable http client transport has no pending server request",
          "native http unknown server response message mismatch");
  require(sent.error().detail == "unknown-server-request",
          "native http unknown server response detail mismatch");
  require(sent.error().category == "transport",
          "native http unknown server response category mismatch");

  const auto closed = transport.close();
  require(closed.has_value(),
          "native http unknown server response close should succeed");
}

void test_native_streamable_http_transport_surfaces_unexpected_response_id() {
  HttpServerFixture fixture;

  fixture.server().Post("/mcp", [](const httplib::Request&,
                                   httplib::Response& response) {
    response.set_content(serialize_test_response(mcp::protocol::JsonRpcResponse{
                             .id = std::int64_t{999},
                             .result = Json{{"ok", true}},
                         }),
                         "application/json");
  });

  mcp::transport::StreamableHttpClientTransport transport(
      mcp::transport::StreamableHttpClientTransportOptions{
          .host = "127.0.0.1",
          .port = fixture.port(),
          .path = "/mcp",
          .timeout = std::chrono::milliseconds(2000),
      });

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PingMethod),
      .params = Json::object(),
      .id = std::int64_t{811},
  });
  require(sent.has_value(), "native http unexpected-id send should succeed");

  auto received = transport.receive();
  require(received.has_value(), "native http unexpected-id receive failed");
  require(received->has_value(), "native http unexpected-id response missing");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(response != nullptr,
          "native http unexpected-id should receive response message");
  require(response->id == mcp::protocol::RequestId{std::int64_t{811}},
          "native http unexpected-id response id mismatch");
  require(response->error.has_value(),
          "native http unexpected-id should surface an error response");
  require(response->error->message ==
              "http transport received an unexpected response",
          "native http unexpected-id error message mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native http unexpected-id close should succeed");
}

void test_native_streamable_http_transport_rejects_duplicate_request_id() {
  HttpServerFixture fixture;

  fixture.server().Post("/mcp", [](const httplib::Request& request,
                                   httplib::Response& response) {
    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "native duplicate request should parse");
    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "native duplicate should send request");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    response.set_content(serialize_test_response(mcp::protocol::JsonRpcResponse{
                             .id = rpc_request->id,
                             .result = Json{{"ok", true}},
                         }),
                         "application/json");
  });

  mcp::transport::StreamableHttpClientTransport transport(
      mcp::transport::StreamableHttpClientTransportOptions{
          .host = "127.0.0.1",
          .port = fixture.port(),
          .path = "/mcp",
          .timeout = std::chrono::milliseconds(2000),
      });

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PingMethod),
      .params = Json::object(),
      .id = std::int64_t{812},
  });
  require(sent.has_value(), "native http duplicate first send should succeed");

  const auto duplicate = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PingMethod),
      .params = Json::object(),
      .id = std::int64_t{812},
  });
  require(!duplicate.has_value(),
          "native http should reject duplicate in-flight request ids");
  require(duplicate.error().message == "duplicate streamable http request id",
          "native http duplicate request message mismatch");
  require(duplicate.error().detail == "812",
          "native http duplicate request detail mismatch");
  require(duplicate.error().category == "transport",
          "native http duplicate request category mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native http duplicate close should succeed");
}

void test_native_streamable_http_transport_close_unblocks_receive() {
  mcp::transport::StreamableHttpClientTransport transport(
      mcp::transport::StreamableHttpClientTransportOptions{
          .host = "127.0.0.1",
          .port = 40216,
          .path = "/native-client-close",
      });

  std::atomic<bool> receive_finished{false};
  std::atomic<bool> received_end{false};
  std::thread receive_thread([&]() {
    const auto received = transport.receive();
    received_end.store(received.has_value() && !received->has_value());
    receive_finished.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  require(!receive_finished.load(),
          "native http receive should block before close");
  const auto closed = transport.close();
  require(closed.has_value(), "native http close should succeed");
  if (receive_thread.joinable()) {
    receive_thread.join();
  }
  require(receive_finished.load(), "native http close should unblock receive");
  require(received_end.load(), "native http receive should report end");
}

void test_native_streamable_http_transport_receives_server_request() {
  constexpr int kPort = 40211;
  const std::string kPath = "/native-mcp";
  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::make_response(
              request.id, Json{
                              {"protocolVersion",
                               std::string(mcp::protocol::McpProtocolVersion)},
                              {"capabilities", Json::object()},
                              {"serverInfo", Json{{"name", "native-http-test"},
                                                  {"version", "1"}}},
                          });
        }
        return mcp::protocol::make_response(request.id, Json::object());
      });
  require(wait_for_http_initialize(kPort, kPath),
          "native http server should start");

  mcp::transport::StreamableHttpClientTransport transport(
      mcp::transport::StreamableHttpClientTransportOptions{
          .host = "127.0.0.1",
          .port = kPort,
          .path = kPath,
          .timeout = std::chrono::milliseconds(2000),
      });

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params =
          Json{
              {"protocolVersion",
               std::string(mcp::protocol::McpProtocolVersion)},
              {"capabilities", Json::object()},
              {"clientInfo", Json{{"name", "native"}, {"version", "1"}}},
          },
      .id = std::int64_t{3},
  });
  require(sent.has_value(), "native http initialize send should succeed");
  auto received = transport.receive();
  require(received.has_value() && received->has_value(),
          "native http initialize response missing");

  std::optional<mcp::protocol::JsonRpcResponse> server_request_response;
  std::thread request_thread([&]() {
    auto response =
        server_transport.transport().send_request(mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::SamplingCreateMessageMethod),
            .params = Json{{"messages", Json::array()}, {"maxTokens", 16}},
            .id = std::int64_t{44},
        });
    if (response) {
      server_request_response = std::move(*response);
    }
  });

  received = transport.receive();
  require(received.has_value(), "native http server request receive failed");
  require(received->has_value(), "native http server request missing");
  const auto* server_request =
      std::get_if<mcp::protocol::JsonRpcRequest>(&received->value());
  require(server_request != nullptr,
          "native http should receive server request");
  require(server_request->method ==
              std::string(mcp::protocol::SamplingCreateMessageMethod),
          "native http server request method mismatch");

  sent = transport.send(mcp::protocol::make_response(
      server_request->id,
      Json{
          {"role", "assistant"},
          {"content", Json{{"type", "text"}, {"text", "sampled"}}},
          {"model", "test-model"},
          {"stopReason", "endTurn"},
      }));
  require(sent.has_value(), "native http server response send should succeed");

  if (request_thread.joinable()) {
    request_thread.join();
  }
  require(server_request_response.has_value(),
          "server should receive native http response");
  require(server_request_response->result.has_value(),
          "native http server response should have result");
  require(
      server_request_response->result->at("content").at("text") == "sampled",
      "native http server response content mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native http close should succeed");
  server_transport.transport().stop();
}

void test_native_streamable_http_server_transport_exposes_server_contract() {
  static_assert(
      std::is_base_of_v<mcp::transport::ServerTransport,
                        mcp::transport::StreamableHttpServerTransport>);

  constexpr int kPort = 40212;
  const std::string kPath = "/native-server-mcp";
  mcp::transport::StreamableHttpServerTransport transport(
      mcp::transport::StreamableHttpServerTransportOptions{
          .listen_host = "127.0.0.1",
          .listen_port = kPort,
          .path = kPath,
      });

  std::optional<httplib::Result> posted_initialize;
  std::thread post_thread([&]() {
    const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::InitializeMethod),
        .params =
            Json{
                {"protocolVersion",
                 std::string(mcp::protocol::McpProtocolVersion)},
                {"capabilities", Json::object()},
                {"clientInfo", Json{{"name", "native"}, {"version", "1"}}},
            },
        .id = std::int64_t{7},
    });
    for (int attempt = 0; attempt < 100; ++attempt) {
      httplib::Client client("127.0.0.1", kPort);
      client.set_connection_timeout(0, 100000);
      client.set_read_timeout(2, 0);
      client.set_write_timeout(2, 0);
      auto response = client.Post(
          kPath,
          httplib::Headers{
              {"Accept", "application/json, text/event-stream"},
              {"Content-Type", "application/json"},
              {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
          },
          body, "application/json");
      if (response) {
        posted_initialize = std::move(response);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  auto received = transport.receive();
  require(received.has_value(), "native server receive should succeed");
  require(received->has_value(), "native server should receive initialize");
  const auto* initialize_request =
      std::get_if<mcp::protocol::JsonRpcRequest>(&received->value());
  require(initialize_request != nullptr,
          "native server should receive request message");
  require(initialize_request->method == mcp::protocol::InitializeMethod,
          "native server request method mismatch");

  const auto sent = transport.send(mcp::protocol::make_response(
      initialize_request->id,
      Json{
          {"protocolVersion", std::string(mcp::protocol::McpProtocolVersion)},
          {"capabilities", Json::object()},
          {"serverInfo", Json{{"name", "native-server"}, {"version", "1"}}},
      }));
  require(sent.has_value(), "native server response send should succeed");

  if (post_thread.joinable()) {
    post_thread.join();
  }
  require(posted_initialize.has_value(),
          "native server initialize post should complete");
  require((*posted_initialize)->status == 200,
          "native server initialize post status mismatch");
  require((*posted_initialize)->has_header("Mcp-Session-Id"),
          "native server initialize should create a session");

  const auto closed = transport.close();
  require(closed.has_value(), "native server close should succeed");
}

void test_native_streamable_http_server_transport_diagnostics_timeout_cleanup() {
  constexpr int kPort = 40213;
  const std::string kPath = "/native-server-timeout";
  mcp::transport::StreamableHttpServerTransport transport(
      mcp::transport::StreamableHttpServerTransportOptions{
          .listen_host = "127.0.0.1",
          .listen_port = kPort,
          .path = kPath,
          .request_timeout = std::chrono::milliseconds(50),
      });

  std::optional<httplib::Result> posted_initialize;
  std::thread post_thread([&]() {
    const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::InitializeMethod),
        .params =
            Json{
                {"protocolVersion",
                 std::string(mcp::protocol::McpProtocolVersion)},
                {"capabilities", Json::object()},
                {"clientInfo", Json{{"name", "native"}, {"version", "1"}}},
            },
        .id = std::int64_t{71},
    });
    for (int attempt = 0; attempt < 100; ++attempt) {
      httplib::Client client("127.0.0.1", kPort);
      client.set_connection_timeout(0, 100000);
      client.set_read_timeout(2, 0);
      client.set_write_timeout(2, 0);
      auto response = client.Post(
          kPath,
          httplib::Headers{
              {"Accept", "application/json, text/event-stream"},
              {"Content-Type", "application/json"},
              {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
          },
          body, "application/json");
      if (response) {
        posted_initialize = std::move(response);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  auto received = transport.receive();
  require(received.has_value(), "native timeout server receive should succeed");
  require(received->has_value(),
          "native timeout server should receive initialize");
  const auto* initialize_request =
      std::get_if<mcp::protocol::JsonRpcRequest>(&received->value());
  require(initialize_request != nullptr,
          "native timeout server should receive request message");

  const auto initialized = transport.send(mcp::protocol::make_response(
      initialize_request->id,
      Json{
          {"protocolVersion", std::string(mcp::protocol::McpProtocolVersion)},
          {"capabilities", Json::object()},
          {"serverInfo",
           Json{{"name", "native-server-timeout"}, {"version", "1"}}},
      }));
  require(initialized.has_value(),
          "native timeout server initialize response should send");

  if (post_thread.joinable()) {
    post_thread.join();
  }
  require(posted_initialize.has_value(),
          "native timeout server initialize post should complete");
  require((*posted_initialize)->status == 200,
          "native timeout server initialize status mismatch");

  const auto request_sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::RootsListMethod),
      .params = Json::object(),
      .id = std::int64_t{72},
  });
  require(request_sent.has_value(),
          "native timeout server request should be accepted");

  received = transport.receive();
  require(received.has_value(),
          "native timeout server error receive should succeed");
  require(received->has_value(),
          "native timeout server error response should be queued");
  const auto* timeout_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(timeout_response != nullptr,
          "native timeout server should receive error response");
  require(timeout_response->error.has_value(),
          "native timeout server request should surface an error");
  require(
      timeout_response->error->message.find("timed out") != std::string::npos,
      "native timeout server error should be a timeout");

  const auto diagnostics = transport.diagnostics();
  require(diagnostics.at("pendingClientRequests").get<std::size_t>() == 0,
          "native timeout server should not leave pending client requests");
  require(diagnostics.at("activeRequestWorkers").get<std::size_t>() == 0,
          "native timeout server should not leave active request workers");
  require(diagnostics.at("completedRequestWorkers").get<std::size_t>() >= 1,
          "native timeout server completed worker count mismatch");
  require(diagnostics.at("failedRequestWorkers").get<std::size_t>() >= 1,
          "native timeout server failed worker count mismatch");
  require(diagnostics.at("timedOutRequestWorkers").get<std::size_t>() >= 1,
          "native timeout server timeout worker count mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native timeout server close should succeed");
}

void test_native_streamable_http_server_transport_rejects_unknown_client_response_id() {
  constexpr int kPort = 40217;
  const std::string kPath = "/native-server-unknown-response";
  mcp::transport::StreamableHttpServerTransport transport(
      mcp::transport::StreamableHttpServerTransportOptions{
          .listen_host = "127.0.0.1",
          .listen_port = kPort,
          .path = kPath,
      });

  const auto sent = transport.send(mcp::protocol::JsonRpcResponse{
      .id = mcp::protocol::RequestId{std::string("unknown-client-request")},
      .result = Json::object(),
  });
  require(
      !sent.has_value(),
      "native server should reject response without pending client request");
  require(sent.error().message ==
              "streamable http server transport has no pending client request",
          "native server unknown client response message mismatch");
  require(sent.error().detail == "unknown-client-request",
          "native server unknown client response detail mismatch");
  require(sent.error().category == "transport",
          "native server unknown client response category mismatch");

  const auto closed = transport.close();
  require(closed.has_value(),
          "native server unknown client response close should succeed");
}

void test_native_streamable_http_server_transport_close_unblocks_receive() {
  constexpr int kPort = 40218;
  const std::string kPath = "/native-server-close";
  mcp::transport::StreamableHttpServerTransport transport(
      mcp::transport::StreamableHttpServerTransportOptions{
          .listen_host = "127.0.0.1",
          .listen_port = kPort,
          .path = kPath,
      });

  std::atomic<bool> receive_finished{false};
  std::atomic<bool> received_end{false};
  std::thread receive_thread([&]() {
    const auto received = transport.receive();
    received_end.store(received.has_value() && !received->has_value());
    receive_finished.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  require(!receive_finished.load(),
          "native server receive should block before close");
  const auto closed = transport.close();
  require(closed.has_value(), "native server close should succeed");
  if (receive_thread.joinable()) {
    receive_thread.join();
  }
  require(receive_finished.load(),
          "native server close should unblock receive");
  require(received_end.load(), "native server receive should report end");
}

void test_native_streamable_http_server_transport_rejects_duplicate_client_request_id() {
  constexpr int kPort = 40219;
  const std::string kPath = "/native-server-duplicate-request";
  mcp::transport::StreamableHttpServerTransport transport(
      mcp::transport::StreamableHttpServerTransportOptions{
          .listen_host = "127.0.0.1",
          .listen_port = kPort,
          .path = kPath,
      });

  const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params =
          Json{
              {"protocolVersion",
               std::string(mcp::protocol::McpProtocolVersion)},
              {"capabilities", Json::object()},
              {"clientInfo", Json{{"name", "native"}, {"version", "1"}}},
          },
      .id = std::int64_t{81},
  });
  const auto headers = httplib::Headers{
      {"Accept", "application/json, text/event-stream"},
      {"Content-Type", "application/json"},
      {"Mcp-Method", std::string(mcp::protocol::InitializeMethod)},
  };

  std::optional<httplib::Result> first_post;
  std::thread first_thread([&]() {
    for (int attempt = 0; attempt < 100; ++attempt) {
      httplib::Client client("127.0.0.1", kPort);
      client.set_read_timeout(2, 0);
      auto response = client.Post(kPath, headers, body, "application/json");
      if (response) {
        first_post = std::move(response);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  auto received = transport.receive();
  require(received.has_value(),
          "native duplicate server receive should succeed");
  require(received->has_value(),
          "native duplicate server should receive first request");
  const auto* first_request =
      std::get_if<mcp::protocol::JsonRpcRequest>(&received->value());
  require(first_request != nullptr,
          "native duplicate server should receive request message");
  require(first_request->id == mcp::protocol::RequestId{std::int64_t{81}},
          "native duplicate server first request id mismatch");

  httplib::Client duplicate_client("127.0.0.1", kPort);
  duplicate_client.set_read_timeout(2, 0);
  const auto duplicate_post =
      duplicate_client.Post(kPath, headers, body, "application/json");
  require(static_cast<bool>(duplicate_post),
          "native duplicate server second post should return");
  require(duplicate_post->status == 200,
          "native duplicate server second post status mismatch");
  const auto duplicate_response =
      mcp::protocol::parse_response(duplicate_post->body);
  require(duplicate_response.has_value(),
          "native duplicate server error response should parse");
  require(duplicate_response->error.has_value(),
          "native duplicate server response should contain error");
  require(duplicate_response->error->message == "duplicate client request id",
          "native duplicate server error message mismatch");

  const auto sent = transport.send(mcp::protocol::make_response(
      first_request->id,
      Json{
          {"protocolVersion", std::string(mcp::protocol::McpProtocolVersion)},
          {"capabilities", Json::object()},
          {"serverInfo", Json{{"name", "native-server"}, {"version", "1"}}},
      }));
  require(sent.has_value(),
          "native duplicate server first response should send");
  if (first_thread.joinable()) {
    first_thread.join();
  }
  require(first_post.has_value(),
          "native duplicate server first post should complete");
  require((*first_post)->status == 200,
          "native duplicate server first post status mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native duplicate server close should succeed");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, void (*)()>> tests = {
      {"http transport decodes sse post and answers interleaved request",
       test_http_transport_decodes_sse_post_and_answers_interleaved_request},
      {"http transport posts error for throwing interleaved request handler",
       test_http_transport_posts_error_for_throwing_interleaved_request_handler},
      {"http transport opens get sse after session and dispatches notification",
       test_http_transport_opens_get_sse_after_session_and_dispatches_notification},
      {"http transport sets method and name headers",
       test_http_transport_sets_method_and_name_headers},
      {"server http transport delivers outbound notification",
       test_server_http_transport_delivers_outbound_notification},
      {"server http transport replays after last event id",
       test_server_http_transport_replays_after_last_event_id},
      {"server http transport rejects expired last event id",
       test_server_http_transport_rejects_expired_last_event_id},
      {"server http transport applies outbound backpressure",
       test_server_http_transport_applies_outbound_backpressure},
      {"server http transport keeps pending request across reconnect",
       test_server_http_transport_keeps_pending_request_across_reconnect},
      {"server http transport routes notifications by session",
       test_server_http_transport_routes_notifications_by_session},
      {"server http transport rejects concurrent sse streams",
       test_server_http_transport_rejects_concurrent_sse_streams},
      {"server http transport rejects duplicate inflight response",
       test_server_http_transport_rejects_duplicate_inflight_response},
      {"server http transport rejects stale session after delete",
       test_server_http_transport_rejects_stale_session_after_delete},
      {"server http transport rejects initialize protocol version mismatch",
       test_server_http_transport_rejects_initialize_protocol_version_mismatch},
      {"server http transport rejects malformed post body",
       test_server_http_transport_rejects_malformed_post_body},
      {"server http transport emits sse retry priming",
       test_server_http_transport_emits_sse_retry_priming},
      {"server http transport accepts client notification with 202",
       test_server_http_transport_accepts_client_notification_with_202},
      {"server http transport rejects mismatched origin when allowlisted",
       test_server_http_transport_rejects_mismatched_origin_when_allowlisted},
      {"server http transport can request client",
       test_server_http_transport_can_request_client},
      {"server http transport request timeout sends cancelled over sse",
       test_server_http_transport_request_timeout_sends_cancelled_over_sse},
      {"server http transport explicit cancel sends cancelled over sse",
       test_server_http_transport_explicit_cancel_sends_cancelled_over_sse},
      {"server http transport writes error for throwing request handler",
       test_server_http_transport_writes_error_for_throwing_request_handler},
      {"client http transport times out initialize",
       test_client_http_transport_times_out_initialize},
      {"client http transport rejects unexpected response id",
       test_client_http_transport_rejects_unexpected_response_id},
      {"client http transport uses uri and auth header",
       test_client_http_transport_uses_uri_and_auth_header},
      {"client connect_streamable_http accepts uri string",
       test_client_connect_streamable_http_accepts_uri_string},
      {"native streamable http transport exposes client contract",
       test_native_streamable_http_transport_exposes_client_contract},
      {"native streamable http transport diagnostics timeout cleanup",
       test_native_streamable_http_transport_diagnostics_timeout_cleanup},
      {"native streamable http transport rejects unknown server response id",
       test_native_streamable_http_transport_rejects_unknown_server_response_id},
      {"native streamable http transport surfaces unexpected response id",
       test_native_streamable_http_transport_surfaces_unexpected_response_id},
      {"native streamable http transport rejects duplicate request id",
       test_native_streamable_http_transport_rejects_duplicate_request_id},
      {"native streamable http transport close unblocks receive",
       test_native_streamable_http_transport_close_unblocks_receive},
      {"native streamable http transport receives server request",
       test_native_streamable_http_transport_receives_server_request},
      {"native streamable http server transport exposes server contract",
       test_native_streamable_http_server_transport_exposes_server_contract},
      {"native streamable http server transport diagnostics timeout cleanup",
       test_native_streamable_http_server_transport_diagnostics_timeout_cleanup},
      {"native streamable http server transport rejects unknown client "
       "response id",
       test_native_streamable_http_server_transport_rejects_unknown_client_response_id},
      {"native streamable http server transport close unblocks receive",
       test_native_streamable_http_server_transport_close_unblocks_receive},
      {"native streamable http server transport rejects duplicate client "
       "request id",
       test_native_streamable_http_server_transport_rejects_duplicate_client_request_id},
  };

  std::size_t failures = 0;
  for (const auto& [name, test] : tests) {
    try {
      std::cout << "[RUN] " << name << '\n';
      std::cout.flush();
      test();
      std::cout << "[PASS] " << name << '\n';
      std::cout.flush();
    } catch (const std::exception& ex) {
      ++failures;
      std::cerr << "[FAIL] " << name << ": " << ex.what() << '\n';
      std::cerr.flush();
    }
  }

  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }

  std::cout << tests.size() << " test(s) passed\n";
  return 0;
}
