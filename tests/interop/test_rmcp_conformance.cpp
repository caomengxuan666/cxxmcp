// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cxxmcp/client/client.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/server/http_transport.hpp"
#include "httplib.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void set_process_env(std::string_view key, const std::string& value) {
  const std::string key_string(key);
#ifdef _WIN32
  _putenv_s(key_string.c_str(), value.c_str());
#else
  setenv(key_string.c_str(), value.c_str(), 1);
#endif
}

std::filesystem::path repo_root() {
  return std::filesystem::path(MCP_TEST_SOURCE_DIR);
}

std::filesystem::path conformance_manifest() {
  return repo_root() / "reference" / "rmcp" / "conformance" / "Cargo.toml";
}

std::filesystem::path conformance_target_dir() {
  return repo_root() / "build" / "rmcp-conformance-target";
}

std::filesystem::path conformance_client_executable() {
  auto path = conformance_target_dir() / "debug" / "conformance-client";
#ifdef _WIN32
  path += ".exe";
#endif
  return path;
}

std::string quote_path(const std::filesystem::path& path) {
  return "\"" + path.string() + "\"";
}

std::string quote_text(const std::string& value) { return "\"" + value + "\""; }

bool run_command_with_timeout(const std::string& command,
                              std::chrono::milliseconds timeout,
                              std::string* output = nullptr) {
#ifdef _WIN32
  std::wstring wide_command(command.begin(), command.end());
  std::vector<wchar_t> buffer(wide_command.begin(), wide_command.end());
  buffer.push_back(L'\0');

  SECURITY_ATTRIBUTES security_attributes{};
  security_attributes.nLength = sizeof(security_attributes);
  security_attributes.bInheritHandle = TRUE;

  HANDLE output_read = nullptr;
  HANDLE output_write = nullptr;
  if (output != nullptr) {
    if (!CreatePipe(&output_read, &output_write, &security_attributes, 0)) {
      return false;
    }
    SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
  startup.wShowWindow = SW_HIDE;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput =
      output_write != nullptr ? output_write : GetStdHandle(STD_OUTPUT_HANDLE);
  startup.hStdError =
      output_write != nullptr ? output_write : GetStdHandle(STD_ERROR_HANDLE);

  std::thread reader;
  if (output != nullptr) {
    output->clear();
    reader = std::thread([output, output_read]() {
      char buffer[4096];
      DWORD read = 0;
      while (ReadFile(output_read, buffer, sizeof(buffer), &read, nullptr) &&
             read > 0) {
        output->append(buffer, buffer + read);
      }
    });
  }

  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    if (output_write != nullptr) {
      CloseHandle(output_write);
    }
    if (output_read != nullptr) {
      CloseHandle(output_read);
    }
    if (reader.joinable()) {
      reader.join();
    }
    return false;
  }

  if (output_write != nullptr) {
    CloseHandle(output_write);
  }

  const DWORD wait = WaitForSingleObject(process.hProcess,
                                         static_cast<DWORD>(timeout.count()));
  DWORD exit_code = 1;
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, INFINITE);
  }
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (reader.joinable()) {
    reader.join();
  }
  if (output_read != nullptr) {
    CloseHandle(output_read);
  }
  return wait != WAIT_TIMEOUT && exit_code == 0;
#else
  (void)output;
  return std::system(command.c_str()) == 0;
#endif
}

void configure_cargo_proxy() {
  const std::string proxy = "http://127.0.0.1:7897";
  set_process_env("CARGO_HTTP_PROXY", proxy);
  set_process_env("CARGO_HTTPS_PROXY", proxy);
  set_process_env("HTTP_PROXY", proxy);
  set_process_env("HTTPS_PROXY", proxy);
  set_process_env("ALL_PROXY", proxy);
}

void build_conformance_client() {
  if (std::filesystem::exists(conformance_client_executable())) {
    return;
  }

  configure_cargo_proxy();
  set_process_env("CARGO_TARGET_DIR", conformance_target_dir().string());

  const std::string command = "cargo build --manifest-path " +
                              quote_path(conformance_manifest()) +
                              " --bin conformance-client";
  require(std::system(command.c_str()) == 0,
          "RMCP conformance client build should succeed");
}

class RunningInteropServer {
 public:
  RunningInteropServer() {
    server_.Post("/mcp", [this](const httplib::Request& request,
                                httplib::Response& response) {
      const auto message = mcp::protocol::parse_message(request.body);
      if (!message) {
        response.status = 400;
        response.set_content(message.error().message, "text/plain");
        return;
      }

      if (const auto* notification =
              std::get_if<mcp::protocol::JsonRpcNotification>(&*message)) {
        (void)notification;
        response.status = 202;
        return;
      }

      const auto* rpc_request =
          std::get_if<mcp::protocol::JsonRpcRequest>(&*message);
      if (rpc_request == nullptr) {
        response.status = 400;
        response.set_content("unexpected message", "text/plain");
        return;
      }

      if (rpc_request->method == mcp::protocol::InitializeMethod) {
        {
          std::lock_guard lock(mutex_);
          session_id_ = "mcp-session-1";
        }

        response.set_header("Mcp-Session-Id", session_id_);
        const auto initialize_response = mcp::protocol::make_response(
            rpc_request->id,
            Json{
                {"protocolVersion",
                 std::string(mcp::protocol::McpProtocolVersion)},
                {"capabilities", Json{{"tools", Json::object()}}},
                {"serverInfo",
                 Json{{"name", "cxxmcp-interop"}, {"version", "1"}}},
            });
        const auto serialized =
            mcp::protocol::serialize_response(initialize_response);
        require(serialized.has_value(), "initialize response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::ToolsListMethod) {
        const auto tool_response = mcp::protocol::make_response(
            rpc_request->id,
            Json{
                {"tools", Json::array({mcp::protocol::tool_definition_to_json(
                              mcp::protocol::ToolDefinition{
                                  .name = "test_simple_text",
                                  .description = "Returns simple text content",
                                  .input_schema =
                                      Json{
                                          {"type", "object"},
                                          {"properties", Json::object()},
                                      },
                                  .streaming = false,
                              })})},
            });
        const auto serialized =
            mcp::protocol::serialize_response(tool_response);
        require(serialized.has_value(), "tools/list response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::ToolsCallMethod) {
        const auto tool_name =
            rpc_request->params.at("name").get<std::string>();
        require(tool_name == "test_simple_text",
                "interop server should receive the expected tool");
        mcp::protocol::ToolResult result;
        result.content.push_back(mcp::protocol::ContentBlock{
            .type = "text",
            .text = "This is a simple text response for testing.",
            .data = Json::object(),
        });
        const auto call_response = mcp::protocol::make_response(
            rpc_request->id, mcp::protocol::tool_result_to_json(result));
        const auto serialized =
            mcp::protocol::serialize_response(call_response);
        require(serialized.has_value(), "tools/call response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::PingMethod) {
        const auto ping_response =
            mcp::protocol::make_response(rpc_request->id, Json::object());
        const auto serialized =
            mcp::protocol::serialize_response(ping_response);
        require(serialized.has_value(), "ping response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      const auto error_response = mcp::protocol::make_error_response(
          rpc_request->id,
          mcp::protocol::make_error(mcp::protocol::ErrorCode::MethodNotFound,
                                    "unexpected method"));
      const auto serialized = mcp::protocol::serialize_response(error_response);
      require(serialized.has_value(), "error response should serialize");
      response.status = 200;
      response.set_content(*serialized, "application/json");
    });

    server_.Get("/mcp", [this](const httplib::Request& request,
                               httplib::Response& response) {
      if (!request.has_header("Mcp-Session-Id")) {
        response.status = 400;
        return;
      }
      std::string session_id;
      {
        std::lock_guard lock(mutex_);
        session_id = session_id_;
      }
      if (session_id.empty() ||
          request.get_header_value("Mcp-Session-Id") != session_id) {
        response.status = 404;
        return;
      }

      response.set_chunked_content_provider(
          "text/event-stream",
          [this, &request, session_id](std::size_t, httplib::DataSink& sink) {
            while (!stopped_.load()) {
              if (!sink.is_writable() || request.is_connection_closed()) {
                sink.done();
                return false;
              }
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            sink.done();
            return false;
          });
    });

    port_ = static_cast<std::uint16_t>(server_.bind_to_any_port("127.0.0.1"));
    require(port_ > 0, "failed to bind interop server");
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  ~RunningInteropServer() {
    stopped_.store(true);
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  std::uint16_t port() const { return port_; }

 private:
  httplib::Server server_;
  std::thread thread_;
  std::atomic<bool> stopped_{false};
  mutable std::mutex mutex_;
  std::string session_id_;
  std::uint16_t port_ = 0;
};

void test_rmcp_conformance_client_tools_call() {
  build_conformance_client();

  RunningInteropServer server;
  const auto port = server.port();

  set_process_env("MCP_CONFORMANCE_SCENARIO", "tools_call");
  set_process_env("NO_PROXY", "127.0.0.1,localhost");
  set_process_env("no_proxy", "127.0.0.1,localhost");
  set_process_env("RUST_BACKTRACE", "1");
  set_process_env("RUST_LOG", "debug");
  const auto command =
      quote_path(conformance_client_executable()) + " " +
      quote_text("http://127.0.0.1:" + std::to_string(port) + "/mcp");
  std::string output;
  if (!run_command_with_timeout(command, std::chrono::seconds(60), &output)) {
    throw std::runtime_error(
        "RMCP conformance tools_call scenario should succeed. Output:\n" +
        output);
  }
}

}  // namespace

int main() {
  try {
    test_rmcp_conformance_client_tools_call();
  } catch (const std::exception& ex) {
    std::cerr << "[FAIL] rmcp conformance interop: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "[PASS] rmcp conformance interop\n";
  return 0;
}
