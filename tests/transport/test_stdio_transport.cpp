// Copyright (c) 2025 [caomengxuan666]

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/client.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server.hpp"

namespace {

using mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string serialize_request_line(
    const mcp::protocol::JsonRpcRequest& request) {
  const auto serialized = mcp::protocol::serialize_request(request);
  require(serialized.has_value(), "request should serialize");
  return *serialized + '\n';
}

std::string serialize_notification_line(
    const mcp::protocol::JsonRpcNotification& notification) {
  const auto serialized = mcp::protocol::serialize_notification(notification);
  require(serialized.has_value(), "notification should serialize");
  return *serialized + '\n';
}

mcp::protocol::JsonRpcResponse make_initialize_response(
    const mcp::protocol::RequestId& id) {
  return mcp::protocol::make_response(
      id,
      Json{
          {"protocolVersion", std::string(mcp::protocol::McpProtocolVersion)},
          {"capabilities", Json::object()},
          {"serverInfo", Json{{"name", "stdio-test"}, {"version", "1"}}},
      });
}

std::string initialized_stdio_prefix() {
  return serialize_request_line(mcp::protocol::JsonRpcRequest{
             .method = std::string(mcp::protocol::InitializeMethod),
             .params =
                 Json{
                     {"protocolVersion",
                      std::string(mcp::protocol::McpProtocolVersion)},
                     {"capabilities", Json::object()},
                     {"clientInfo",
                      Json{{"name", "stdio-test"}, {"version", "1"}}},
                 },
             .id = std::int64_t{0},
         }) +
         serialize_notification_line(
             mcp::protocol::make_initialized_notification());
}

std::vector<std::string> non_empty_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find('\n', start);
    const auto line = text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!line.empty()) {
      lines.push_back(line);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return lines;
}

void test_client_handles_incoming_request_while_waiting_for_response() {
  const auto incoming_request_text =
      serialize_request_line(mcp::protocol::JsonRpcRequest{
          .method = "custom/echo",
          .params = Json::object(),
          .id = std::string("srv-1"),
      });
  const auto outgoing_response_text = mcp::protocol::serialize_response(
      mcp::protocol::make_response(std::int64_t{7}, Json{{"ok", true}}));
  require(outgoing_response_text.has_value(),
          "outgoing response should serialize");

  std::istringstream input(incoming_request_text + *outgoing_response_text +
                           '\n');
  std::ostringstream output;
  mcp::client::StdioTransport transport(input, output);

  bool custom_request_handled = false;
  require(transport
              .start([&](const mcp::protocol::JsonRpcRequest& request)
                         -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
                custom_request_handled = true;
                require(request.method == "custom/echo",
                        "incoming client request method mismatch");
                return mcp::protocol::make_response(request.id,
                                                    Json{{"echoed", true}});
              })
              .has_value(),
          "client stdio transport start failed");

  const auto actual = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = Json::object(),
      .id = std::int64_t{7},
  });

  require(actual.has_value(), "client stdio send should return response");
  require(actual->result->at("ok") == true, "client stdio response mismatch");
  require(custom_request_handled, "client stdio request handler should run");

  const auto output_text = output.str();
  const auto first_newline = output_text.find('\n');
  require(first_newline != std::string::npos,
          "client stdio output should contain a request");
  const auto second_newline = output_text.find('\n', first_newline + 1);
  require(second_newline != std::string::npos,
          "client stdio output should contain a response");

  const auto first_line = output_text.substr(0, first_newline);
  const auto second_line =
      output_text.substr(first_newline + 1, second_newline - first_newline - 1);
  const auto written_request = mcp::protocol::parse_request(first_line);
  require(written_request.has_value(), "written client request should parse");
  require(written_request->method == "ping",
          "written client request method mismatch");
  const auto written_response = mcp::protocol::parse_response(second_line);
  require(written_response.has_value(), "written client response should parse");
  require(written_response->result->at("echoed") == true,
          "written client response mismatch");
}

void test_client_writes_request_and_reads_response() {
  const auto response =
      mcp::protocol::make_response(std::int64_t{7}, Json{{"ok", true}});
  const auto response_text = mcp::protocol::serialize_response(response);
  require(response_text.has_value(), "response should serialize");

  std::istringstream input(*response_text + '\n');
  std::ostringstream output;
  mcp::client::StdioTransport transport(input, output);

  const auto actual = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = Json::object(),
      .id = std::int64_t{7},
  });

  require(actual.has_value(), "client send should return response");
  require(actual->result.has_value(), "client response should contain result");
  require(actual->result->at("ok") == true, "client response payload mismatch");

  const auto written = output.str();
  require(!written.empty() && written.back() == '\n',
          "client request should be newline-delimited");
  const auto parsed_request =
      mcp::protocol::parse_request(written.substr(0, written.size() - 1));
  require(parsed_request.has_value(), "client request should parse");
  require(parsed_request->method == "ping", "client request method mismatch");
  require(std::get<std::int64_t>(parsed_request->id) == 7,
          "client request id mismatch");
}

void test_client_rejects_unexpected_stdio_response_id() {
  const auto response =
      mcp::protocol::make_response(std::int64_t{8}, Json{{"ok", true}});
  const auto response_text = mcp::protocol::serialize_response(response);
  require(response_text.has_value(), "response should serialize");

  std::istringstream input(*response_text + '\n');
  std::ostringstream output;
  mcp::client::StdioTransport transport(input, output);

  const auto actual = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = Json::object(),
      .id = std::int64_t{7},
  });

  require(!actual.has_value(),
          "client stdio should reject unexpected response id");
  require(actual.error().message ==
              "stdio transport received an unexpected response",
          "client stdio unexpected response message mismatch");
  require(actual.error().detail == "8",
          "client stdio unexpected response detail mismatch");
  require(actual.error().category == "transport",
          "client stdio unexpected response category mismatch");
}

void test_client_reports_stdio_eof_before_response() {
  std::istringstream input;
  std::ostringstream output;
  mcp::client::StdioTransport transport(input, output);

  const auto actual = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = Json::object(),
      .id = std::int64_t{7},
  });

  require(!actual.has_value(),
          "client stdio should fail when stream closes before response");
  require(actual.error().message == "failed to read stdio response",
          "client stdio EOF message mismatch");
  require(actual.error().category == "transport",
          "client stdio EOF category mismatch");
}

void test_client_reports_stdio_parse_error_before_response() {
  std::istringstream input("{not-json}\n");
  std::ostringstream output;
  mcp::client::StdioTransport transport(input, output);

  require(transport.start({}).has_value(), "client stdio start failed");

  const auto actual = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = Json::object(),
      .id = std::int64_t{7},
  });

  require(!actual.has_value(),
          "client stdio should fail on malformed input before response");
  require(actual.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::ParseError),
          "client stdio malformed input code mismatch");
  require(actual.error().category == "protocol",
          "client stdio malformed input category mismatch");
}

void test_client_writes_error_for_throwing_incoming_stdio_request_handler() {
  const auto incoming_request_text =
      serialize_request_line(mcp::protocol::JsonRpcRequest{
          .method = "custom/throw",
          .params = Json::object(),
          .id = std::string("srv-throw"),
      });
  const auto outgoing_response_text = mcp::protocol::serialize_response(
      mcp::protocol::make_response(std::int64_t{7}, Json{{"ok", true}}));
  require(outgoing_response_text.has_value(),
          "outgoing response should serialize");

  std::istringstream input(incoming_request_text + *outgoing_response_text +
                           '\n');
  std::ostringstream output;
  mcp::client::StdioTransport transport(input, output);

  require(transport
              .start([](const mcp::protocol::JsonRpcRequest&)
                         -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
                throw std::runtime_error("client stdio handler threw");
              })
              .has_value(),
          "client stdio transport start failed");

  const auto actual = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = Json::object(),
      .id = std::int64_t{7},
  });
  require(actual.has_value(),
          "client stdio should keep waiting after handler error response");

  const auto output_text = output.str();
  const auto first_newline = output_text.find('\n');
  require(first_newline != std::string::npos,
          "client stdio output should contain request line");
  const auto second_newline = output_text.find('\n', first_newline + 1);
  require(second_newline != std::string::npos,
          "client stdio output should contain handler error line");
  const auto error_line =
      output_text.substr(first_newline + 1, second_newline - first_newline - 1);
  const auto parsed_error = mcp::protocol::parse_response(error_line);
  require(parsed_error.has_value(),
          "client stdio handler error response should parse");
  require(parsed_error->error.has_value(),
          "client stdio handler error response should contain error");
  require(parsed_error->error->message == "handler failed",
          "client stdio handler error message mismatch");
  require(parsed_error->error->data.has_value() &&
              *parsed_error->error->data == "client stdio handler threw",
          "client stdio handler error detail mismatch");
}

void test_server_reads_request_and_writes_response() {
  const auto input_text = initialized_stdio_prefix() +
                          serialize_request_line(mcp::protocol::JsonRpcRequest{
                              .method = "echo",
                              .params = Json{{"value", 42}},
                              .id = std::string("req-1"),
                          });
  std::istringstream input(input_text);
  std::ostringstream output;
  mcp::server::StdioTransport transport(input, output);

  const auto started =
      transport.start([](const mcp::protocol::JsonRpcRequest& request,
                         const mcp::server::SessionContext& context) {
        if (request.method == mcp::protocol::InitializeMethod) {
          return make_initialize_response(request.id);
        }
        require(request.method == "echo", "server request method mismatch");
        require(request.params.at("value") == 42,
                "server request params mismatch");
        require(context.session_id == "stdio", "server session id mismatch");
        require(context.remote_address == "stdio",
                "server remote address mismatch");
        return mcp::protocol::make_response(request.id,
                                            Json{{"echoed", request.params}});
      });

  require(started.has_value(), "server start should complete at EOF");
  const auto lines = non_empty_lines(output.str());
  require(lines.size() == 2,
          "server should write initialize and echo response");
  const auto parsed_response = mcp::protocol::parse_response(lines.back());
  require(parsed_response.has_value(), "server response should parse");
  require(parsed_response->result.has_value(),
          "server response should contain result");
  require(parsed_response->result->at("echoed").at("value") == 42,
          "server response payload mismatch");
}

void test_server_rejects_request_before_initialized_notification() {
  const auto input_text = serialize_request_line(mcp::protocol::JsonRpcRequest{
      .method = "echo",
      .params = Json::object(),
      .id = std::string("req-before-init"),
  });
  std::istringstream input(input_text);
  std::ostringstream output;
  mcp::server::StdioTransport transport(input, output);

  bool called = false;
  const auto started =
      transport.start([&called](const mcp::protocol::JsonRpcRequest& request,
                                const mcp::server::SessionContext&) {
        called = true;
        return make_initialize_response(request.id);
      });

  require(started.has_value(),
          "server should keep running after pre-initialized request rejection");
  require(!called, "pre-initialized request should not reach handler");
  const auto parsed_response = mcp::protocol::parse_response(output.str());
  require(parsed_response.has_value(),
          "pre-initialized request rejection should parse");
  require(parsed_response->error.has_value(),
          "pre-initialized request should be an error response");
  require(parsed_response->error->message ==
              "stdio transport session is not initialized",
          "pre-initialized request error message mismatch");
  require(parsed_response->id.has_value() &&
              *parsed_response->id ==
                  mcp::protocol::RequestId{std::string("req-before-init")},
          "pre-initialized request id mismatch");
}

void test_server_writes_error_for_unexpected_stdio_response() {
  const auto response =
      mcp::protocol::make_response(std::int64_t{10}, Json{{"ok", true}});
  const auto response_text = mcp::protocol::serialize_response(response);
  require(response_text.has_value(), "response should serialize");

  std::istringstream input(*response_text + '\n');
  std::ostringstream output;
  mcp::server::StdioTransport transport(input, output);

  bool called = false;
  const auto started =
      transport.start([&called](const mcp::protocol::JsonRpcRequest&,
                                const mcp::server::SessionContext&) {
        called = true;
        return mcp::protocol::make_response(std::int64_t{1}, Json::object());
      });

  require(started.has_value(),
          "server should keep running through unexpected response until EOF");
  require(!called, "server handler should not run for response messages");
  const auto parsed_response = mcp::protocol::parse_response(output.str());
  require(parsed_response.has_value(),
          "server unexpected response error should parse");
  require(parsed_response->error.has_value(),
          "server unexpected response should contain error");
  require(parsed_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "server unexpected response code mismatch");
  require(parsed_response->error->message ==
              "stdio transport expected a JSON-RPC request",
          "server unexpected response message mismatch");
  require(
      parsed_response->id.has_value() &&
          *parsed_response->id == mcp::protocol::RequestId{std::int64_t{10}},
      "server unexpected response id mismatch");
}

void test_server_writes_error_for_throwing_stdio_request_handler() {
  const auto input_text = initialized_stdio_prefix() +
                          serialize_request_line(mcp::protocol::JsonRpcRequest{
                              .method = "custom/throw",
                              .params = Json::object(),
                              .id = std::string("req-throw"),
                          });
  std::istringstream input(input_text);
  std::ostringstream output;
  mcp::server::StdioTransport transport(input, output);

  const auto started =
      transport.start([](const mcp::protocol::JsonRpcRequest& request,
                         const mcp::server::SessionContext&)
                          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        if (request.method == mcp::protocol::InitializeMethod) {
          return make_initialize_response(request.id);
        }
        throw std::runtime_error("server stdio handler threw");
      });

  require(started.has_value(),
          "server should convert throwing handler to error response");
  const auto lines = non_empty_lines(output.str());
  require(lines.size() == 2,
          "server should write initialize and handler error response");
  const auto parsed_response = mcp::protocol::parse_response(lines.back());
  require(parsed_response.has_value(),
          "server handler error response should parse");
  require(parsed_response->error.has_value(),
          "server handler error response should contain error");
  require(parsed_response->error->message == "handler failed",
          "server handler error message mismatch");
  require(parsed_response->error->data.has_value() &&
              *parsed_response->error->data == "server stdio handler threw",
          "server handler error detail mismatch");
}

void test_server_writes_parse_error_for_bad_json() {
  std::istringstream input("{not json\n");
  std::ostringstream output;
  mcp::server::StdioTransport transport(input, output);

  bool called = false;
  const auto started =
      transport.start([&called](const mcp::protocol::JsonRpcRequest&,
                                const mcp::server::SessionContext&) {
        called = true;
        return mcp::protocol::make_response(std::int64_t{1}, Json::object());
      });

  require(started.has_value(),
          "server should keep running through parse error until EOF");
  require(!called, "server handler should not run for bad JSON");
  const auto parsed_response = mcp::protocol::parse_response(output.str());
  require(parsed_response.has_value(),
          "server parse error response should parse");
  require(parsed_response->error.has_value(),
          "server parse error response should contain error");
  require(parsed_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::ParseError),
          "server parse error code mismatch");
  require(!parsed_response->id.has_value(),
          "server parse error id should be null");
}

void test_server_handles_notifications() {
  const auto notification = mcp::protocol::make_notification(
      std::string(mcp::protocol::RootsListChangedNotificationMethod),
      Json::object());
  const auto serialized = mcp::protocol::serialize_notification(notification);
  require(serialized.has_value(), "notification should serialize");

  std::istringstream input(initialized_stdio_prefix() + *serialized + '\n');
  std::ostringstream output;
  mcp::server::StdioTransport transport(input, output);

  bool notification_called = false;
  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        require(request.method == mcp::protocol::InitializeMethod,
                "server notification test should only receive initialize");
        return make_initialize_response(request.id);
      },
      [&notification_called](const mcp::protocol::JsonRpcNotification& received,
                             const mcp::server::SessionContext& context) {
        if (received.method == mcp::protocol::InitializedMethod) {
          return mcp::core::Unit{};
        }
        notification_called = true;
        require(received.method ==
                    mcp::protocol::RootsListChangedNotificationMethod,
                "server notification method mismatch");
        require(context.session_id == "stdio",
                "server notification session mismatch");
        return mcp::core::Unit{};
      });

  require(started.has_value(), "server should complete after notification EOF");
  require(notification_called, "server notification handler should run");
  const auto lines = non_empty_lines(output.str());
  require(lines.size() == 1,
          "server should only write an initialize response for notifications");
}

void test_server_handles_cancelled_notification() {
  const auto notification = mcp::protocol::JsonRpcNotification{
      .method = std::string(mcp::protocol::CancelledNotificationMethod),
      .params = Json{{"requestId", 7}, {"reason", "request timeout"}},
  };
  const auto serialized = mcp::protocol::serialize_notification(notification);
  require(serialized.has_value(), "cancelled notification should serialize");

  std::istringstream input(initialized_stdio_prefix() + *serialized + '\n');
  std::ostringstream output;
  mcp::server::StdioTransport transport(input, output);

  bool notification_called = false;
  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&) {
        require(request.method == mcp::protocol::InitializeMethod,
                "server cancellation test should only receive initialize");
        return make_initialize_response(request.id);
      },
      [&notification_called](const mcp::protocol::JsonRpcNotification& received,
                             const mcp::server::SessionContext&) {
        if (received.method == mcp::protocol::InitializedMethod) {
          return mcp::core::Unit{};
        }
        notification_called = true;
        require(received.method == mcp::protocol::CancelledNotificationMethod,
                "server cancellation notification method mismatch");
        const auto cancelled =
            mcp::protocol::cancelled_notification_params_from_json(
                received.params);
        require(cancelled.has_value(),
                "server cancellation params should parse");
        require(std::get<std::int64_t>(cancelled->request_id) == 7,
                "server cancellation request id mismatch");
        require(cancelled->reason == "request timeout",
                "server cancellation reason mismatch");
        return mcp::core::Unit{};
      });

  require(started.has_value(),
          "server should complete after cancelled notification EOF");
  require(notification_called,
          "server cancellation notification handler should run");
  const auto lines = non_empty_lines(output.str());
  require(lines.size() == 1,
          "server should only write an initialize response for cancellation");
}

void test_client_writes_cancelled_notification_to_stdio() {
  std::istringstream input;
  std::ostringstream output;
  mcp::client::StdioTransport transport(input, output);

  const auto sent =
      transport.send_notification(mcp::protocol::JsonRpcNotification{
          .method = std::string(mcp::protocol::CancelledNotificationMethod),
          .params = Json{{"requestId", "req-1"}, {"reason", "user cancel"}},
      });
  require(sent.has_value(),
          "client stdio cancellation notification should send");

  const auto parsed = mcp::protocol::parse_notification(output.str());
  require(parsed.has_value(),
          "client stdio cancellation notification should parse");
  require(parsed->method == mcp::protocol::CancelledNotificationMethod,
          "client stdio cancellation notification method mismatch");
  const auto cancelled =
      mcp::protocol::cancelled_notification_params_from_json(parsed->params);
  require(cancelled.has_value(),
          "client stdio cancellation params should parse");
  require(std::get<std::string>(cancelled->request_id) == "req-1",
          "client stdio cancellation request id mismatch");
  require(cancelled->reason == "user cancel",
          "client stdio cancellation reason mismatch");
}

void test_server_writes_outbound_notification_to_stdio() {
  std::istringstream input;
  std::ostringstream output;
  mcp::server::StdioTransport transport(input, output);

  const auto sent =
      transport.send_notification(mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ToolsListChangedNotificationMethod),
          .params = mcp::protocol::Json::object(),
      });
  require(sent.has_value(),
          "stdio transport outbound notification should succeed");

  const auto parsed = mcp::protocol::parse_notification(output.str());
  require(parsed.has_value(), "stdio outbound notification should parse");
  require(parsed->method == mcp::protocol::ToolsListChangedNotificationMethod,
          "stdio outbound notification method mismatch");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, void (*)()>> tests = {
      {"client writes request and reads response",
       test_client_writes_request_and_reads_response},
      {"client handles incoming request while waiting for response",
       test_client_handles_incoming_request_while_waiting_for_response},
      {"client rejects unexpected stdio response id",
       test_client_rejects_unexpected_stdio_response_id},
      {"client reports stdio eof before response",
       test_client_reports_stdio_eof_before_response},
      {"client reports stdio parse error before response",
       test_client_reports_stdio_parse_error_before_response},
      {"client writes error for throwing incoming stdio request handler",
       test_client_writes_error_for_throwing_incoming_stdio_request_handler},
      {"server reads request and writes response",
       test_server_reads_request_and_writes_response},
      {"server rejects request before initialized notification",
       test_server_rejects_request_before_initialized_notification},
      {"server writes error for unexpected stdio response",
       test_server_writes_error_for_unexpected_stdio_response},
      {"server writes error for throwing stdio request handler",
       test_server_writes_error_for_throwing_stdio_request_handler},
      {"server writes parse error for bad json",
       test_server_writes_parse_error_for_bad_json},
      {"server handles notifications", test_server_handles_notifications},
      {"server handles cancelled notification",
       test_server_handles_cancelled_notification},
      {"client writes cancelled notification to stdio",
       test_client_writes_cancelled_notification_to_stdio},
      {"server writes outbound notification to stdio",
       test_server_writes_outbound_notification_to_stdio},
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
