// Copyright (c) 2025 [caomengxuan666]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/completion.hpp"
#include "cxxmcp/protocol/elicitation.hpp"
#include "cxxmcp/protocol/logging.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/roots.hpp"
#include "cxxmcp/protocol/sampling.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/server/http_transport.hpp"
#include "cxxmcp/service.hpp"
#include "cxxmcp/transport.hpp"
#include "httplib.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using mcp::protocol::Json;

constexpr std::string_view kRmcpReferenceVersion = "1.7.0";
constexpr std::string_view kRmcpConformanceVersion = "0.1.0";
constexpr std::string_view kElicitationDefaultsRequestId =
    "cxxmcp-elicitation-defaults-1";
constexpr std::string_view kRootsRoundTripRequestId = "cxxmcp-roots-list-1";
constexpr std::string_view kSamplingRoundTripRequestId =
    "cxxmcp-sampling-create-1";

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::mutex& process_env_mutex() {
  static std::mutex mutex;
  return mutex;
}

void set_process_env(std::string_view key, const std::string& value) {
  std::lock_guard lock(process_env_mutex());
  const std::string key_string(key);
#ifdef _WIN32
  _putenv_s(key_string.c_str(), value.c_str());
  SetEnvironmentVariableA(key_string.c_str(), value.c_str());
#else
  setenv(key_string.c_str(), value.c_str(), 1);
#endif
}

void unset_process_env(std::string_view key) {
  std::lock_guard lock(process_env_mutex());
  const std::string key_string(key);
#ifdef _WIN32
  _putenv_s(key_string.c_str(), "");
  SetEnvironmentVariableA(key_string.c_str(), nullptr);
#else
  unsetenv(key_string.c_str());
#endif
}

std::optional<std::string> get_process_env(std::string_view key) {
  std::lock_guard lock(process_env_mutex());
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

std::filesystem::path repo_root() {
  return std::filesystem::path(MCP_TEST_SOURCE_DIR);
}

std::filesystem::path conformance_manifest() {
  return repo_root() / "reference" / "rmcp" / "conformance" / "Cargo.toml";
}

std::filesystem::path pagination_client_manifest() {
  return repo_root() / "tests" / "fixtures" / "rmcp_pagination_client" /
         "Cargo.toml";
}

std::filesystem::path completion_client_manifest() {
  return repo_root() / "tests" / "fixtures" / "rmcp_completion_client" /
         "Cargo.toml";
}

std::filesystem::path roots_sampling_client_manifest() {
  return repo_root() / "tests" / "fixtures" / "rmcp_roots_sampling_client" /
         "Cargo.toml";
}

std::filesystem::path task_lifecycle_client_manifest() {
  return repo_root() / "tests" / "fixtures" / "rmcp_task_lifecycle_client" /
         "Cargo.toml";
}

std::filesystem::path reverse_server_manifest() {
  return repo_root() / "tests" / "fixtures" / "rmcp_reverse_server" /
         "Cargo.toml";
}

std::filesystem::path conformance_target_dir() {
  return repo_root() / "build" / "rmcp-conformance-target";
}

std::filesystem::path pagination_client_target_dir() {
  return repo_root() / "build" / "rmcp-pagination-client-target";
}

std::filesystem::path completion_client_target_dir() {
  return repo_root() / "build" / "rmcp-completion-client-target";
}

std::filesystem::path roots_sampling_client_target_dir() {
  return repo_root() / "build" / "rmcp-roots-sampling-client-target";
}

std::filesystem::path task_lifecycle_client_target_dir() {
  return repo_root() / "build" / "rmcp-task-lifecycle-client-target";
}

std::filesystem::path reverse_server_target_dir() {
  return repo_root() / "build" / "rmcp-reverse-server-target";
}

std::filesystem::path conformance_client_executable() {
  auto path = conformance_target_dir() / "debug" / "conformance-client";
#ifdef _WIN32
  path += ".exe";
#endif
  return path;
}

std::filesystem::path conformance_server_executable() {
  auto path = conformance_target_dir() / "debug" / "conformance-server";
#ifdef _WIN32
  path += ".exe";
#endif
  return path;
}

std::filesystem::path pagination_client_executable() {
  auto path =
      pagination_client_target_dir() / "debug" / "rmcp_pagination_client";
#ifdef _WIN32
  path += ".exe";
#endif
  return path;
}

std::filesystem::path completion_client_executable() {
  auto path =
      completion_client_target_dir() / "debug" / "rmcp_completion_client";
#ifdef _WIN32
  path += ".exe";
#endif
  return path;
}

std::filesystem::path roots_sampling_client_executable() {
  auto path = roots_sampling_client_target_dir() / "debug" /
              "rmcp_roots_sampling_client";
#ifdef _WIN32
  path += ".exe";
#endif
  return path;
}

std::filesystem::path task_lifecycle_client_executable() {
  auto path = task_lifecycle_client_target_dir() / "debug" /
              "rmcp_task_lifecycle_client";
#ifdef _WIN32
  path += ".exe";
#endif
  return path;
}

std::filesystem::path reverse_server_executable() {
  auto path = reverse_server_target_dir() / "debug" / "rmcp_reverse_server";
#ifdef _WIN32
  path += ".exe";
#endif
  return path;
}

std::string quote_path(const std::filesystem::path& path) {
  return "\"" + path.string() + "\"";
}

std::string quote_text(const std::string& value) { return "\"" + value + "\""; }

bool is_pagination_scenario() {
  return get_process_env("MCP_CONFORMANCE_SCENARIO").value_or(std::string()) ==
         "rmcp-pagination";
}

std::optional<std::string> pagination_cursor(const Json& params) {
  if (!params.is_object() || !params.contains("cursor") ||
      params.at("cursor").is_null()) {
    return std::nullopt;
  }
  return params.at("cursor").get<std::string>();
}

void write_invalid_cursor_response(
    const std::optional<mcp::protocol::RequestId>& id,
    httplib::Response& response) {
  const auto error_response = mcp::protocol::make_error_response(
      id, mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                    "invalid pagination cursor"));
  const auto serialized = mcp::protocol::serialize_response(error_response);
  require(serialized.has_value(),
          "invalid pagination cursor response should serialize");
  response.set_content(*serialized, "application/json");
}

std::string make_sse_message_event(std::uint64_t event_id,
                                   const std::string& payload) {
  return "id: " + std::to_string(event_id) + "\n" + "data: " + payload + "\n\n";
}

bool request_id_matches(const std::optional<mcp::protocol::RequestId>& id,
                        std::string_view expected) {
  if (!id.has_value()) {
    return false;
  }
  const auto* value = std::get_if<std::string>(&*id);
  return value != nullptr && *value == expected;
}

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

mcp::protocol::Task make_task(std::string task_id,
                              mcp::protocol::TaskStatus status,
                              std::string status_message = {}) {
  mcp::protocol::Task task;
  task.task_id = std::move(task_id);
  task.status = status;
  if (!status_message.empty()) {
    task.status_message = std::move(status_message);
  }
  task.created_at = "2026-05-25T00:00:00Z";
  task.last_updated_at = "2026-05-25T00:00:01Z";
  task.ttl = static_cast<std::int64_t>(300);
  task.poll_interval = static_cast<std::int64_t>(1);
  return task;
}

void configure_cargo_proxy() {
  for (std::string_view key :
       {"CARGO_HTTP_PROXY", "CARGO_HTTPS_PROXY", "HTTP_PROXY", "HTTPS_PROXY",
        "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"}) {
    unset_process_env(key);
  }

  const auto configured_proxy = get_process_env("CXXMCP_CARGO_PROXY");
  if (!configured_proxy.has_value() || configured_proxy->empty()) {
    return;
  }

  const std::string& proxy = *configured_proxy;
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

void build_conformance_server() {
  if (std::filesystem::exists(conformance_server_executable())) {
    return;
  }

  configure_cargo_proxy();
  set_process_env("CARGO_TARGET_DIR", conformance_target_dir().string());

  const std::string command = "cargo build --manifest-path " +
                              quote_path(conformance_manifest()) +
                              " --bin conformance-server";
  require(std::system(command.c_str()) == 0,
          "RMCP conformance server build should succeed");
}

void build_pagination_client() {
  if (std::filesystem::exists(pagination_client_executable())) {
    return;
  }

  configure_cargo_proxy();
  set_process_env("CARGO_TARGET_DIR", pagination_client_target_dir().string());

  const std::string command =
      "cargo build --manifest-path " + quote_path(pagination_client_manifest());
  require(std::system(command.c_str()) == 0,
          "RMCP pagination client build should succeed");
}

void build_completion_client() {
  if (std::filesystem::exists(completion_client_executable())) {
    return;
  }

  configure_cargo_proxy();
  set_process_env("CARGO_TARGET_DIR", completion_client_target_dir().string());

  const std::string command =
      "cargo build --manifest-path " + quote_path(completion_client_manifest());
  require(std::system(command.c_str()) == 0,
          "RMCP completion client build should succeed");
}

void build_roots_sampling_client() {
  if (std::filesystem::exists(roots_sampling_client_executable())) {
    return;
  }

  configure_cargo_proxy();
  set_process_env("CARGO_TARGET_DIR",
                  roots_sampling_client_target_dir().string());

  const std::string command = "cargo build --manifest-path " +
                              quote_path(roots_sampling_client_manifest());
  require(std::system(command.c_str()) == 0,
          "RMCP roots/sampling client build should succeed");
}

void build_task_lifecycle_client() {
  if (std::filesystem::exists(task_lifecycle_client_executable())) {
    return;
  }

  configure_cargo_proxy();
  set_process_env("CARGO_TARGET_DIR",
                  task_lifecycle_client_target_dir().string());

  const std::string command = "cargo build --manifest-path " +
                              quote_path(task_lifecycle_client_manifest());
  require(std::system(command.c_str()) == 0,
          "RMCP task lifecycle client build should succeed");
}

void build_reverse_server() {
  configure_cargo_proxy();
  set_process_env("CARGO_TARGET_DIR", reverse_server_target_dir().string());

  const std::string command =
      "cargo build --manifest-path " + quote_path(reverse_server_manifest());
  require(std::system(command.c_str()) == 0,
          "RMCP reverse server build should succeed");
}

std::uint16_t choose_loopback_port() {
#ifdef _WIN32
  const auto process_id = static_cast<unsigned>(GetCurrentProcessId());
#else
  const auto process_id = static_cast<unsigned>(::getpid());
#endif
  return static_cast<std::uint16_t>(45000 + (process_id % 10000));
}

bool wait_for_http_endpoint(std::uint16_t port) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(0, 100000);
    client.set_read_timeout(1, 0);
    client.set_write_timeout(0, 200000);
    const auto body =
        Json{{"jsonrpc", "2.0"},
             {"id", 1},
             {"method", mcp::protocol::InitializeMethod},
             {"params",
              Json{{"protocolVersion", mcp::protocol::McpProtocolVersion},
                   {"capabilities", Json::object()},
                   {"clientInfo",
                    Json{{"name", "cxxmcp-ready-probe"}, {"version", "1"}}}}}}
            .dump();
    if (client.Post("/mcp", body, "application/json") != nullptr) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

class RunningRmcpConformanceServer {
 public:
  explicit RunningRmcpConformanceServer(std::uint16_t port) : port_(port) {
    build_conformance_server();
    set_process_env("PORT", std::to_string(port_));
    set_process_env("RUST_LOG", "warn");

#ifdef _WIN32
    const auto command = quote_path(conformance_server_executable());
    std::wstring wide_command(command.begin(), command.end());
    std::vector<wchar_t> buffer(wide_command.begin(), wide_command.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process_)) {
      throw std::runtime_error("failed to start RMCP conformance server");
    }
#else
    pid_ = ::fork();
    if (pid_ < 0) {
      throw std::runtime_error("failed to fork RMCP conformance server");
    }
    if (pid_ == 0) {
      const auto executable = conformance_server_executable().string();
      ::execl(executable.c_str(), executable.c_str(), nullptr);
      _exit(127);
    }
#endif

    require(wait_for_http_endpoint(port_),
            "RMCP conformance server should become ready");
  }

  ~RunningRmcpConformanceServer() { stop(); }

  RunningRmcpConformanceServer(const RunningRmcpConformanceServer&) = delete;
  RunningRmcpConformanceServer& operator=(const RunningRmcpConformanceServer&) =
      delete;

  std::uint16_t port() const noexcept { return port_; }

 private:
  void stop() noexcept {
#ifdef _WIN32
    if (process_.hProcess != nullptr) {
      TerminateProcess(process_.hProcess, 0);
      WaitForSingleObject(process_.hProcess, 5000);
      CloseHandle(process_.hThread);
      CloseHandle(process_.hProcess);
      process_.hThread = nullptr;
      process_.hProcess = nullptr;
    }
#else
    if (pid_ > 0) {
      ::kill(pid_, SIGTERM);
      for (int attempt = 0; attempt < 50; ++attempt) {
        int status = 0;
        const auto waited = ::waitpid(pid_, &status, WNOHANG);
        if (waited == pid_) {
          pid_ = -1;
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      ::kill(pid_, SIGKILL);
      int status = 0;
      ::waitpid(pid_, &status, 0);
      pid_ = -1;
    }
#endif
  }

  std::uint16_t port_ = 0;
#ifdef _WIN32
  PROCESS_INFORMATION process_{};
#else
  pid_t pid_ = -1;
#endif
};

class RunningRmcpReverseServer {
 public:
  explicit RunningRmcpReverseServer(std::uint16_t port) : port_(port) {
    build_reverse_server();
    set_process_env("PORT", std::to_string(port_));
    set_process_env("RUST_LOG", "warn");

#ifdef _WIN32
    const auto command = quote_path(reverse_server_executable());
    std::wstring wide_command(command.begin(), command.end());
    std::vector<wchar_t> buffer(wide_command.begin(), wide_command.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process_)) {
      throw std::runtime_error("failed to start RMCP reverse server");
    }
#else
    pid_ = ::fork();
    if (pid_ < 0) {
      throw std::runtime_error("failed to fork RMCP reverse server");
    }
    if (pid_ == 0) {
      const auto executable = reverse_server_executable().string();
      ::execl(executable.c_str(), executable.c_str(), nullptr);
      _exit(127);
    }
#endif

    require(wait_for_http_endpoint(port_),
            "RMCP reverse server should become ready");
  }

  ~RunningRmcpReverseServer() { stop(); }

  RunningRmcpReverseServer(const RunningRmcpReverseServer&) = delete;
  RunningRmcpReverseServer& operator=(const RunningRmcpReverseServer&) = delete;

  std::uint16_t port() const noexcept { return port_; }

 private:
  void stop() noexcept {
#ifdef _WIN32
    if (process_.hProcess != nullptr) {
      TerminateProcess(process_.hProcess, 0);
      WaitForSingleObject(process_.hProcess, 5000);
      CloseHandle(process_.hThread);
      CloseHandle(process_.hProcess);
      process_.hThread = nullptr;
      process_.hProcess = nullptr;
    }
#else
    if (pid_ > 0) {
      ::kill(pid_, SIGTERM);
      for (int attempt = 0; attempt < 50; ++attempt) {
        int status = 0;
        const auto waited = ::waitpid(pid_, &status, WNOHANG);
        if (waited == pid_) {
          pid_ = -1;
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      ::kill(pid_, SIGKILL);
      int status = 0;
      ::waitpid(pid_, &status, 0);
      pid_ = -1;
    }
#endif
  }

  std::uint16_t port_ = 0;
#ifdef _WIN32
  PROCESS_INFORMATION process_{};
#else
  pid_t pid_ = -1;
#endif
};

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
        if (notification->method == mcp::protocol::ProgressNotificationMethod) {
          const auto params =
              mcp::protocol::progress_notification_params_from_json(
                  notification->params);
          if (!params.has_value()) {
            response.status = 400;
            response.set_content("invalid progress notification", "text/plain");
            return;
          }
        }
        if (notification->method ==
            mcp::protocol::CancelledNotificationMethod) {
          const auto params =
              mcp::protocol::cancelled_notification_params_from_json(
                  notification->params);
          if (!params.has_value()) {
            response.status = 400;
            response.set_content("invalid cancellation notification",
                                 "text/plain");
            return;
          }
        }
        if (notification->method ==
            mcp::protocol::RootsListChangedNotificationMethod) {
          if (!notification->params.empty() &&
              !notification->params.is_object()) {
            response.status = 400;
            response.set_content("invalid roots/list_changed notification",
                                 "text/plain");
            return;
          }
        }
        if (notification->method ==
            mcp::protocol::ElicitationCompleteNotificationMethod) {
          const auto params =
              mcp::protocol::elicitation_complete_notification_params_from_json(
                  notification->params);
          if (!params.has_value()) {
            response.status = 400;
            response.set_content("invalid elicitation complete notification",
                                 "text/plain");
            return;
          }
        }
        if (notification->method ==
            mcp::protocol::TasksStatusNotificationMethod) {
          const auto task = mcp::protocol::task_from_json(notification->params);
          if (!task.has_value()) {
            response.status = 400;
            response.set_content("invalid task status notification",
                                 "text/plain");
            return;
          }
        }
        response.status = 202;
        return;
      }

      if (const auto* rpc_response =
              std::get_if<mcp::protocol::JsonRpcResponse>(&*message)) {
        if (request_id_matches(rpc_response->id,
                               kElicitationDefaultsRequestId)) {
          {
            std::lock_guard lock(mutex_);
            elicitation_defaults_response_ = *rpc_response;
          }
          cv_.notify_all();
        }
        if (request_id_matches(rpc_response->id, kRootsRoundTripRequestId)) {
          {
            std::lock_guard lock(mutex_);
            roots_roundtrip_response_ = *rpc_response;
          }
          cv_.notify_all();
        }
        if (request_id_matches(rpc_response->id, kSamplingRoundTripRequestId)) {
          {
            std::lock_guard lock(mutex_);
            sampling_roundtrip_response_ = *rpc_response;
          }
          cv_.notify_all();
        }
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

      if (rpc_request->method != mcp::protocol::InitializeMethod &&
          request.has_header("Mcp-Session-Id")) {
        std::string session_id;
        {
          std::lock_guard lock(mutex_);
          session_id = session_id_;
        }
        if (session_id.empty() ||
            request.get_header_value("Mcp-Session-Id") != session_id) {
          response.status = 404;
          response.set_content("unknown session", "text/plain");
          return;
        }
      }

      if (rpc_request->method == mcp::protocol::InitializeMethod) {
        if (!rpc_request->params.contains("protocolVersion") ||
            !rpc_request->params.at("protocolVersion").is_string()) {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        "missing protocol version"));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "initialize error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }
        const auto requested_version =
            rpc_request->params.at("protocolVersion").get<std::string>();
        if (!mcp::protocol::is_supported_protocol_version(requested_version)) {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        "unsupported MCP protocol version(\"" +
                                            requested_version + "\")"));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "initialize error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        {
          std::lock_guard lock(mutex_);
          session_id_ = "mcp-session-1";
          response.set_header("Mcp-Session-Id", session_id_);
        }
        const auto negotiated_version =
            mcp::protocol::negotiate_protocol_version(requested_version);
        require(negotiated_version.has_value(),
                "interop server should validate protocol version before "
                "negotiating");
        const auto initialize_response = mcp::protocol::make_response(
            rpc_request->id,
            Json{
                {"protocolVersion", std::string(*negotiated_version)},
                {"capabilities",
                 Json{
                     {"tools", Json::object()},
                     {"prompts", Json::object()},
                     {"resources", Json{{"subscribe", true}}},
                     {"logging", Json::object()},
                     {"completions", Json::object()},
                 }},
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
        if (is_pagination_scenario()) {
          const auto cursor = pagination_cursor(rpc_request->params);
          Json tools = Json::array();
          Json result;
          if (!cursor.has_value()) {
            tools.push_back(mcp::protocol::tool_definition_to_json(
                mcp::protocol::ToolDefinition{
                    .name = "page-tool-one",
                    .description = "First paginated tool",
                    .input_schema = Json{{"type", "object"}},
                }));
            result = Json{{"tools", std::move(tools)},
                          {"nextCursor", "tools-page-2"}};
          } else if (*cursor == "tools-page-2") {
            tools.push_back(mcp::protocol::tool_definition_to_json(
                mcp::protocol::ToolDefinition{
                    .name = "page-tool-two",
                    .description = "Second paginated tool",
                    .input_schema = Json{{"type", "object"}},
                }));
            result = Json{{"tools", std::move(tools)}};
          } else {
            write_invalid_cursor_response(rpc_request->id, response);
            return;
          }
          const auto tool_response =
              mcp::protocol::make_response(rpc_request->id, std::move(result));
          const auto serialized =
              mcp::protocol::serialize_response(tool_response);
          require(serialized.has_value(),
                  "paginated tools/list response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        Json tools = Json::array(
            {mcp::protocol::tool_definition_to_json(
                 mcp::protocol::ToolDefinition{
                     .name = "test_simple_text",
                     .description = "Returns simple text content",
                     .input_schema =
                         Json{
                             {"type", "object"},
                             {"properties", Json::object()},
                         },
                     .streaming = false,
                 }),
             mcp::protocol::tool_definition_to_json(
                 mcp::protocol::ToolDefinition{
                     .name = "test_reconnection",
                     .description = "Exercises the RMCP SSE retry scenario",
                     .input_schema =
                         Json{
                             {"type", "object"},
                             {"properties", Json::object()},
                         },
                     .streaming = false,
                 })});
        if (get_process_env("MCP_CONFORMANCE_SCENARIO")
                .value_or(std::string()) ==
            "elicitation-sep1034-client-defaults") {
          tools.push_back(mcp::protocol::tool_definition_to_json(
              mcp::protocol::ToolDefinition{
                  .name = "test_elicitation_sep1034_defaults",
                  .description =
                      "Exercises the RMCP elicitation defaults client scenario",
                  .input_schema =
                      Json{
                          {"type", "object"},
                          {"properties", Json::object()},
                      },
                  .streaming = false,
              }));
        }
        if (get_process_env("MCP_CONFORMANCE_SCENARIO")
                .value_or(std::string()) == "rmcp-roots-sampling") {
          tools.push_back(mcp::protocol::tool_definition_to_json(
              mcp::protocol::ToolDefinition{
                  .name = "test_roots_roundtrip",
                  .description = "Exercises roots/list with an RMCP client",
                  .input_schema =
                      Json{{"type", "object"}, {"properties", Json::object()}},
                  .streaming = false,
              }));
          tools.push_back(mcp::protocol::tool_definition_to_json(
              mcp::protocol::ToolDefinition{
                  .name = "test_sampling_roundtrip",
                  .description =
                      "Exercises sampling/createMessage with an RMCP client",
                  .input_schema =
                      Json{{"type", "object"}, {"properties", Json::object()}},
                  .streaming = false,
              }));
        }
        if (get_process_env("MCP_CONFORMANCE_SCENARIO")
                .value_or(std::string()) == "rmcp-task-lifecycle") {
          tools.push_back(mcp::protocol::tool_definition_to_json(
              mcp::protocol::ToolDefinition{
                  .name = "test_progress_roundtrip",
                  .description =
                      "Exercises notifications/progress with an RMCP client",
                  .input_schema =
                      Json{{"type", "object"}, {"properties", Json::object()}},
                  .streaming = false,
              }));
        }
        const auto tool_response = mcp::protocol::make_response(
            rpc_request->id, Json{{"tools", std::move(tools)}});
        const auto serialized =
            mcp::protocol::serialize_response(tool_response);
        require(serialized.has_value(), "tools/list response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::ToolsCallMethod) {
        const auto tool_name =
            rpc_request->params.at("name").get<std::string>();
        if (tool_name != "test_simple_text" &&
            tool_name != "test_reconnection" &&
            tool_name != "test_elicitation_sep1034_defaults" &&
            tool_name != "test_roots_roundtrip" &&
            tool_name != "test_sampling_roundtrip" &&
            tool_name != "test_progress_roundtrip") {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::ToolNotFound,
                                        "tool not found"));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "tools/call error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        if (rpc_request->params.contains("task")) {
          const auto task_params =
              mcp::protocol::task_request_parameters_from_json(
                  rpc_request->params.at("task"));
          if (!task_params.has_value()) {
            const auto error_response = mcp::protocol::make_error_response(
                rpc_request->id, mcp::protocol::make_error(
                                     mcp::protocol::ErrorCode::InvalidParams,
                                     task_params.error().message));
            const auto serialized =
                mcp::protocol::serialize_response(error_response);
            require(serialized.has_value(),
                    "task creation error response should serialize");
            response.set_content(*serialized, "application/json");
            return;
          }

          const auto task_response = mcp::protocol::make_response(
              rpc_request->id,
              mcp::protocol::create_task_result_to_json(
                  mcp::protocol::CreateTaskResult{
                      make_task("task-created",
                                mcp::protocol::TaskStatus::Working,
                                "created from tools/call"),
                      std::nullopt}));
          const auto serialized =
              mcp::protocol::serialize_response(task_response);
          require(serialized.has_value(),
                  "task creation response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        if (tool_name == "test_elicitation_sep1034_defaults") {
          write_elicitation_defaults_sse_response(rpc_request->id, response);
          return;
        }
        if (tool_name == "test_roots_roundtrip") {
          write_roots_roundtrip_sse_response(rpc_request->id, response);
          return;
        }
        if (tool_name == "test_sampling_roundtrip") {
          write_sampling_roundtrip_sse_response(rpc_request->id, response);
          return;
        }
        if (tool_name == "test_progress_roundtrip") {
          write_progress_roundtrip_sse_response(rpc_request->id, response);
          return;
        }

        const auto result_text =
            tool_name == "test_reconnection"
                ? std::string("Reconnection scenario completed.")
                : std::string("This is a simple text response for testing.");
        mcp::protocol::ToolResult result;
        result.content.push_back(mcp::protocol::ContentBlock{
            .type = "text",
            .text = result_text,
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

      if (rpc_request->method == mcp::protocol::PromptsListMethod) {
        if (is_pagination_scenario()) {
          const auto cursor = pagination_cursor(rpc_request->params);
          Json prompts = Json::array();
          Json result;
          if (!cursor.has_value()) {
            prompts.push_back(
                mcp::protocol::prompt_to_json(mcp::protocol::Prompt{
                    .name = "page-prompt-one",
                    .description = "First paginated prompt",
                }));
            result = Json{{"prompts", std::move(prompts)},
                          {"nextCursor", "prompts-page-2"}};
          } else if (*cursor == "prompts-page-2") {
            prompts.push_back(
                mcp::protocol::prompt_to_json(mcp::protocol::Prompt{
                    .name = "page-prompt-two",
                    .description = "Second paginated prompt",
                }));
            result = Json{{"prompts", std::move(prompts)}};
          } else {
            write_invalid_cursor_response(rpc_request->id, response);
            return;
          }
          const auto prompt_response =
              mcp::protocol::make_response(rpc_request->id, std::move(result));
          const auto serialized =
              mcp::protocol::serialize_response(prompt_response);
          require(serialized.has_value(),
                  "paginated prompts/list response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        const auto list_response = mcp::protocol::make_response(
            rpc_request->id,
            mcp::protocol::prompts_list_result_to_json(
                mcp::protocol::PromptsListResult{
                    .prompts =
                        {
                            mcp::protocol::Prompt{
                                .title = "Summarize Prompt",
                                .name = "summarize",
                                .description = "Summarize supplied text",
                                .arguments =
                                    {
                                        mcp::protocol::PromptArgument{
                                            .name = "text",
                                            .description = "Text to summarize",
                                            .required = true,
                                        },
                                    },
                            },
                        },
                }));
        const auto serialized =
            mcp::protocol::serialize_response(list_response);
        require(serialized.has_value(),
                "prompts/list response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::PromptsGetMethod) {
        const auto prompt_name =
            rpc_request->params.at("name").get<std::string>();
        if (prompt_name != "summarize") {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        "prompt not found"));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "prompts/get error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        mcp::protocol::PromptsGetResult result;
        result.description = "Rendered summary prompt";
        result.messages.push_back(mcp::protocol::PromptMessage{
            .role = "user",
            .content =
                mcp::protocol::ContentBlock{
                    .type = "text",
                    .text =
                        "Summarize: " +
                        rpc_request->params.value("arguments", Json::object())
                            .value("text", std::string("")),
                    .data = Json::object(),
                },
        });
        const auto get_response = mcp::protocol::make_response(
            rpc_request->id, mcp::protocol::prompts_get_result_to_json(result));
        const auto serialized = mcp::protocol::serialize_response(get_response);
        require(serialized.has_value(),
                "prompts/get response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::ResourcesListMethod) {
        if (is_pagination_scenario()) {
          const auto cursor = pagination_cursor(rpc_request->params);
          Json resources = Json::array();
          Json result;
          if (!cursor.has_value()) {
            resources.push_back(
                mcp::protocol::resource_to_json(mcp::protocol::Resource{
                    .uri = "file:///page-one",
                    .name = "page-one",
                    .description = "First paginated resource",
                    .mime_type = "text/plain",
                }));
            result = Json{{"resources", std::move(resources)},
                          {"nextCursor", "resources-page-2"}};
          } else if (*cursor == "resources-page-2") {
            resources.push_back(
                mcp::protocol::resource_to_json(mcp::protocol::Resource{
                    .uri = "file:///page-two",
                    .name = "page-two",
                    .description = "Second paginated resource",
                    .mime_type = "text/plain",
                }));
            result = Json{{"resources", std::move(resources)}};
          } else {
            write_invalid_cursor_response(rpc_request->id, response);
            return;
          }
          const auto resource_response =
              mcp::protocol::make_response(rpc_request->id, std::move(result));
          const auto serialized =
              mcp::protocol::serialize_response(resource_response);
          require(serialized.has_value(),
                  "paginated resources/list response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        const auto list_response = mcp::protocol::make_response(
            rpc_request->id,
            mcp::protocol::resources_list_result_to_json(
                mcp::protocol::ResourcesListResult{
                    .resources =
                        {
                            mcp::protocol::Resource{
                                .title = "Readme",
                                .uri = "file:///workspace/README.md",
                                .name = "readme",
                                .description = "Project readme",
                                .mime_type = "text/markdown",
                            },
                        },
                }));
        const auto serialized =
            mcp::protocol::serialize_response(list_response);
        require(serialized.has_value(),
                "resources/list response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::ResourcesReadMethod) {
        const auto resource_uri =
            rpc_request->params.at("uri").get<std::string>();
        if (resource_uri != "file:///workspace/README.md") {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id, mcp::protocol::make_error(
                                   mcp::protocol::ErrorCode::ResourceNotFound,
                                   "resource not found"));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "resources/read error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        const auto read_response = mcp::protocol::make_response(
            rpc_request->id,
            mcp::protocol::resources_read_result_to_json(
                mcp::protocol::ResourcesReadResult{
                    .contents =
                        {
                            mcp::protocol::ResourceContents{
                                .uri = "file:///workspace/README.md",
                                .mime_type = "text/markdown",
                                .text = std::string("# cxxmcp"),
                            },
                        },
                }));
        const auto serialized =
            mcp::protocol::serialize_response(read_response);
        require(serialized.has_value(),
                "resources/read response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::ResourcesTemplatesListMethod) {
        if (is_pagination_scenario()) {
          const auto cursor = pagination_cursor(rpc_request->params);
          Json templates = Json::array();
          Json result;
          if (!cursor.has_value()) {
            templates.push_back(mcp::protocol::resource_template_to_json(
                mcp::protocol::ResourceTemplate{
                    .uri_template = "file:///page-one/{name}",
                    .name = "page-template-one",
                    .description = "First paginated resource template",
                    .mime_type = "text/plain",
                }));
            result = Json{{"resourceTemplates", std::move(templates)},
                          {"nextCursor", "templates-page-2"}};
          } else if (*cursor == "templates-page-2") {
            templates.push_back(mcp::protocol::resource_template_to_json(
                mcp::protocol::ResourceTemplate{
                    .uri_template = "file:///page-two/{name}",
                    .name = "page-template-two",
                    .description = "Second paginated resource template",
                    .mime_type = "text/plain",
                }));
            result = Json{{"resourceTemplates", std::move(templates)}};
          } else {
            write_invalid_cursor_response(rpc_request->id, response);
            return;
          }
          const auto template_response =
              mcp::protocol::make_response(rpc_request->id, std::move(result));
          const auto serialized =
              mcp::protocol::serialize_response(template_response);
          require(serialized.has_value(),
                  "paginated resource templates response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        const auto templates_response = mcp::protocol::make_response(
            rpc_request->id,
            mcp::protocol::resource_templates_list_result_to_json(
                mcp::protocol::ResourceTemplatesListResult{
                    .resource_templates =
                        {
                            mcp::protocol::ResourceTemplate{
                                .title = "Workspace File",
                                .uri_template = "file:///workspace/{path}",
                                .name = "workspace-file",
                                .description = "Workspace file template",
                                .mime_type = "text/plain",
                            },
                        },
                }));
        const auto serialized =
            mcp::protocol::serialize_response(templates_response);
        require(serialized.has_value(),
                "resources/templates/list response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::ResourcesSubscribeMethod ||
          rpc_request->method == mcp::protocol::ResourcesUnsubscribeMethod) {
        const auto resource_uri =
            rpc_request->params.at("uri").get<std::string>();
        if (resource_uri != "file:///workspace/README.md") {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id, mcp::protocol::make_error(
                                   mcp::protocol::ErrorCode::ResourceNotFound,
                                   "resource not found"));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "resources subscribe error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        const auto empty_response =
            mcp::protocol::make_response(rpc_request->id, Json::object());
        const auto serialized =
            mcp::protocol::serialize_response(empty_response);
        require(serialized.has_value(),
                "resource subscription response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::CompletionCompleteMethod) {
        const auto params =
            mcp::protocol::complete_params_from_json(rpc_request->params);
        if (!params.has_value()) {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        params.error().message));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "completion/complete error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        const auto completed = mcp::protocol::make_response(
            rpc_request->id,
            mcp::protocol::complete_result_to_json(
                mcp::protocol::CompleteResult{
                    .completion =
                        mcp::protocol::CompletionResult{
                            .values =
                                {
                                    params->argument.value + "-one",
                                    params->argument.value + "-two",
                                },
                            .total = 2,
                        },
                }));
        const auto serialized = mcp::protocol::serialize_response(completed);
        require(serialized.has_value(),
                "completion/complete response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::LoggingSetLevelMethod) {
        const auto params = mcp::protocol::logging_set_level_params_from_json(
            rpc_request->params);
        if (!params.has_value()) {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        params.error().message));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "logging/setLevel error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        const auto level_response =
            mcp::protocol::make_response(rpc_request->id, Json::object());
        const auto serialized =
            mcp::protocol::serialize_response(level_response);
        require(serialized.has_value(),
                "logging/setLevel response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::RootsListMethod) {
        const auto roots_response = mcp::protocol::make_response(
            rpc_request->id,
            mcp::protocol::roots_list_result_to_json(
                mcp::protocol::RootsListResult{
                    {mcp::protocol::Root{"file:///workspace", "workspace",
                                         Json{{"source", "cxxmcp-interop"}}}},
                    Json{{"observedBy", "conformance"}}}));
        const auto serialized =
            mcp::protocol::serialize_response(roots_response);
        require(serialized.has_value(), "roots/list response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::SamplingCreateMessageMethod) {
        const auto params =
            mcp::protocol::create_message_params_from_json(rpc_request->params);
        if (!params.has_value()) {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        params.error().message));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "sampling/createMessage error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        std::string prompt_text = "empty";
        if (!params->messages.empty()) {
          prompt_text = params->messages.front().content.text;
        }
        mcp::protocol::CreateMessageResult result;
        result.role = "assistant";
        result.content = mcp::protocol::ContentBlock::text_content(
            "sampled response to: " + prompt_text);
        result.model = "cxxmcp-conformance-model";
        result.stop_reason = "endTurn";
        result.meta = Json{{"sampledBy", "interop-fixture"}};
        const auto sampling_response = mcp::protocol::make_response(
            rpc_request->id,
            mcp::protocol::create_message_result_to_json(result));
        const auto serialized =
            mcp::protocol::serialize_response(sampling_response);
        require(serialized.has_value(),
                "sampling/createMessage response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::ElicitationCreateMethod) {
        const auto params =
            mcp::protocol::create_elicitation_request_param_from_json(
                rpc_request->params);
        if (!params.has_value()) {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        params.error().message));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "elicitation/create error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        mcp::protocol::CreateElicitationResult result;
        result.action = mcp::protocol::ElicitationAction::Accept;
        if (params->mode == mcp::protocol::ElicitationMode::Url) {
          result.content = Json{{"openedUrl", *params->url},
                                {"elicitationId", *params->elicitation_id}};
        } else {
          result.content =
              Json{{"username", "testuser"}, {"email", "test@example.com"}};
        }
        const auto elicitation_response = mcp::protocol::make_response(
            rpc_request->id,
            mcp::protocol::create_elicitation_result_to_json(result));
        const auto serialized =
            mcp::protocol::serialize_response(elicitation_response);
        require(serialized.has_value(),
                "elicitation/create response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::TasksListMethod) {
        if (is_pagination_scenario()) {
          const auto cursor = pagination_cursor(rpc_request->params);
          Json tasks = Json::array();
          Json result;
          if (!cursor.has_value()) {
            tasks.push_back(mcp::protocol::task_to_json(
                make_task("page-task-one", mcp::protocol::TaskStatus::Working,
                          "first paginated task")));
            result = Json{{"tasks", std::move(tasks)},
                          {"nextCursor", "tasks-page-2"},
                          {"total", 2}};
          } else if (*cursor == "tasks-page-2") {
            tasks.push_back(mcp::protocol::task_to_json(
                make_task("page-task-two", mcp::protocol::TaskStatus::Completed,
                          "second paginated task")));
            result = Json{{"tasks", std::move(tasks)}, {"total", 2}};
          } else {
            write_invalid_cursor_response(rpc_request->id, response);
            return;
          }
          const auto task_response =
              mcp::protocol::make_response(rpc_request->id, std::move(result));
          const auto serialized =
              mcp::protocol::serialize_response(task_response);
          require(serialized.has_value(),
                  "paginated tasks/list response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        const auto task_list_params_json = rpc_request->params.is_null()
                                               ? Json::object()
                                               : rpc_request->params;
        const auto params =
            mcp::protocol::task_list_params_from_json(task_list_params_json);
        if (!params.has_value()) {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        params.error().message));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "tasks/list error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        bool cancelled = false;
        {
          std::lock_guard lock(mutex_);
          cancelled = task_cancelled_;
        }
        mcp::protocol::TaskListResult result;
        result.tasks.push_back(make_task(
            "task-working", mcp::protocol::TaskStatus::Working, "running"));
        result.tasks.push_back(make_task("task-completed",
                                         mcp::protocol::TaskStatus::Completed,
                                         "completed"));
        result.tasks.push_back(make_task(
            "task-failed", mcp::protocol::TaskStatus::Failed, "failed"));
        result.tasks.push_back(make_task(
            "task-timeout", mcp::protocol::TaskStatus::Failed, "timed out"));
        result.tasks.push_back(make_task(
            "task-retained", mcp::protocol::TaskStatus::Completed, "retained"));
        result.tasks.push_back(
            make_task("task-cancelled",
                      cancelled ? mcp::protocol::TaskStatus::Cancelled
                                : mcp::protocol::TaskStatus::Working,
                      cancelled ? "cancelled" : "cancellable"));
        const auto task_response = mcp::protocol::make_response(
            rpc_request->id, mcp::protocol::task_list_result_to_json(result));
        const auto serialized =
            mcp::protocol::serialize_response(task_response);
        require(serialized.has_value(), "tasks/list response should serialize");
        response.set_content(*serialized, "application/json");
        return;
      }

      if (rpc_request->method == mcp::protocol::TasksGetMethod ||
          rpc_request->method == mcp::protocol::TasksCancelMethod ||
          rpc_request->method == mcp::protocol::TasksResultMethod) {
        const bool is_cancel =
            rpc_request->method == mcp::protocol::TasksCancelMethod;
        const bool is_result =
            rpc_request->method == mcp::protocol::TasksResultMethod;
        std::string task_id;
        std::optional<mcp::core::Error> task_id_error;
        if (is_cancel) {
          const auto params =
              mcp::protocol::task_cancel_params_from_json(rpc_request->params);
          if (params.has_value()) {
            task_id = params->task_id;
          } else {
            task_id_error = params.error();
          }
        } else {
          const auto params =
              mcp::protocol::task_get_params_from_json(rpc_request->params);
          if (params.has_value()) {
            task_id = params->task_id;
          } else {
            task_id_error = params.error();
          }
        }
        if (task_id_error.has_value()) {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        task_id_error->message));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "task detail error response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        if (is_cancel && task_id == "task-cancelled") {
          std::lock_guard lock(mutex_);
          task_cancelled_ = true;
        }

        const bool cancelled = [&]() {
          std::lock_guard lock(mutex_);
          return task_cancelled_;
        }();
        mcp::protocol::Task task;
        if (task_id == "task-working") {
          task =
              make_task(task_id, mcp::protocol::TaskStatus::Working, "running");
        } else if (task_id == "task-completed") {
          task = make_task(task_id, mcp::protocol::TaskStatus::Completed,
                           "completed");
        } else if (task_id == "task-failed") {
          task =
              make_task(task_id, mcp::protocol::TaskStatus::Failed, "failed");
        } else if (task_id == "task-timeout") {
          task = make_task(task_id, mcp::protocol::TaskStatus::Failed,
                           "timed out");
        } else if (task_id == "task-retained") {
          task = make_task(task_id, mcp::protocol::TaskStatus::Completed,
                           "retained");
        } else if (task_id == "task-cancelled") {
          task = make_task(task_id,
                           cancelled ? mcp::protocol::TaskStatus::Cancelled
                                     : mcp::protocol::TaskStatus::Working,
                           cancelled ? "cancelled" : "cancellable");
        } else {
          const auto error_response = mcp::protocol::make_error_response(
              rpc_request->id,
              mcp::protocol::make_error(mcp::protocol::ErrorCode::InvalidParams,
                                        "task not found"));
          const auto serialized =
              mcp::protocol::serialize_response(error_response);
          require(serialized.has_value(),
                  "task not found response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        if (is_result) {
          if (task.status != mcp::protocol::TaskStatus::Completed) {
            const auto error_response = mcp::protocol::make_error_response(
                rpc_request->id,
                mcp::protocol::make_error(
                    mcp::protocol::ErrorCode::InvalidParams,
                    "task result is only available for completed tasks"));
            const auto serialized =
                mcp::protocol::serialize_response(error_response);
            require(serialized.has_value(),
                    "task result error response should serialize");
            response.set_content(*serialized, "application/json");
            return;
          }
          const auto result_response = mcp::protocol::make_response(
              rpc_request->id,
              Json{{"content",
                    Json::array({Json{{"type", "text"},
                                      {"text", task_id + " result"}}})}});
          const auto serialized =
              mcp::protocol::serialize_response(result_response);
          require(serialized.has_value(),
                  "tasks/result response should serialize");
          response.set_content(*serialized, "application/json");
          return;
        }

        const auto task_response = mcp::protocol::make_response(
            rpc_request->id, mcp::protocol::task_to_json(task));
        const auto serialized =
            mcp::protocol::serialize_response(task_response);
        require(serialized.has_value(),
                "task detail response should serialize");
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

    server_.Delete("/mcp", [this](const httplib::Request& request,
                                  httplib::Response& response) {
      if (!request.has_header("Mcp-Session-Id")) {
        response.status = 400;
        return;
      }
      std::lock_guard lock(mutex_);
      if (session_id_.empty() ||
          request.get_header_value("Mcp-Session-Id") != session_id_) {
        response.status = 404;
        return;
      }
      session_id_.clear();
      response.status = 204;
    });

    port_ = static_cast<std::uint16_t>(server_.bind_to_any_port("127.0.0.1"));
    require(port_ > 0, "failed to bind interop server");
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  ~RunningInteropServer() {
    stopped_.store(true);
    cv_.notify_all();
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  std::uint16_t port() const { return port_; }

 private:
  std::string make_elicitation_defaults_request_payload() {
    const auto request = mcp::protocol::make_request(
        mcp::protocol::ElicitationCreateMethod,
        std::string(kElicitationDefaultsRequestId),
        Json{
            {"mode", "form"},
            {"message", "Please provide values (all have defaults)"},
            {"requestedSchema",
             Json{
                 {"type", "object"},
                 {"properties",
                  Json{
                      {"name", Json{{"type", "string"},
                                    {"description", "User's name"},
                                    {"default", "John Doe"}}},
                      {"age", Json{{"type", "integer"},
                                   {"description", "User's age"},
                                   {"default", 30}}},
                      {"score", Json{{"type", "number"},
                                     {"description", "User's score"},
                                     {"default", 95.5}}},
                      {"status",
                       Json{{"type", "string"},
                            {"description", "User's status"},
                            {"enum",
                             Json::array({"active", "inactive", "pending"})},
                            {"default", "active"}}},
                      {"verified",
                       Json{{"type", "boolean"},
                            {"description", "Whether user is verified"},
                            {"default", true}}},
                  }},
             }},
        });
    const auto serialized = mcp::protocol::serialize_request(request);
    require(serialized.has_value(),
            "elicitation/create request should serialize");
    return *serialized;
  }

  std::string wait_for_elicitation_defaults_content() {
    std::optional<mcp::protocol::JsonRpcResponse> response;
    {
      std::unique_lock lock(mutex_);
      const bool received =
          cv_.wait_for(lock, std::chrono::seconds(10), [this]() {
            return stopped_.load() ||
                   elicitation_defaults_response_.has_value();
          });
      require(received && elicitation_defaults_response_.has_value(),
              "RMCP client should respond to elicitation/create");
      response = elicitation_defaults_response_;
      elicitation_defaults_response_.reset();
    }

    require(response->has_result(),
            "elicitation/create response should contain a result");
    const auto result =
        mcp::protocol::create_elicitation_result_from_json(*response->result);
    require(result.has_value(),
            "elicitation/create response result should parse");
    require(result->action == mcp::protocol::ElicitationAction::Accept,
            "elicitation/create response should accept defaults");
    require(result->content.has_value() && result->content->is_object(),
            "elicitation/create response should include content");
    const auto& content = *result->content;
    require(content.value("name", std::string()) == "John Doe",
            "elicitation default name mismatch");
    require(content.value("age", 0) == 30, "elicitation default age mismatch");
    require(content.value("score", 0.0) == 95.5,
            "elicitation default score mismatch");
    require(content.value("status", std::string()) == "active",
            "elicitation default status mismatch");
    require(content.value("verified", false),
            "elicitation default verified mismatch");

    return "Elicitation defaults scenario completed with content: " +
           content.dump();
  }

  std::string make_roots_roundtrip_request_payload() {
    const auto request = mcp::protocol::make_request(
        mcp::protocol::RootsListMethod, std::string(kRootsRoundTripRequestId));
    const auto serialized = mcp::protocol::serialize_request(request);
    require(serialized.has_value(), "roots/list request should serialize");
    return *serialized;
  }

  std::string wait_for_roots_roundtrip_content() {
    std::optional<mcp::protocol::JsonRpcResponse> response;
    {
      std::unique_lock lock(mutex_);
      const bool received =
          cv_.wait_for(lock, std::chrono::seconds(10), [this]() {
            return stopped_.load() || roots_roundtrip_response_.has_value();
          });
      require(received && roots_roundtrip_response_.has_value(),
              "RMCP client should respond to roots/list");
      response = roots_roundtrip_response_;
      roots_roundtrip_response_.reset();
    }

    require(response->has_result(),
            "roots/list response should contain a result");
    const auto result =
        mcp::protocol::roots_list_result_from_json(*response->result);
    require(result.has_value(), "roots/list response result should parse");
    require(result->roots.size() == 1, "roots/list should return one root");
    require(result->roots.front().uri == "file:///rmcp-root",
            "roots/list RMCP root uri mismatch");
    require(result->roots.front().name == "rmcp-root",
            "roots/list RMCP root name mismatch");

    return "Roots round-trip completed for " + result->roots.front().uri;
  }

  std::string make_sampling_roundtrip_request_payload() {
    mcp::protocol::CreateMessageParams params;
    params.messages.push_back(mcp::protocol::SamplingMessage::text(
        "user", "Describe the RMCP roots fixture"));
    params.max_tokens = 64;
    const auto request = mcp::protocol::make_request(
        mcp::protocol::SamplingCreateMessageMethod,
        std::string(kSamplingRoundTripRequestId),
        mcp::protocol::create_message_params_to_json(params));
    const auto serialized = mcp::protocol::serialize_request(request);
    require(serialized.has_value(),
            "sampling/createMessage request should serialize");
    return *serialized;
  }

  std::string wait_for_sampling_roundtrip_content() {
    std::optional<mcp::protocol::JsonRpcResponse> response;
    {
      std::unique_lock lock(mutex_);
      const bool received =
          cv_.wait_for(lock, std::chrono::seconds(10), [this]() {
            return stopped_.load() || sampling_roundtrip_response_.has_value();
          });
      require(received && sampling_roundtrip_response_.has_value(),
              "RMCP client should respond to sampling/createMessage");
      response = sampling_roundtrip_response_;
      sampling_roundtrip_response_.reset();
    }

    require(response->has_result(),
            "sampling/createMessage response should contain a result");
    const auto result =
        mcp::protocol::create_message_result_from_json(*response->result);
    require(result.has_value(),
            "sampling/createMessage response result should parse");
    require(result->model == "rmcp-fixture-model",
            "sampling/createMessage RMCP model mismatch");
    require(
        result->content.text == "RMCP sampled: Describe the RMCP roots fixture",
        "sampling/createMessage RMCP text mismatch");

    return "Sampling round-trip completed with model " + result->model;
  }

  void write_server_request_tool_response(
      mcp::protocol::RequestId tool_call_id, std::string request_payload,
      std::function<std::string()> wait_for_result,
      httplib::Response& response) {
    response.set_chunked_content_provider(
        "text/event-stream",
        [tool_call_id = std::move(tool_call_id),
         request_payload = std::move(request_payload),
         wait_for_result = std::move(wait_for_result), sent_request = false,
         sent_response = false](std::size_t, httplib::DataSink& sink) mutable {
          if (!sent_request) {
            const auto event = make_sse_message_event(1, request_payload);
            if (!sink.write(event.data(), event.size())) {
              sink.done();
              return false;
            }
            sent_request = true;
            return true;
          }

          if (!sent_response) {
            mcp::protocol::ToolResult result;
            result.content.push_back(mcp::protocol::ContentBlock{
                .type = "text",
                .text = wait_for_result(),
                .data = Json::object(),
            });
            const auto tool_response = mcp::protocol::make_response(
                tool_call_id, mcp::protocol::tool_result_to_json(result));
            const auto serialized =
                mcp::protocol::serialize_response(tool_response);
            require(serialized.has_value(),
                    "server-request tools/call response should serialize");
            const auto event = make_sse_message_event(2, *serialized);
            if (!sink.write(event.data(), event.size())) {
              sink.done();
              return false;
            }
            sent_response = true;
          }

          sink.done();
          return false;
        });
  }

  void write_roots_roundtrip_sse_response(mcp::protocol::RequestId tool_call_id,
                                          httplib::Response& response) {
    {
      std::lock_guard lock(mutex_);
      roots_roundtrip_response_.reset();
    }
    write_server_request_tool_response(
        std::move(tool_call_id), make_roots_roundtrip_request_payload(),
        [this]() { return wait_for_roots_roundtrip_content(); }, response);
  }

  void write_sampling_roundtrip_sse_response(
      mcp::protocol::RequestId tool_call_id, httplib::Response& response) {
    {
      std::lock_guard lock(mutex_);
      sampling_roundtrip_response_.reset();
    }
    write_server_request_tool_response(
        std::move(tool_call_id), make_sampling_roundtrip_request_payload(),
        [this]() { return wait_for_sampling_roundtrip_content(); }, response);
  }

  void write_progress_roundtrip_sse_response(
      mcp::protocol::RequestId tool_call_id, httplib::Response& response) {
    response.set_chunked_content_provider(
        "text/event-stream",
        [tool_call_id = std::move(tool_call_id), sent_progress = false,
         sent_response = false](std::size_t, httplib::DataSink& sink) mutable {
          if (!sent_progress) {
            const auto notification = mcp::protocol::make_notification(
                mcp::protocol::ProgressNotificationMethod,
                mcp::protocol::progress_notification_params_to_json(
                    mcp::protocol::ProgressNotificationParams{
                        .progress_token = std::string("rmcp-progress-token"),
                        .progress = 0.5,
                        .total = 1.0,
                        .message = "rmcp progress",
                    }));
            const auto serialized =
                mcp::protocol::serialize_notification(notification);
            require(serialized.has_value(),
                    "progress notification should serialize");
            const auto event = make_sse_message_event(1, *serialized);
            if (!sink.write(event.data(), event.size())) {
              sink.done();
              return false;
            }
            sent_progress = true;
            return true;
          }

          if (!sent_response) {
            mcp::protocol::ToolResult result;
            result.content.push_back(mcp::protocol::ContentBlock{
                .type = "text",
                .text = "Progress round-trip completed",
                .data = Json::object(),
            });
            const auto tool_response = mcp::protocol::make_response(
                tool_call_id, mcp::protocol::tool_result_to_json(result));
            const auto serialized =
                mcp::protocol::serialize_response(tool_response);
            require(serialized.has_value(),
                    "progress tools/call response should serialize");
            const auto event = make_sse_message_event(2, *serialized);
            if (!sink.write(event.data(), event.size())) {
              sink.done();
              return false;
            }
            sent_response = true;
          }

          sink.done();
          return false;
        });
  }

  void write_elicitation_defaults_sse_response(
      mcp::protocol::RequestId tool_call_id, httplib::Response& response) {
    const auto request_payload = make_elicitation_defaults_request_payload();
    {
      std::lock_guard lock(mutex_);
      elicitation_defaults_response_.reset();
    }
    response.set_chunked_content_provider(
        "text/event-stream",
        [this, tool_call_id = std::move(tool_call_id), request_payload,
         sent_request = false,
         sent_response = false](std::size_t, httplib::DataSink& sink) mutable {
          if (!sent_request) {
            const auto event = make_sse_message_event(1, request_payload);
            if (!sink.write(event.data(), event.size())) {
              sink.done();
              return false;
            }
            sent_request = true;
            return true;
          }

          if (!sent_response) {
            const auto result_text = wait_for_elicitation_defaults_content();
            mcp::protocol::ToolResult result;
            result.content.push_back(mcp::protocol::ContentBlock{
                .type = "text",
                .text = result_text,
                .data = Json::object(),
            });
            const auto tool_response = mcp::protocol::make_response(
                tool_call_id, mcp::protocol::tool_result_to_json(result));
            const auto serialized =
                mcp::protocol::serialize_response(tool_response);
            require(serialized.has_value(),
                    "elicitation tools/call response should serialize");
            const auto event = make_sse_message_event(2, *serialized);
            if (!sink.write(event.data(), event.size())) {
              sink.done();
              return false;
            }
            sent_response = true;
          }

          sink.done();
          return false;
        });
  }

  httplib::Server server_;
  std::thread thread_;
  std::atomic<bool> stopped_{false};
  std::condition_variable cv_;
  mutable std::mutex mutex_;
  std::string session_id_;
  std::optional<mcp::protocol::JsonRpcResponse> elicitation_defaults_response_;
  std::optional<mcp::protocol::JsonRpcResponse> roots_roundtrip_response_;
  std::optional<mcp::protocol::JsonRpcResponse> sampling_roundtrip_response_;
  bool task_cancelled_ = false;
  std::uint16_t port_ = 0;
};

mcp::core::Error canonical_invalid_cursor_error(std::string family) {
  return mcp::core::Error{
      static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
      "invalid canonical pagination cursor", std::move(family), "protocol"};
}

mcp::protocol::ToolDefinition canonical_tool(std::string name,
                                             std::string description) {
  return mcp::protocol::ToolDefinition{
      .name = std::move(name),
      .description = std::move(description),
      .input_schema = Json{{"type", "object"}, {"properties", Json::object()}},
  };
}

bool wait_for_canonical_http_server(std::uint16_t port) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(0, 100000);
    client.set_read_timeout(1, 0);
    const auto response =
        client.Get("/mcp", httplib::Headers{{"Accept", "text/event-stream"}});
    if (response != nullptr) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

class RunningCanonicalServerPeerHttpServer {
 public:
  RunningCanonicalServerPeerHttpServer()
      : port_(static_cast<std::uint16_t>(choose_loopback_port() + 37)) {
    auto peer =
        mcp::ServerPeer::builder()
            .name("cxxmcp-canonical-rmcp-interop")
            .version("1")
            .tool(canonical_tool("page-tool-one", "First paginated tool"),
                  [](const mcp::server::ToolContext&) {
                    mcp::protocol::ToolResult result;
                    result.content.push_back(mcp::protocol::ContentBlock{
                        .type = "text",
                        .text = "canonical page tool one",
                        .data = Json::object(),
                    });
                    return result;
                  })
            .on_tools_list(
                [](const mcp::protocol::PaginatedRequestParams& params,
                   const mcp::server::SessionContext&)
                    -> mcp::core::Result<mcp::protocol::ToolsListResult> {
                  mcp::protocol::ToolsListResult result;
                  if (!params.cursor.has_value()) {
                    result.tools.push_back(canonical_tool(
                        "page-tool-one", "First paginated tool"));
                    result.next_cursor = "tools-page-2";
                    return mcp::core::Result<mcp::protocol::ToolsListResult>{
                        result};
                  }
                  if (*params.cursor == "tools-page-2") {
                    result.tools.push_back(canonical_tool(
                        "page-tool-two", "Second paginated tool"));
                    return mcp::core::Result<mcp::protocol::ToolsListResult>{
                        result};
                  }
                  return mcp::core::unexpected(
                      canonical_invalid_cursor_error("tools"));
                })
            .on_prompts_list(
                [](const mcp::protocol::PaginatedRequestParams& params,
                   const mcp::server::SessionContext&)
                    -> mcp::core::Result<mcp::protocol::PromptsListResult> {
                  mcp::protocol::PromptsListResult result;
                  if (!params.cursor.has_value()) {
                    result.prompts.push_back(mcp::protocol::Prompt{
                        .name = "page-prompt-one",
                        .description = "First paginated prompt",
                    });
                    result.next_cursor = "prompts-page-2";
                    return mcp::core::Result<mcp::protocol::PromptsListResult>{
                        result};
                  }
                  if (*params.cursor == "prompts-page-2") {
                    result.prompts.push_back(mcp::protocol::Prompt{
                        .name = "page-prompt-two",
                        .description = "Second paginated prompt",
                    });
                    return mcp::core::Result<mcp::protocol::PromptsListResult>{
                        result};
                  }
                  return mcp::core::unexpected(
                      canonical_invalid_cursor_error("prompts"));
                })
            .on_resources_list([](const mcp::protocol::PaginatedRequestParams&
                                      params,
                                  const mcp::server::SessionContext&)
                                   -> mcp::core::Result<
                                       mcp::protocol::ResourcesListResult> {
              mcp::protocol::ResourcesListResult result;
              if (!params.cursor.has_value()) {
                result.resources.push_back(mcp::protocol::Resource{
                    .uri = "file:///page-one",
                    .name = "page-resource-one",
                    .description = "First paginated resource",
                });
                result.next_cursor = "resources-page-2";
                return mcp::core::Result<mcp::protocol::ResourcesListResult>{
                    result};
              }
              if (*params.cursor == "resources-page-2") {
                result.resources.push_back(mcp::protocol::Resource{
                    .uri = "file:///page-two",
                    .name = "page-resource-two",
                    .description = "Second paginated resource",
                });
                return mcp::core::Result<mcp::protocol::ResourcesListResult>{
                    result};
              }
              return mcp::core::unexpected(
                  canonical_invalid_cursor_error("resources"));
            })
            .on_resource_templates_list(
                [](const mcp::protocol::PaginatedRequestParams& params,
                   const mcp::server::SessionContext&)
                    -> mcp::core::Result<
                        mcp::protocol::ResourceTemplatesListResult> {
                  mcp::protocol::ResourceTemplatesListResult result;
                  if (!params.cursor.has_value()) {
                    result.resource_templates.push_back(
                        mcp::protocol::ResourceTemplate{
                            .uri_template = "file:///page-one/{id}",
                            .name = "page-template-one",
                            .description = "First paginated resource template",
                        });
                    result.next_cursor = "templates-page-2";
                    return mcp::core::Result<
                        mcp::protocol::ResourceTemplatesListResult>{result};
                  }
                  if (*params.cursor == "templates-page-2") {
                    result.resource_templates.push_back(
                        mcp::protocol::ResourceTemplate{
                            .uri_template = "file:///page-two/{id}",
                            .name = "page-template-two",
                            .description = "Second paginated resource template",
                        });
                    return mcp::core::Result<
                        mcp::protocol::ResourceTemplatesListResult>{result};
                  }
                  return mcp::core::unexpected(
                      canonical_invalid_cursor_error("templates"));
                })
            .on_task_list(
                [](const mcp::protocol::TaskListParams& params,
                   const mcp::server::SessionContext&)
                    -> mcp::core::Result<mcp::protocol::TaskListResult> {
                  mcp::protocol::TaskListResult result;
                  if (!params.cursor.has_value()) {
                    result.tasks.push_back(make_task(
                        "page-task-one", mcp::protocol::TaskStatus::Working,
                        "canonical task page one"));
                    result.next_cursor = "tasks-page-2";
                    return mcp::core::Result<mcp::protocol::TaskListResult>{
                        result};
                  }
                  if (*params.cursor == "tasks-page-2") {
                    result.tasks.push_back(make_task(
                        "page-task-two", mcp::protocol::TaskStatus::Completed,
                        "canonical task page two"));
                    result.total = 2;
                    return mcp::core::Result<mcp::protocol::TaskListResult>{
                        result};
                  }
                  return mcp::core::unexpected(
                      canonical_invalid_cursor_error("tasks"));
                })
            .build();
    require(peer.has_value(), "canonical ServerPeer should build");

    auto transport =
        std::make_unique<mcp::transport::StreamableHttpServerTransport>(
            mcp::transport::StreamableHttpServerTransportOptions{
                .listen_host = "127.0.0.1",
                .listen_port = port_,
                .path = "/mcp",
            });
    auto running = mcp::serve(std::move(*peer), std::move(transport));
    require(running.has_value(), "canonical ServerPeer service should start");
    running_.emplace(std::move(*running));
    require(wait_for_canonical_http_server(port_),
            "canonical ServerPeer HTTP endpoint should become reachable");
  }

  ~RunningCanonicalServerPeerHttpServer() {
    if (running_.has_value()) {
      (void)running_->stop();
    }
  }

  std::uint16_t port() const { return port_; }

 private:
  std::uint16_t port_ = 0;
  std::optional<mcp::RunningService<mcp::RoleServer>> running_;
};

Json rpc_request(std::int64_t id, std::string method,
                 Json params = Json::object()) {
  Json request = Json::object();
  request["jsonrpc"] = "2.0";
  request["id"] = id;
  request["method"] = std::move(method);
  if (!params.empty()) {
    request["params"] = std::move(params);
  }
  return request;
}

Json rpc_notification(std::string method, Json params = Json::object()) {
  Json notification = Json::object();
  notification["jsonrpc"] = "2.0";
  notification["method"] = std::move(method);
  if (!params.empty()) {
    notification["params"] = std::move(params);
  }
  return notification;
}

Json post_json_rpc(std::uint16_t port, const Json& request) {
  httplib::Client client("127.0.0.1", port);
  const auto response = client.Post("/mcp", request.dump(), "application/json");
  require(response != nullptr, "HTTP JSON-RPC POST should receive a response");
  require(response->status == 200, "HTTP JSON-RPC response status mismatch");
  return Json::parse(response->body);
}

httplib::Result post_json_rpc_with_session(std::uint16_t port,
                                           const Json& request,
                                           const std::string& session_id) {
  httplib::Client client("127.0.0.1", port);
  httplib::Headers headers{
      {"Accept", "application/json"},
      {"Content-Type", "application/json"},
      {"MCP-Protocol-Version", mcp::protocol::McpProtocolVersion},
      {"Mcp-Session-Id", session_id},
  };
  if (request.contains("method") && request.at("method").is_string()) {
    headers.emplace("Mcp-Method", request.at("method").get<std::string>());
  }
  return client.Post("/mcp", std::move(headers), request.dump(),
                     "application/json");
}

void post_json_rpc_notification(std::uint16_t port, const Json& notification) {
  httplib::Client client("127.0.0.1", port);
  const auto response =
      client.Post("/mcp", notification.dump(), "application/json");
  require(response != nullptr, "HTTP JSON-RPC notification should respond");
  require(response->status == 202,
          "HTTP JSON-RPC notification response status mismatch");
}

void post_json_rpc_initialized_notification(std::uint16_t port,
                                            const std::string& session_id) {
  const auto initialized = post_json_rpc_with_session(
      port, rpc_notification(mcp::protocol::InitializedMethod), session_id);
  require(initialized != nullptr,
          "HTTP initialized notification should receive a response");
  require(initialized->status == 202,
          "HTTP initialized notification response status mismatch");
}

Json expect_result(const Json& response) {
  if (!response.contains("result")) {
    throw std::runtime_error("JSON-RPC response should have result: " +
                             response.dump());
  }
  if (response.contains("error")) {
    throw std::runtime_error("JSON-RPC response should not error: " +
                             response.dump());
  }
  return response.at("result");
}

Json expect_error(const Json& response, int code) {
  if (!response.contains("error")) {
    throw std::runtime_error("JSON-RPC response should have error: " +
                             response.dump());
  }
  if (response.at("error").at("code") != code) {
    throw std::runtime_error("JSON-RPC error code mismatch: " +
                             response.dump());
  }
  return response.at("error");
}

void test_cxxmcp_streamable_http_session_stale_matrix() {
  RunningInteropServer server;
  const auto port = server.port();

  httplib::Client client("127.0.0.1", port);
  const auto initialized = client.Post(
      "/mcp",
      rpc_request(
          1, mcp::protocol::InitializeMethod,
          Json{{"protocolVersion", mcp::protocol::McpProtocolVersion},
               {"clientInfo", Json{{"name", "session-test"}, {"version", "1"}}},
               {"capabilities", Json::object()}})
          .dump(),
      "application/json");
  require(initialized != nullptr, "initialize should return a response");
  require(initialized->status == 200, "initialize should return HTTP 200");
  require(initialized->has_header("Mcp-Session-Id"),
          "initialize should issue a session id");
  const auto session_id = initialized->get_header_value("Mcp-Session-Id");
  require(!session_id.empty(), "session id must not be empty");
  post_json_rpc_initialized_notification(port, session_id);

  httplib::Client wrong_get("127.0.0.1", port);
  wrong_get.set_read_timeout(std::chrono::milliseconds(100));
  const auto wrong_session =
      wrong_get.Get("/mcp", httplib::Headers{
                                {"Mcp-Session-Id", "missing-session"},
                                {"Accept", "text/event-stream"},
                            });
  require(wrong_session != nullptr,
          "wrong-session SSE request should return a response");
  require(wrong_session->status == 404,
          "wrong-session SSE request should be rejected");

  const auto tool_list = post_json_rpc_with_session(
      port, rpc_request(2, mcp::protocol::ToolsListMethod), session_id);
  require(tool_list != nullptr, "session POST should return a response");
  require(tool_list->status == 200, "valid session POST should succeed");

  const auto deleted =
      client.Delete("/mcp", httplib::Headers{{"Mcp-Session-Id", session_id}});
  require(deleted != nullptr, "session DELETE should return a response");
  require(deleted->status == 204, "session DELETE should terminate session");

  const auto stale_post = post_json_rpc_with_session(
      port, rpc_request(3, mcp::protocol::ToolsListMethod), session_id);
  require(stale_post != nullptr, "stale session POST should return a response");
  require(stale_post->status == 404, "stale session POST should be rejected");

  httplib::Client stale_get("127.0.0.1", port);
  stale_get.set_read_timeout(std::chrono::milliseconds(100));
  const auto stale_stream =
      stale_get.Get("/mcp", httplib::Headers{
                                {"Mcp-Session-Id", session_id},
                                {"Accept", "text/event-stream"},
                            });
  require(stale_stream != nullptr,
          "stale session SSE request should return a response");
  require(stale_stream->status == 404,
          "stale session SSE request should be rejected");
}

void test_cxxmcp_streamable_http_interop_matrix_core_methods() {
  RunningInteropServer server;
  const auto port = server.port();

  const auto initialized = expect_result(post_json_rpc(
      port,
      rpc_request(
          1, mcp::protocol::InitializeMethod,
          Json{{"protocolVersion", mcp::protocol::McpProtocolVersion},
               {"clientInfo", Json{{"name", "cxxmcp-test"}, {"version", "1"}}},
               {"capabilities", Json::object()}})));
  require(initialized.at("capabilities").contains("tools"),
          "initialize should advertise tools");
  require(initialized.at("capabilities").contains("prompts"),
          "initialize should advertise prompts");
  require(initialized.at("capabilities").contains("resources"),
          "initialize should advertise resources");
  require(initialized.at("capabilities").contains("logging"),
          "initialize should advertise logging");
  require(initialized.at("capabilities").contains("completions"),
          "initialize should advertise completions");

  const auto tools =
      expect_result(
          post_json_rpc(port, rpc_request(2, mcp::protocol::ToolsListMethod)))
          .at("tools");
  require(tools.size() == 2, "tools/list should expose two default tools");
  require(tools.at(0).at("name") == "test_simple_text",
          "tools/list tool name mismatch");

  const auto tool_result = expect_result(
      post_json_rpc(port, rpc_request(3, mcp::protocol::ToolsCallMethod,
                                      Json{{"name", "test_simple_text"}})));
  require(tool_result.at("content").at(0).at("text") ==
              "This is a simple text response for testing.",
          "tools/call text result mismatch");

  expect_error(
      post_json_rpc(port, rpc_request(4, mcp::protocol::ToolsCallMethod,
                                      Json{{"name", "missing_tool"}})),
      static_cast<int>(mcp::protocol::ErrorCode::ToolNotFound));

  const auto prompts =
      expect_result(
          post_json_rpc(port, rpc_request(5, mcp::protocol::PromptsListMethod)))
          .at("prompts");
  require(prompts.size() == 1, "prompts/list should expose one prompt");
  require(prompts.at(0).at("name") == "summarize",
          "prompts/list prompt name mismatch");

  const auto prompt_result = expect_result(post_json_rpc(
      port, rpc_request(6, mcp::protocol::PromptsGetMethod,
                        Json{{"name", "summarize"},
                             {"arguments", Json{{"text", "hello"}}}})));
  require(prompt_result.at("messages").at(0).at("content").at("text") ==
              "Summarize: hello",
          "prompts/get message text mismatch");

  const auto resources =
      expect_result(
          post_json_rpc(port,
                        rpc_request(7, mcp::protocol::ResourcesListMethod)))
          .at("resources");
  require(resources.size() == 1, "resources/list should expose one resource");
  require(resources.at(0).at("uri") == "file:///workspace/README.md",
          "resources/list uri mismatch");

  const auto read_result = expect_result(post_json_rpc(
      port, rpc_request(8, mcp::protocol::ResourcesReadMethod,
                        Json{{"uri", "file:///workspace/README.md"}})));
  require(read_result.at("contents").at(0).at("text") == "# cxxmcp",
          "resources/read text mismatch");

  const auto templates =
      expect_result(
          post_json_rpc(
              port,
              rpc_request(9, mcp::protocol::ResourcesTemplatesListMethod)))
          .at("resourceTemplates");
  require(templates.size() == 1,
          "resources/templates/list should expose one template");
  require(templates.at(0).at("uriTemplate") == "file:///workspace/{path}",
          "resources/templates/list uriTemplate mismatch");

  expect_result(post_json_rpc(
      port, rpc_request(10, mcp::protocol::ResourcesSubscribeMethod,
                        Json{{"uri", "file:///workspace/README.md"}})));
  expect_result(post_json_rpc(
      port, rpc_request(11, mcp::protocol::ResourcesUnsubscribeMethod,
                        Json{{"uri", "file:///workspace/README.md"}})));

  const auto completion_result = expect_result(post_json_rpc(
      port,
      rpc_request(
          12, mcp::protocol::CompletionCompleteMethod,
          Json{{"ref", Json{{"type", "ref/prompt"}, {"name", "summarize"}}},
               {"argument", Json{{"name", "text"}, {"value", "he"}}}})));
  require(completion_result.at("completion").at("values").at(0) == "he-one",
          "completion/complete first value mismatch");
  require(completion_result.at("completion").at("total") == 2,
          "completion/complete total mismatch");

  expect_result(
      post_json_rpc(port, rpc_request(13, mcp::protocol::LoggingSetLevelMethod,
                                      Json{{"level", "warning"}})));
  post_json_rpc_notification(
      port, rpc_notification(mcp::protocol::LoggingMessageNotificationMethod,
                             Json{{"level", "info"},
                                  {"logger", "interop"},
                                  {"data", Json{{"message", "hello"}}}}));
  post_json_rpc_notification(
      port, rpc_notification(mcp::protocol::ProgressNotificationMethod,
                             Json{{"progressToken", "progress-1"},
                                  {"progress", 1.0},
                                  {"total", 2.0},
                                  {"message", "halfway"}}));
  post_json_rpc_notification(
      port, rpc_notification(
                mcp::protocol::CancelledNotificationMethod,
                Json{{"requestId", 99}, {"reason", "client cancelled"}}));

  const auto roots_result = expect_result(
      post_json_rpc(port, rpc_request(14, mcp::protocol::RootsListMethod)));
  const auto parsed_roots =
      mcp::protocol::roots_list_result_from_json(roots_result);
  require(parsed_roots.has_value(), "roots/list result should parse");
  require(parsed_roots->roots.size() == 1, "roots/list should expose one root");
  require(parsed_roots->roots.front().uri == "file:///workspace",
          "roots/list uri mismatch");
  post_json_rpc_notification(
      port, rpc_notification(mcp::protocol::RootsListChangedNotificationMethod,
                             Json::object()));

  const auto sampling_result = expect_result(post_json_rpc(
      port,
      rpc_request(
          15, mcp::protocol::SamplingCreateMessageMethod,
          Json{{"messages",
                Json::array({Json{
                    {"role", "user"},
                    {"content", Json{{"type", "text"}, {"text", "hello"}}}}})},
               {"modelPreferences",
                Json{{"hints", Json::array({Json{{"name", "mock-model"}}})},
                     {"speedPriority", 0.7}}},
               {"systemPrompt", "answer briefly"},
               {"includeContext", "none"},
               {"temperature", 0.2},
               {"maxTokens", 32},
               {"stopSequences", Json::array({"stop"})},
               {"metadata", Json{{"source", "interop"}}},
               {"tools",
                Json::array({Json{
                    {"name", "lookup"},
                    {"description", "lookup helper"},
                    {"inputSchema", Json{{"type", "object"},
                                         {"properties", Json::object()}}}}})},
               {"toolChoice", Json{{"mode", "auto"}}}})));
  const auto parsed_sampling =
      mcp::protocol::create_message_result_from_json(sampling_result);
  require(parsed_sampling.has_value(),
          "sampling/createMessage result should parse");
  require(parsed_sampling->model == "cxxmcp-conformance-model",
          "sampling/createMessage model mismatch");
  require(parsed_sampling->content.text == "sampled response to: hello",
          "sampling/createMessage text mismatch");
  expect_error(
      post_json_rpc(port,
                    rpc_request(16, mcp::protocol::SamplingCreateMessageMethod,
                                Json{{"messages", Json::array()}})),
      static_cast<int>(mcp::protocol::ErrorCode::InvalidParams));

  const auto form_elicitation = expect_result(post_json_rpc(
      port,
      rpc_request(
          17, mcp::protocol::ElicitationCreateMethod,
          Json{{"message", "Need account details"},
               {"mode", "form"},
               {"requestedSchema",
                Json{{"type", "object"},
                     {"properties",
                      Json{{"username",
                            Json{{"type", "string"}, {"default", "testuser"}}},
                           {"email", Json{{"type", "string"},
                                          {"format", "email"},
                                          {"default", "test@example.com"}}}}},
                     {"required", Json::array({"username", "email"})}}}})));
  const auto parsed_form_elicitation =
      mcp::protocol::create_elicitation_result_from_json(form_elicitation);
  require(parsed_form_elicitation.has_value(),
          "form elicitation result should parse");
  require(parsed_form_elicitation->action ==
              mcp::protocol::ElicitationAction::Accept,
          "form elicitation action mismatch");
  require(parsed_form_elicitation->content->at("email") == "test@example.com",
          "form elicitation content mismatch");

  const auto url_elicitation = expect_result(post_json_rpc(
      port, rpc_request(18, mcp::protocol::ElicitationCreateMethod,
                        Json{{"message", "Authorize external flow"},
                             {"mode", "url"},
                             {"elicitationId", "elicitation-url-1"},
                             {"url", "https://example.test/authorize"}})));
  const auto parsed_url_elicitation =
      mcp::protocol::create_elicitation_result_from_json(url_elicitation);
  require(parsed_url_elicitation.has_value(),
          "URL elicitation result should parse");
  require(parsed_url_elicitation->content->at("elicitationId") ==
              "elicitation-url-1",
          "URL elicitation id mismatch");
  post_json_rpc_notification(
      port,
      rpc_notification(mcp::protocol::ElicitationCompleteNotificationMethod,
                       Json{{"elicitationId", "elicitation-url-1"}}));

  const auto created_task = expect_result(
      post_json_rpc(port, rpc_request(19, mcp::protocol::ToolsCallMethod,
                                      Json{{"name", "test_simple_text"},
                                           {"task", Json{{"ttl", 60}}}})));
  const auto parsed_created_task =
      mcp::protocol::create_task_result_from_json(created_task);
  require(parsed_created_task.has_value(), "task create result should parse");
  require(parsed_created_task->task.task_id == "task-created",
          "task create id mismatch");

  const auto tasks = expect_result(
      post_json_rpc(port, rpc_request(20, mcp::protocol::TasksListMethod)));
  const auto parsed_tasks = mcp::protocol::task_list_result_from_json(tasks);
  require(parsed_tasks.has_value(), "tasks/list result should parse");
  require(parsed_tasks->tasks.size() == 6,
          "tasks/list should expose lifecycle states");

  const auto completed_task = expect_result(
      post_json_rpc(port, rpc_request(21, mcp::protocol::TasksGetMethod,
                                      Json{{"taskId", "task-completed"}})));
  const auto parsed_completed_task =
      mcp::protocol::task_from_json(completed_task);
  require(parsed_completed_task.has_value(), "tasks/get result should parse");
  require(parsed_completed_task->status == mcp::protocol::TaskStatus::Completed,
          "tasks/get completed status mismatch");

  const auto cancelled_task = expect_result(
      post_json_rpc(port, rpc_request(22, mcp::protocol::TasksCancelMethod,
                                      Json{{"taskId", "task-cancelled"}})));
  const auto parsed_cancelled_task =
      mcp::protocol::task_from_json(cancelled_task);
  require(parsed_cancelled_task.has_value(),
          "tasks/cancel result should parse");
  require(parsed_cancelled_task->status == mcp::protocol::TaskStatus::Cancelled,
          "tasks/cancel status mismatch");

  const auto retained_result = expect_result(
      post_json_rpc(port, rpc_request(23, mcp::protocol::TasksResultMethod,
                                      Json{{"taskId", "task-retained"}})));
  require(
      retained_result.at("content").at(0).at("text") == "task-retained result",
      "tasks/result retained payload mismatch");
  expect_error(
      post_json_rpc(port, rpc_request(24, mcp::protocol::TasksResultMethod,
                                      Json{{"taskId", "task-failed"}})),
      static_cast<int>(mcp::protocol::ErrorCode::InvalidParams));
  expect_error(
      post_json_rpc(port, rpc_request(25, mcp::protocol::TasksResultMethod,
                                      Json{{"taskId", "task-timeout"}})),
      static_cast<int>(mcp::protocol::ErrorCode::InvalidParams));
  post_json_rpc_notification(
      port,
      rpc_notification(mcp::protocol::TasksStatusNotificationMethod,
                       mcp::protocol::task_to_json(make_task(
                           "task-working", mcp::protocol::TaskStatus::Working,
                           "still running"))));

  expect_error(post_json_rpc(port, rpc_request(26, "experimental/unknown",
                                               Json::object())),
               static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound));
  expect_error(
      post_json_rpc(port,
                    rpc_request(27, mcp::protocol::InitializeMethod,
                                Json{{"protocolVersion", "1900-01-01"},
                                     {"capabilities", Json::object()},
                                     {"clientInfo", Json{{"name", "old-client"},
                                                         {"version", "1"}}}})),
      static_cast<int>(mcp::protocol::ErrorCode::InvalidParams));

  httplib::Client client("127.0.0.1", port);
  const auto malformed = client.Post("/mcp", "{not json", "application/json");
  require(malformed != nullptr, "malformed POST should receive a response");
  require(malformed->status == 400, "malformed POST should fail with HTTP 400");
}

void test_cxxmcp_client_against_rmcp_conformance_http_server() {
  const auto port = choose_loopback_port();
  RunningRmcpConformanceServer server(port);

  mcp::client::Client::StreamableHttpEndpoint endpoint;
  endpoint.uri = "http://127.0.0.1:" + std::to_string(server.port()) + "/mcp";
  endpoint.timeout = std::chrono::seconds(5);

  auto client =
      mcp::client::Client::connect_streamable_http(std::move(endpoint));
  const auto initialized =
      client.initialize("cxxmcp-rmcp-conformance-client", "1");
  require(initialized.has_value(),
          "cxxmcp client should initialize RMCP conformance server");
  require(initialized->at("serverInfo").at("name") == "rust-conformance-server",
          "RMCP serverInfo name mismatch");
  require(client.notify_initialized().has_value(),
          "cxxmcp client should send initialized notification");

  const auto tools = client.list_tools();
  require(tools.has_value(), "cxxmcp client should list RMCP tools");
  require(std::any_of(tools->begin(), tools->end(),
                      [](const mcp::protocol::ToolDefinition& tool) {
                        return tool.name == "test_simple_text";
                      }),
          "RMCP tools should include test_simple_text");
  require(std::any_of(tools->begin(), tools->end(),
                      [](const mcp::protocol::ToolDefinition& tool) {
                        return tool.name == "test_elicitation_sep1034_defaults";
                      }),
          "RMCP tools should include elicitation defaults tool");

  mcp::protocol::ToolCall simple_call;
  simple_call.name = "test_simple_text";
  const auto simple_result = client.call_tool(simple_call);
  require(simple_result.has_value(),
          "cxxmcp client should call RMCP simple tool");
  require(!simple_result->content.empty(),
          "RMCP simple tool should return content");
  require(simple_result->content.front().text ==
              "This is a simple text response for testing.",
          "RMCP simple tool text mismatch");

  const auto resources = client.list_resources();
  require(resources.has_value(), "cxxmcp client should list RMCP resources");
  require(std::any_of(resources->begin(), resources->end(),
                      [](const mcp::protocol::Resource& resource) {
                        return resource.uri == "test://static-text";
                      }),
          "RMCP resources should include static text resource");
  const auto resource = client.read_resource("test://static-text");
  require(resource.has_value(), "cxxmcp client should read RMCP resource");
  require(!resource->contents.empty(),
          "RMCP resource read should return content");
  require(resource->contents.front().text ==
              "This is the content of the static text resource.",
          "RMCP resource text mismatch");

  const auto templates = client.list_resource_templates();
  require(templates.has_value(),
          "cxxmcp client should list RMCP resource templates");
  require(!templates->empty(), "RMCP should expose resource templates");

  const auto prompts = client.list_prompts();
  require(prompts.has_value(), "cxxmcp client should list RMCP prompts");
  require(std::any_of(prompts->begin(), prompts->end(),
                      [](const mcp::protocol::Prompt& prompt) {
                        return prompt.name == "test_prompt_with_arguments";
                      }),
          "RMCP prompts should include argument prompt");
  const auto prompt =
      client.get_prompt("test_prompt_with_arguments",
                        Json{{"name", "Alice"}, {"style", "formal"}});
  require(prompt.has_value(), "cxxmcp client should get RMCP prompt");
  require(!prompt->messages.empty(), "RMCP prompt should return messages");
  require(
      prompt->messages.front().content.text.find("Alice") != std::string::npos,
      "RMCP prompt should include rendered argument");

  mcp::protocol::ToolCall image_call;
  image_call.name = "test_image_content";
  const auto image_result = client.call_tool(image_call);
  require(image_result.has_value(),
          "cxxmcp client should call RMCP image tool");
  require(image_result->content.size() == 1 &&
              image_result->content.front().type == "image",
          "RMCP image tool should return image content");
  require(image_result->content.front().mime_type == "image/png",
          "RMCP image content mime mismatch");

  mcp::protocol::ToolCall audio_call;
  audio_call.name = "test_audio_content";
  const auto audio_result = client.call_tool(audio_call);
  require(audio_result.has_value(),
          "cxxmcp client should call RMCP audio tool");
  require(audio_result->content.size() == 1 &&
              audio_result->content.front().type == "audio",
          "RMCP audio tool should return audio content");
  require(audio_result->content.front().mime_type == "audio/wav",
          "RMCP audio content mime mismatch");

  mcp::protocol::ToolCall embedded_resource_call;
  embedded_resource_call.name = "test_embedded_resource";
  const auto embedded_resource_result =
      client.call_tool(embedded_resource_call);
  require(embedded_resource_result.has_value(),
          "cxxmcp client should call RMCP embedded resource tool");
  require(embedded_resource_result->content.size() == 1 &&
              embedded_resource_result->content.front().resource.has_value(),
          "RMCP embedded resource tool should return resource content");
  require(embedded_resource_result->content.front().resource->uri ==
              "test://embedded-resource",
          "RMCP embedded resource uri mismatch");

  mcp::protocol::ToolCall multiple_content_call;
  multiple_content_call.name = "test_multiple_content_types";
  const auto multiple_content_result = client.call_tool(multiple_content_call);
  require(multiple_content_result.has_value(),
          "cxxmcp client should call RMCP multiple-content tool");
  require(multiple_content_result->content.size() == 3,
          "RMCP multiple-content tool should return all content blocks");

  mcp::protocol::ToolCall error_call;
  error_call.name = "test_error_handling";
  const auto error_result = client.call_tool(error_call);
  require(error_result.has_value(),
          "cxxmcp client should receive RMCP error tool result");
  require(error_result->is_error.value_or(false),
          "RMCP error tool should be represented as tool result is_error");

  std::atomic<int> progress_notifications{0};
  client.on_progress(
      [&progress_notifications](
          const mcp::protocol::ProgressNotificationParams& params) {
        if (params.progress_token ==
            mcp::protocol::ProgressToken{std::string("cxxmcp-progress")}) {
          progress_notifications.fetch_add(1, std::memory_order_relaxed);
        }
      });
  mcp::protocol::ToolCall progress_call;
  progress_call.name = "test_tool_with_progress";
  progress_call.meta =
      mcp::protocol::meta_with_progress_token(std::string("cxxmcp-progress"));
  const auto progress_result = client.call_tool(progress_call);
  require(progress_result.has_value(),
          "cxxmcp client should call RMCP progress tool");
  require(progress_notifications.load(std::memory_order_relaxed) >= 1,
          "cxxmcp client should observe RMCP progress notifications");

  client.on_create_message_request(
      [](const mcp::protocol::CreateMessageParams& params)
          -> mcp::core::Result<mcp::protocol::CreateMessageResult> {
        require(!params.messages.empty(),
                "RMCP sampling request should include messages");
        return mcp::protocol::CreateMessageResult::text(
            "assistant", "cxxmcp sampled response", "cxxmcp-test-model");
      });
  mcp::protocol::ToolCall sampling_call;
  sampling_call.name = "test_sampling";
  sampling_call.arguments = Json{{"prompt", "sample this"}};
  const auto sampling_tool_result = client.call_tool(sampling_call);
  require(sampling_tool_result.has_value(),
          "cxxmcp client should handle RMCP sampling request");
  require(!sampling_tool_result->is_error.value_or(false),
          sampling_tool_result->content.empty()
              ? "RMCP sampling tool should not return an error"
              : sampling_tool_result->content.front().text);
  require(sampling_tool_result->content.front().text.find(
              "cxxmcp sampled response") != std::string::npos,
          "RMCP sampling tool should include cxxmcp sampling response");

  require(client.set_level("info").has_value(),
          "cxxmcp client should set RMCP logging level");

  const auto raw_completion = client.raw_request(mcp::protocol::make_request(
      mcp::protocol::CompletionCompleteMethod, std::int64_t{9001},
      Json{{"ref", Json{{"type", "ref/prompt"},
                        {"name", "test_prompt_with_arguments"}}},
           {"argument", Json{{"name", "name"}, {"value", "A"}}}}));
  require(raw_completion.has_value(),
          "cxxmcp client raw completion should reach RMCP server");
  const auto parsed_completion =
      mcp::protocol::complete_result_from_json(*raw_completion);
  require(parsed_completion.has_value(),
          "RMCP raw completion result should parse");
  require(!parsed_completion->completion.values.empty() &&
              parsed_completion->completion.values.front() == "Alice",
          "RMCP completion result mismatch");

  require(client
              .raw_request(mcp::protocol::make_request(
                  mcp::protocol::ResourcesSubscribeMethod, std::int64_t{9002},
                  Json{{"uri", "test://static-text"}}))
              .has_value(),
          "cxxmcp client raw subscribe should reach RMCP server");
  require(client
              .raw_request(mcp::protocol::make_request(
                  mcp::protocol::ResourcesUnsubscribeMethod, std::int64_t{9003},
                  Json{{"uri", "test://static-text"}}))
              .has_value(),
          "cxxmcp client raw unsubscribe should reach RMCP server");

  client.stop();
}

void test_cxxmcp_client_against_rmcp_reverse_http_server() {
  const auto port = choose_loopback_port() + 31;
  RunningRmcpReverseServer server(port);

  mcp::client::Client::StreamableHttpEndpoint endpoint;
  endpoint.uri = "http://127.0.0.1:" + std::to_string(server.port()) + "/mcp";
  endpoint.timeout = std::chrono::seconds(5);

  auto client =
      mcp::client::Client::connect_streamable_http(std::move(endpoint));
  const auto initialized = client.initialize("cxxmcp-rmcp-reverse-client", "1");
  require(initialized.has_value(),
          "cxxmcp client should initialize RMCP reverse server");
  require(initialized->at("serverInfo").at("name") == "rmcp-reverse-server",
          "RMCP reverse serverInfo name mismatch");
  require(initialized->at("capabilities").contains("completions"),
          "RMCP reverse server should advertise completions");
  require(initialized->at("capabilities").contains("tasks"),
          "RMCP reverse server should advertise tasks");
  require(client.notify_initialized().has_value(),
          "cxxmcp client should notify initialized for RMCP reverse server");

  const auto all_tools = client.list_all_tools();
  require(all_tools.has_value(),
          "cxxmcp client should page through RMCP reverse tools");
  require(all_tools->size() == 7, "RMCP reverse paginated tool count mismatch");

  client.set_roots({mcp::client::Client::Root{
      "file:///cxxmcp-rmcp-reverse-root", "cxxmcp-rmcp-reverse-root"}});
  mcp::protocol::ToolCall roots_call;
  roots_call.name = "reverse_roots";
  const auto roots_result = client.call_tool(roots_call);
  require(roots_result.has_value(),
          "cxxmcp should answer RMCP reverse roots/list request");
  require(!roots_result->content.empty() &&
              roots_result->content.front().text.find(
                  "file:///cxxmcp-rmcp-reverse-root") != std::string::npos,
          "RMCP reverse roots/list result should include cxxmcp root uri");

  const auto completion =
      client.complete_prompt_simple("reverse_prompt", "name", "A");
  require(completion.has_value(),
          "cxxmcp completion helper should work against RMCP reverse server");
  require(completion->size() == 2 && completion->front() == "Alice",
          "RMCP reverse completion values mismatch");

  require(client.subscribe("test://reverse/static").has_value(),
          "cxxmcp subscribe helper should work against RMCP reverse server");
  require(client.unsubscribe("test://reverse/static").has_value(),
          "cxxmcp unsubscribe helper should work against RMCP reverse server");

  const auto resource = client.read_resource("test://reverse/static");
  require(resource.has_value(),
          "cxxmcp client should read RMCP reverse resource");
  require(resource->contents.size() == 1 &&
              resource->contents.front().text == "reverse static resource",
          "RMCP reverse resource content mismatch");

  const auto tasks = client.list_tasks();
  require(tasks.has_value(), tasks.has_value()
                                 ? "cxxmcp list_tasks helper should work "
                                   "against RMCP reverse server"
                                 : tasks.error().message);
  require(tasks->size() == 2, "RMCP reverse tasks/list count mismatch");

  const auto completed_task = client.get_task("reverse-completed");
  require(completed_task.has_value(),
          "cxxmcp get_task helper should work against RMCP reverse server");
  require(completed_task->status == mcp::protocol::TaskStatus::Completed,
          "RMCP reverse tasks/get status mismatch");

  const auto cancelled_task = client.cancel_task("reverse-working");
  require(cancelled_task.has_value(),
          "cxxmcp cancel_task helper should work against RMCP reverse server");
  require(cancelled_task->status == mcp::protocol::TaskStatus::Cancelled,
          "RMCP reverse tasks/cancel status mismatch");

  std::atomic<int> progress_notifications{0};
  client.on_progress(
      [&progress_notifications](
          const mcp::protocol::ProgressNotificationParams& params) {
        if (params.progress_token ==
            mcp::protocol::ProgressToken{std::string("reverse-progress")}) {
          progress_notifications.fetch_add(1, std::memory_order_relaxed);
        }
      });
  mcp::protocol::ToolCall progress_call;
  progress_call.name = "reverse_progress";
  progress_call.meta =
      mcp::protocol::meta_with_progress_token(std::string("reverse-progress"));
  const auto progress_result = client.call_tool(progress_call);
  require(progress_result.has_value(),
          "cxxmcp should call RMCP reverse progress tool");
  require(progress_notifications.load(std::memory_order_relaxed) == 1,
          "cxxmcp should observe RMCP reverse progress notification");

  mcp::CancellationSource cancellation;
  mcp::RequestOptions cancellation_options;
  cancellation_options.cancellation_token = cancellation.token();
  mcp::protocol::ToolCall cancellable_call;
  cancellable_call.name = "reverse_cancellable";
  auto cancellable_handle =
      client.call_tool_async(cancellable_call, std::move(cancellation_options));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  cancellation.cancel();
  const auto cancelled_result = cancellable_handle.await_response();
  require(!cancelled_result.has_value(),
          "cxxmcp async RMCP reverse tool call should observe cancellation");
  require(cancelled_result.error().category == "cancellation",
          "cxxmcp async RMCP reverse cancellation should use cancellation "
          "error category");

  bool cancellation_observed = false;
  for (int attempt = 0; attempt < 50 && !cancellation_observed; ++attempt) {
    mcp::protocol::ToolCall status_call;
    status_call.name = "reverse_cancellable";
    status_call.arguments = Json{{"mode", "status"}};
    const auto status_result = client.call_tool(status_call);
    require(status_result.has_value(),
            "cxxmcp should query RMCP reverse cancellation status");
    require(!status_result->content.empty(),
            "RMCP reverse cancellation status should return content");
    const auto& status = status_result->content.front().text;
    cancellation_observed =
        status.find("notifications=0") == std::string::npos &&
        status.find("cancelled=0") == std::string::npos;
    if (!cancellation_observed) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  require(cancellation_observed,
          "RMCP reverse server should observe notifications/cancelled and "
          "cancel the in-flight tool context");

  client.on_create_message_request(
      [](const mcp::protocol::CreateMessageParams& params)
          -> mcp::core::Result<mcp::protocol::CreateMessageResult> {
        require(!params.messages.empty(),
                "RMCP reverse sampling should include a message");
        return mcp::protocol::CreateMessageResult::text(
            "assistant", "reverse sampled by cxxmcp", "cxxmcp-test-model");
      });
  mcp::protocol::ToolCall sampling_call;
  sampling_call.name = "reverse_sampling";
  const auto sampling_result = client.call_tool(sampling_call);
  require(sampling_result.has_value(),
          "cxxmcp should satisfy RMCP reverse sampling request");
  require(!sampling_result->is_error.value_or(false),
          "RMCP reverse sampling tool should not return isError");
  require(sampling_result->content.front().text.find(
              "reverse sampled by cxxmcp") != std::string::npos,
          "RMCP reverse sampling result mismatch");

  mcp::protocol::ToolCall error_call;
  error_call.name = "reverse_error";
  const auto error_result = client.call_tool(error_call);
  require(error_result.has_value(),
          "cxxmcp should receive RMCP reverse error tool result");
  require(error_result->is_error.value_or(false),
          "RMCP reverse error tool should set isError");

  client.stop();
}

void run_rmcp_conformance_scenario(std::string_view scenario) {
  build_conformance_client();

  set_process_env("MCP_CONFORMANCE_SCENARIO", std::string(scenario));
  set_process_env("NO_PROXY", "127.0.0.1,localhost");
  set_process_env("no_proxy", "127.0.0.1,localhost");
  set_process_env("RUST_BACKTRACE", "1");
  set_process_env("RUST_LOG", "debug");

  RunningInteropServer server;
  const auto port = server.port();

  const auto command =
      quote_path(conformance_client_executable()) + " " +
      quote_text("http://127.0.0.1:" + std::to_string(port) + "/mcp");
  std::string output;
  if (!run_command_with_timeout(command, std::chrono::seconds(60), &output)) {
    throw std::runtime_error("RMCP conformance scenario should succeed: " +
                             std::string(scenario) + "\nOutput:\n" + output);
  }
}

void run_rmcp_pagination_client_scenario() {
  build_pagination_client();

  set_process_env("MCP_CONFORMANCE_SCENARIO", "rmcp-pagination");
  set_process_env("NO_PROXY", "127.0.0.1,localhost");
  set_process_env("no_proxy", "127.0.0.1,localhost");
  set_process_env("RUST_BACKTRACE", "1");
  set_process_env("RUST_LOG", "debug");

  RunningInteropServer server;
  const auto port = server.port();

  const auto command =
      quote_path(pagination_client_executable()) + " " +
      quote_text("http://127.0.0.1:" + std::to_string(port) + "/mcp");
  std::string output;
  if (!run_command_with_timeout(command, std::chrono::seconds(60), &output)) {
    throw std::runtime_error(
        "RMCP pagination client scenario should succeed\n"
        "Output:\n" +
        output);
  }
}

void run_rmcp_canonical_server_peer_http_scenario() {
  build_conformance_client();
  build_pagination_client();

  RunningCanonicalServerPeerHttpServer server;
  const auto endpoint =
      "http://127.0.0.1:" + std::to_string(server.port()) + "/mcp";

  set_process_env("NO_PROXY", "127.0.0.1,localhost");
  set_process_env("no_proxy", "127.0.0.1,localhost");
  set_process_env("RUST_BACKTRACE", "1");
  set_process_env("RUST_LOG", "debug");

  set_process_env("MCP_CONFORMANCE_SCENARIO", "tools_call");
  std::string tools_output;
  const auto tools_command =
      quote_path(conformance_client_executable()) + " " + quote_text(endpoint);
  if (!run_command_with_timeout(tools_command, std::chrono::seconds(60),
                                &tools_output)) {
    throw std::runtime_error(
        "RMCP conformance tools_call should succeed against canonical "
        "ServerPeer HTTP stack\nOutput:\n" +
        tools_output);
  }

  std::string pagination_output;
  const auto pagination_command =
      quote_path(pagination_client_executable()) + " " + quote_text(endpoint);
  if (!run_command_with_timeout(pagination_command, std::chrono::seconds(60),
                                &pagination_output)) {
    throw std::runtime_error(
        "RMCP pagination client should succeed against canonical ServerPeer "
        "HTTP stack\nOutput:\n" +
        pagination_output);
  }
}

void run_rmcp_completion_client_scenario() {
  build_completion_client();

  set_process_env("MCP_CONFORMANCE_SCENARIO", "rmcp-completion");
  set_process_env("NO_PROXY", "127.0.0.1,localhost");
  set_process_env("no_proxy", "127.0.0.1,localhost");
  set_process_env("RUST_BACKTRACE", "1");
  set_process_env("RUST_LOG", "debug");

  RunningInteropServer server;
  const auto port = server.port();

  const auto command =
      quote_path(completion_client_executable()) + " " +
      quote_text("http://127.0.0.1:" + std::to_string(port) + "/mcp");
  std::string output;
  if (!run_command_with_timeout(command, std::chrono::seconds(60), &output)) {
    throw std::runtime_error(
        "RMCP completion client scenario should succeed\n"
        "Output:\n" +
        output);
  }
}

void run_rmcp_roots_sampling_client_scenario() {
  build_roots_sampling_client();

  set_process_env("MCP_CONFORMANCE_SCENARIO", "rmcp-roots-sampling");
  set_process_env("NO_PROXY", "127.0.0.1,localhost");
  set_process_env("no_proxy", "127.0.0.1,localhost");
  set_process_env("RUST_BACKTRACE", "1");
  set_process_env("RUST_LOG", "debug");

  RunningInteropServer server;
  const auto port = server.port();

  const auto command =
      quote_path(roots_sampling_client_executable()) + " " +
      quote_text("http://127.0.0.1:" + std::to_string(port) + "/mcp");
  std::string output;
  if (!run_command_with_timeout(command, std::chrono::seconds(60), &output)) {
    throw std::runtime_error(
        "RMCP roots/sampling client scenario should succeed\n"
        "Output:\n" +
        output);
  }
}

void run_rmcp_task_lifecycle_client_scenario() {
  build_task_lifecycle_client();

  set_process_env("MCP_CONFORMANCE_SCENARIO", "rmcp-task-lifecycle");
  set_process_env("NO_PROXY", "127.0.0.1,localhost");
  set_process_env("no_proxy", "127.0.0.1,localhost");
  set_process_env("RUST_BACKTRACE", "1");
  set_process_env("RUST_LOG", "debug");

  RunningInteropServer server;
  const auto port = server.port();

  const auto command =
      quote_path(task_lifecycle_client_executable()) + " " +
      quote_text("http://127.0.0.1:" + std::to_string(port) + "/mcp");
  std::string output;
  if (!run_command_with_timeout(command, std::chrono::seconds(60), &output)) {
    throw std::runtime_error(
        "RMCP task lifecycle client scenario should succeed\n"
        "Output:\n" +
        output);
  }
}

}  // namespace

int main() {
  try {
    std::cout << "[INFO] cxxmcp protocol " << mcp::protocol::McpProtocolVersion
              << ", RMCP reference " << kRmcpReferenceVersion
              << ", RMCP conformance " << kRmcpConformanceVersion
              << ", scenarios: initialize, tools_call, "
                 "elicitation-sep1034-client-defaults, sse-retry, "
                 "rmcp-pagination, rmcp-completion, rmcp-roots-sampling, "
                 "rmcp-task-lifecycle, rmcp-canonical-server-peer-http, "
                 "rmcp-reverse-server\n";
    test_cxxmcp_streamable_http_session_stale_matrix();
    test_cxxmcp_streamable_http_interop_matrix_core_methods();
    test_cxxmcp_client_against_rmcp_conformance_http_server();
    test_cxxmcp_client_against_rmcp_reverse_http_server();
    run_rmcp_pagination_client_scenario();
    run_rmcp_canonical_server_peer_http_scenario();
    run_rmcp_completion_client_scenario();
    run_rmcp_roots_sampling_client_scenario();
    run_rmcp_task_lifecycle_client_scenario();
    run_rmcp_conformance_scenario("initialize");
    run_rmcp_conformance_scenario("tools_call");
    run_rmcp_conformance_scenario("elicitation-sep1034-client-defaults");
    run_rmcp_conformance_scenario("sse-retry");
  } catch (const std::exception& ex) {
    std::cerr << "[FAIL] rmcp conformance interop: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "[PASS] rmcp conformance interop\n";
  return 0;
}
