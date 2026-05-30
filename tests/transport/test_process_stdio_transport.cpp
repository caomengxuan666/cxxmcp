// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
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
#include "cxxmcp/error.hpp"
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

mcp::client::ProcessStdioTransportOptions typescript_stdio_child_options() {
  mcp::client::ProcessStdioTransportOptions options;
  options.command = "node";
  options.args = {typescript_stdio_child_bootstrap_script().string()};
  // Cold Windows runners can spend more than the default request timeout
  // installing the Node fixture dependencies before the child server responds.
  options.request_timeout = std::chrono::seconds(120);
  return options;
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

std::optional<std::string> get_process_env(std::string_view key) {
  const std::string key_string(key);
#ifdef _WIN32
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, key_string.c_str()) != 0 || value == nullptr) {
    return std::nullopt;
  }
  std::string result(value);
  std::free(value);
  return result;
#else
  const char* value = std::getenv(key_string.c_str());
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

std::unordered_map<std::string, std::string> rust_cargo_env() {
  const auto target_dir = std::filesystem::temp_directory_path() /
                          "mcp-rust-process-stdio-child-target";
  std::unordered_map<std::string, std::string> env = {
      {"CARGO_TARGET_DIR", target_dir.string()},
  };

  if (const auto proxy = get_process_env("CXXMCP_CARGO_PROXY");
      proxy.has_value() && !proxy->empty()) {
    env["CARGO_HTTP_PROXY"] = *proxy;
    env["CARGO_HTTPS_PROXY"] = *proxy;
    env["HTTP_PROXY"] = *proxy;
    env["HTTPS_PROXY"] = *proxy;
    env["ALL_PROXY"] = *proxy;
  }
  return env;
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
  std::string command = "cargo build ";
  if (const auto offline = get_process_env("CXXMCP_RUST_FIXTURE_OFFLINE");
      offline.has_value() && *offline == "1") {
    command += "--offline ";
  }
  command += "--manifest-path \"" + rust_stdio_child_manifest().string() + "\"";
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

void test_process_stdio_transport_returns_handler_error_to_child_request() {
  mcp::client::ProcessStdioTransport transport(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {},
          .cwd = {},
          .env = {},
      });

  const auto started = transport.start(
      [](const mcp::protocol::JsonRpcRequest&)
          -> mcp::core::Result<mcp::protocol::JsonRpcResponse> {
        return mcp::core::unexpected(mcp::errors::handler_failed(
            "process stdio handler rejected request"));
      },
      [](const mcp::protocol::JsonRpcNotification&)
          -> mcp::core::Result<mcp::core::Unit> { return mcp::core::Unit{}; });
  require(started.has_value(), "process handler-error transport should start");

  const auto response = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "custom/interleave",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{177},
  });
  require(response.has_value(),
          "process handler-error request should receive child response");
  require(response->result.has_value(),
          "process handler-error child response should contain result");
  require(response->result->at("ok") == false,
          "process handler-error child response ok mismatch");
  require(response->result->at("handlerError") == "handler failed",
          "process handler-error message should be stable");

  transport.stop();
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
        return mcp::core::unexpected(mcp::core::Error{
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
        .method = mcp::protocol::InitializeMethod,
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
        return mcp::core::unexpected(mcp::core::Error{
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
        .method = mcp::protocol::InitializeMethod,
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
          .method = mcp::protocol::CancelledNotificationMethod,
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

void test_process_stdio_transport_rejects_unexpected_response_id() {
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--wrong-response-id"},
          .cwd = {},
          .env = {},
      });

  const auto result = transport->send(mcp::protocol::JsonRpcRequest{
      .method = "custom/wrong-id",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{77},
  });

  require(!result.has_value(),
          "process stdio should reject unexpected response ids");
  require(result.error().message ==
              "process stdio transport received an unexpected response",
          "process stdio unexpected response message mismatch");
  require(result.error().detail == "999",
          "process stdio unexpected response detail mismatch");
  require(result.error().category == "transport",
          "process stdio unexpected response category mismatch");
}

void test_process_stdio_transport_rejects_duplicate_request_id() {
  auto transport = std::make_unique<mcp::client::ProcessStdioTransport>(
      mcp::client::ProcessStdioTransportOptions{
          .command = MCP_TEST_CHILD_EXE,
          .args = {"--ignore-requests"},
          .cwd = {},
          .env = {},
          .request_timeout = std::chrono::milliseconds(500),
      });
  require(transport->start({}).has_value(),
          "process stdio duplicate test transport should start");

  mcp::core::Result<mcp::protocol::JsonRpcResponse> first_result;
  std::thread first_request([&] {
    first_result = transport->send(mcp::protocol::JsonRpcRequest{
        .method = "custom/pending",
        .params = mcp::protocol::Json::object(),
        .id = std::int64_t{77},
    });
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const auto duplicate = transport->send(mcp::protocol::JsonRpcRequest{
      .method = "custom/duplicate",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{77},
  });

  require(!duplicate.has_value(),
          "process stdio should reject duplicate request ids");
  require(duplicate.error().message == "duplicate process stdio request id",
          "process stdio duplicate request message mismatch");
  require(duplicate.error().detail == "77",
          "process stdio duplicate request detail mismatch");
  require(duplicate.error().category == "transport",
          "process stdio duplicate request category mismatch");

  transport->stop();
  first_request.join();
  const bool first_request_stopped =
      (!first_result.has_value() &&
       first_result.error().message ==
           "process stdio transport reader stopped") ||
      (first_result.has_value() && first_result->error.has_value() &&
       first_result->error->message ==
           "process stdio transport reader stopped");
  require(first_request_stopped,
          "first duplicate-id request should finish as a stable stop error");
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
        return mcp::core::unexpected(mcp::core::Error{
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
      typescript_stdio_child_options());
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
      typescript_stdio_child_options());
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
      .method = mcp::protocol::InitializeMethod,
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

void test_native_process_stdio_transport_reuses_child_for_multiple_requests() {
  constexpr int kToolRequestCount = 16;

  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{700},
  });
  require(sent.has_value(), "native long-running initialize send failed");

  auto received = transport.receive();
  require(received.has_value(),
          "native long-running initialize receive failed");
  require(received->has_value(),
          "native long-running initialize response missing");
  const auto* initialize_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(initialize_response != nullptr,
          "native long-running initialize should receive response");
  require(
      initialize_response->id == mcp::protocol::RequestId{std::int64_t{700}},
      "native long-running initialize response id mismatch");
  require(initialize_response->result.has_value(),
          "native long-running initialize should have result");

  sent = transport.send(mcp::protocol::JsonRpcNotification{
      .method = "notifications/initialized",
      .params = mcp::protocol::Json::object(),
  });
  require(sent.has_value(),
          "native long-running initialized notification send failed");

  for (int i = 0; i < kToolRequestCount; ++i) {
    const auto request_id = std::int64_t{701 + i};
    sent = transport.send(mcp::protocol::JsonRpcRequest{
        .method = "tools/call",
        .params =
            mcp::protocol::Json{
                {"name", "echo"},
                {"arguments", mcp::protocol::Json{{"round", i}}},
            },
        .id = request_id,
    });
    require(sent.has_value(), "native long-running tool request send failed");

    received = transport.receive();
    require(received.has_value(),
            "native long-running tool response receive failed");
    require(received->has_value(), "native long-running tool response missing");
    const auto* response =
        std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
    require(response != nullptr,
            "native long-running should receive tool response");
    require(response->id == mcp::protocol::RequestId{request_id},
            "native long-running tool response id mismatch");
    require(response->result.has_value(),
            "native long-running tool response should have result");
    require(response->result->at("content").at(0).at("text") == "echo",
            "native long-running tool response content mismatch");
    require(response->result->at("structuredContent").at("round") == i,
            "native long-running structured content mismatch");
  }

  const auto diagnostics = transport.diagnostics();
  require(diagnostics.at("closed") == false,
          "native long-running transport should remain open");
  require(diagnostics.at("queued").get<std::size_t>() == 0,
          "native long-running transport should drain queued responses");
  require(diagnostics.at("pendingServerRequests").get<std::size_t>() == 0,
          "native long-running transport should not leak server requests");
  require(diagnostics.at("activeRequestWorkers").get<std::size_t>() == 0,
          "native long-running transport should not leak active workers");
  require(diagnostics.at("completedRequestWorkers").get<std::size_t>() >=
              static_cast<std::size_t>(kToolRequestCount + 1),
          "native long-running completed worker count mismatch");
  require(diagnostics.at("failedRequestWorkers").get<std::size_t>() == 0,
          "native long-running request workers should not fail");
  require(diagnostics.at("timedOutRequestWorkers").get<std::size_t>() == 0,
          "native long-running request workers should not time out");

  const auto closed = transport.close();
  require(closed.has_value(), "native long-running close should succeed");
  require(transport.diagnostics().at("closed") == true,
          "native long-running diagnostics should report closed");
}

void test_native_process_stdio_transport_diagnostics_timeout_cleanup() {
  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  options.args = {"--ignore-requests"};
  options.request_timeout = std::chrono::milliseconds(100);
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
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

void test_native_process_stdio_transport_surfaces_unexpected_response_id() {
  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  options.args = {"--wrong-response-id"};
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "custom/wrong-id",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{602},
  });
  require(sent.has_value(), "native process unexpected-id send should succeed");

  auto received = transport.receive();
  require(received.has_value(), "native process unexpected-id receive failed");
  require(received->has_value(),
          "native process unexpected-id response missing");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(response != nullptr,
          "native process unexpected-id should receive response message");
  require(response->id == mcp::protocol::RequestId{std::int64_t{602}},
          "native process unexpected-id response id mismatch");
  require(response->error.has_value(),
          "native process unexpected-id should surface an error response");
  require(response->error->message ==
              "process stdio transport received an unexpected response",
          "native process unexpected-id error message mismatch");

  const auto closed = transport.close();
  require(closed.has_value(),
          "native process unexpected-id close should succeed");
}

void test_native_process_stdio_transport_rejects_duplicate_request_id() {
  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  options.args = {"--ignore-requests"};
  options.request_timeout = std::chrono::milliseconds(500);
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "custom/pending",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{603},
  });
  require(sent.has_value(),
          "native process duplicate first send should succeed");

  const auto duplicate = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "custom/duplicate",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{603},
  });
  require(!duplicate.has_value(),
          "native process should reject duplicate in-flight request ids");
  require(
      duplicate.error().message == "duplicate process stdio client request id",
      "native process duplicate request message mismatch");
  require(duplicate.error().detail == "603",
          "native process duplicate request detail mismatch");
  require(duplicate.error().category == "transport",
          "native process duplicate request category mismatch");

  const auto closed = transport.close();
  require(closed.has_value(), "native process duplicate close should succeed");
}

void test_native_process_stdio_transport_rejects_unknown_server_response_id() {
  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  const auto sent = transport.send(mcp::protocol::JsonRpcResponse{
      .id = mcp::protocol::RequestId{std::string("unknown-server-request")},
      .result = mcp::protocol::Json::object(),
  });
  require(!sent.has_value(),
          "native process should reject response without pending request");
  require(sent.error().message ==
              "process stdio client transport has no pending server request",
          "native process unknown server response message mismatch");
  require(sent.error().detail == "unknown-server-request",
          "native process unknown server response detail mismatch");
  require(sent.error().category == "transport",
          "native process unknown server response category mismatch");

  const auto closed = transport.close();
  require(closed.has_value(),
          "native process unknown server response close should succeed");
}

void test_native_process_stdio_transport_surfaces_malformed_child_output() {
  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  options.args = {"--malformed-output"};
  options.request_timeout = std::chrono::milliseconds(500);
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = mcp::protocol::InitializeMethod,
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{601},
  });
  require(sent.has_value(), "native malformed request should send");

  auto received = transport.receive();
  require(received.has_value(), "native malformed receive should complete");
  require(received->has_value(), "native malformed response should be queued");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(response != nullptr,
          "native malformed output should produce response message");
  require(response->error.has_value(),
          "native malformed output should surface error response");

  const auto closed = transport.close();
  require(closed.has_value(), "native malformed close should succeed");
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

void test_native_process_stdio_transport_round_trips_handler_error_response() {
  mcp::transport::ProcessStdioClientTransportOptions options;
  options.command = MCP_TEST_CHILD_EXE;
  mcp::transport::ProcessStdioClientTransport transport(std::move(options));

  auto sent = transport.send(mcp::protocol::JsonRpcRequest{
      .method = "custom/interleave",
      .params = mcp::protocol::Json::object(),
      .id = std::int64_t{178},
  });
  require(sent.has_value(),
          "native handler-error interleaved request send should succeed");

  auto received = transport.receive();
  require(received.has_value(),
          "native handler-error server request receive failed");
  require(received->has_value(), "native handler-error server request missing");
  const auto* server_request =
      std::get_if<mcp::protocol::JsonRpcRequest>(&received->value());
  require(server_request != nullptr,
          "native handler-error should receive server request first");

  sent = transport.send(mcp::protocol::make_error_response(
      std::optional<mcp::protocol::RequestId>{server_request->id},
      mcp::protocol::make_error(
          mcp::protocol::ErrorCode::InternalError, "handler failed",
          mcp::protocol::Json("native handler rejected"))));
  require(sent.has_value(), "native handler-error response should send");

  received = transport.receive();
  require(received.has_value(),
          "native handler-error final response receive failed");
  require(received->has_value(), "native handler-error final response missing");
  const auto* final_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&received->value());
  require(final_response != nullptr,
          "native handler-error should receive final response");
  require(final_response->result.has_value(),
          "native handler-error final response should have result");
  require(final_response->result->at("ok") == false,
          "native handler-error final response ok mismatch");
  require(final_response->result->at("handlerError") == "handler failed",
          "native handler-error final response message mismatch");

  const auto closed = transport.close();
  require(closed.has_value(),
          "native handler-error interleave close should succeed");
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
      {"process stdio transport returns handler error to child request",
       test_process_stdio_transport_returns_handler_error_to_child_request},
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
      {"process stdio transport rejects unexpected response id",
       test_process_stdio_transport_rejects_unexpected_response_id},
      {"process stdio transport rejects duplicate request id",
       test_process_stdio_transport_rejects_duplicate_request_id},
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
      {"native process stdio transport reuses child for multiple requests",
       test_native_process_stdio_transport_reuses_child_for_multiple_requests},
      {"native process stdio transport diagnostics timeout cleanup",
       test_native_process_stdio_transport_diagnostics_timeout_cleanup},
      {"native process stdio transport surfaces unexpected response id",
       test_native_process_stdio_transport_surfaces_unexpected_response_id},
      {"native process stdio transport rejects duplicate request id",
       test_native_process_stdio_transport_rejects_duplicate_request_id},
      {"native process stdio transport rejects unknown server response id",
       test_native_process_stdio_transport_rejects_unknown_server_response_id},
      {"native process stdio transport surfaces malformed child output",
       test_native_process_stdio_transport_surfaces_malformed_child_output},
      {"native process stdio transport receives server request",
       test_native_process_stdio_transport_receives_server_request},
      {"native process stdio transport round trips handler error response",
       test_native_process_stdio_transport_round_trips_handler_error_response},
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
