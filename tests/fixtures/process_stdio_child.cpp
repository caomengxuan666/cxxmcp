// Copyright (c) 2025 [caomengxuan666]

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/protocol/tool.hpp"

namespace {

using mcp::protocol::Json;

std::string g_observed_arg;

void write_response(const mcp::protocol::JsonRpcResponse& response) {
  const auto serialized = mcp::protocol::serialize_response(response);
  if (!serialized) {
    return;
  }
  std::cout << *serialized << '\n';
  std::cout.flush();
}

void write_request(const mcp::protocol::JsonRpcRequest& request) {
  const auto serialized = mcp::protocol::serialize_request(request);
  if (!serialized) {
    return;
  }
  std::cout << *serialized << '\n';
  std::cout.flush();
}

std::optional<mcp::protocol::JsonRpcResponse> read_response() {
  std::string line;
  if (!std::getline(std::cin, line)) {
    return std::nullopt;
  }
  const auto parsed = mcp::protocol::parse_response(line);
  if (!parsed) {
    return std::nullopt;
  }
  return *parsed;
}

std::string get_process_env_or_empty(std::string_view key) {
  const std::string key_string(key);
#ifdef _WIN32
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, key_string.c_str()) != 0 || value == nullptr) {
    return {};
  }
  std::string result(value);
  std::free(value);
  return result;
#else
  const char* value = std::getenv(key_string.c_str());
  return value == nullptr ? std::string{} : std::string(value);
#endif
}

void write_error(const mcp::protocol::JsonRpcRequest& request,
                 mcp::protocol::ErrorCode code, std::string message) {
  write_response(mcp::protocol::make_error_response(
      std::optional<mcp::protocol::RequestId>{request.id},
      mcp::protocol::make_error(code, std::move(message))));
}

mcp::protocol::Task make_task(std::string task_id,
                              mcp::protocol::TaskStatus status,
                              std::string status_message) {
  mcp::protocol::Task task;
  task.task_id = std::move(task_id);
  task.status = status;
  task.status_message = std::move(status_message);
  task.created_at = "2026-05-25T00:00:00Z";
  task.last_updated_at = "2026-05-25T00:00:01Z";
  task.ttl = static_cast<std::int64_t>(300);
  task.poll_interval = static_cast<std::int64_t>(1);
  return task;
}

void handle_request(const mcp::protocol::JsonRpcRequest& request) {
  if (request.method == mcp::protocol::InitializeMethod) {
    write_response(mcp::protocol::make_response(
        request.id,
        Json{
            {"protocolVersion", mcp::protocol::McpProtocolVersion},
            {"capabilities",
             Json{{"tools", Json::object()},
                  {"prompts", Json::object()},
                  {"resources", Json::object()},
                  {"tasks",
                   Json{{"list", Json::object()},
                        {"cancel", Json::object()},
                        {"requests",
                         Json{{"tools", Json{{"call", Json::object()}}}}}}}}},
            {"serverInfo",
             Json{{"name", "process-stdio-child"}, {"version", "1"}}},
        }));
    return;
  }

  if (request.method == "tools/list") {
    write_response(mcp::protocol::make_response(
        request.id,
        Json{
            {"tools", Json::array({
                          Json{
                              {"name", "echo"},
                              {"description", "Echo test tool"},
                              {"inputSchema", Json{{"type", "object"}}},
                          },
                      })},
        }));
    return;
  }

  if (request.method == "prompts/list") {
    write_response(mcp::protocol::make_response(
        request.id,
        Json{
            {"prompts", Json::array({
                            Json{
                                {"name", "summarize"},
                                {"description", "Summarize test prompt"},
                                {"arguments",
                                 Json::array({
                                     Json{{"name", "text"},
                                          {"description", "Text to summarize"},
                                          {"required", true}},
                                 })},
                            },
                        })},
        }));
    return;
  }

  if (request.method == "prompts/get") {
    if (request.params.value("name", "") != "summarize") {
      write_error(request, mcp::protocol::ErrorCode::InvalidParams,
                  "unknown prompt");
      return;
    }
    write_response(mcp::protocol::make_response(
        request.id,
        mcp::protocol::prompts_get_result_to_json(
            mcp::protocol::PromptsGetResult{
                .description = "Summarize test prompt",
                .messages = {mcp::protocol::PromptMessage{
                    .role = "user",
                    .content =
                        mcp::protocol::ContentBlock{
                            .type = "text",
                            .text = "Summarize " +
                                    request.params
                                        .value("arguments", Json::object())
                                        .value("text", ""),
                            .data = Json::object(),
                        },
                }},
            })));
    return;
  }

  if (request.method == "resources/list") {
    write_response(mcp::protocol::make_response(
        request.id,
        Json{
            {"resources", Json::array({
                              Json{
                                  {"uri", "file:///workspace/README.md"},
                                  {"name", "Readme"},
                                  {"description", "Workspace readme"},
                                  {"mimeType", "text/markdown"},
                              },
                          })},
        }));
    return;
  }

  if (request.method == "resources/read") {
    if (request.params.value("uri", "") != "file:///workspace/README.md") {
      write_error(request, mcp::protocol::ErrorCode::InvalidParams,
                  "unknown resource");
      return;
    }
    write_response(mcp::protocol::make_response(
        request.id, mcp::protocol::resources_read_result_to_json(
                        mcp::protocol::ResourcesReadResult{
                            .contents = {mcp::protocol::ResourceContents{
                                .uri = request.params.value("uri", ""),
                                .mime_type = "text/markdown",
                                .text = "hello from readme",
                                .blob = std::nullopt,
                            }},
                        })));
    return;
  }

  if (request.method == "tasks/list") {
    write_response(mcp::protocol::make_response(
        request.id,
        mcp::protocol::task_list_result_to_json(mcp::protocol::TaskListResult{
            .tasks =
                {
                    make_task("task-working",
                              mcp::protocol::TaskStatus::Working, "running"),
                    make_task("task-completed",
                              mcp::protocol::TaskStatus::Completed,
                              "completed"),
                    make_task("task-failed", mcp::protocol::TaskStatus::Failed,
                              "failed"),
                },
            .total = std::int64_t{3},
        })));
    return;
  }

  if (request.method == "tasks/get") {
    const auto task_id = request.params.value("taskId", "");
    if (task_id != "task-completed" && task_id != "task-working" &&
        task_id != "task-failed") {
      write_error(request, mcp::protocol::ErrorCode::InvalidParams,
                  "unknown task");
      return;
    }
    auto status = mcp::protocol::TaskStatus::Working;
    std::string message = "running";
    if (task_id == "task-completed") {
      status = mcp::protocol::TaskStatus::Completed;
      message = "completed";
    } else if (task_id == "task-failed") {
      status = mcp::protocol::TaskStatus::Failed;
      message = "failed";
    }
    write_response(mcp::protocol::make_response(
        request.id,
        mcp::protocol::task_get_result_to_json(mcp::protocol::TaskGetResult{
            .task = make_task(task_id, status, message),
        })));
    return;
  }

  if (request.method == "tasks/cancel") {
    const auto task_id = request.params.value("taskId", "");
    if (task_id.empty()) {
      write_error(request, mcp::protocol::ErrorCode::InvalidParams,
                  "missing task");
      return;
    }
    write_response(mcp::protocol::make_response(
        request.id,
        mcp::protocol::task_cancel_result_to_json(
            mcp::protocol::TaskCancelResult{
                .task = make_task(task_id, mcp::protocol::TaskStatus::Cancelled,
                                  "cancelled"),
            })));
    return;
  }

  if (request.method == "tools/call") {
    write_response(mcp::protocol::make_response(
        request.id,
        mcp::protocol::tool_result_to_json(mcp::protocol::ToolResult{
            .content = {mcp::protocol::ContentBlock{
                .type = "text",
                .text = request.params.value("name", ""),
                .data = Json::object(),
            }},
            .structured_content =
                request.params.value("arguments", Json::object()),
            .is_error = false,
        })));
    return;
  }

  if (request.method == "custom/interleave") {
    write_request(mcp::protocol::JsonRpcRequest{
        .method = "sampling/createMessage",
        .params =
            Json{
                {"messages",
                 Json::array({
                     Json{{"role", "user"},
                          {"content", Json{{"type", "text"},
                                           {"text", "hello from child"}}}},
                 })},
                {"maxTokens", 16},
            },
        .id = std::string("server-1"),
    });

    const auto response = read_response();
    if (!response) {
      return;
    }
    if (!response->id.has_value() ||
        !std::holds_alternative<std::string>(*response->id) ||
        std::get<std::string>(*response->id) != "server-1") {
      return;
    }
    if (response->error.has_value()) {
      write_response(mcp::protocol::make_response(
          request.id,
          Json{{"handlerError", response->error->message}, {"ok", false}}));
      return;
    }
    if (!response->result.has_value()) {
      return;
    }

    write_response(
        mcp::protocol::make_response(request.id, Json{{"ok", true}}));
    return;
  }

  if (request.method == "custom/options") {
    write_response(mcp::protocol::make_response(
        request.id,
        Json{
            {"arg", g_observed_arg},
            {"env", get_process_env_or_empty("CXXMCP_PROCESS_TEST_ENV")},
            {"cwd", std::filesystem::current_path().string()},
        }));
    return;
  }

  write_error(request, mcp::protocol::ErrorCode::MethodNotFound,
              "method not found");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    g_observed_arg = argv[2];
  }

  if (argc > 1 && std::string_view(argv[1]) == "--ignore-requests") {
    std::string ignored;
    while (std::getline(std::cin, ignored)) {
    }
    return 0;
  }
  if (argc > 1 && std::string_view(argv[1]) == "--exit-immediately") {
    return 0;
  }
  if (argc > 1 && std::string_view(argv[1]) == "--stderr-before-response") {
    std::cerr << "child stderr is intentionally noisy\n";
    std::cerr.flush();
  }

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--malformed-output") {
      std::cout << "{not-json}\n";
      std::cout.flush();
      continue;
    }

    const auto message = mcp::protocol::parse_message(line);
    if (!message) {
      continue;
    }
    if (const auto* request =
            std::get_if<mcp::protocol::JsonRpcRequest>(&*message)) {
      if (argc > 1 && std::string_view(argv[1]) == "--wrong-response-id") {
        write_response(mcp::protocol::make_response(std::int64_t{999},
                                                    Json{{"ok", true}}));
        continue;
      }
      handle_request(*request);
    }
  }

  return 0;
}
