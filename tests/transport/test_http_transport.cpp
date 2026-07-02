// Copyright (c) 2025 [caomengxuan666]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "cxxmcp/client/http_transport.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server/http_transport.hpp"
#include "cxxmcp/server/peer.hpp"
#include "cxxmcp/server/server.hpp"
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
          .method = mcp::protocol::InitializeMethod,
          .params = Json::object(),
          .id = std::int64_t{1},
      });
  const httplib::Headers initialize_headers{
      {"Accept", "application/json, text/event-stream"},
      {"Content-Type", "application/json"},
      {"Mcp-Method", mcp::protocol::InitializeMethod},
  };

  for (int attempt = 0; attempt < 400; ++attempt) {
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

class RawTcpSocket {
 public:
#ifdef _WIN32
  using SocketHandle = SOCKET;
  static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
  using SocketHandle = int;
  static constexpr SocketHandle kInvalidSocket = -1;
#endif

  RawTcpSocket() = default;

  RawTcpSocket(const RawTcpSocket&) = delete;
  RawTcpSocket& operator=(const RawTcpSocket&) = delete;

  RawTcpSocket(RawTcpSocket&& other) noexcept : socket_(other.socket_) {
    other.socket_ = kInvalidSocket;
  }

  RawTcpSocket& operator=(RawTcpSocket&& other) noexcept {
    if (this != &other) {
      close();
      socket_ = other.socket_;
      other.socket_ = kInvalidSocket;
    }
    return *this;
  }

  ~RawTcpSocket() { close(); }

  static std::optional<RawTcpSocket> connect_localhost(
      int port, std::chrono::milliseconds receive_timeout =
                    std::chrono::milliseconds(1000)) {
    initialize_platform();
    RawTcpSocket raw;
    raw.socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (raw.socket_ == kInvalidSocket) {
      return std::nullopt;
    }
    raw.set_receive_timeout(receive_timeout);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(raw.socket_, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
      return std::nullopt;
    }
    return raw;
  }

  bool send_all(const std::string& data) {
    const char* cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
      const int sent = ::send(socket_, cursor, static_cast<int>(remaining), 0);
      if (sent <= 0) {
        return false;
      }
      cursor += sent;
      remaining -= static_cast<std::size_t>(sent);
    }
    return true;
  }

  std::string receive_available() {
    std::string received;
    char buffer[512];
    while (true) {
      const int count = ::recv(socket_, buffer, sizeof(buffer), 0);
      if (count > 0) {
        received.append(buffer, static_cast<std::size_t>(count));
        if (count < static_cast<int>(sizeof(buffer))) {
          return received;
        }
        continue;
      }
      return received;
    }
  }

  void close() noexcept {
    if (socket_ == kInvalidSocket) {
      return;
    }
#ifdef _WIN32
    ::closesocket(socket_);
#else
    ::close(socket_);
#endif
    socket_ = kInvalidSocket;
  }

 private:
  static void initialize_platform() {
#ifdef _WIN32
    static const int initialized = [] {
      WSADATA data;
      return WSAStartup(MAKEWORD(2, 2), &data);
    }();
    (void)initialized;
#endif
  }

  void set_receive_timeout(std::chrono::milliseconds timeout) {
#ifdef _WIN32
    const DWORD timeout_ms = static_cast<DWORD>(timeout.count());
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval value{};
    value.tv_sec = static_cast<decltype(value.tv_sec)>(timeout.count() / 1000);
    value.tv_usec =
        static_cast<decltype(value.tv_usec)>((timeout.count() % 1000) * 1000);
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
#endif
  }

  SocketHandle socket_ = kInvalidSocket;
};

std::string http_post_header(std::string_view path,
                             std::size_t content_length) {
  return "POST " + std::string(path) +
         " HTTP/1.1\r\n"
         "Host: 127.0.0.1\r\n"
         "Accept: application/json, text/event-stream\r\n"
         "Content-Type: application/json\r\n"
         "Content-Length: " +
         std::to_string(content_length) + "\r\n\r\n";
}

std::string initialize_http_session(int port, const std::string& path,
                                    std::int64_t id = 1) {
  httplib::Client http_client("127.0.0.1", port);
  const auto initialize_response =
      http_client.Post(path,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
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

httplib::Result post_initialized_notification(int port, const std::string& path,
                                              const std::string& session_id) {
  httplib::Client http_client("127.0.0.1", port);
  return http_client.Post(
      path,
      httplib::Headers{
          {"Accept", "application/json, text/event-stream"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
          {"Mcp-Session-Id", session_id},
          {"Mcp-Method", mcp::protocol::InitializedMethod},
      },
      serialize_test_notification(
          mcp::protocol::make_initialized_notification()),
      "application/json");
}

void require_initialized_notification(int port, const std::string& path,
                                      const std::string& session_id) {
  const auto initialized =
      post_initialized_notification(port, path, session_id);
  require(static_cast<bool>(initialized),
          "initialized notification should return");
  require(initialized->status == 202,
          "initialized notification should be accepted");
}

httplib::Headers sse_headers(const std::string& session_id) {
  return httplib::Headers{
      {"Mcp-Session-Id", session_id},
      {"Accept", "text/event-stream"},
      {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
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
            .method = mcp::protocol::SamplingCreateMessageMethod,
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
            .method = mcp::protocol::SamplingCreateMessageMethod,
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
                         mcp::protocol::McpProtocolVersion2025_06_18},
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
                mcp::protocol::McpProtocolVersion2025_06_18,
            "sse get should use negotiated protocol version");
    get_seen.store(true);

    const auto notification =
        serialize_test_notification(mcp::protocol::JsonRpcNotification{
            .method = mcp::protocol::ToolsListChangedNotificationMethod,
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
        return mcp::core::unexpected(mcp::core::Error{
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
      .method = mcp::protocol::InitializeMethod,
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
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{29},
  });
  for (int attempt = 0; attempt < 100; ++attempt) {
    httplib::Client client("127.0.0.1", kPort);
    auto response =
        client.Post(kPath,
                    httplib::Headers{
                        {"Accept", "application/json, text/event-stream"},
                        {"Content-Type", "application/json"},
                        {"Mcp-Method", mcp::protocol::InitializeMethod},
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
        return mcp::core::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected request",
        });
      },
      [](const mcp::protocol::JsonRpcNotification&)
          -> mcp::core::Result<mcp::core::Unit> { return mcp::core::Unit{}; });
  require(started.has_value(), "http transport should start");

  const auto initialized = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(initialized.has_value(), "initialize should succeed");
  require(wait_for([&]() { return get_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "client should open a stream after initialize");

  const auto call = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::ToolsCallMethod,
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
          .method = mcp::protocol::InitializeMethod,
          .params = Json::object(),
          .id = std::int64_t{2},
      });
  require(!initialize_request.empty(), "initialize request should serialize");

  httplib::Client http_client("127.0.0.1", kPort);
  const auto initialize_response =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
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
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
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
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
            .method = mcp::protocol::ToolsListChangedNotificationMethod,
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
          .params = Json{{"index", 1}},
      });
  require(first.has_value(), "first queued notification should succeed");

  const auto second = server_transport.transport().send_notification(
      mcp::protocol::JsonRpcNotification{
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
          .params = Json{{"index", 2}},
      });
  require(!second.has_value(),
          "second queued notification should hit backpressure");
  require(second.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::RateLimited),
          "backpressure should map to rate limited");

  server_transport.transport().stop();
}

void test_server_http_transport_bounds_outbound_queue_bytes() {
  constexpr int kPort = 40223;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .max_pending_sse_events = 8,
              .max_pending_sse_bytes = 512,
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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

  const auto small_notification =
      server_transport.transport().send_notification(
          mcp::protocol::JsonRpcNotification{
              .method = mcp::protocol::ToolsListChangedNotificationMethod,
              .params = Json{{"index", 1}},
          });
  require(small_notification.has_value(), "small notification should queue");

  const auto oversized = server_transport.transport().send_notification(
      mcp::protocol::JsonRpcNotification{
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
          .params = Json{{"blob", std::string(1024, 'x')}},
      });
  require(!oversized.has_value(),
          "oversized notification should hit byte backpressure");
  require(oversized.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::RateLimited),
          "byte backpressure should map to rate limited");

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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
            .method = mcp::protocol::RootsListMethod,
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
          {"Accept", "application/json, text/event-stream"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
          .method = mcp::protocol::ProgressNotificationMethod,
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
            .method = mcp::protocol::RootsListMethod,
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
      {"Accept", "application/json, text/event-stream"},
      {"Content-Type", "application/json"},
      {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
                 {"Accept", "application/json, text/event-stream"},
                 {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
                 {"Mcp-Session-Id", session_id},
             });
  require(deleted != nullptr, "session delete should return");
  require(deleted->status == 204, "session delete should terminate session");

  const auto stale_post = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json, text/event-stream"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
          {"Mcp-Session-Id", session_id},
          {"Mcp-Method", mcp::protocol::ToolsListChangedNotificationMethod},
      },
      serialize_test_notification(mcp::protocol::JsonRpcNotification{
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
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
            .result =
                Json{{"protocolVersion", mcp::protocol::McpProtocolVersion},
                     {"capabilities", Json::object()},
                     {"serverInfo",
                      Json{{"name", "server-http-test"}, {"version", "1"}}}},
        };
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");
  handler_called.store(false);

  Json initialize_params = Json::object();
  initialize_params["protocolVersion"] = mcp::protocol::McpProtocolVersion;
  initialize_params["capabilities"] = Json::object();
  initialize_params["clientInfo"] =
      Json{{"name", "version-test"}, {"version", "1"}};

  httplib::Client http_client("127.0.0.1", kPort);
  const auto mismatch =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                           {"MCP-Protocol-Version", "2024-11-05"},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
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
  const auto unsupported =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                           {"MCP-Protocol-Version", "1900-01-01"},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
                           {"Accept", "application/json, text/event-stream"},
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

  std::string deeply_nested =
      R"({"jsonrpc":"2.0","method":"ping","id":72,"params":)";
  for (int index = 0; index < 140; ++index) {
    deeply_nested.append(R"({"x":)");
  }
  deeply_nested.append("0");
  for (int index = 0; index < 140; ++index) {
    deeply_nested.append("}");
  }
  deeply_nested.append("}");

  const auto too_deep =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                       },
                       deeply_nested, "application/json");
  require(too_deep != nullptr, "too-deep post should return");
  require(too_deep->status == 400, "too-deep post should be rejected");
  const auto too_deep_body = mcp::protocol::parse_response(too_deep->body);
  require(too_deep_body.has_value(),
          "too-deep post error response should parse");
  require(too_deep_body->error.has_value(),
          "too-deep post response should contain error");
  require(too_deep_body->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "too-deep post error code mismatch");
  require(!handler_called.load(), "too-deep post should not reach handler");

  server_transport.transport().stop();
}

void test_server_http_transport_rejects_oversized_post_body() {
  constexpr int kPort = 40224;
  const std::string kPath = "/mcp";
  std::atomic<bool> handler_called{false};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .max_request_body_bytes = 16,
              .read_timeout = std::chrono::milliseconds(500),
              .write_timeout = std::chrono::milliseconds(500),
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        handler_called.store(true);
        return mcp::protocol::make_response(request.id, Json::object());
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for(
              [&] {
                httplib::Client http_client("127.0.0.1", kPort);
                http_client.set_connection_timeout(0, 100000);
                http_client.set_read_timeout(0, 200000);
                return static_cast<bool>(http_client.Post(
                    kPath,
                    httplib::Headers{
                        {"Accept", "application/json, text/event-stream"},
                        {"Content-Type", "application/json"},
                    },
                    "{}", "application/json"));
              },
              std::chrono::seconds(2)),
          "server transport should become reachable");
  handler_called.store(false);

  httplib::Client http_client("127.0.0.1", kPort);
  http_client.set_read_timeout(std::chrono::seconds(1));
  const auto oversized =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                       },
                       std::string(17, 'x'), "application/json");
  require(oversized != nullptr, "oversized post should return");
  require(oversized->status == 413, "oversized post should be rejected");
  require(!handler_called.load(), "oversized post should not reach handler");

  server_transport.transport().stop();
}

void test_server_http_transport_slow_client_body_does_not_reach_handler() {
  constexpr int kPort = 40244;
  const std::string kPath = "/slow-body";
  std::atomic<bool> handler_called{false};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .read_timeout = std::chrono::milliseconds(100),
              .write_timeout = std::chrono::milliseconds(250),
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        handler_called.store(true);
        return mcp::protocol::make_response(request.id, Json::object());
      });

  std::optional<RawTcpSocket> socket;
  require(wait_for(
              [&] {
                socket = RawTcpSocket::connect_localhost(
                    kPort, std::chrono::milliseconds(1000));
                return socket.has_value();
              },
              std::chrono::seconds(2)),
          "server transport should accept raw slow-body client");

  require(socket->send_all(http_post_header(kPath, 1024)),
          "slow-body client should send headers");
  require(socket->send_all("{"), "slow-body client should send partial body");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  (void)socket->receive_available();

  require(!handler_called.load(),
          "slow incomplete body should time out before reaching handler");
  server_transport.transport().stop();
}

void test_server_http_transport_closed_client_during_response_does_not_hang() {
  constexpr int kPort = 40254;
  const std::string kPath = "/closed-client";
  std::atomic<bool> handler_called{false};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .read_timeout = std::chrono::milliseconds(250),
              .write_timeout = std::chrono::milliseconds(100),
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        handler_called.store(true);
        return mcp::protocol::make_response(
            request.id,
            Json{
                {"protocolVersion", mcp::protocol::McpProtocolVersion},
                {"capabilities", Json::object()},
                {"serverInfo",
                 Json{{"name", "closed-client"}, {"version", "1"}}},
            });
      });

  const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params =
          Json{{"protocolVersion", mcp::protocol::McpProtocolVersion},
               {"capabilities", Json::object()},
               {"clientInfo", Json{{"name", "closed"}, {"version", "1"}}}},
      .id = std::int64_t{99},
  });

  std::optional<RawTcpSocket> socket;
  require(wait_for(
              [&] {
                socket = RawTcpSocket::connect_localhost(
                    kPort, std::chrono::milliseconds(1000));
                return socket.has_value();
              },
              std::chrono::seconds(2)),
          "server transport should accept raw closed-client connection");
  require(socket->send_all(http_post_header(kPath, body.size()) + body),
          "closed client should send complete request");
  socket->close();

  require(
      wait_for([&] { return handler_called.load(); }, std::chrono::seconds(2)),
      "closed client request should reach handler without hanging");
  server_transport.transport().stop();
}

void test_server_http_transport_limits_active_sessions() {
  constexpr int kPort = 40234;
  const std::string kPath = "/mcp";
  std::atomic<int> initialize_count{0};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .max_sessions = 1,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          initialize_count.fetch_add(1);
          return mcp::protocol::make_response(
              request.id,
              Json{
                  {"protocolVersion", mcp::protocol::McpProtocolVersion},
                  {"capabilities", Json::object()},
                  {"serverInfo",
                   Json{{"name", "server-http-test"}, {"version", "1"}}},
              });
        }
        return mcp::protocol::make_response(request.id, Json::object());
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");

  httplib::Client http_client("127.0.0.1", kPort);
  const auto response =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
                           .params = Json::object(),
                           .id = std::int64_t{42},
                       }),
                       "application/json");
  require(response != nullptr, "limited initialize should return");
  require(response->status == 429,
          "limited initialize should return too many requests");
  const auto parsed = mcp::protocol::parse_response(response->body);
  require(parsed.has_value(), "limited initialize error should parse");
  require(parsed->error.has_value(),
          "limited initialize response should contain error");
  require(parsed->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::RateLimited),
          "limited initialize error code mismatch");
  require(initialize_count.load() == 1,
          "limited initialize should not reach handler again");

  server_transport.transport().stop();
}

void test_server_http_transport_requires_initialized_before_business_request() {
  constexpr int kPort = 40225;
  const std::string kPath = "/mcp";
  std::atomic<int> tools_list_count{0};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::make_response(
              request.id,
              Json{
                  {"protocolVersion", mcp::protocol::McpProtocolVersion},
                  {"capabilities", Json::object()},
                  {"serverInfo",
                   Json{{"name", "server-http-test"}, {"version", "1"}}},
              });
        }
        if (request.method == mcp::protocol::ToolsListMethod) {
          tools_list_count.fetch_add(1);
          return mcp::protocol::make_response(
              request.id, mcp::protocol::tools_list_result_to_json(
                              mcp::protocol::ToolsListResult{}));
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

  httplib::Headers tools_headers{
      {"Accept", "application/json, text/event-stream"},
      {"Content-Type", "application/json"},
      {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
      {"Mcp-Session-Id", session_id},
      {"Mcp-Method", mcp::protocol::ToolsListMethod},
  };
  const auto tools_request =
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = mcp::protocol::ToolsListMethod,
          .params = Json::object(),
          .id = std::int64_t{3},
      });

  httplib::Client http_client("127.0.0.1", kPort);
  const auto rejected =
      http_client.Post(kPath, tools_headers, tools_request, "application/json");
  require(static_cast<bool>(rejected),
          "business request before initialized should return");
  require(rejected->status == 400,
          "business request before initialized should be rejected");
  const auto rejected_response = mcp::protocol::parse_response(rejected->body);
  require(rejected_response.has_value(),
          "business request rejection should parse");
  require(rejected_response->error.has_value(),
          "business request rejection should be an error response");
  require(rejected_response->error->message ==
              "http transport session is not initialized",
          "business request rejection message mismatch");
  require(tools_list_count.load() == 0,
          "business request before initialized should not reach handler");

  require_initialized_notification(kPort, kPath, session_id);
  const auto accepted =
      http_client.Post(kPath, tools_headers, tools_request, "application/json");
  require(static_cast<bool>(accepted),
          "business request after initialized should return");
  require(accepted->status == 200,
          "business request after initialized should be accepted");
  const auto accepted_response = mcp::protocol::parse_response(accepted->body);
  require(accepted_response.has_value(),
          "business request accepted response should parse");
  require(accepted_response->result.has_value(),
          "business request after initialized should succeed");
  require(tools_list_count.load() == 1,
          "business request after initialized should reach handler");

  server_transport.transport().stop();
}

Json stateless_meta() {
  return Json{{"io.modelcontextprotocol/protocolVersion",
               mcp::protocol::McpProtocolVersion},
              {"io.modelcontextprotocol/clientInfo",
               Json{{"name", "stateless-test"}, {"version", "1"}}},
              {"io.modelcontextprotocol/clientCapabilities", Json::object()}};
}

void test_server_http_transport_stateless_accepts_request_without_session() {
  constexpr int kPort = 40244;
  const std::string kPath = "/mcp";
  std::string observed_session_id = "not-called";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .stateless = true,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext& context) {
        observed_session_id = context.session_id;
        require(request.method == mcp::protocol::ToolsListMethod,
                "stateless request method mismatch");
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result = Json{{"tools", Json::array()}},
        };
      });

  httplib::Client http_client("127.0.0.1", kPort);
  httplib::Result response;
  const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::ToolsListMethod,
      .params = Json{{"_meta", stateless_meta()}},
      .id = std::int64_t{81},
  });
  for (int attempt = 0; attempt < 100; ++attempt) {
    response = http_client.Post(
        kPath,
        httplib::Headers{
            {"Accept", "application/json, text/event-stream"},
            {"Content-Type", "application/json"},
            {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
            {"Mcp-Method", mcp::protocol::ToolsListMethod},
        },
        body, "application/json");
    if (response) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  require(static_cast<bool>(response), "stateless request should respond");
  require(response->status == 200, "stateless request should return 200");
  require(!response->has_header("Mcp-Session-Id"),
          "stateless response must not return a session id");
  require(observed_session_id.empty(),
          "stateless context should not carry a session id");
  const auto parsed = mcp::protocol::parse_response(response->body);
  require(parsed.has_value(), "stateless response should parse");
  require(parsed->result.has_value(), "stateless response should succeed");

  server_transport.transport().stop();
}

void test_server_http_transport_stateless_rejects_task_state_methods() {
  constexpr int kPort = 40246;
  const std::string kPath = "/mcp";
  std::atomic<int> handler_calls{0};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .stateless = true,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        (void)request;
        ++handler_calls;
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result = Json::object(),
        };
      });

  httplib::Client http_client("127.0.0.1", kPort);
  httplib::Result response;
  const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::TasksListMethod,
      .params = Json{{"_meta", stateless_meta()}},
      .id = std::int64_t{84},
  });
  for (int attempt = 0; attempt < 100; ++attempt) {
    response = http_client.Post(
        kPath,
        httplib::Headers{
            {"Accept", "application/json, text/event-stream"},
            {"Content-Type", "application/json"},
            {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
            {"Mcp-Method", mcp::protocol::TasksListMethod},
        },
        body, "application/json");
    if (response) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  require(static_cast<bool>(response), "stateless tasks/list should respond");
  require(response->status == 404,
          "stateless tasks/list should be unavailable");
  const auto parsed = mcp::protocol::parse_response(response->body);
  require(parsed.has_value(), "stateless tasks/list response should parse");
  require(parsed->error.has_value(),
          "stateless tasks/list should return an error");
  require(parsed->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
          "stateless tasks/list error code mismatch");
  require(handler_calls.load() == 0,
          "stateless tasks/list must not reach handler");

  server_transport.transport().stop();
}

void test_server_http_transport_stateless_rejects_task_tool_call() {
  constexpr int kPort = 40247;
  const std::string kPath = "/mcp";
  std::atomic<int> handler_calls{0};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .stateless = true,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        (void)request;
        ++handler_calls;
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result = Json::object(),
        };
      });

  httplib::Client http_client("127.0.0.1", kPort);
  httplib::Result response;
  const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::ToolsCallMethod,
      .params = Json{{"name", "slow"},
                     {"arguments", Json::object()},
                     {"task", Json::object()},
                     {"_meta", stateless_meta()}},
      .id = std::int64_t{85},
  });
  for (int attempt = 0; attempt < 100; ++attempt) {
    response = http_client.Post(
        kPath,
        httplib::Headers{
            {"Accept", "application/json, text/event-stream"},
            {"Content-Type", "application/json"},
            {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
            {"Mcp-Method", mcp::protocol::ToolsCallMethod},
            {"Mcp-Name", "slow"},
        },
        body, "application/json");
    if (response) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  require(static_cast<bool>(response),
          "stateless task tools/call should respond");
  require(response->status == 404,
          "stateless task tools/call should be unavailable");
  const auto parsed = mcp::protocol::parse_response(response->body);
  require(parsed.has_value(),
          "stateless task tools/call response should parse");
  require(parsed->error.has_value(),
          "stateless task tools/call should return an error");
  require(parsed->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
          "stateless task tools/call error code mismatch");
  require(handler_calls.load() == 0,
          "stateless task tools/call must not reach handler");

  server_transport.transport().stop();
}

void test_server_http_transport_stateless_initialize_does_not_create_session() {
  constexpr int kPort = 40245;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .stateless = true,
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext& context) {
        require(context.session_id.empty(),
                "stateless initialize context should not have a session id");
        require(request.method == mcp::protocol::InitializeMethod,
                "initialize method mismatch");
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result =
                Json{{"protocolVersion", mcp::protocol::McpProtocolVersion},
                     {"capabilities", Json::object()},
                     {"serverInfo",
                      Json{{"name", "stateless"}, {"version", "1"}}}},
        };
      });

  httplib::Client http_client("127.0.0.1", kPort);
  httplib::Result response;
  const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = Json{{"protocolVersion", mcp::protocol::McpProtocolVersion},
                     {"clientInfo",
                      Json{{"name", "stateless-client"}, {"version", "1"}}},
                     {"capabilities", Json::object()}},
      .id = std::int64_t{82},
  });
  for (int attempt = 0; attempt < 100; ++attempt) {
    response =
        http_client.Post(kPath,
                         httplib::Headers{
                             {"Accept", "application/json, text/event-stream"},
                             {"Content-Type", "application/json"},
                             {"Mcp-Method", mcp::protocol::InitializeMethod},
                         },
                         body, "application/json");
    if (response) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  require(static_cast<bool>(response), "stateless initialize should respond");
  require(response->status == 200, "stateless initialize should return 200");
  require(!response->has_header("Mcp-Session-Id"),
          "stateless initialize must not create a session");

  server_transport.transport().stop();
}

void test_client_http_transport_stateless_adds_meta_without_session() {
  HttpServerFixture fixture;
  std::optional<Json> observed_meta;
  bool saw_session_header = false;

  fixture.server().Post("/mcp", [&](const httplib::Request& request,
                                    httplib::Response& response) {
    saw_session_header = request.has_header("Mcp-Session-Id");
    const auto parsed = mcp::protocol::parse_request(request.body);
    require(parsed.has_value(), "stateless client request should parse");
    require(parsed->params.is_object(), "stateless params should be object");
    require(parsed->params.contains("_meta"),
            "stateless client should add _meta");
    observed_meta = parsed->params.at("_meta");
    response.status = 200;
    response.set_content(serialize_test_response(mcp::protocol::JsonRpcResponse{
                             .id = parsed->id,
                             .result = Json{{"tools", Json::array()}},
                         }),
                         "application/json");
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = fixture.port(),
      .path = "/mcp",
      .stateless = true,
  });

  const auto response = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::ToolsListMethod,
      .params = Json::object(),
      .id = std::int64_t{83},
  });
  require(response.has_value(), "stateless client send should succeed");
  require(response->result.has_value(), "stateless client response should win");
  require(!saw_session_header, "stateless client must not send session header");
  require(observed_meta.has_value(),
          "stateless client meta should be observed");
  require(observed_meta->at("io.modelcontextprotocol/protocolVersion") ==
              mcp::protocol::McpProtocolVersion,
          "stateless client protocolVersion meta mismatch");
  require(observed_meta->contains("io.modelcontextprotocol/clientInfo"),
          "stateless clientInfo meta missing");
  require(observed_meta->contains("io.modelcontextprotocol/clientCapabilities"),
          "stateless clientCapabilities meta missing");
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
  const auto initialize_response =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
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
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
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
  std::string observed_method;

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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
      [&](const mcp::protocol::JsonRpcNotification& notification,
          const mcp::server::SessionContext& context) {
        observed_session_id = context.session_id;
        observed_method = notification.method;
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
          {"Accept", "application/json, text/event-stream"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
          {"Mcp-Method", mcp::protocol::ToolsListChangedNotificationMethod},
      },
      serialize_test_notification(mcp::protocol::JsonRpcNotification{
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
          .params = Json::object(),
      }),
      "application/json");
  require(rejected_without_session != nullptr,
          "notification without session should return a response");
  require(rejected_without_session->status == 404,
          "notification without session should be rejected");

  const auto initialize_response =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
                           .params = Json::object(),
                           .id = std::int64_t{1},
                       }),
                       "application/json");
  require(static_cast<bool>(initialize_response), "initialize should succeed");
  require(initialize_response->has_header("Mcp-Session-Id"),
          "initialize should return a session id");
  const auto session_id =
      initialize_response->get_header_value("Mcp-Session-Id");

  const auto rejected_before_initialized = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json, text/event-stream"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
          {"Mcp-Session-Id", session_id},
          {"Mcp-Method", mcp::protocol::ToolsListChangedNotificationMethod},
      },
      serialize_test_notification(mcp::protocol::JsonRpcNotification{
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
          .params = Json::object(),
      }),
      "application/json");
  require(static_cast<bool>(rejected_before_initialized),
          "notification before initialized should return");
  require(rejected_before_initialized->status == 400,
          "notification before initialized should be rejected");
  require(observed_method.empty(),
          "notification before initialized should not reach handler");

  require_initialized_notification(kPort, kPath, session_id);
  require(
      observed_session_id == session_id,
      "initialized notification session id should match negotiated session");
  require(observed_method == mcp::protocol::InitializedMethod,
          "initialized notification should reach handler");

  const auto notification_response = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json, text/event-stream"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
          {"Mcp-Session-Id", session_id},
          {"Mcp-Method", mcp::protocol::ToolsListChangedNotificationMethod},
      },
      serialize_test_notification(mcp::protocol::JsonRpcNotification{
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
          .params = Json::object(),
      }),
      "application/json");
  require(static_cast<bool>(notification_response),
          "notification should be accepted");
  require(notification_response->status == 202,
          "notification status should be 202 Accepted");
  require(observed_session_id == session_id,
          "notification session id should match negotiated session");
  require(observed_method == mcp::protocol::ToolsListChangedNotificationMethod,
          "notification method should reach handler after initialized");

  const auto rejected = http_client.Delete(
      kPath, httplib::Headers{
                 {"Accept", "application/json, text/event-stream"},
                 {"MCP-Protocol-Version", "0.0.0"},
                 {"Mcp-Session-Id", session_id},
             });
  require(rejected != nullptr,
          "delete with wrong protocol version should return a response");
  require(rejected->status == 400,
          "delete with wrong protocol version should be rejected");

  server_transport.transport().stop();
}

void test_server_http_transport_copies_headers_to_session_context() {
  constexpr int kPort = 40220;
  const std::string kPath = "/mcp";
  std::string observed_authorization;
  std::string observed_tenant;

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext& context) {
        observed_authorization = context.headers.at("Authorization");
        observed_tenant = context.headers.at("X-Tenant");
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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

  httplib::Client http_client("127.0.0.1", kPort);
  httplib::Result response;
  for (int attempt = 0; attempt < 100; ++attempt) {
    response =
        http_client.Post(kPath,
                         httplib::Headers{
                             {"Accept", "application/json, text/event-stream"},
                             {"Content-Type", "application/json"},
                             {"Mcp-Method", mcp::protocol::InitializeMethod},
                             {"Authorization", "Bearer header-token"},
                             {"X-Tenant", "tenant-a"},
                         },
                         serialize_test_request(mcp::protocol::JsonRpcRequest{
                             .method = mcp::protocol::InitializeMethod,
                             .params = Json::object(),
                             .id = std::int64_t{1},
                         }),
                         "application/json");
    if (response) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  require(static_cast<bool>(response), "initialize should succeed");
  require(response->status == 200, "initialize status should be 200");
  require(observed_authorization == "Bearer header-token",
          "session context authorization header mismatch");
  require(observed_tenant == "tenant-a",
          "session context custom header mismatch");

  server_transport.transport().stop();
}

void test_server_http_transport_unauthorized_uses_bearer_challenge() {
  class RejectingAuthProvider final : public mcp::server::AuthProvider {
   public:
    mcp::core::Result<mcp::server::AuthIdentity> authenticate(
        const mcp::server::AuthRequest&) override {
      return mcp::core::unexpected(mcp::server::make_auth_error(
          "authentication failed", "missing bearer token"));
    }
  };

  constexpr int kPort = 40221;
  const std::string kPath = "/mcp";
  mcp::server::Server server(mcp::server::ServerOptions{});
  server.set_auth_provider(std::make_unique<RejectingAuthProvider>());

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .auth_challenge = "Bearer realm=\"cxxmcp\"",
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext& context) {
        return server.handle_request(request, context);
      });

  httplib::Result response;
  for (int attempt = 0; attempt < 100; ++attempt) {
    httplib::Client http_client("127.0.0.1", kPort);
    response =
        http_client.Post(kPath,
                         httplib::Headers{
                             {"Accept", "application/json, text/event-stream"},
                             {"Content-Type", "application/json"},
                             {"Mcp-Method", mcp::protocol::InitializeMethod},
                         },
                         serialize_test_request(mcp::protocol::JsonRpcRequest{
                             .method = mcp::protocol::InitializeMethod,
                             .params = Json::object(),
                             .id = std::int64_t{41},
                         }),
                         "application/json");
    if (response) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  require(static_cast<bool>(response),
          "unauthorized initialize should return a response");
  require(response->status == 401, "unauthorized initialize should return 401");
  require(response->has_header("WWW-Authenticate"),
          "unauthorized response should include WWW-Authenticate");
  require(response->get_header_value("WWW-Authenticate") ==
              "Bearer realm=\"cxxmcp\"",
          "unauthorized response challenge mismatch");
  require(!response->has_header("Mcp-Session-Id"),
          "unauthorized initialize must not create a session");

  const auto parsed = mcp::protocol::parse_response(response->body);
  require(parsed.has_value(), "unauthorized response body should parse");
  require(parsed->error.has_value(), "unauthorized body should be an error");
  require(parsed->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "unauthorized error code mismatch");
  require(parsed->error->data.has_value() &&
              parsed->error->data->at("category") == "auth",
          "unauthorized error category mismatch");

  server_transport.transport().stop();
}

void test_server_http_transport_authorized_request_sets_auth_identity() {
  class TokenAuthProvider final : public mcp::server::AuthProvider {
   public:
    mcp::core::Result<mcp::server::AuthIdentity> authenticate(
        const mcp::server::AuthRequest& request) override {
      const auto authorization = request.headers.find("Authorization");
      if (authorization == request.headers.end() ||
          authorization->second != "Bearer valid-token") {
        return mcp::core::unexpected(mcp::server::make_auth_error(
            "authentication failed", "invalid bearer token"));
      }
      observed_http_methods.push_back(request.http_method.value_or(""));
      observed_http_urls.push_back(request.http_url.value_or(""));
      mcp::server::AuthIdentity identity;
      identity.subject = "http-subject";
      identity.claims.emplace("scope", "tools:call");
      return identity;
    }

    std::vector<std::string> observed_http_methods;
    std::vector<std::string> observed_http_urls;
  };

  constexpr int kPort = 40222;
  const std::string kPath = "/mcp";
  mcp::server::Server server(mcp::server::ServerOptions{});
  auto provider = std::make_unique<TokenAuthProvider>();
  auto* provider_ptr = provider.get();
  server.set_auth_provider(std::move(provider));
  const auto added = server.tools().add(
      mcp::protocol::ToolDefinition{
          .name = "whoami",
          .description = "Return authenticated subject",
          .input_schema = Json::object(),
      },
      [](const mcp::server::ToolContext& context)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        require(context.auth_identity.has_value(),
                "HTTP tool context should contain auth identity");
        require(context.auth_identity->claims.at("scope") == "tools:call",
                "HTTP tool auth claim mismatch");
        return mcp::protocol::ToolResult::text(context.auth_identity->subject);
      });
  require(added.has_value(), "failed to register authenticated HTTP tool");

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext& context) {
        return server.handle_request(request, context);
      });

  httplib::Client http_client("127.0.0.1", kPort);
  httplib::Result initialize_response;
  for (int attempt = 0; attempt < 100; ++attempt) {
    initialize_response = http_client.Post(
        kPath,
        httplib::Headers{
            {"Accept", "application/json, text/event-stream"},
            {"Content-Type", "application/json"},
            {"Mcp-Method", mcp::protocol::InitializeMethod},
            {"Authorization", "Bearer valid-token"},
        },
        serialize_test_request(mcp::protocol::JsonRpcRequest{
            .method = mcp::protocol::InitializeMethod,
            .params =
                Json{
                    {"protocolVersion", mcp::protocol::McpProtocolVersion},
                    {"capabilities", Json::object()},
                    {"clientInfo",
                     Json{{"name", "auth-test"}, {"version", "1"}}},
                },
            .id = std::int64_t{51},
        }),
        "application/json");
    if (initialize_response) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  require(static_cast<bool>(initialize_response),
          "authorized initialize should return a response");
  require(initialize_response->status == 200,
          "authorized initialize should return 200");
  require(initialize_response->has_header("Mcp-Session-Id"),
          "authorized initialize should create a session");
  const auto session_id =
      initialize_response->get_header_value("Mcp-Session-Id");
  require_initialized_notification(kPort, kPath, session_id);

  const auto tool_response = http_client.Post(
      kPath,
      httplib::Headers{
          {"Accept", "application/json, text/event-stream"},
          {"Content-Type", "application/json"},
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
          {"Mcp-Session-Id", session_id},
          {"Mcp-Method", mcp::protocol::ToolsCallMethod},
          {"Mcp-Name", "whoami"},
          {"Authorization", "Bearer valid-token"},
      },
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = mcp::protocol::ToolsCallMethod,
          .params = mcp::protocol::tool_call_to_json(mcp::protocol::ToolCall{
              .name = "whoami",
              .arguments = Json::object(),
          }),
          .id = std::int64_t{52},
      }),
      "application/json");

  require(static_cast<bool>(tool_response),
          "authorized tool call should return a response");
  require(tool_response->status == 200,
          "authorized tool call should return 200");
  const auto parsed = mcp::protocol::parse_response(tool_response->body);
  require(parsed.has_value(), "authorized tool response should parse");
  require(parsed->result.has_value(),
          "authorized tool response should succeed");
  require(parsed->result->at("content").at(0).at("text") == "http-subject",
          "authorized tool subject mismatch");
  require(provider_ptr->observed_http_methods.size() >= 2,
          "auth provider should receive HTTP method for each request");
  require(provider_ptr->observed_http_urls.size() >= 2,
          "auth provider should receive HTTP URL for each request");
  for (const auto& method : provider_ptr->observed_http_methods) {
    require(method == "POST", "auth provider HTTP method mismatch");
  }
  for (const auto& url : provider_ptr->observed_http_urls) {
    require(url == "http://127.0.0.1:40222/mcp",
            "auth provider HTTP URL mismatch");
  }

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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
                 {"Accept", "application/json, text/event-stream"},
                 {"Origin", "https://evil.example"},
                 {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
                 {"Mcp-Session-Id", "http-session"},
             });
  require(rejected != nullptr,
          "delete with mismatched origin should return a response");
  require(rejected->status == 400,
          "delete with mismatched origin should be rejected");

  server_transport.transport().stop();
}

void test_server_http_transport_starts_on_any_address_without_origin_allowlist() {
  constexpr int kPort = 40242;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "0.0.0.0",
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
          "server transport should listen on any address");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should be reachable through localhost");

  server_transport.transport().stop();
}

void test_server_http_transport_rejects_disallowed_host() {
  constexpr int kPort = 40235;
  const std::string kPath = "/mcp";
  std::atomic<int> initialize_count{0};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          initialize_count.fetch_add(1);
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
  const auto successful_initializes = initialize_count.load();

  httplib::Client http_client("127.0.0.1", kPort);
  const auto rejected =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Host", "evil.example"},
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
                           .params = Json::object(),
                           .id = std::int64_t{61},
                       }),
                       "application/json");
  require(rejected != nullptr,
          "initialize with disallowed host should return a response");
  require(rejected->status == 403,
          "initialize with disallowed host should be forbidden");
  require(initialize_count.load() == successful_initializes,
          "disallowed host should not reach request handler");

  server_transport.transport().stop();
}

void test_server_http_transport_accepts_standard_post_without_custom_headers() {
  constexpr int kPort = 40237;
  const std::string kPath = "/mcp";

  std::atomic<int> handled{0};
  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
          }),
      [&](const mcp::protocol::JsonRpcRequest& request,
          const mcp::server::SessionContext&) {
        require(request.method == mcp::protocol::InitializeMethod,
                "server should receive initialize without custom headers");
        handled.fetch_add(1);
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result =
                Json{
                    {"protocolVersion", mcp::protocol::McpProtocolVersion},
                    {"capabilities", Json::object()},
                    {"serverInfo",
                     Json{{"name", "server-http-test"}, {"version", "1"}}},
                },
        };
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");
  handled.store(0);

  httplib::Client http_client("127.0.0.1", kPort);
  const auto initialize = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{73},
  });
  const auto response =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                       },
                       initialize, "application/json");

  require(response != nullptr,
          "standard Streamable HTTP POST should return a response");
  require(response->status == 200,
          "standard Streamable HTTP POST should be accepted");
  require(response->has_header("Mcp-Session-Id"),
          "standard initialize should issue a session id");
  require(handled.load() == 1,
          "standard Streamable HTTP POST should reach handler");

  const auto parsed = mcp::protocol::parse_response(response->body);
  require(parsed.has_value(),
          "standard Streamable HTTP POST response should parse");
  require(parsed->result.has_value(),
          "standard Streamable HTTP POST should return a result");

  server_transport.transport().stop();
}

void test_server_http_transport_rejects_invalid_required_headers() {
  constexpr int kPort = 40236;
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
                      {"capabilities", Json::object()},
                      {"serverInfo",
                       Json{{"name", "server-http-test"}, {"version", "1"}}},
                  },
          };
        }
        return mcp::protocol::make_response(request.id, Json::object());
      });

  require(!server_transport.start_error().has_value(),
          "server transport should start");
  require(wait_for_http_initialize(kPort, kPath),
          "server transport should become reachable");

  httplib::Client http_client("127.0.0.1", kPort);
  const auto initialize = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{71},
  });

  const auto bad_accept =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       initialize, "application/json");
  require(bad_accept != nullptr, "bad POST Accept should return");
  require(bad_accept->status == 406, "bad POST Accept should be rejected");
  const auto bad_accept_body = mcp::protocol::parse_response(bad_accept->body);
  require(bad_accept_body.has_value(),
          "bad POST Accept response should be JSON-RPC");
  require(bad_accept_body->error.has_value(),
          "bad POST Accept response should contain an error");

  const auto bad_content_type =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "text/plain"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       initialize, "text/plain");
  require(bad_content_type != nullptr, "bad POST Content-Type should return");
  require(bad_content_type->status == 415,
          "bad POST Content-Type should be rejected");
  const auto bad_content_type_body =
      mcp::protocol::parse_response(bad_content_type->body);
  require(bad_content_type_body.has_value(),
          "bad POST Content-Type response should be JSON-RPC");
  require(bad_content_type_body->error.has_value(),
          "bad POST Content-Type response should contain an error");

  const auto session_id = initialize_http_session(kPort, kPath, 72);
  httplib::Client sse_client("127.0.0.1", kPort);
  sse_client.set_read_timeout(std::chrono::seconds(1));
  const auto bad_get_accept = sse_client.Get(
      kPath, httplib::Headers{
                 {"Accept", "application/json"},
                 {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
                 {"Mcp-Session-Id", session_id},
             });
  require(bad_get_accept != nullptr, "bad GET Accept should return");
  require(bad_get_accept->status == 406, "bad GET Accept should be rejected");
  const auto bad_get_accept_body =
      mcp::protocol::parse_response(bad_get_accept->body);
  require(bad_get_accept_body.has_value(),
          "bad GET Accept response should be JSON-RPC");
  require(bad_get_accept_body->error.has_value(),
          "bad GET Accept response should contain an error");

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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
  initialize_params["protocolVersion"] = mcp::protocol::McpProtocolVersion;
  initialize_params["capabilities"] = Json{
      {"roots", Json{{"listChanged", true}}},
      {"sampling", Json::object()},
      {"elicitation", Json{{"form", Json::object()}, {"url", Json::object()}}},
  };
  initialize_params["clientInfo"] =
      Json{{"name", "http-transport-test"}, {"version", "1"}};

  const auto initialized = client_transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
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
      {"Accept", "application/json, text/event-stream"},
      {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
      {"Mcp-Session-Id", session_id},
  };
  const auto deleted = admin_client.Delete(kPath, delete_headers);
  require(deleted != nullptr, "session delete request should succeed");
  require(deleted->status == 204, "session delete status mismatch");
  require(!server_transport.transport().client_capabilities().has_value(),
          "server should clear client capabilities after session delete");

  const auto reinitialized =
      client_transport.send(mcp::protocol::JsonRpcRequest{
          .method = mcp::protocol::InitializeMethod,
          .params = initialize_params,
          .id = std::int64_t{2},
      });
  require(reinitialized.has_value(),
          "client transport should reinitialize after delete");
  require(reinitialized->result.has_value(),
          "reinitialize response should contain a result");
  require(reinitialized->result->at("protocolVersion") ==
              mcp::protocol::McpProtocolVersion,
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
  auto handle = peer.request_async(mcp::protocol::RootsListMethod,
                                   Json::object(), options);

  const auto response = handle.await_response();
  require(!response.has_value(), "server request handle should time out");
  require(response.error().message == "request timed out",
          "server request handle timeout message mismatch");

  const auto expected_id = request_id_json(handle.request_id());
  const auto body = read_sse_until(
      kPort, kPath, session_id, [&](const std::string& candidate) {
        return candidate.find(mcp::protocol::RootsListMethod) !=
                   std::string::npos &&
               candidate.find(mcp::protocol::CancelledNotificationMethod) !=
                   std::string::npos &&
               candidate.find("\"requestId\":" + expected_id) !=
                   std::string::npos;
      });
  require(body.find(mcp::protocol::RootsListMethod) != std::string::npos,
          "SSE stream should deliver the timed-out server request");
  require(body.find(mcp::protocol::CancelledNotificationMethod) !=
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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

  auto handle =
      peer.request_async(mcp::protocol::RootsListMethod, Json::object());
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  const auto cancelled = handle.cancel("request cancelled");
  require(cancelled.has_value(), "server request handle cancel should send");

  const auto expected_id = request_id_json(handle.request_id());
  const auto body = read_sse_until(
      kPort, kPath, session_id, [&](const std::string& candidate) {
        return candidate.find(mcp::protocol::RootsListMethod) !=
                   std::string::npos &&
               candidate.find(mcp::protocol::CancelledNotificationMethod) !=
                   std::string::npos &&
               candidate.find("\"requestId\":" + expected_id) !=
                   std::string::npos;
      });
  require(body.find(mcp::protocol::RootsListMethod) != std::string::npos,
          "SSE stream should deliver the cancelled server request");
  require(body.find(mcp::protocol::CancelledNotificationMethod) !=
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
                        {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(!response.has_value(), "initialize should honor HTTP timeout");
  require(response.error().message == "http transport request timed out",
          "HTTP timeout public message should be stable");
  require(response.error().category == "transport",
          "HTTP timeout error category mismatch");
  require(!response.error().detail.empty(),
          "HTTP timeout library detail should be structured as detail");
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
      .method = mcp::protocol::PingMethod,
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
      .method = mcp::protocol::PingMethod,
      .params = Json::object(),
      .id = std::int64_t{9},
  });

  require(response.has_value(), "uri transport request should succeed");
  require(request_seen.load(), "uri transport should reach server");
  require(observed_path == "/api/mcp", "uri transport should use uri path");
  require(observed_authorization == "Bearer token-123",
          "uri transport should inject authorization header");
}

void test_client_http_transport_explicit_authorization_header_wins_case_insensitive() {
  HttpServerFixture fixture;
  std::atomic<bool> request_seen{false};
  std::string observed_authorization;
  int authorization_header_count = 0;

  auto header_name_equals = [](std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      char left = lhs[index];
      char right = rhs[index];
      if (left >= 'A' && left <= 'Z') {
        left = static_cast<char>(left - 'A' + 'a');
      }
      if (right >= 'A' && right <= 'Z') {
        right = static_cast<char>(right - 'A' + 'a');
      }
      if (left != right) {
        return false;
      }
    }
    return true;
  };

  fixture.server().Post("/explicit-auth", [&](const httplib::Request& request,
                                              httplib::Response& response) {
    for (const auto& header : request.headers) {
      if (header_name_equals(header.first, "Authorization")) {
        ++authorization_header_count;
        observed_authorization = header.second;
      }
    }

    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "explicit auth request should parse");
    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "explicit auth should send a request");
    response.set_content(serialize_test_response(mcp::protocol::JsonRpcResponse{
                             .id = rpc_request->id,
                             .result = Json::object(),
                         }),
                         "application/json");
    request_seen.store(true);
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .uri = "http://127.0.0.1:" + std::to_string(fixture.port()) +
             "/explicit-auth",
      .headers = {{"authorization", "DPoP explicit-token"}},
      .auth_header = "token-123",
      .timeout = std::chrono::milliseconds(2000),
  });

  const auto response = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::PingMethod,
      .params = Json::object(),
      .id = std::int64_t{90},
  });

  require(response.has_value(), "explicit auth request should succeed");
  require(request_seen.load(), "explicit auth request should reach server");
  require(authorization_header_count == 1,
          "explicit Authorization header should not be duplicated");
  require(observed_authorization == "DPoP explicit-token",
          "explicit Authorization value should win over bearer helper");
}

void test_client_http_transport_refreshes_bearer_once_on_401() {
  HttpServerFixture fixture;
  std::atomic<int> request_count{0};
  std::vector<std::string> observed_authorizations;
  std::mutex observed_mutex;

  fixture.server().Post("/auth-refresh", [&](const httplib::Request& request,
                                             httplib::Response& response) {
    {
      std::lock_guard lock(observed_mutex);
      observed_authorizations.push_back(
          request.has_header("Authorization")
              ? request.get_header_value("Authorization")
              : std::string{});
    }
    request_count.fetch_add(1);
    if (!request.has_header("Authorization") ||
        request.get_header_value("Authorization") != "Bearer fresh-token") {
      response.status = 401;
      response.set_header(
          "WWW-Authenticate",
          "Bearer error=\"invalid_token\", resource_metadata=\"https://"
          "resource.example/.well-known/oauth-protected-resource\"");
      return;
    }

    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "refresh retry request should parse");
    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "refresh retry should send a request");
    response.set_content(serialize_test_response(mcp::protocol::JsonRpcResponse{
                             .id = rpc_request->id,
                             .result = Json{{"ok", true}},
                         }),
                         "application/json");
  });

  int refresh_calls = 0;
  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .uri = "http://127.0.0.1:" + std::to_string(fixture.port()) +
             "/auth-refresh",
      .auth_header = "stale-token",
      .auth_refresh_handler =
          [&](const mcp::client::HttpAuthChallenge& challenge)
          -> std::optional<std::string> {
        ++refresh_calls;
        require(challenge.status_code == 401,
                "refresh challenge status mismatch");
        require(challenge.method == mcp::protocol::PingMethod,
                "refresh challenge method mismatch");
        require(challenge.www_authenticate.has_value(),
                "refresh challenge should include WWW-Authenticate");
        return "fresh-token";
      },
      .timeout = std::chrono::milliseconds(2000),
  });

  const auto response = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::PingMethod,
      .params = Json::object(),
      .id = std::int64_t{91},
  });

  require(response.has_value(), "request should succeed after auth refresh");
  require(refresh_calls == 1, "auth refresh should be called once");
  require(request_count.load() == 2, "auth refresh should retry once");
  {
    std::lock_guard lock(observed_mutex);
    require(observed_authorizations.size() == 2,
            "auth refresh should produce two requests");
    require(observed_authorizations[0] == "Bearer stale-token",
            "initial bearer token mismatch");
    require(observed_authorizations[1] == "Bearer fresh-token",
            "refreshed bearer token mismatch");
  }
}

void test_client_http_transport_reports_403_as_auth_error() {
  HttpServerFixture fixture;
  fixture.server().Post("/auth-forbidden", [&](const httplib::Request&,
                                               httplib::Response& response) {
    response.status = 403;
    response.set_header("WWW-Authenticate",
                        "Bearer error=\"insufficient_scope\", scope=\"admin\"");
    response.set_content("forbidden", "text/plain");
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .uri = "http://127.0.0.1:" + std::to_string(fixture.port()) +
             "/auth-forbidden",
      .auth_header = "token",
      .timeout = std::chrono::milliseconds(2000),
  });

  const auto response = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::PingMethod,
      .params = Json::object(),
      .id = std::int64_t{92},
  });
  require(!response.has_value(), "403 should fail as auth error");
  require(response.error().category == "auth",
          "403 error category should be auth");
  require(response.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "403 error code should be permission denied");
  require(response.error().detail ==
              "Bearer error=\"insufficient_scope\", scope=\"admin\"",
          "403 error detail should carry WWW-Authenticate");
}

void test_client_http_transport_bearer_helper_applies_to_session_requests() {
  HttpServerFixture fixture;
  std::atomic<bool> post_seen{false};
  std::atomic<bool> get_seen{false};
  std::atomic<bool> delete_seen{false};
  std::mutex observed_mutex;
  std::string post_authorization;
  std::string get_authorization;
  std::string delete_authorization;

  auto capture_authorization = [&](const httplib::Request& request,
                                   std::string* target) {
    std::lock_guard lock(observed_mutex);
    if (request.has_header("Authorization")) {
      *target = request.get_header_value("Authorization");
    }
  };

  fixture.server().Post("/mcp", [&](const httplib::Request& request,
                                    httplib::Response& response) {
    capture_authorization(request, &post_authorization);
    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "bearer helper initialize should parse");
    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr,
            "bearer helper should send initialize request");
    response.set_header("Mcp-Session-Id", "auth-session");
    response.set_content(
        serialize_test_response(mcp::protocol::JsonRpcResponse{
            .id = rpc_request->id,
            .result =
                Json{
                    {"protocolVersion", mcp::protocol::McpProtocolVersion},
                    {"capabilities", Json::object()},
                    {"serverInfo",
                     Json{{"name", "auth-helper"}, {"version", "1"}}},
                },
        }),
        "application/json");
    post_seen.store(true);
  });

  fixture.server().Get("/mcp", [&](const httplib::Request& request,
                                   httplib::Response& response) {
    capture_authorization(request, &get_authorization);
    response.set_content(":\n\n", "text/event-stream");
    get_seen.store(true);
  });

  fixture.server().Delete("/mcp", [&](const httplib::Request& request,
                                      httplib::Response& response) {
    capture_authorization(request, &delete_authorization);
    response.status = 202;
    delete_seen.store(true);
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .uri = "http://127.0.0.1:" + std::to_string(fixture.port()) + "/mcp",
      .auth_header = "token-123",
      .timeout = std::chrono::milliseconds(2000),
  });
  const auto started =
      transport.start([](const mcp::protocol::JsonRpcRequest&)
                          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return mcp::core::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected request",
        });
      });
  require(started.has_value(), "bearer helper transport should start");

  const auto response = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{10},
  });
  require(response.has_value(), "bearer helper initialize should succeed");
  require(wait_for([&]() { return post_seen.load() && get_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "bearer helper should reach POST and SSE GET");

  transport.stop();
  require(wait_for([&]() { return delete_seen.load(); },
                   std::chrono::milliseconds(1000)),
          "bearer helper should terminate HTTP session");

  std::lock_guard lock(observed_mutex);
  require(post_authorization == "Bearer token-123",
          "bearer helper should inject Authorization on POST");
  require(get_authorization == "Bearer token-123",
          "bearer helper should inject Authorization on SSE GET");
  require(delete_authorization == "Bearer token-123",
          "bearer helper should inject Authorization on DELETE");
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
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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

void test_http_transport_load_smoke_concurrent_sessions() {
  constexpr int kSessionCount = 4;
  std::atomic<int> initialize_count{0};
  std::atomic<int> sse_count{0};
  std::mutex sessions_mutex;
  std::vector<std::string> sessions;

  HttpServerFixture fixture;
  fixture.server().Post("/load-sessions", [&](const httplib::Request& request,
                                              httplib::Response& response) {
    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "load initialize request should parse");
    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "load client should send a request");
    require(rpc_request->method == mcp::protocol::InitializeMethod,
            "load client should initialize");

    const auto index = initialize_count.fetch_add(1) + 1;
    const auto session_id = "load-session-" + std::to_string(index);
    {
      std::lock_guard lock(sessions_mutex);
      sessions.push_back(session_id);
    }
    response.set_header("Mcp-Session-Id", session_id);
    response.set_content(
        serialize_test_response(mcp::protocol::JsonRpcResponse{
            .id = rpc_request->id,
            .result =
                Json{
                    {"protocolVersion", mcp::protocol::McpProtocolVersion},
                    {"capabilities", Json::object()},
                    {"serverInfo",
                     Json{{"name", "load-session-test"}, {"version", "1"}}},
                },
        }),
        "application/json");
  });
  fixture.server().Get("/load-sessions", [&](const httplib::Request& request,
                                             httplib::Response& response) {
    require(request.has_header("Mcp-Session-Id"),
            "load SSE GET should include a session id");
    sse_count.fetch_add(1);
    response.set_chunked_content_provider(
        "text/event-stream", [](size_t, httplib::DataSink& sink) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          if (!sink.is_writable()) {
            sink.done();
            return false;
          }
          return true;
        });
  });

  std::vector<std::unique_ptr<mcp::client::HttpTransport>> transports;
  transports.reserve(kSessionCount);
  for (int i = 0; i < kSessionCount; ++i) {
    transports.push_back(std::make_unique<mcp::client::HttpTransport>(
        mcp::client::HttpTransportOptions{
            .host = "127.0.0.1",
            .port = fixture.port(),
            .path = "/load-sessions",
            .timeout = std::chrono::milliseconds(2000),
        }));
    const auto started = transports.back()->start(
        [](const mcp::protocol::JsonRpcRequest&)
            -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
          return mcp::core::unexpected(mcp::core::Error{
              static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
              "unexpected request",
          });
        });
    require(started.has_value(), "load session transport should start");
  }

  std::vector<std::thread> threads;
  std::vector<std::exception_ptr> failures(kSessionCount);
  for (int i = 0; i < kSessionCount; ++i) {
    threads.emplace_back([&, i]() {
      try {
        const auto initialized =
            transports[i]->send(mcp::protocol::JsonRpcRequest{
                .method = mcp::protocol::InitializeMethod,
                .params = Json::object(),
                .id = std::int64_t{i + 1},
            });
        require(initialized.has_value(),
                "concurrent session initialize should succeed");
      } catch (...) {
        failures[i] = std::current_exception();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  for (const auto& failure : failures) {
    if (failure) {
      std::rethrow_exception(failure);
    }
  }

  require(initialize_count.load() == kSessionCount,
          "all concurrent sessions should initialize");
  require(wait_for([&]() { return sse_count.load() == kSessionCount; },
                   std::chrono::milliseconds(1000)),
          "all concurrent sessions should open SSE streams");

  {
    std::lock_guard lock(sessions_mutex);
    std::sort(sessions.begin(), sessions.end());
    const auto unique_end = std::unique(sessions.begin(), sessions.end());
    require(unique_end == sessions.end(),
            "concurrent HTTP sessions should be distinct");
  }

  for (auto& transport : transports) {
    transport->stop();
  }
}

void test_http_transport_load_smoke_many_in_flight_requests() {
  constexpr int kRequestCount = 6;
  constexpr auto kGateTimeout = std::chrono::milliseconds(5000);
  std::mutex gate_mutex;
  std::condition_variable gate_cv;
  int waiting_requests = 0;
  bool release_responses = false;
  std::exception_ptr handler_failure;
  std::atomic<int> completed_requests{0};

  HttpServerFixture fixture;
  fixture.server().Post("/load-inflight", [&](const httplib::Request& request,
                                              httplib::Response& response) {
    try {
      const auto parsed = mcp::protocol::parse_message(request.body);
      require(parsed.has_value(), "in-flight request should parse");
      const auto* rpc_request =
          std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
      require(rpc_request != nullptr, "in-flight client should send a request");
      require(rpc_request->method == mcp::protocol::PingMethod,
              "in-flight client should send ping");
      const auto response_id = rpc_request->id;

      {
        std::unique_lock lock(gate_mutex);
        ++waiting_requests;
        gate_cv.notify_all();
        gate_cv.wait_for(lock, kGateTimeout,
                         [&]() { return release_responses; });
      }

      response.set_content(
          serialize_test_response(mcp::protocol::JsonRpcResponse{
              .id = response_id,
              .result = Json{{"ok", true}},
          }),
          "application/json");
      completed_requests.fetch_add(1);
    } catch (...) {
      {
        std::lock_guard lock(gate_mutex);
        if (!handler_failure) {
          handler_failure = std::current_exception();
        }
      }
      gate_cv.notify_all();
      response.status = 500;
      response.set_content("handler failure", "text/plain");
    }
  });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = fixture.port(),
      .path = "/load-inflight",
      .timeout = std::chrono::milliseconds(2000),
  });
  const auto started =
      transport.start([](const mcp::protocol::JsonRpcRequest&)
                          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return mcp::core::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected request",
        });
      });
  require(started.has_value(), "in-flight transport should start");

  std::vector<std::thread> threads;
  std::vector<std::exception_ptr> failures(kRequestCount);
  for (int i = 0; i < kRequestCount; ++i) {
    threads.emplace_back([&, i]() {
      try {
        const auto response = transport.send(mcp::protocol::JsonRpcRequest{
            .method = mcp::protocol::PingMethod,
            .params = Json::object(),
            .id = std::int64_t{i + 1},
        });
        require(response.has_value(),
                "many in-flight HTTP requests should succeed");
        require(
            response->result.has_value() && response->result->at("ok") == true,
            "many in-flight HTTP response payload mismatch");
      } catch (...) {
        failures[i] = std::current_exception();
      }
    });
  }

  bool observed_all_requests = false;
  {
    std::unique_lock lock(gate_mutex);
    observed_all_requests = gate_cv.wait_for(lock, kGateTimeout, [&]() {
      return waiting_requests == kRequestCount || handler_failure != nullptr;
    });
    release_responses = true;
  }
  gate_cv.notify_all();

  for (auto& thread : threads) {
    thread.join();
  }
  if (handler_failure) {
    std::rethrow_exception(handler_failure);
  }
  require(observed_all_requests && waiting_requests == kRequestCount,
          "server should observe all in-flight HTTP requests");
  for (const auto& failure : failures) {
    if (failure) {
      std::rethrow_exception(failure);
    }
  }

  require(completed_requests.load() == kRequestCount,
          "server should complete all in-flight HTTP requests");
  transport.stop();
}

void test_http_transport_load_smoke_high_volume_notifications() {
  constexpr int kNotificationCount = 32;
  constexpr std::string_view kSessionId = "load-notifications-session";
  std::atomic<int> notification_count{0};
  std::atomic<int> unexpected_count{0};
  std::atomic<int> sse_get_count{0};

  HttpServerFixture fixture;
  fixture.server().Post(
      "/load-notifications",
      [&](const httplib::Request& request, httplib::Response& response) {
        const auto parsed = mcp::protocol::parse_message(request.body);
        require(parsed.has_value(),
                "notification load initialize should parse");
        const auto* rpc_request =
            std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
        require(rpc_request != nullptr,
                "notification load should send a request");
        require(rpc_request->method == mcp::protocol::InitializeMethod,
                "notification load should initialize");

        response.set_header("Mcp-Session-Id", std::string(kSessionId));
        response.set_content(
            serialize_test_response(mcp::protocol::JsonRpcResponse{
                .id = rpc_request->id,
                .result =
                    Json{
                        {"protocolVersion", mcp::protocol::McpProtocolVersion},
                        {"capabilities", Json::object()},
                        {"serverInfo", Json{{"name", "notification-load-test"},
                                            {"version", "1"}}},
                    },
            }),
            "application/json");
      });
  fixture.server().Get(
      "/load-notifications",
      [&](const httplib::Request& request, httplib::Response& response) {
        require(request.has_header("Mcp-Session-Id"),
                "notification load SSE should carry session");
        require(request.get_header_value("Mcp-Session-Id") == kSessionId,
                "notification load SSE session mismatch");
        sse_get_count.fetch_add(1);
        response.set_chunked_content_provider(
            "text/event-stream", [sent = 0, kNotificationCount](
                                     size_t, httplib::DataSink& sink) mutable {
              if (sent >= kNotificationCount) {
                sink.done();
                return false;
              }
              const auto event = sse_event(serialize_test_notification(
                  mcp::protocol::JsonRpcNotification{
                      .method = mcp::protocol::ProgressNotificationMethod,
                      .params = Json{{"progressToken", "load-progress"},
                                     {"progress", sent},
                                     {"total", kNotificationCount}},
                  }));
              ++sent;
              return sink.write(event.data(), event.size());
            });
      });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = fixture.port(),
      .path = "/load-notifications",
      .timeout = std::chrono::milliseconds(2000),
  });
  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return mcp::core::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected request",
        });
      },
      [&](const mcp::protocol::JsonRpcNotification& notification) {
        if (notification.method != mcp::protocol::ProgressNotificationMethod) {
          unexpected_count.fetch_add(1);
          return mcp::core::Result<mcp::core::Unit>{mcp::core::Unit{}};
        }
        notification_count.fetch_add(1);
        return mcp::core::Result<mcp::core::Unit>{mcp::core::Unit{}};
      });
  require(started.has_value(),
          "high-volume notification transport should start");

  const auto initialized = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(initialized.has_value(),
          "high-volume notification initialize should succeed");
  require(wait_for(
              [&]() { return notification_count.load() == kNotificationCount; },
              std::chrono::milliseconds(1000)),
          "client should dispatch all high-volume SSE notifications");
  require(unexpected_count.load() == 0,
          "high-volume SSE stream should only deliver expected notifications");
  require(sse_get_count.load() == 1,
          "high-volume notification load should use one SSE stream");

  transport.stop();
}

void test_http_transport_stop_returns_with_active_sse_stream() {
  constexpr std::string_view kSessionId = "stop-load-session";
  std::atomic<bool> stream_started{false};
  std::atomic<bool> allow_stream_close{false};
  std::atomic<bool> stop_done{false};
  std::atomic<int> notifications_seen{0};

  HttpServerFixture fixture;
  fixture.server().Post("/load-stop", [&](const httplib::Request& request,
                                          httplib::Response& response) {
    const auto parsed = mcp::protocol::parse_message(request.body);
    require(parsed.has_value(), "shutdown load initialize should parse");
    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    require(rpc_request != nullptr, "shutdown load should send a request");
    require(rpc_request->method == mcp::protocol::InitializeMethod,
            "shutdown load should initialize");

    response.set_header("Mcp-Session-Id", std::string(kSessionId));
    response.set_content(
        serialize_test_response(mcp::protocol::JsonRpcResponse{
            .id = rpc_request->id,
            .result =
                Json{
                    {"protocolVersion", mcp::protocol::McpProtocolVersion},
                    {"capabilities", Json::object()},
                    {"serverInfo",
                     Json{{"name", "shutdown-load-test"}, {"version", "1"}}},
                },
        }),
        "application/json");
  });
  fixture.server().Get(
      "/load-stop", [&](const httplib::Request&, httplib::Response& response) {
        response.set_chunked_content_provider(
            "text/event-stream",
            [&, sent = 0](size_t, httplib::DataSink& sink) mutable {
              stream_started.store(true);
              if (allow_stream_close.load() || !sink.is_writable()) {
                sink.done();
                return false;
              }

              const auto event = sse_event(serialize_test_notification(
                  mcp::protocol::JsonRpcNotification{
                      .method = mcp::protocol::ProgressNotificationMethod,
                      .params = Json{{"progressToken", "shutdown-load"},
                                     {"progress", sent++}},
                  }));
              std::this_thread::sleep_for(std::chrono::milliseconds(5));
              return sink.write(event.data(), event.size());
            });
      });

  mcp::client::HttpTransport transport(mcp::client::HttpTransportOptions{
      .host = "127.0.0.1",
      .port = fixture.port(),
      .path = "/load-stop",
      .timeout = std::chrono::milliseconds(2000),
  });
  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return mcp::core::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected request",
        });
      },
      [&](const mcp::protocol::JsonRpcNotification& notification) {
        if (notification.method == mcp::protocol::ProgressNotificationMethod) {
          notifications_seen.fetch_add(1);
        }
        return mcp::core::Result<mcp::core::Unit>{mcp::core::Unit{}};
      });
  require(started.has_value(), "shutdown load transport should start");

  const auto initialized = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(initialized.has_value(), "shutdown load initialize should succeed");
  require(wait_for([&]() { return stream_started.load(); },
                   std::chrono::milliseconds(1000)),
          "shutdown load should open an active SSE stream");
  require(wait_for([&]() { return notifications_seen.load() > 0; },
                   std::chrono::milliseconds(1000)),
          "shutdown load should receive at least one notification");

  std::thread stopper([&]() {
    transport.stop();
    stop_done.store(true);
  });
  const bool completed = wait_for([&]() { return stop_done.load(); },
                                  std::chrono::milliseconds(1000));
  allow_stream_close.store(true);
  if (stopper.joinable()) {
    stopper.join();
  }
  require(completed, "transport stop should return with active SSE load");
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
              request.id,
              Json{
                  {"protocolVersion", mcp::protocol::McpProtocolVersion},
                  {"capabilities", Json::object()},
                  {"serverInfo",
                   Json{{"name", "native-http-test"}, {"version", "1"}}},
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
      .method = mcp::protocol::InitializeMethod,
      .params =
          Json{
              {"protocolVersion", mcp::protocol::McpProtocolVersion},
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

  sent = transport.send(mcp::protocol::make_initialized_notification());
  require(sent.has_value(), "native http initialized notification send failed");

  sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::ToolsListMethod,
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
                        {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
      .method = mcp::protocol::InitializeMethod,
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
      .method = mcp::protocol::PingMethod,
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
      .method = mcp::protocol::PingMethod,
      .params = Json::object(),
      .id = std::int64_t{812},
  });
  require(sent.has_value(), "native http duplicate first send should succeed");

  const auto duplicate = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::PingMethod,
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
              request.id,
              Json{
                  {"protocolVersion", mcp::protocol::McpProtocolVersion},
                  {"capabilities", Json::object()},
                  {"serverInfo",
                   Json{{"name", "native-http-test"}, {"version", "1"}}},
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
      .method = mcp::protocol::InitializeMethod,
      .params =
          Json{
              {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
            .method = mcp::protocol::SamplingCreateMessageMethod,
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
  require(server_request->method == mcp::protocol::SamplingCreateMessageMethod,
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
        .method = mcp::protocol::InitializeMethod,
        .params =
            Json{
                {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
      auto response =
          client.Post(kPath,
                      httplib::Headers{
                          {"Accept", "application/json, text/event-stream"},
                          {"Authorization", "Bearer native-token"},
                          {"Content-Type", "application/json"},
                          {"Mcp-Method", mcp::protocol::InitializeMethod},
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
  const auto received_context = transport.last_received_context();
  require(received_context.has_value(),
          "native server should expose received HTTP context");
  require(received_context->headers.count("Authorization") == 1,
          "native server HTTP context should include authorization header");
  require(
      received_context->headers.at("Authorization") == "Bearer native-token",
      "native server HTTP context authorization mismatch");
  require(received_context->http_method.has_value() &&
              *received_context->http_method == "POST",
          "native server HTTP context method mismatch");

  const auto sent = transport.send(mcp::protocol::make_response(
      initialize_request->id,
      Json{
          {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
        .method = mcp::protocol::InitializeMethod,
        .params =
            Json{
                {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
      auto response =
          client.Post(kPath,
                      httplib::Headers{
                          {"Accept", "application/json, text/event-stream"},
                          {"Content-Type", "application/json"},
                          {"Mcp-Method", mcp::protocol::InitializeMethod},
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
          {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
      .method = mcp::protocol::RootsListMethod,
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

void test_native_streamable_http_server_transport_allows_duplicate_client_request_ids_across_sessions() {
  constexpr int kPort = 40238;
  const std::string kPath = "/native-server-session-request-ids";
  mcp::transport::StreamableHttpServerTransport transport(
      mcp::transport::StreamableHttpServerTransportOptions{
          .listen_host = "127.0.0.1",
          .listen_port = kPort,
          .path = kPath,
      });

  auto initialize_session = [&](std::string client_name,
                                std::int64_t id) -> std::string {
    const auto body = serialize_test_request(mcp::protocol::JsonRpcRequest{
        .method = mcp::protocol::InitializeMethod,
        .params =
            Json{
                {"protocolVersion", mcp::protocol::McpProtocolVersion},
                {"capabilities", Json::object()},
                {"clientInfo",
                 Json{{"name", std::move(client_name)}, {"version", "1"}}},
            },
        .id = id,
    });
    const auto headers = httplib::Headers{
        {"Accept", "application/json, text/event-stream"},
        {"Content-Type", "application/json"},
        {"Mcp-Method", mcp::protocol::InitializeMethod},
    };

    std::optional<httplib::Result> posted;
    std::thread post_thread([&]() {
      httplib::Client client("127.0.0.1", kPort);
      client.set_read_timeout(2, 0);
      posted = client.Post(kPath, headers, body, "application/json");
    });

    auto received = transport.receive();
    require(received.has_value(), "session-id init receive should succeed");
    require(received->has_value(), "session-id init should receive request");
    const auto* request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&received->value());
    require(request != nullptr, "session-id init should receive request");
    require(request->id == mcp::protocol::RequestId{id},
            "session-id init request id mismatch");
    const auto sent = transport.send(mcp::protocol::make_response(
        request->id,
        Json{
            {"protocolVersion", mcp::protocol::McpProtocolVersion},
            {"capabilities", Json::object()},
            {"serverInfo", Json{{"name", "native-server"}, {"version", "1"}}},
        }));
    require(sent.has_value(), "session-id init response should send");

    if (post_thread.joinable()) {
      post_thread.join();
    }
    require(posted.has_value(), "session-id init post should complete");
    require((*posted)->status == 200, "session-id init status mismatch");
    require((*posted)->has_header("Mcp-Session-Id"),
            "session-id init should return session id");
    return (*posted)->get_header_value("Mcp-Session-Id");
  };

  auto consume_initialized = [&](const std::string& session_id) {
    require_initialized_notification(kPort, kPath, session_id);
    auto received = transport.receive();
    require(received.has_value(),
            "session-id initialized receive should succeed");
    require(received->has_value(),
            "session-id initialized should receive notification");
    const auto* notification =
        std::get_if<mcp::protocol::JsonRpcNotification>(&received->value());
    require(notification != nullptr,
            "session-id initialized should be a notification");
    require(notification->method == mcp::protocol::InitializedMethod,
            "session-id initialized method mismatch");
  };

  const auto first_session = initialize_session("first", 91);
  const auto second_session = initialize_session("second", 92);
  consume_initialized(first_session);
  consume_initialized(second_session);

  const auto request_body =
      serialize_test_request(mcp::protocol::JsonRpcRequest{
          .method = mcp::protocol::PingMethod,
          .params = Json::object(),
          .id = std::int64_t{93},
      });
  auto request_headers = [](const std::string& session_id) {
    return httplib::Headers{
        {"Accept", "application/json, text/event-stream"},
        {"Content-Type", "application/json"},
        {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
        {"Mcp-Session-Id", session_id},
        {"Mcp-Method", mcp::protocol::PingMethod},
    };
  };

  std::optional<httplib::Result> first_post;
  std::optional<httplib::Result> second_post;
  std::thread first_thread([&]() {
    httplib::Client client("127.0.0.1", kPort);
    client.set_read_timeout(2, 0);
    first_post = client.Post(kPath, request_headers(first_session),
                             request_body, "application/json");
  });
  std::thread second_thread([&]() {
    httplib::Client client("127.0.0.1", kPort);
    client.set_read_timeout(2, 0);
    second_post = client.Post(kPath, request_headers(second_session),
                              request_body, "application/json");
  });

  for (int i = 0; i < 2; ++i) {
    auto received = transport.receive();
    require(received.has_value(),
            "session-id duplicate request receive should succeed");
    require(received->has_value(),
            "session-id duplicate request should be received");
    const auto* request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&received->value());
    require(request != nullptr, "session-id duplicate should be request");
    require(request->id == mcp::protocol::RequestId{std::int64_t{93}},
            "session-id duplicate request id mismatch");
    const auto sent = transport.send(
        mcp::protocol::make_response(request->id, Json::object()));
    require(sent.has_value(), "session-id duplicate response should send");
  }

  if (first_thread.joinable()) {
    first_thread.join();
  }
  if (second_thread.joinable()) {
    second_thread.join();
  }
  require(first_post.has_value(), "first session duplicate post should finish");
  require(second_post.has_value(),
          "second session duplicate post should finish");
  require((*first_post)->status == 200,
          "first session duplicate post status mismatch");
  require((*second_post)->status == 200,
          "second session duplicate post status mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "session-id duplicate close should succeed");
}

void test_native_streamable_http_server_transport_distinguishes_numeric_and_string_request_ids() {
  constexpr int kPort = 40239;
  const std::string kPath = "/native-server-request-id-types";
  mcp::transport::StreamableHttpServerTransport transport(
      mcp::transport::StreamableHttpServerTransportOptions{
          .listen_host = "127.0.0.1",
          .listen_port = kPort,
          .path = kPath,
      });

  const auto init_body = serialize_test_request(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params =
          Json{
              {"protocolVersion", mcp::protocol::McpProtocolVersion},
              {"capabilities", Json::object()},
              {"clientInfo", Json{{"name", "typed-id"}, {"version", "1"}}},
          },
      .id = std::int64_t{94},
  });
  const auto init_headers = httplib::Headers{
      {"Accept", "application/json, text/event-stream"},
      {"Content-Type", "application/json"},
      {"Mcp-Method", mcp::protocol::InitializeMethod},
  };
  std::optional<httplib::Result> init_post;
  std::thread init_thread([&]() {
    httplib::Client client("127.0.0.1", kPort);
    client.set_read_timeout(2, 0);
    init_post = client.Post(kPath, init_headers, init_body, "application/json");
  });

  auto init_received = transport.receive();
  require(init_received.has_value(), "typed-id init receive should succeed");
  require(init_received->has_value(), "typed-id init should receive request");
  const auto* init_request =
      std::get_if<mcp::protocol::JsonRpcRequest>(&init_received->value());
  require(init_request != nullptr, "typed-id init should receive request");
  const auto init_sent = transport.send(mcp::protocol::make_response(
      init_request->id,
      Json{
          {"protocolVersion", mcp::protocol::McpProtocolVersion},
          {"capabilities", Json::object()},
          {"serverInfo", Json{{"name", "native-server"}, {"version", "1"}}},
      }));
  require(init_sent.has_value(), "typed-id init response should send");
  if (init_thread.joinable()) {
    init_thread.join();
  }
  require(init_post.has_value(), "typed-id init post should complete");
  require((*init_post)->status == 200, "typed-id init status mismatch");
  require((*init_post)->has_header("Mcp-Session-Id"),
          "typed-id init should return session id");
  const auto session_id = (*init_post)->get_header_value("Mcp-Session-Id");

  require_initialized_notification(kPort, kPath, session_id);
  auto initialized = transport.receive();
  require(initialized.has_value(),
          "typed-id initialized receive should succeed");
  require(initialized->has_value(), "typed-id initialized should be received");

  auto request_headers = [&](const std::string& method) {
    return httplib::Headers{
        {"Accept", "application/json, text/event-stream"},
        {"Content-Type", "application/json"},
        {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
        {"Mcp-Session-Id", session_id},
        {"Mcp-Method", method},
    };
  };
  const auto numeric_body = serialize_test_request(
      mcp::protocol::JsonRpcRequest{.method = mcp::protocol::PingMethod,
                                    .params = Json::object(),
                                    .id = std::int64_t{95}});
  const auto string_body = serialize_test_request(
      mcp::protocol::JsonRpcRequest{.method = mcp::protocol::PingMethod,
                                    .params = Json::object(),
                                    .id = std::string{"95"}});

  std::optional<httplib::Result> numeric_post;
  std::optional<httplib::Result> string_post;
  std::thread numeric_thread([&]() {
    httplib::Client client("127.0.0.1", kPort);
    client.set_read_timeout(2, 0);
    numeric_post =
        client.Post(kPath, request_headers(mcp::protocol::PingMethod),
                    numeric_body, "application/json");
  });
  std::thread string_thread([&]() {
    httplib::Client client("127.0.0.1", kPort);
    client.set_read_timeout(2, 0);
    string_post = client.Post(kPath, request_headers(mcp::protocol::PingMethod),
                              string_body, "application/json");
  });

  for (int i = 0; i < 2; ++i) {
    auto received = transport.receive();
    require(received.has_value(), "typed-id request receive should succeed");
    require(received->has_value(), "typed-id request should be received");
    const auto* request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&received->value());
    require(request != nullptr, "typed-id should receive request");
    const auto sent = transport.send(
        mcp::protocol::make_response(request->id, Json::object()));
    require(sent.has_value(), "typed-id response should send");
  }

  if (numeric_thread.joinable()) {
    numeric_thread.join();
  }
  if (string_thread.joinable()) {
    string_thread.join();
  }
  require(numeric_post.has_value(), "numeric typed-id post should finish");
  require(string_post.has_value(), "string typed-id post should finish");
  require((*numeric_post)->status == 200, "numeric typed-id status mismatch");
  require((*string_post)->status == 200, "string typed-id status mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "typed-id close should succeed");
}

void test_server_sse_priming_event_with_polling() {
  constexpr int kPort = 40260;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .enable_sse_polling = true,
              .sse_disconnect_retry = std::chrono::milliseconds(2000),
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
  const auto initialize_response =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
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
          {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
      },
      [&](const char* data, size_t len) {
        body.append(data, len);
        return body.find("retry: 2000") == std::string::npos;
      });
  (void)stream;

  require(body.find("id: 0") != std::string::npos,
          "priming event should contain id: 0");
  require(body.find("retry: 2000") != std::string::npos,
          "priming event should contain retry: 2000");
  require(body.find("data: {}") != std::string::npos,
          "priming event should contain data: {}");
  server_transport.transport().stop();
}

void test_server_sse_disconnect_and_reconnect() {
  constexpr int kPort = 40261;
  const std::string kPath = "/mcp";

  std::atomic<bool> notification_seen{false};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .enable_sse_polling = true,
              .max_sse_replay_events = 16,
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
  const auto initialize_response =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
                           .params = Json::object(),
                           .id = std::int64_t{1},
                       }),
                       "application/json");
  require(static_cast<bool>(initialize_response), "initialize should succeed");
  const auto session_id =
      initialize_response->get_header_value("Mcp-Session-Id");

  // Send a notification before disconnect so we can verify replay after
  // reconnect.
  const auto sent = server_transport.transport().send_notification(
      mcp::protocol::JsonRpcNotification{
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
          .params = Json::object(),
      });
  require(sent.has_value(), "server outbound notification should succeed");

  // Open first SSE stream and read priming + notification.
  std::string first_body;
  const auto first_stream = http_client.Get(
      kPath, sse_headers(session_id), [&](const char* data, size_t len) {
        first_body.append(data, len);
        return first_body.find("id: 0") == std::string::npos ||
               first_body.find("tools/list_changed") == std::string::npos;
      });
  (void)first_stream;
  require(first_body.find("id: 0") != std::string::npos,
          "first stream should have priming event");

  // Disconnect the SSE stream.
  server_transport.transport().disconnect_session_sse(session_id);

  // Wait for stream to close.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Reconnect with Last-Event-ID. The server should replay retained events.
  std::string second_body;
  httplib::Headers reconnect_headers{
      {"Mcp-Session-Id", session_id},
      {"Accept", "text/event-stream"},
      {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
      {"Last-Event-ID", "0"},
  };
  const auto second_stream = http_client.Get(
      kPath, reconnect_headers, [&](const char* data, size_t len) {
        second_body.append(data, len);
        // Wait for the priming event on reconnect.
        return second_body.find("retry:") == std::string::npos;
      });
  (void)second_stream;

  require(second_body.find("retry:") != std::string::npos,
          "reconnect stream should have priming event");
  server_transport.transport().stop();
}

void test_server_sse_disconnect_sends_retry_hint() {
  constexpr int kPort = 40262;
  const std::string kPath = "/mcp";

  std::atomic<bool> priming_seen{false};
  std::atomic<bool> stream_done{false};

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              .enable_sse_polling = true,
              .sse_disconnect_retry = std::chrono::milliseconds(3000),
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
  const auto initialize_response =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
                           .params = Json::object(),
                           .id = std::int64_t{1},
                       }),
                       "application/json");
  require(static_cast<bool>(initialize_response), "initialize should succeed");
  const auto session_id =
      initialize_response->get_header_value("Mcp-Session-Id");

  // Open SSE stream and keep it open (return true to continue reading).
  std::string body;
  std::mutex body_mutex;
  std::thread sse_thread([&]() {
    http_client.Get(kPath, sse_headers(session_id),
                    [&](const char* data, size_t len) {
                      std::lock_guard lock(body_mutex);
                      body.append(data, len);
                      if (body.find("retry: 3000") != std::string::npos &&
                          !priming_seen.load()) {
                        priming_seen.store(true);
                      }
                      // Keep reading until the server disconnects us.
                      return !stream_done.load();
                    });
  });

  // Wait for the priming event to be received.
  require(wait_for([&]() { return priming_seen.load(); },
                   std::chrono::milliseconds(2000)),
          "should receive priming event");

  // Trigger disconnect. The server should send a retry hint before closing.
  server_transport.transport().disconnect_session_sse(session_id);

  // Wait for the stream to be closed by the server.
  require(wait_for(
              [&]() {
                // The stream will close when the server disconnects.
                // Check if the body has grown with the retry hint.
                std::lock_guard lock(body_mutex);
                return body.find("retry: 3000", body.find("retry: 3000") + 1) !=
                       std::string::npos;
              },
              std::chrono::milliseconds(2000)),
          "should receive disconnect retry hint");

  stream_done.store(true);
  if (sse_thread.joinable()) {
    sse_thread.join();
  }

  // The body should contain at least two "retry:" lines: one from priming,
  // one from the disconnect hint.
  std::string body_snapshot;
  {
    std::lock_guard lock(body_mutex);
    body_snapshot = body;
  }
  const auto first_retry = body_snapshot.find("retry: 3000");
  require(first_retry != std::string::npos, "should have priming retry hint");
  const auto second_retry = body_snapshot.find("retry: 3000", first_retry + 1);
  require(second_retry != std::string::npos,
          "should have disconnect retry hint");
  server_transport.transport().stop();
}

void test_server_sse_no_priming_without_polling() {
  constexpr int kPort = 40263;
  const std::string kPath = "/mcp";

  RunningServerTransportFixture server_transport(
      std::make_unique<mcp::server::HttpTransport>(
          mcp::server::HttpTransportOptions{
              .listen_host = "127.0.0.1",
              .listen_port = kPort,
              .path = kPath,
              // enable_sse_polling = false (default), no sse_retry
          }),
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return mcp::protocol::JsonRpcResponse{
              .id = request.id,
              .result =
                  Json{
                      {"protocolVersion", mcp::protocol::McpProtocolVersion},
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
  const auto initialize_response =
      http_client.Post(kPath,
                       httplib::Headers{
                           {"Accept", "application/json, text/event-stream"},
                           {"Content-Type", "application/json"},
                           {"Mcp-Method", mcp::protocol::InitializeMethod},
                       },
                       serialize_test_request(mcp::protocol::JsonRpcRequest{
                           .method = mcp::protocol::InitializeMethod,
                           .params = Json::object(),
                           .id = std::int64_t{1},
                       }),
                       "application/json");
  require(static_cast<bool>(initialize_response), "initialize should succeed");
  const auto session_id =
      initialize_response->get_header_value("Mcp-Session-Id");

  // Send a notification so the SSE stream has something to deliver.
  const auto sent = server_transport.transport().send_notification(
      mcp::protocol::JsonRpcNotification{
          .method = mcp::protocol::ToolsListChangedNotificationMethod,
          .params = Json::object(),
      });
  require(sent.has_value(), "server outbound notification should succeed");

  std::string body;
  const auto stream = http_client.Get(
      kPath, sse_headers(session_id), [&](const char* data, size_t len) {
        body.append(data, len);
        return body.find("tools/list_changed") == std::string::npos;
      });
  (void)stream;

  // Without enable_sse_polling or sse_retry, no priming event should be sent.
  require(body.find("retry:") == std::string::npos,
          "should not emit retry priming without polling enabled");
  require(body.find("id: 0") == std::string::npos,
          "should not emit priming id without polling enabled");
  require(body.find("tools/list_changed") != std::string::npos,
          "should still deliver the notification");
  server_transport.transport().stop();
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
      {"server http transport bounds outbound queue bytes",
       test_server_http_transport_bounds_outbound_queue_bytes},
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
      {"server http transport rejects oversized post body",
       test_server_http_transport_rejects_oversized_post_body},
      {"server http transport slow client body does not reach handler",
       test_server_http_transport_slow_client_body_does_not_reach_handler},
      {"server http transport closed client during response does not hang",
       test_server_http_transport_closed_client_during_response_does_not_hang},
      {"server http transport limits active sessions",
       test_server_http_transport_limits_active_sessions},
      {"server http transport requires initialized before business request",
       test_server_http_transport_requires_initialized_before_business_request},
      {"server http transport stateless accepts request without session",
       test_server_http_transport_stateless_accepts_request_without_session},
      {"server http transport stateless rejects task state methods",
       test_server_http_transport_stateless_rejects_task_state_methods},
      {"server http transport stateless rejects task tool call",
       test_server_http_transport_stateless_rejects_task_tool_call},
      {"server http transport stateless initialize does not create session",
       test_server_http_transport_stateless_initialize_does_not_create_session},
      {"server http transport emits sse retry priming",
       test_server_http_transport_emits_sse_retry_priming},
      {"server http transport accepts client notification with 202",
       test_server_http_transport_accepts_client_notification_with_202},
      {"server http transport copies headers to session context",
       test_server_http_transport_copies_headers_to_session_context},
      {"server http transport unauthorized uses bearer challenge",
       test_server_http_transport_unauthorized_uses_bearer_challenge},
      {"server http transport authorized request sets auth identity",
       test_server_http_transport_authorized_request_sets_auth_identity},
      {"server http transport rejects mismatched origin when allowlisted",
       test_server_http_transport_rejects_mismatched_origin_when_allowlisted},
      {"server http transport starts on any address without origin allowlist",
       test_server_http_transport_starts_on_any_address_without_origin_allowlist},
      {"server http transport rejects disallowed host",
       test_server_http_transport_rejects_disallowed_host},
      {"server http transport accepts standard post without custom headers",
       test_server_http_transport_accepts_standard_post_without_custom_headers},
      {"server http transport rejects invalid required headers",
       test_server_http_transport_rejects_invalid_required_headers},
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
      {"client http transport explicit authorization header wins case "
       "insensitive",
       test_client_http_transport_explicit_authorization_header_wins_case_insensitive},
      {"client http transport refreshes bearer once on 401",
       test_client_http_transport_refreshes_bearer_once_on_401},
      {"client http transport reports 403 as auth error",
       test_client_http_transport_reports_403_as_auth_error},
      {"client http transport bearer helper applies to session requests",
       test_client_http_transport_bearer_helper_applies_to_session_requests},
      {"client http transport stateless adds meta without session",
       test_client_http_transport_stateless_adds_meta_without_session},
      {"client connect_streamable_http accepts uri string",
       test_client_connect_streamable_http_accepts_uri_string},
      {"http transport load smoke concurrent sessions",
       test_http_transport_load_smoke_concurrent_sessions},
      {"http transport load smoke many in-flight requests",
       test_http_transport_load_smoke_many_in_flight_requests},
      {"http transport load smoke high-volume notifications",
       test_http_transport_load_smoke_high_volume_notifications},
      {"http transport stop returns with active SSE stream",
       test_http_transport_stop_returns_with_active_sse_stream},
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
      {"server sse priming event with polling",
       test_server_sse_priming_event_with_polling},
      {"server sse disconnect and reconnect",
       test_server_sse_disconnect_and_reconnect},
      {"server sse disconnect sends retry hint",
       test_server_sse_disconnect_sends_retry_hint},
      {"server sse no priming without polling",
       test_server_sse_no_priming_without_polling},
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
