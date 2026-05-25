// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include "cxxmcp/client/process_stdio_transport.hpp"
#include "cxxmcp/client/session.hpp"
#include "cxxmcp/transport/process_stdio_transport.hpp"

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::filesystem::path python_stdio_child_script() {
  return std::filesystem::path(MCP_TEST_SOURCE_DIR) / "tests" / "fixtures" /
         "process_stdio_child.py";
}

std::filesystem::path typescript_stdio_child_bootstrap_script() {
  return std::filesystem::path(MCP_TEST_SOURCE_DIR) / "tests" / "fixtures" /
         "process_stdio_child_node.mjs";
}

std::filesystem::path rust_stdio_child_manifest() {
  return std::filesystem::path(MCP_TEST_SOURCE_DIR) / "tests" / "fixtures" /
         "rust_process_stdio_child" / "Cargo.toml";
}

std::filesystem::path rust_stdio_child_executable() {
  auto target_dir = std::filesystem::temp_directory_path() /
                    "mcp-rust-process-stdio-child-target";
  auto binary = target_dir / "debug" / "rust_process_stdio_child";
#ifdef _WIN32
  binary += ".exe";
#endif
  return binary;
}

std::unordered_map<std::string, std::string> rust_cargo_env() {
  const auto target_dir = std::filesystem::temp_directory_path() /
                          "mcp-rust-process-stdio-child-target";
  return {
      {"CARGO_TARGET_DIR", target_dir.string()},
      {"CARGO_HTTP_PROXY", ""},
      {"CARGO_HTTPS_PROXY", ""},
      {"HTTP_PROXY", ""},
      {"HTTPS_PROXY", ""},
      {"ALL_PROXY", ""},
  };
}

void set_process_env(std::string_view key, const std::string& value) {
  const std::string key_string(key);
#ifdef _WIN32
  _putenv_s(key_string.c_str(), value.c_str());
#else
  setenv(key_string.c_str(), value.c_str(), 1);
#endif
}

void prepare_rust_stdio_child_build_env() {
  const auto env = rust_cargo_env();
  for (const auto& [key, value] : env) {
    set_process_env(key, value);
  }
}

void build_rust_stdio_child() {
  prepare_rust_stdio_child_build_env();
  const auto command = "cargo build --offline --manifest-path \"" +
                       rust_stdio_child_manifest().string() + "\"";
  require(std::system(command.c_str()) == 0,
          "rust process fixture build should succeed");
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
  require(marked.has_value(),
          "process initialized notification should succeed");

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
  require(result->content.front().text == "echo",
          "process tool result text mismatch");
  require(result->structured_content.has_value(),
          "process structured content missing");
  require(result->structured_content->at("value") == 42,
          "process structured content mismatch");
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
  client.on_create_message_request(
      [&](const mcp::protocol::CreateMessageParams& params)
          -> mcp::core::Result<mcp::protocol::CreateMessageResult> {
        sampled_text = params.messages.front().content.text;
        return mcp::protocol::CreateMessageResult{
            .role = "assistant",
            .content =
                mcp::protocol::ContentBlock{
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
  require(result->at("ok") == true,
          "interleaved process request result mismatch");
  require(sampled_text == "hello from child",
          "interleaved child request payload mismatch");
}

void test_process_stdio_transport_times_out_unresponsive_child() {
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--ignore-requests"},
          .cwd = {},
          .env = {},
          .request_timeout = std::chrono::milliseconds(100),
      });
  mcp::client::McpClientSession session(std::move(transport));

  const auto initialized = session.initialize();
  require(!initialized.has_value(),
          "unresponsive process initialize should fail");
  require(initialized.error().message == "process stdio request timed out",
          "unresponsive process initialize should report timeout");
}

void test_process_stdio_transport_stop_unblocks_pending_request() {
  mcp::client::ProcessStdioTransport transport(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--ignore-requests"},
          .cwd = {},
          .env = {},
      });

  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return std::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected child request",
        });
      },
      [](const mcp::protocol::JsonRpcNotification&)
          -> mcp::core::Result<mcp::core::Unit> { return mcp::core::Unit{}; });
  require(started.has_value(), "process transport start should succeed");

  std::atomic_bool request_finished{false};
  bool stable_error_seen = false;
  std::thread request_thread([&]() {
    const auto response = transport.send(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::InitializeMethod),
        .params = mcp::protocol::Json::object(),
        .id = std::int64_t{99},
    });
    request_finished.store(true);
    if (!response.has_value()) {
      stable_error_seen =
          response.error().message == "process stdio transport reader stopped";
      return;
    }
    stable_error_seen =
        response->error.has_value() &&
        response->error->message == "process stdio transport reader stopped";
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  require(!request_finished.load(),
          "request should still be pending before stop");
  transport.stop();

  if (request_thread.joinable()) {
    request_thread.join();
  }
  require(request_finished.load(), "stop should unblock pending request");
  require(stable_error_seen,
          "pending request should receive stable reader stopped error");
}

void test_process_stdio_transport_sends_cancelled_notification_while_pending() {
  mcp::client::ProcessStdioTransport transport(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--ignore-requests"},
          .cwd = {},
          .env = {},
      });

  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return std::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected child request",
        });
      },
      [](const mcp::protocol::JsonRpcNotification&)
          -> mcp::core::Result<mcp::core::Unit> { return mcp::core::Unit{}; });
  require(started.has_value(), "process transport start should succeed");

  std::atomic_bool request_finished{false};
  std::thread request_thread([&]() {
    (void)transport.send(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::InitializeMethod),
        .params = mcp::protocol::Json::object(),
        .id = std::int64_t{101},
    });
    request_finished.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  require(!request_finished.load(),
          "request should still be pending before cancellation notification");

  const auto notified =
      transport.send_notification(mcp::protocol::JsonRpcNotification{
          .method = std::string(mcp::protocol::CancelledNotificationMethod),
          .params =
              mcp::protocol::Json{
                  {"requestId", 101},
                  {"reason", "request timeout"},
              },
      });
  require(notified.has_value(),
          "process stdio cancellation notification should send while pending");

  transport.stop();
  if (request_thread.joinable()) {
    request_thread.join();
  }
  require(request_finished.load(),
          "stop should still unblock pending request after cancellation");
}

void test_process_stdio_transport_passes_args_cwd_and_env_without_shell() {
  const auto test_cwd =
      std::filesystem::temp_directory_path() / "cxxmcp-process-stdio-cwd";
  std::filesystem::create_directories(test_cwd);

  const std::string expected_arg = "literal value with spaces && no shell";
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--echo-options", expected_arg},
          .cwd = test_cwd.string(),
          .env = {{"CXXMCP_PROCESS_TEST_ENV", "env-present"}},
      });
  mcp::client::Client client(std::move(transport));

  const auto result = client.raw_request(mcp::protocol::JsonRpcRequest{
      .method = "custom/options",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{88},
  });
  require(result.has_value(), "process options request should succeed");
  require(result->at("arg") == expected_arg,
          "process argument should be passed without shell splitting");
  require(result->at("env") == "env-present",
          "process environment override mismatch");
  require(std::filesystem::equivalent(
              std::filesystem::path(result->at("cwd").get<std::string>()),
              test_cwd),
          "process working directory mismatch");
}

void test_process_stdio_transport_handles_child_exit_before_response() {
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--exit-immediately"},
          .cwd = {},
          .env = {},
          .request_timeout = std::chrono::milliseconds(500),
      });
  mcp::client::McpClientSession session(std::move(transport));

  const auto initialized = session.initialize();
  require(!initialized.has_value(),
          "child exit before response should fail initialize");
}

#ifndef _WIN32
void test_posix_process_stdio_write_after_child_exit_returns_error() {
  mcp::client::ProcessStdioTransport transport(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--exit-immediately"},
          .cwd = {},
          .env = {},
          .request_timeout = std::chrono::milliseconds(500),
      });

  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return std::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "unexpected child request",
        });
      },
      [](const mcp::protocol::JsonRpcNotification&)
          -> mcp::core::Result<mcp::core::Unit> { return mcp::core::Unit{}; });
  require(started.has_value(), "posix process transport start should succeed");

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const auto sent =
      transport.send_notification(mcp::protocol::JsonRpcNotification{
          .method = "notifications/initialized",
          .params = mcp::protocol::Json::object(),
      });
  require(!sent.has_value(),
          "posix write after child exit should fail without terminating host");

  transport.stop();
}
#endif

void test_process_stdio_transport_handles_malformed_child_output() {
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--malformed-output"},
          .cwd = {},
          .env = {},
          .request_timeout = std::chrono::milliseconds(500),
      });
  mcp::client::McpClientSession session(std::move(transport));

  const auto initialized = session.initialize();
  require(!initialized.has_value(),
          "malformed child output should fail initialize");
}

void test_process_stdio_transport_ignores_child_stderr() {
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--stderr-before-response"},
          .cwd = {},
          .env = {},
      });
  mcp::client::McpClientSession session(std::move(transport));

  const auto initialized = session.initialize();
  require(initialized.has_value(),
          "stderr output should not block stdout protocol flow");
}

void test_process_stdio_transport_runs_python_mcp_server() {
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = "uv",
          .args = {"run", "--with", "mcp", "python",
                   python_stdio_child_script().string()},
          .cwd = {},
          .env = {},
      });
  mcp::client::McpClientSession session(std::move(transport));

  const auto initialized = session.initialize();
  require(initialized.has_value(), "python process initialize should succeed");

  const auto marked = session.mark_initialized();
  require(marked.has_value(),
          "python process initialized notification should succeed");

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
  require(initialized.has_value(),
          "typescript process initialize should succeed");

  const auto marked = session.mark_initialized();
  require(marked.has_value(),
          "typescript process initialized notification should succeed");

  const auto tools = session.discover_tools();
  require(tools.has_value(),
          "typescript process tools discovery should succeed");
  require(tools->size() == 1, "typescript process tool count mismatch");
  require(tools->front().name == "echo",
          "typescript process tool name mismatch");
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
  require(result->content.size() == 1,
          "typescript process tool result content mismatch");
  require(result->content.front().text == "echo",
          "typescript process tool result text mismatch");
  require(result->structured_content.has_value(),
          "typescript structured content missing");
  require(result->structured_content->at("value") == 42,
          "typescript structured content mismatch");
}

void test_process_stdio_transport_runs_rust_mcp_server() {
  build_rust_stdio_child();
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = rust_stdio_child_executable().string(),
          .args = {},
          .cwd = {},
          .env = rust_cargo_env(),
      });
  mcp::client::McpClientSession session(std::move(transport));

  const auto initialized = session.initialize();
  require(initialized.has_value(), "rust process initialize should succeed");

  const auto marked = session.mark_initialized();
  require(marked.has_value(),
          "rust process initialized notification should succeed");

  const auto tools = session.discover_tools();
  require(tools.has_value(), "rust process tools discovery should succeed");
  require(tools->size() == 1, "rust process tool count mismatch");
  require(tools->front().name == "echo", "rust process tool name mismatch");
}

void test_process_stdio_transport_calls_rust_tool() {
  build_rust_stdio_child();
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = rust_stdio_child_executable().string(),
          .args = {},
          .cwd = {},
          .env = rust_cargo_env(),
      });
  mcp::client::McpClientSession session(std::move(transport));

  const auto initialized = session.initialize();
  require(initialized.has_value(), "rust process initialize should succeed");

  const auto marked = session.mark_initialized();
  require(marked.has_value(),
          "rust process initialized notification should succeed");

  const auto result = session.call_tool(mcp::protocol::ToolCall{
      .name = "echo",
      .arguments = mcp::protocol::Json{{"value", 42}},
  });
  require(result.has_value(), "rust process tool call should succeed");
  require(result->content.size() == 1,
          "rust process tool result content mismatch");
  require(result->content.front().text == "42",
          "rust process tool result text mismatch");
}

void test_native_process_stdio_transport_exposes_client_contract() {
  static_assert(std::is_base_of_v<mcp::transport::ClientTransport,
                                  mcp::transport::ProcessStdioClientTransport>);

  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  require(transport.name() == "process-stdio",
          "native process transport name mismatch");
  require(transport.diagnostics().at("name") == "process-stdio",
          "native process transport diagnostics mismatch");

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{1},
  });
  require(sent.has_value(), "native process initialize send should succeed");

  auto received = transport.receive();
  require(received.has_value(), "native process initialize receive failed");
  require(received->has_value(), "native process initialize response missing");
  const auto* initialize_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(initialize_response != nullptr,
          "native process initialize should receive response");
  require(initialize_response->result.has_value(),
          "native process initialize should have result");

  sent = transport.send(mcp::protocol::JsonRpcNotification{
      .method = "notifications/initialized",
      .params = mcp::protocol::Json::object(),
  });
  require(sent.has_value(),
          "native process initialized notification should send");

  sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{2},
  });
  require(sent.has_value(), "native process tools/list send should succeed");

  received = transport.receive();
  require(received.has_value(), "native process tools/list receive failed");
  require(received->has_value(), "native process tools/list response missing");
  const auto* tools_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(tools_response != nullptr,
          "native process tools/list should receive response");
  require(tools_response->result.has_value(),
          "native process tools/list should have result");
  require(tools_response->result->at("tools").size() == 1,
          "native process tools/list count mismatch");
  const auto diagnostics = transport.diagnostics();
  require(diagnostics.at("activeRequestWorkers").get<std::size_t>() == 0,
          "native process request workers should be inactive");
  require(diagnostics.at("completedRequestWorkers").get<std::size_t>() >= 2,
          "native process completed request worker count mismatch");
  require(diagnostics.at("failedRequestWorkers").get<std::size_t>() == 0,
          "native process failed request worker count mismatch");
  require(diagnostics.at("timedOutRequestWorkers").get<std::size_t>() == 0,
          "native process timeout request worker count mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native process close should succeed");
}

void test_native_process_stdio_transport_diagnostics_timeout_cleanup() {
  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  options.args = {"--ignore-requests"};
  options.request_timeout = std::chrono::milliseconds(100);
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{501},
  });
  require(sent.has_value(), "native process timeout request should send");

  auto received = transport.receive();
  require(received.has_value(), "native process timeout receive failed");
  require(received->has_value(), "native process timeout response missing");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(response != nullptr,
          "native process timeout should receive response message");
  require(response->error.has_value(),
          "native process timeout should surface an error response");
  require(response->error->message == "process stdio request timed out",
          "native process timeout error message mismatch");

  const auto diagnostics = transport.diagnostics();
  require(diagnostics.at("pendingServerRequests").get<std::size_t>() == 0,
          "native process timeout should not leave pending server requests");
  require(diagnostics.at("activeRequestWorkers").get<std::size_t>() == 0,
          "native process timeout should not leave active request workers");
  require(diagnostics.at("completedRequestWorkers").get<std::size_t>() >= 1,
          "native process timeout completed worker count mismatch");
  require(diagnostics.at("failedRequestWorkers").get<std::size_t>() >= 1,
          "native process timeout failed worker count mismatch");
  require(diagnostics.at("timedOutRequestWorkers").get<std::size_t>() >= 1,
          "native process timeout worker count mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native process timeout close should succeed");
}

void test_native_process_stdio_transport_receives_server_request() {
  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "custom/interleave",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{77},
  });
  require(sent.has_value(), "native interleaved request send should succeed");

  auto received = transport.receive();
  require(received.has_value(), "native server request receive failed");
  require(received->has_value(), "native server request missing");
  const auto* server_request =
      std::get_if<mcp::protocol::JsonRpcRequest>(&received->value());
  require(server_request != nullptr,
          "native interleave should receive server request first");
  require(server_request->method == "sampling/createMessage",
          "native server request method mismatch");

  sent = transport.send(mcp::protocol::make_response(
      server_request->id,
      mcp::protocol::Json{
          {"role", "assistant"},
          {"content",
           mcp::protocol::Json{{"type", "text"}, {"text", "sampled"}}},
          {"model", "test-model"},
          {"stopReason", "endTurn"},
      }));
  require(sent.has_value(), "native server request response should send");

  received = transport.receive();
  require(received.has_value(), "native final response receive failed");
  require(received->has_value(), "native final response missing");
  const auto* final_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(final_response != nullptr,
          "native interleave should receive final response");
  require(final_response->result.has_value(),
          "native interleave final response should have result");
  require(final_response->result->at("ok") == true,
          "native interleave final response mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native interleave close should succeed");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, void (*)()>> tests = {
      {"process stdio transport runs child MCP server",
       test_process_stdio_transport_runs_child_mcp_server},
      {"process stdio transport calls child tool",
       test_process_stdio_transport_calls_child_tool},
      {"process stdio transport handles interleaved child request",
       test_process_stdio_transport_handles_interleaved_child_request},
      {"process stdio transport times out unresponsive child",
       test_process_stdio_transport_times_out_unresponsive_child},
      {"process stdio transport stop unblocks pending request",
       test_process_stdio_transport_stop_unblocks_pending_request},
      {"process stdio transport sends cancelled notification while pending",
       test_process_stdio_transport_sends_cancelled_notification_while_pending},
      {"process stdio transport passes args cwd and env without shell",
       test_process_stdio_transport_passes_args_cwd_and_env_without_shell},
      {"process stdio transport handles child exit before response",
       test_process_stdio_transport_handles_child_exit_before_response},
#ifndef _WIN32
      {"posix process stdio write after child exit returns error",
       test_posix_process_stdio_write_after_child_exit_returns_error},
#endif
      {"process stdio transport handles malformed child output",
       test_process_stdio_transport_handles_malformed_child_output},
      {"process stdio transport ignores child stderr",
       test_process_stdio_transport_ignores_child_stderr},
      {"process stdio transport runs python MCP server",
       test_process_stdio_transport_runs_python_mcp_server},
      {"process stdio transport runs typescript MCP server",
       test_process_stdio_transport_runs_typescript_mcp_server},
      {"process stdio transport calls typescript tool",
       test_process_stdio_transport_calls_typescript_tool},
      {"process stdio transport runs rust MCP server",
       test_process_stdio_transport_runs_rust_mcp_server},
      {"process stdio transport calls rust tool",
       test_process_stdio_transport_calls_rust_tool},
      {"native process stdio transport exposes client contract",
       test_native_process_stdio_transport_exposes_client_contract},
      {"native process stdio transport diagnostics timeout cleanup",
       test_native_process_stdio_transport_diagnostics_timeout_cleanup},
      {"native process stdio transport receives server request",
       test_native_process_stdio_transport_receives_server_request},
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
