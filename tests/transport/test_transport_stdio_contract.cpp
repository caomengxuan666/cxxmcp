// Copyright (c) 2025 [caomengxuan666]

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <variant>

#include "cxxmcp/transport.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void test_stdio_transport_sends_messages() {
  std::istringstream input;
  std::ostringstream output;
  mcp::transport::ClientStdioTransport transport(input, output);
  const auto diagnostics = transport.diagnostics();
  require(diagnostics.at("name") == "stdio", "stdio diagnostics name mismatch");
  require(diagnostics.at("closed") == false,
          "stdio diagnostics closed mismatch");

  const auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "ping",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{1},
  });
  require(sent.has_value(), "stdio send failed");

  const auto parsed = mcp::protocol::parse_message(output.str());
  require(parsed.has_value(), "stdio output should parse");
  require(std::holds_alternative<mcp::protocol::JsonRpcRequest>(*parsed),
          "stdio output variant mismatch");
}

void test_stdio_transport_receives_messages() {
  std::istringstream input(
      "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\","
      "\"params\":{}}\n");
  std::ostringstream output;
  mcp::transport::ServerStdioTransport transport(input, output);

  const auto received = transport.receive();
  require(received.has_value(), "stdio receive failed");
  require(received->has_value(), "stdio receive message missing");
  require(std::holds_alternative<mcp::protocol::JsonRpcNotification>(
              received->value()),
          "stdio received variant mismatch");

  const auto eof = transport.receive();
  require(eof.has_value(), "stdio eof receive failed");
  require(!eof->has_value(), "stdio eof should return nullopt");
}

void test_stdio_transport_reports_parse_errors_and_close() {
  std::istringstream input("{not-json}\n");
  std::ostringstream output;
  mcp::transport::ClientStdioTransport transport(input, output);

  const auto invalid = transport.receive();
  require(!invalid.has_value(), "invalid stdio input should fail");
  require(invalid.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::ParseError),
          "invalid stdio error code mismatch");

  require(transport.close().has_value(), "stdio close failed");
  require(transport.diagnostics().at("closed") == true,
          "stdio diagnostics should report closed");
  const auto after_close = transport.receive();
  require(after_close.has_value(), "stdio receive after close should succeed");
  require(!after_close->has_value(), "stdio receive after close should end");

  const auto send_after_close =
      transport.send(mcp::protocol::JsonRpcNotification{
          .method = "notifications/initialized",
          .params = mcp::protocol::Json::object(),
      });
  require(!send_after_close.has_value(), "stdio send after close should fail");
}

}  // namespace

int main() {
  try {
    test_stdio_transport_sends_messages();
    test_stdio_transport_receives_messages();
    test_stdio_transport_reports_parse_errors_and_close();
    std::cout << "stdio contract transport tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "stdio contract transport tests failed: " << ex.what() << '\n';
    return 1;
  }
}
