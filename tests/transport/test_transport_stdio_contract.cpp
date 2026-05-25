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
  require(invalid.error().category == "protocol",
          "invalid stdio error category mismatch");

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
  require(send_after_close.error().category == "transport",
          "stdio send after close category mismatch");
}

void test_stdio_transport_interops_without_process_ownership() {
  std::istringstream client_input;
  std::ostringstream client_to_server;
  mcp::transport::ClientStdioTransport client_writer(client_input,
                                                     client_to_server);

  const auto client_request_id = mcp::protocol::RequestId{std::int64_t{42}};
  const auto client_sent = client_writer.send(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = mcp::protocol::Json::object(),
      .id = client_request_id,
  });
  require(client_sent.has_value(), "client-to-server stdio send failed");

  std::istringstream server_input(client_to_server.str());
  std::ostringstream server_to_client;
  mcp::transport::ServerStdioTransport server(server_input, server_to_client);
  const auto server_received = server.receive();
  require(server_received.has_value(), "server stdio receive failed");
  require(server_received->has_value(), "server stdio message missing");
  const auto* request =
      std::get_if<mcp::protocol::JsonRpcRequest>(&server_received->value());
  require(request != nullptr, "server stdio expected client request");
  require(request->method == "tools/list",
          "server stdio client request method mismatch");

  const auto server_response_sent = server.send(mcp::protocol::JsonRpcResponse{
      .id = request->id,
      .result = mcp::protocol::Json{{"tools", mcp::protocol::Json::array()}},
  });
  require(server_response_sent.has_value(),
          "server-to-client stdio response send failed");

  std::istringstream client_response_input(server_to_client.str());
  std::ostringstream unused_client_output;
  mcp::transport::ClientStdioTransport client_reader(client_response_input,
                                                     unused_client_output);
  const auto client_received = client_reader.receive();
  require(client_received.has_value(), "client stdio response receive failed");
  require(client_received->has_value(), "client stdio response missing");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&client_received->value());
  require(response != nullptr, "client stdio expected server response");
  require(response->id == client_request_id,
          "client stdio response id mismatch");
  require(response->result.has_value(), "client stdio response result missing");

  std::istringstream server_request_input;
  std::ostringstream server_request_output;
  mcp::transport::ServerStdioTransport server_writer(server_request_input,
                                                     server_request_output);

  const auto server_request_id = mcp::protocol::RequestId{std::string("srv-1")};
  const auto server_request_sent =
      server_writer.send(mcp::protocol::JsonRpcRequest{
          .method = "roots/list",
          .params = mcp::protocol::Json::object(),
          .id = server_request_id,
      });
  require(server_request_sent.has_value(),
          "server-to-client stdio request send failed");

  std::istringstream client_request_input(server_request_output.str());
  std::ostringstream client_response_output;
  mcp::transport::ClientStdioTransport client_responder(client_request_input,
                                                        client_response_output);
  const auto inbound_server_request = client_responder.receive();
  require(inbound_server_request.has_value(),
          "client stdio server request receive failed");
  require(inbound_server_request->has_value(),
          "client stdio server request missing");
  const auto* server_request = std::get_if<mcp::protocol::JsonRpcRequest>(
      &inbound_server_request->value());
  require(server_request != nullptr,
          "client stdio expected server-to-client request");
  require(server_request->method == "roots/list",
          "client stdio server request method mismatch");

  const auto client_response_sent =
      client_responder.send(mcp::protocol::JsonRpcResponse{
          .id = server_request->id,
          .result =
              mcp::protocol::Json{{"roots", mcp::protocol::Json::array()}},
      });
  require(client_response_sent.has_value(),
          "client-to-server stdio response send failed");

  std::istringstream server_response_input(client_response_output.str());
  std::ostringstream unused_server_output;
  mcp::transport::ServerStdioTransport server_reader(server_response_input,
                                                     unused_server_output);
  const auto server_response = server_reader.receive();
  require(server_response.has_value(), "server stdio response receive failed");
  require(server_response->has_value(), "server stdio response missing");
  const auto* final_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&server_response->value());
  require(final_response != nullptr, "server stdio expected client response");
  require(final_response->id == server_request_id,
          "server stdio response id mismatch");
  require(final_response->result.has_value(),
          "server stdio response result missing");
}

}  // namespace

int main() {
  try {
    test_stdio_transport_sends_messages();
    test_stdio_transport_receives_messages();
    test_stdio_transport_reports_parse_errors_and_close();
    test_stdio_transport_interops_without_process_ownership();
    std::cout << "stdio contract transport tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "stdio contract transport tests failed: " << ex.what() << '\n';
    return 1;
  }
}
