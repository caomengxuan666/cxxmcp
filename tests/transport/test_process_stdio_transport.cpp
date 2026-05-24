#include "mcp/client/process_stdio_transport.hpp"
#include "mcp/client/session.hpp"

#include <iostream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::filesystem::path python_stdio_child_script() {
    return std::filesystem::path(MCP_TEST_SOURCE_DIR) / "tests" / "fixtures" / "process_stdio_child.py";
}

std::filesystem::path typescript_stdio_child_bootstrap_script() {
    return std::filesystem::path(MCP_TEST_SOURCE_DIR) / "tests" / "fixtures" / "process_stdio_child_node.mjs";
}

std::filesystem::path rust_stdio_child_manifest() {
    return std::filesystem::path(MCP_TEST_SOURCE_DIR) / "tests" / "fixtures" / "rust_process_stdio_child" /
           "Cargo.toml";
}

std::unordered_map<std::string, std::string> rust_cargo_env() {
    const auto target_dir = std::filesystem::temp_directory_path() / "mcp-rust-process-stdio-child-target";
    return {
        {"CARGO_TARGET_DIR", target_dir.string()},
        {"CARGO_HTTP_PROXY", ""},
        {"CARGO_HTTPS_PROXY", ""},
        {"HTTP_PROXY", ""},
        {"HTTPS_PROXY", ""},
        {"ALL_PROXY", ""},
    };
}

void test_process_stdio_transport_runs_child_mcp_server() {
    auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = MCP_TEST_CHILD_EXE,
            .args = {},
            .cwd = {},
            .env = {},
        });
    mcp::client::McpClientSession session(std::move(transport));

    const auto initialized = session.initialize();
    require(initialized.has_value(), "process initialize should succeed");

    const auto marked = session.mark_initialized();
    require(marked.has_value(), "process initialized notification should succeed");

    const auto tools = session.discover_tools();
    require(tools.has_value(), "process tools discovery should succeed");
    require(tools->size() == 1, "process tool count mismatch");
    require(tools->front().name == "echo", "process tool name mismatch");
}

void test_process_stdio_transport_calls_child_tool() {
    auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = MCP_TEST_CHILD_EXE,
            .args = {},
            .cwd = {},
            .env = {},
        });
    mcp::client::Client client(std::move(transport));

    const auto result = client.call_tool(mcp::protocol::ToolCall{
        .name = "echo",
        .arguments = mcp::protocol::Json{{"value", 42}},
    });
    require(result.has_value(), "process tool call should succeed");
    require(result->content.size() == 1, "process tool result content mismatch");
    require(result->content.front().text == "echo", "process tool result text mismatch");
    require(result->structured_content.has_value(), "process structured content missing");
    require(result->structured_content->at("value") == 42, "process structured content mismatch");
}

void test_process_stdio_transport_handles_interleaved_child_request() {
    auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = MCP_TEST_CHILD_EXE,
            .args = {},
            .cwd = {},
            .env = {},
        });
    mcp::client::Client client(std::move(transport));

    std::string sampled_text;
    client.on_create_message_request([&](const mcp::protocol::CreateMessageParams& params)
                                         -> mcp::core::Result<mcp::protocol::CreateMessageResult> {
        sampled_text = params.messages.front().content.text;
        return mcp::protocol::CreateMessageResult{
            .role = "assistant",
            .content = mcp::protocol::ContentBlock{
                .type = "text",
                .text = "sampled",
            },
            .model = "test-model",
            .stop_reason = "endTurn",
        };
    });

    const auto result = client.raw_request(mcp::protocol::JsonRpcRequest{
        .method = "custom/interleave",
        .params = mcp::protocol::Json::object(),
        .id = std::int64_t{77},
    });
    require(result.has_value(), "interleaved process request should succeed");
    require(result->at("ok") == true, "interleaved process request result mismatch");
    require(sampled_text == "hello from child", "interleaved child request payload mismatch");
}

void test_process_stdio_transport_runs_python_mcp_server() {
    auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = "uv",
            .args = {"run",
                     "--with",
                     "mcp",
                     "python",
                     python_stdio_child_script().string()},
            .cwd = {},
            .env = {},
        });
    mcp::client::McpClientSession session(std::move(transport));

    const auto initialized = session.initialize();
    require(initialized.has_value(), "python process initialize should succeed");

    const auto marked = session.mark_initialized();
    require(marked.has_value(), "python process initialized notification should succeed");

    const auto tools = session.discover_tools();
    require(tools.has_value(), "python process tools discovery should succeed");
    require(tools->size() == 1, "python process tool count mismatch");
    require(tools->front().name == "echo", "python process tool name mismatch");
}

void test_process_stdio_transport_runs_typescript_mcp_server() {
    auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = "node",
            .args = {typescript_stdio_child_bootstrap_script().string()},
            .cwd = {},
            .env = {},
        });
    mcp::client::McpClientSession session(std::move(transport));

    const auto initialized = session.initialize();
    require(initialized.has_value(), "typescript process initialize should succeed");

    const auto marked = session.mark_initialized();
    require(marked.has_value(), "typescript process initialized notification should succeed");

    const auto tools = session.discover_tools();
    require(tools.has_value(), "typescript process tools discovery should succeed");
    require(tools->size() == 1, "typescript process tool count mismatch");
    require(tools->front().name == "echo", "typescript process tool name mismatch");
}

void test_process_stdio_transport_calls_typescript_tool() {
    auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = "node",
            .args = {typescript_stdio_child_bootstrap_script().string()},
            .cwd = {},
            .env = {},
        });
    mcp::client::Client client(std::move(transport));

    const auto result = client.call_tool(mcp::protocol::ToolCall{
        .name = "echo",
        .arguments = mcp::protocol::Json{{"value", 42}},
    });
    require(result.has_value(), "typescript process tool call should succeed");
    require(result->content.size() == 1, "typescript process tool result content mismatch");
    require(result->content.front().text == "echo", "typescript process tool result text mismatch");
    require(result->structured_content.has_value(), "typescript structured content missing");
    require(result->structured_content->at("value") == 42, "typescript structured content mismatch");
}

void test_process_stdio_transport_runs_rust_mcp_server() {
    auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = "cargo",
            .args = {"run",
                     "--quiet",
                     "--locked",
                     "--manifest-path",
                     rust_stdio_child_manifest().string()},
            .cwd = {},
            .env = rust_cargo_env(),
        });
    mcp::client::McpClientSession session(std::move(transport));

    const auto initialized = session.initialize();
    require(initialized.has_value(), "rust process initialize should succeed");

    const auto marked = session.mark_initialized();
    require(marked.has_value(), "rust process initialized notification should succeed");

    const auto tools = session.discover_tools();
    require(tools.has_value(), "rust process tools discovery should succeed");
    require(tools->size() == 1, "rust process tool count mismatch");
    require(tools->front().name == "echo", "rust process tool name mismatch");
}

void test_process_stdio_transport_calls_rust_tool() {
    auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = "cargo",
            .args = {"run",
                     "--quiet",
                     "--locked",
                     "--manifest-path",
                     rust_stdio_child_manifest().string()},
            .cwd = {},
            .env = rust_cargo_env(),
        });
    mcp::client::McpClientSession session(std::move(transport));

    const auto initialized = session.initialize();
    require(initialized.has_value(), "rust process initialize should succeed");

    const auto marked = session.mark_initialized();
    require(marked.has_value(), "rust process initialized notification should succeed");

    const auto result = session.call_tool(mcp::protocol::ToolCall{
        .name = "echo",
        .arguments = mcp::protocol::Json{{"value", 42}},
    });
    require(result.has_value(), "rust process tool call should succeed");
    require(result->content.size() == 1, "rust process tool result content mismatch");
    require(result->content.front().text == "42", "rust process tool result text mismatch");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
        {"process stdio transport runs child MCP server", test_process_stdio_transport_runs_child_mcp_server},
        {"process stdio transport calls child tool", test_process_stdio_transport_calls_child_tool},
        {"process stdio transport handles interleaved child request",
         test_process_stdio_transport_handles_interleaved_child_request},
        {"process stdio transport runs python MCP server", test_process_stdio_transport_runs_python_mcp_server},
        {"process stdio transport runs typescript MCP server", test_process_stdio_transport_runs_typescript_mcp_server},
        {"process stdio transport calls typescript tool", test_process_stdio_transport_calls_typescript_tool},
        {"process stdio transport runs rust MCP server", test_process_stdio_transport_runs_rust_mcp_server},
        {"process stdio transport calls rust tool", test_process_stdio_transport_calls_rust_tool},
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
