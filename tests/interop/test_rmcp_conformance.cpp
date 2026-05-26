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
#include "httplib.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using mcp::protocol::Json;

constexpr std::string_view kRmcpReferenceVersion = "1.7.0";
constexpr std::string_view kRmcpConformanceVersion = "0.1.0";

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

void unset_process_env(std::string_view key) {
  const std::string key_string(key);
#ifdef _WIN32
  _putenv_s(key_string.c_str(), "");
#else
  unsetenv(key_string.c_str());
#endif
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

        {
          std::lock_guard lock(mutex_);
          session_id_ = "mcp-session-1";
        }

        response.set_header("Mcp-Session-Id", session_id_);
        const auto initialize_response = mcp::protocol::make_response(
            rpc_request->id,
            Json{
                {"protocolVersion",
                 std::string(mcp::protocol::negotiate_protocol_version(
                     requested_version))},
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
        const auto tool_response = mcp::protocol::make_response(
            rpc_request->id,
            Json{
                {"tools",
                 Json::array({
                     mcp::protocol::tool_definition_to_json(
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
                             .description =
                                 "Exercises the RMCP SSE retry scenario",
                             .input_schema =
                                 Json{
                                     {"type", "object"},
                                     {"properties", Json::object()},
                                 },
                             .streaming = false,
                         }),
                 })},
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
        if (tool_name != "test_simple_text" &&
            tool_name != "test_reconnection") {
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

        mcp::protocol::ToolResult result;
        result.content.push_back(mcp::protocol::ContentBlock{
            .type = "text",
            .text = tool_name == "test_reconnection"
                        ? "Reconnection scenario completed."
                        : "This is a simple text response for testing.",
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
  bool task_cancelled_ = false;
  std::uint16_t port_ = 0;
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
  return client.Post("/mcp",
                     httplib::Headers{
                         {"Accept", "application/json"},
                         {"Content-Type", "application/json"},
                         {"Mcp-Session-Id", session_id},
                     },
                     request.dump(), "application/json");
}

void post_json_rpc_notification(std::uint16_t port, const Json& notification) {
  httplib::Client client("127.0.0.1", port);
  const auto response =
      client.Post("/mcp", notification.dump(), "application/json");
  require(response != nullptr, "HTTP JSON-RPC notification should respond");
  require(response->status == 202,
          "HTTP JSON-RPC notification response status mismatch");
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
          1, std::string(mcp::protocol::InitializeMethod),
          Json{{"protocolVersion",
                std::string(mcp::protocol::McpProtocolVersion)},
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
      port, rpc_request(2, std::string(mcp::protocol::ToolsListMethod)),
      session_id);
  require(tool_list != nullptr, "session POST should return a response");
  require(tool_list->status == 200, "valid session POST should succeed");

  const auto deleted =
      client.Delete("/mcp", httplib::Headers{{"Mcp-Session-Id", session_id}});
  require(deleted != nullptr, "session DELETE should return a response");
  require(deleted->status == 204, "session DELETE should terminate session");

  const auto stale_post = post_json_rpc_with_session(
      port, rpc_request(3, std::string(mcp::protocol::ToolsListMethod)),
      session_id);
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
      port, rpc_request(1, std::string(mcp::protocol::InitializeMethod),
                        Json{{"protocolVersion",
                              std::string(mcp::protocol::McpProtocolVersion)},
                             {"clientInfo",
                              Json{{"name", "cxxmcp-test"}, {"version", "1"}}},
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
          post_json_rpc(
              port,
              rpc_request(2, std::string(mcp::protocol::ToolsListMethod))))
          .at("tools");
  require(tools.size() == 2, "tools/list should expose two tools");
  require(tools.at(0).at("name") == "test_simple_text",
          "tools/list tool name mismatch");

  const auto tool_result = expect_result(post_json_rpc(
      port, rpc_request(3, std::string(mcp::protocol::ToolsCallMethod),
                        Json{{"name", "test_simple_text"}})));
  require(tool_result.at("content").at(0).at("text") ==
              "This is a simple text response for testing.",
          "tools/call text result mismatch");

  expect_error(
      post_json_rpc(port,
                    rpc_request(4, std::string(mcp::protocol::ToolsCallMethod),
                                Json{{"name", "missing_tool"}})),
      static_cast<int>(mcp::protocol::ErrorCode::ToolNotFound));

  const auto prompts =
      expect_result(
          post_json_rpc(
              port,
              rpc_request(5, std::string(mcp::protocol::PromptsListMethod))))
          .at("prompts");
  require(prompts.size() == 1, "prompts/list should expose one prompt");
  require(prompts.at(0).at("name") == "summarize",
          "prompts/list prompt name mismatch");

  const auto prompt_result = expect_result(post_json_rpc(
      port, rpc_request(6, std::string(mcp::protocol::PromptsGetMethod),
                        Json{{"name", "summarize"},
                             {"arguments", Json{{"text", "hello"}}}})));
  require(prompt_result.at("messages").at(0).at("content").at("text") ==
              "Summarize: hello",
          "prompts/get message text mismatch");

  const auto resources =
      expect_result(
          post_json_rpc(
              port,
              rpc_request(7, std::string(mcp::protocol::ResourcesListMethod))))
          .at("resources");
  require(resources.size() == 1, "resources/list should expose one resource");
  require(resources.at(0).at("uri") == "file:///workspace/README.md",
          "resources/list uri mismatch");

  const auto read_result = expect_result(post_json_rpc(
      port, rpc_request(8, std::string(mcp::protocol::ResourcesReadMethod),
                        Json{{"uri", "file:///workspace/README.md"}})));
  require(read_result.at("contents").at(0).at("text") == "# cxxmcp",
          "resources/read text mismatch");

  const auto templates =
      expect_result(
          post_json_rpc(
              port,
              rpc_request(
                  9, std::string(mcp::protocol::ResourcesTemplatesListMethod))))
          .at("resourceTemplates");
  require(templates.size() == 1,
          "resources/templates/list should expose one template");
  require(templates.at(0).at("uriTemplate") == "file:///workspace/{path}",
          "resources/templates/list uriTemplate mismatch");

  expect_result(post_json_rpc(
      port,
      rpc_request(10, std::string(mcp::protocol::ResourcesSubscribeMethod),
                  Json{{"uri", "file:///workspace/README.md"}})));
  expect_result(post_json_rpc(
      port,
      rpc_request(11, std::string(mcp::protocol::ResourcesUnsubscribeMethod),
                  Json{{"uri", "file:///workspace/README.md"}})));

  const auto completion_result = expect_result(post_json_rpc(
      port,
      rpc_request(
          12, std::string(mcp::protocol::CompletionCompleteMethod),
          Json{{"ref", Json{{"type", "ref/prompt"}, {"name", "summarize"}}},
               {"argument", Json{{"name", "text"}, {"value", "he"}}}})));
  require(completion_result.at("completion").at("values").at(0) == "he-one",
          "completion/complete first value mismatch");
  require(completion_result.at("completion").at("total") == 2,
          "completion/complete total mismatch");

  expect_result(post_json_rpc(
      port, rpc_request(13, std::string(mcp::protocol::LoggingSetLevelMethod),
                        Json{{"level", "warning"}})));
  post_json_rpc_notification(
      port, rpc_notification(
                std::string(mcp::protocol::LoggingMessageNotificationMethod),
                Json{{"level", "info"},
                     {"logger", "interop"},
                     {"data", Json{{"message", "hello"}}}}));
  post_json_rpc_notification(
      port,
      rpc_notification(std::string(mcp::protocol::ProgressNotificationMethod),
                       Json{{"progressToken", "progress-1"},
                            {"progress", 1.0},
                            {"total", 2.0},
                            {"message", "halfway"}}));
  post_json_rpc_notification(
      port, rpc_notification(
                std::string(mcp::protocol::CancelledNotificationMethod),
                Json{{"requestId", 99}, {"reason", "client cancelled"}}));

  const auto roots_result = expect_result(post_json_rpc(
      port, rpc_request(14, std::string(mcp::protocol::RootsListMethod))));
  const auto parsed_roots =
      mcp::protocol::roots_list_result_from_json(roots_result);
  require(parsed_roots.has_value(), "roots/list result should parse");
  require(parsed_roots->roots.size() == 1, "roots/list should expose one root");
  require(parsed_roots->roots.front().uri == "file:///workspace",
          "roots/list uri mismatch");
  post_json_rpc_notification(
      port, rpc_notification(
                std::string(mcp::protocol::RootsListChangedNotificationMethod),
                Json::object()));

  const auto sampling_result = expect_result(post_json_rpc(
      port,
      rpc_request(
          15, std::string(mcp::protocol::SamplingCreateMessageMethod),
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
      post_json_rpc(
          port, rpc_request(
                    16, std::string(mcp::protocol::SamplingCreateMessageMethod),
                    Json{{"messages", Json::array()}})),
      static_cast<int>(mcp::protocol::ErrorCode::InvalidParams));

  const auto form_elicitation = expect_result(post_json_rpc(
      port,
      rpc_request(
          17, std::string(mcp::protocol::ElicitationCreateMethod),
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
      port, rpc_request(18, std::string(mcp::protocol::ElicitationCreateMethod),
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
      rpc_notification(
          std::string(mcp::protocol::ElicitationCompleteNotificationMethod),
          Json{{"elicitationId", "elicitation-url-1"}}));

  const auto created_task = expect_result(post_json_rpc(
      port, rpc_request(19, std::string(mcp::protocol::ToolsCallMethod),
                        Json{{"name", "test_simple_text"},
                             {"task", Json{{"ttl", 60}}}})));
  const auto parsed_created_task =
      mcp::protocol::create_task_result_from_json(created_task);
  require(parsed_created_task.has_value(), "task create result should parse");
  require(parsed_created_task->task.task_id == "task-created",
          "task create id mismatch");

  const auto tasks = expect_result(post_json_rpc(
      port, rpc_request(20, std::string(mcp::protocol::TasksListMethod))));
  const auto parsed_tasks = mcp::protocol::task_list_result_from_json(tasks);
  require(parsed_tasks.has_value(), "tasks/list result should parse");
  require(parsed_tasks->tasks.size() == 6,
          "tasks/list should expose lifecycle states");

  const auto completed_task = expect_result(post_json_rpc(
      port, rpc_request(21, std::string(mcp::protocol::TasksGetMethod),
                        Json{{"taskId", "task-completed"}})));
  const auto parsed_completed_task =
      mcp::protocol::task_from_json(completed_task);
  require(parsed_completed_task.has_value(), "tasks/get result should parse");
  require(parsed_completed_task->status == mcp::protocol::TaskStatus::Completed,
          "tasks/get completed status mismatch");

  const auto cancelled_task = expect_result(post_json_rpc(
      port, rpc_request(22, std::string(mcp::protocol::TasksCancelMethod),
                        Json{{"taskId", "task-cancelled"}})));
  const auto parsed_cancelled_task =
      mcp::protocol::task_from_json(cancelled_task);
  require(parsed_cancelled_task.has_value(),
          "tasks/cancel result should parse");
  require(parsed_cancelled_task->status == mcp::protocol::TaskStatus::Cancelled,
          "tasks/cancel status mismatch");

  const auto retained_result = expect_result(post_json_rpc(
      port, rpc_request(23, std::string(mcp::protocol::TasksResultMethod),
                        Json{{"taskId", "task-retained"}})));
  require(
      retained_result.at("content").at(0).at("text") == "task-retained result",
      "tasks/result retained payload mismatch");
  expect_error(
      post_json_rpc(
          port, rpc_request(24, std::string(mcp::protocol::TasksResultMethod),
                            Json{{"taskId", "task-failed"}})),
      static_cast<int>(mcp::protocol::ErrorCode::InvalidParams));
  expect_error(
      post_json_rpc(
          port, rpc_request(25, std::string(mcp::protocol::TasksResultMethod),
                            Json{{"taskId", "task-timeout"}})),
      static_cast<int>(mcp::protocol::ErrorCode::InvalidParams));
  post_json_rpc_notification(
      port, rpc_notification(
                std::string(mcp::protocol::TasksStatusNotificationMethod),
                mcp::protocol::task_to_json(make_task(
                    "task-working", mcp::protocol::TaskStatus::Working,
                    "still running"))));

  expect_error(post_json_rpc(port, rpc_request(26, "experimental/unknown",
                                               Json::object())),
               static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound));
  const auto fallback_init = expect_result(post_json_rpc(
      port, rpc_request(27, std::string(mcp::protocol::InitializeMethod),
                        Json{{"protocolVersion", "1900-01-01"},
                             {"capabilities", Json::object()},
                             {"clientInfo", Json{{"name", "old-client"},
                                                 {"version", "1"}}}})));
  require(fallback_init.at("protocolVersion") ==
              std::string(mcp::protocol::McpProtocolVersion),
          "unknown initialize protocolVersion should negotiate to latest");

  httplib::Client client("127.0.0.1", port);
  const auto malformed = client.Post("/mcp", "{not json", "application/json");
  require(malformed != nullptr, "malformed POST should receive a response");
  require(malformed->status == 400, "malformed POST should fail with HTTP 400");
}

void run_rmcp_conformance_scenario(std::string_view scenario) {
  build_conformance_client();

  RunningInteropServer server;
  const auto port = server.port();

  set_process_env("MCP_CONFORMANCE_SCENARIO", std::string(scenario));
  set_process_env("NO_PROXY", "127.0.0.1,localhost");
  set_process_env("no_proxy", "127.0.0.1,localhost");
  set_process_env("RUST_BACKTRACE", "1");
  set_process_env("RUST_LOG", "debug");
  const auto command =
      quote_path(conformance_client_executable()) + " " +
      quote_text("http://127.0.0.1:" + std::to_string(port) + "/mcp");
  std::string output;
  if (!run_command_with_timeout(command, std::chrono::seconds(60), &output)) {
    throw std::runtime_error("RMCP conformance scenario should succeed: " +
                             std::string(scenario) + "\nOutput:\n" + output);
  }
}

}  // namespace

int main() {
  try {
    std::cout << "[INFO] cxxmcp protocol " << mcp::protocol::McpProtocolVersion
              << ", RMCP reference " << kRmcpReferenceVersion
              << ", RMCP conformance " << kRmcpConformanceVersion
              << ", scenarios: initialize, tools_call, sse-retry\n";
    test_cxxmcp_streamable_http_session_stale_matrix();
    test_cxxmcp_streamable_http_interop_matrix_core_methods();
    run_rmcp_conformance_scenario("initialize");
    run_rmcp_conformance_scenario("tools_call");
    run_rmcp_conformance_scenario("sse-retry");
  } catch (const std::exception& ex) {
    std::cerr << "[FAIL] rmcp conformance interop: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "[PASS] rmcp conformance interop\n";
  return 0;
}
