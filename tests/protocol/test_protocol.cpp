#include "mcp/protocol/serialization.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

using mcp::protocol::ErrorCode;
using mcp::protocol::Json;
using mcp::protocol::JsonRpcMessage;
using mcp::protocol::JsonRpcNotification;
using mcp::protocol::JsonRpcRequest;
using mcp::protocol::JsonRpcResponse;
using mcp::protocol::RequestId;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_error_code(const mcp::core::Result<JsonRpcMessage>& result, ErrorCode expected, std::string_view context) {
    require(!result.has_value(), context);
    require(result.error().code == static_cast<int>(expected), context);
}

fs::path fixture_path(std::string_view name) {
    return fs::path(MCP_TEST_SOURCE_DIR) / "tests" / "fixtures" / "protocol" / std::string(name);
}

Json load_fixture_json(std::string_view name) {
    const auto path = fixture_path(name);
    std::ifstream input(path);
    require(input.is_open(), std::string("failed to open fixture: ") + path.string());

    Json json;
    input >> json;
    return json;
}

std::string load_fixture_text(std::string_view name) {
    const auto path = fixture_path(name);
    std::ifstream input(path);
    require(input.is_open(), std::string("failed to open fixture: ") + path.string());

    return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void expect_serialized_json_eq(const std::string& actual, const Json& expected, std::string_view context) {
    const auto parsed = Json::parse(actual);
    require(parsed == expected, context);
}

void test_initialize_request_round_trip() {
    const auto input = load_fixture_text("initialize.request.json");
    const auto parsed = mcp::protocol::parse_message(input);
    require(parsed.has_value(), "initialize request should parse");

    const auto* request = std::get_if<JsonRpcRequest>(&*parsed);
    require(request != nullptr, "initialize request should be a request");
    require(request->method == mcp::protocol::InitializeMethod, "initialize method mismatch");
    require(std::get<std::int64_t>(request->id) == 1, "initialize id mismatch");

    const auto serialized = mcp::protocol::serialize_message(*parsed);
    require(serialized.has_value(), "initialize request should serialize");
    expect_serialized_json_eq(*serialized, load_fixture_json("initialize.request.json"), "initialize round-trip mismatch");
}

void test_initialized_notification_round_trip() {
    const auto input = load_fixture_text("initialized.notification.json");
    const auto parsed = mcp::protocol::parse_message(input);
    require(parsed.has_value(), "initialized notification should parse");

    const auto* notification = std::get_if<JsonRpcNotification>(&*parsed);
    require(notification != nullptr, "initialized message should be a notification");
    require(notification->method == mcp::protocol::InitializedMethod, "initialized method mismatch");

    const auto serialized = mcp::protocol::serialize_message(*parsed);
    require(serialized.has_value(), "initialized notification should serialize");
    expect_serialized_json_eq(*serialized, load_fixture_json("initialized.notification.json"),
                              "initialized notification round-trip mismatch");
}

void test_ping_request_round_trip() {
    const auto input = load_fixture_text("ping.request.json");
    const auto parsed = mcp::protocol::parse_message(input);
    require(parsed.has_value(), "ping request should parse");

    const auto* request = std::get_if<JsonRpcRequest>(&*parsed);
    require(request != nullptr, "ping message should be a request");
    require(request->method == mcp::protocol::PingMethod, "ping method mismatch");
    require(std::get<std::string>(request->id) == "ping-1", "ping id mismatch");

    const auto serialized = mcp::protocol::serialize_message(*parsed);
    require(serialized.has_value(), "ping request should serialize");
    expect_serialized_json_eq(*serialized, load_fixture_json("ping.request.json"), "ping round-trip mismatch");
}

void test_response_round_trips() {
    const auto success = mcp::protocol::make_response(RequestId{std::string("req-1")}, Json{{"ok", true}});
    const auto success_json = mcp::protocol::serialize_message(JsonRpcMessage{success});
    require(success_json.has_value(), "success response should serialize");
    expect_serialized_json_eq(*success_json, load_fixture_json("response.success.json"),
                              "success response mismatch");

    const auto error = mcp::protocol::make_error_response(RequestId{42},
                                                          mcp::protocol::make_error(ErrorCode::InternalError, "boom"));
    const auto error_json = mcp::protocol::serialize_message(JsonRpcMessage{error});
    require(error_json.has_value(), "error response should serialize");
    expect_serialized_json_eq(*error_json, load_fixture_json("response.error.json"), "error response mismatch");

    const auto parsed_error = mcp::protocol::parse_message(load_fixture_text("response.error.json"));
    require(parsed_error.has_value(), "error response should parse");
    const auto* response = std::get_if<JsonRpcResponse>(&*parsed_error);
    require(response != nullptr, "error response should be a response");
    require(response->error.has_value(), "error response should contain an error");
    require(response->error->code == static_cast<int>(ErrorCode::InternalError), "error code mismatch");
    require(response->error->message == "boom", "error message mismatch");
}

void test_invalid_json_is_rejected() {
    const auto parsed = mcp::protocol::parse_message("{not json");
    require_error_code(parsed, ErrorCode::ParseError, "invalid JSON should be rejected");
}

void test_invalid_messages_are_rejected() {
    const auto missing_method = mcp::protocol::parse_message(load_fixture_text("invalid.missing_method.json"));
    require_error_code(missing_method, ErrorCode::InvalidRequest, "missing method should be rejected");

    const auto version = mcp::protocol::parse_message(load_fixture_text("invalid.version.json"));
    require_error_code(version, ErrorCode::InvalidRequest, "bad version should be rejected");

    const auto null_id = mcp::protocol::parse_message(load_fixture_text("invalid.null_request_id.json"));
    require_error_code(null_id, ErrorCode::InvalidRequest, "null request id should be rejected");

    const auto missing_payload = mcp::protocol::parse_message(load_fixture_text("invalid.response_missing_payload.json"));
    require_error_code(missing_payload, ErrorCode::InvalidRequest, "response without payload should be rejected");

    const auto dual_payload = mcp::protocol::parse_message(load_fixture_text("invalid.dual_result_error.json"));
    require_error_code(dual_payload, ErrorCode::InvalidRequest, "response with both payloads should be rejected");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
        {"initialize request round trip", test_initialize_request_round_trip},
        {"initialized notification round trip", test_initialized_notification_round_trip},
        {"ping request round trip", test_ping_request_round_trip},
        {"response round trips", test_response_round_trips},
        {"invalid json is rejected", test_invalid_json_is_rejected},
        {"invalid messages are rejected", test_invalid_messages_are_rejected},
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
