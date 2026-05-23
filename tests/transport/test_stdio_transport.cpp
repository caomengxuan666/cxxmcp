#include "mcp/client/stdio_transport.hpp"
#include "mcp/protocol/serialization.hpp"
#include "mcp/server/stdio_transport.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using mcp::protocol::Json;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::string serialize_request_line(const mcp::protocol::JsonRpcRequest& request) {
    const auto serialized = mcp::protocol::serialize_request(request);
    require(serialized.has_value(), "request should serialize");
    return *serialized + '\n';
}

void test_client_writes_request_and_reads_response() {
    const auto response = mcp::protocol::make_response(std::int64_t{7}, Json{{"ok", true}});
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
    require(!written.empty() && written.back() == '\n', "client request should be newline-delimited");
    const auto parsed_request = mcp::protocol::parse_request(written.substr(0, written.size() - 1));
    require(parsed_request.has_value(), "client request should parse");
    require(parsed_request->method == "ping", "client request method mismatch");
    require(std::get<std::int64_t>(parsed_request->id) == 7, "client request id mismatch");
}

void test_server_reads_request_and_writes_response() {
    const auto input_text = serialize_request_line(mcp::protocol::JsonRpcRequest{
        .method = "echo",
        .params = Json{{"value", 42}},
        .id = std::string("req-1"),
    });
    std::istringstream input(input_text);
    std::ostringstream output;
    mcp::server::StdioTransport transport(input, output);

    const auto started = transport.start([](const mcp::protocol::JsonRpcRequest& request,
                                            const mcp::server::SessionContext& context) {
        require(request.method == "echo", "server request method mismatch");
        require(request.params.at("value") == 42, "server request params mismatch");
        require(context.session_id == "stdio", "server session id mismatch");
        require(context.remote_address == "stdio", "server remote address mismatch");
        return mcp::protocol::make_response(request.id, Json{{"echoed", request.params}});
    });

    require(started.has_value(), "server start should complete at EOF");
    const auto parsed_response = mcp::protocol::parse_response(output.str());
    require(parsed_response.has_value(), "server response should parse");
    require(parsed_response->result.has_value(), "server response should contain result");
    require(parsed_response->result->at("echoed").at("value") == 42, "server response payload mismatch");
}

void test_server_writes_parse_error_for_bad_json() {
    std::istringstream input("{not json\n");
    std::ostringstream output;
    mcp::server::StdioTransport transport(input, output);

    bool called = false;
    const auto started = transport.start([&called](const mcp::protocol::JsonRpcRequest&,
                                                   const mcp::server::SessionContext&) {
        called = true;
        return mcp::protocol::make_response(std::int64_t{1}, Json::object());
    });

    require(started.has_value(), "server should keep running through parse error until EOF");
    require(!called, "server handler should not run for bad JSON");
    const auto parsed_response = mcp::protocol::parse_response(output.str());
    require(parsed_response.has_value(), "server parse error response should parse");
    require(parsed_response->error.has_value(), "server parse error response should contain error");
    require(parsed_response->error->code == static_cast<int>(mcp::protocol::ErrorCode::ParseError),
            "server parse error code mismatch");
    require(!parsed_response->id.has_value(), "server parse error id should be null");
}

void test_server_ignores_notifications() {
    const auto notification = mcp::protocol::make_initialized_notification();
    const auto serialized = mcp::protocol::serialize_notification(notification);
    require(serialized.has_value(), "notification should serialize");

    std::istringstream input(*serialized + '\n');
    std::ostringstream output;
    mcp::server::StdioTransport transport(input, output);

    bool called = false;
    const auto started = transport.start([&called](const mcp::protocol::JsonRpcRequest&,
                                                   const mcp::server::SessionContext&) {
        called = true;
        return mcp::protocol::make_response(std::int64_t{1}, Json::object());
    });

    require(started.has_value(), "server should complete after notification EOF");
    require(!called, "server handler should not run for notifications");
    require(output.str().empty(), "server should not write a response for notifications");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
        {"client writes request and reads response", test_client_writes_request_and_reads_response},
        {"server reads request and writes response", test_server_reads_request_and_writes_response},
        {"server writes parse error for bad json", test_server_writes_parse_error_for_bad_json},
        {"server ignores notifications", test_server_ignores_notifications},
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
