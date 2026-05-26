// Copyright (c) 2025 [caomengxuan666]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/elicitation.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/service.hpp"

namespace typed_tool_fixture {

struct SumArgs {
  int a = 0;
  int b = 0;
};

struct SumResult {
  int sum = 0;
};

struct PromptArgs {
  std::string text;
};

struct ResourceArgs {
  std::string section;
};

void from_json(const mcp::protocol::Json& json, SumArgs& args) {
  args.a = json.at("a").get<int>();
  args.b = json.at("b").get<int>();
}

void from_json(const mcp::protocol::Json& json, PromptArgs& args) {
  args.text = json.at("text").get<std::string>();
}

void from_json(const mcp::protocol::Json& json, ResourceArgs& args) {
  args.section = json.at("section").get<std::string>();
}

void to_json(mcp::protocol::Json& json, const SumResult& result) {
  json = mcp::protocol::Json{{"sum", result.sum}};
}

}  // namespace typed_tool_fixture

namespace mcp::protocol {

template <>
struct SchemaTraits<typed_tool_fixture::SumArgs> {
  static Json schema() {
    return object_schema()
        .required_property("a", JsonSchema::integer())
        .required_property("b", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <>
struct SchemaTraits<typed_tool_fixture::SumResult> {
  static Json schema() {
    return object_schema()
        .required_property("sum", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

}  // namespace mcp::protocol

namespace {

using mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class RecordingSchemaValidator final : public mcp::server::JsonSchemaValidator {
 public:
  mcp::core::Result<mcp::core::Unit> validate(
      const Json& schema, const Json& instance,
      const mcp::server::SchemaValidationContext& context) const override {
    schemas.push_back(schema);
    instances.push_back(instance);
    contexts.push_back(context);
    if (reject_input &&
        context.target == mcp::server::SchemaValidationTarget::ToolInput) {
      return std::unexpected(mcp::core::Error{0, "input rejected", "bad"});
    }
    if (reject_output &&
        context.target == mcp::server::SchemaValidationTarget::ToolOutput) {
      return std::unexpected(mcp::core::Error{0, "output rejected", "bad"});
    }
    return mcp::core::Unit{};
  }

  bool reject_input = false;
  bool reject_output = false;
  mutable std::vector<Json> schemas;
  mutable std::vector<Json> instances;
  mutable std::vector<mcp::server::SchemaValidationContext> contexts;
};

template <class Predicate>
void wait_until(Predicate predicate, std::string_view message) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (predicate()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error(std::string(message));
}

class LoopbackTransport final : public mcp::client::Transport {
 public:
  explicit LoopbackTransport(mcp::server::Server& server) : server_(server) {}

  mcp::core::Result<mcp::protocol::JsonRpcResponse> send(
      const mcp::protocol::JsonRpcRequest& request) override {
    return server_.handle_request(request, context_);
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    return server_.handle_notification(notification, context_);
  }

 private:
  mcp::server::Server& server_;
  mcp::server::SessionContext context_{
      .session_id = "test-session",
      .remote_address = "127.0.0.1",
  };
};

class RecordingTransport final : public mcp::client::Transport {
 public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse> send(
      const mcp::protocol::JsonRpcRequest& request) override {
    requests.push_back(request);
    if (request.method == "initialize") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = initialize_result,
      };
    }
    if (request.method == "tools/list") {
      if (request.params.contains("cursor")) {
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result =
                Json{
                    {"tools", Json::array({
                                  Json{
                                      {"name", "paged-tool-2"},
                                      {"description", "Second page tool"},
                                      {"inputSchema", Json{{"type", "object"}}},
                                      {"streaming", false},
                                  },
                              })},
                },
        };
      }
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{
                  {"tools", Json::array({
                                Json{
                                    {"name", "fake-tool"},
                                    {"description", "A fake tool"},
                                    {"inputSchema", Json{{"type", "object"}}},
                                    {"streaming", false},
                                },
                            })},
                  {"nextCursor", "page-2"},
              },
      };
    }
    if (request.method == "tools/call") {
      if (request.params.contains("task")) {
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result = mcp::protocol::create_task_result_to_json(
                mcp::protocol::CreateTaskResult{
                    .task =
                        mcp::protocol::Task{
                            .task_id = "task-created",
                            .status = mcp::protocol::TaskStatus::Working,
                            .created_at = "2026-05-24T00:00:00Z",
                            .ttl = std::int64_t{60},
                            .last_updated_at = "2026-05-24T00:00:00Z",
                        },
                }),
        };
      }
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              mcp::protocol::tool_result_to_json(mcp::protocol::ToolResult{
                  .content = {mcp::protocol::ContentBlock{
                      .type = "text",
                      .text = "called",
                      .data = Json::object(),
                  }},
              }),
      };
    }
    if (request.method == "resources/templates/list") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{
                  {"resourceTemplates",
                   Json::array({
                       Json{
                           {"uriTemplate", "file:///tmp/{name}.txt"},
                           {"name", "Temporary file"},
                           {"description", "A temporary text file"},
                           {"mimeType", "text/plain"},
                       },
                   })},
              },
      };
    }
    if (request.method == "completion/complete") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{{"completion", Json{{"values", Json::array({"hello"})}}}},
      };
    }
    if (request.method == "sampling/createMessage") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{{"role", "assistant"},
                   {"content", Json{{"type", "text"}, {"text", "sample"}}},
                   {"model", "test-model"}},
      };
    }
    if (request.method == "elicitation/create") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = Json{{"action", "accept"}, {"content", Json{{"value", 1}}}},
      };
    }
    if (request.method == "tasks/list") {
      if (request.params.contains("cursor")) {
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result = mcp::protocol::task_list_result_to_json(
                mcp::protocol::TaskListResult{
                    .tasks = {mcp::protocol::Task{
                        .task_id = "task-2",
                        .status = mcp::protocol::TaskStatus::Completed,
                        .created_at = "2026-05-24T00:00:10Z",
                        .ttl = std::monostate{},
                        .last_updated_at = "2026-05-24T00:00:20Z",
                    }},
                }),
        };
      }
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::task_list_result_to_json(
              mcp::protocol::TaskListResult{
                  .tasks = {mcp::protocol::Task{
                      .task_id = "task-1",
                      .status = mcp::protocol::TaskStatus::Working,
                      .created_at = "2026-05-24T00:00:00Z",
                      .ttl = std::monostate{},
                      .last_updated_at = "2026-05-24T00:00:05Z",
                  }},
                  .next_cursor = "page-2",
              }),
      };
    }
    if (request.method == "tasks/get" || request.method == "tasks/cancel") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::task_to_json(mcp::protocol::Task{
              .task_id = request.params.at("taskId").get<std::string>(),
              .status = request.method == "tasks/cancel"
                            ? mcp::protocol::TaskStatus::Cancelled
                            : mcp::protocol::TaskStatus::Working,
              .created_at = "2026-05-24T00:00:00Z",
              .ttl = std::monostate{},
              .last_updated_at = "2026-05-24T00:00:05Z",
          }),
      };
    }
    if (request.method == "tasks/result") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = Json{{"value", "task-complete"}},
      };
    }
    if (request.method == "logging/setLevel" ||
        request.method == "resources/subscribe" ||
        request.method == "resources/unsubscribe") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = Json::object(),
      };
    }
    if (request.method == "prompts/list") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{
                  {"prompts", Json::array({
                                  Json{
                                      {"name", "summarize"},
                                      {"description", "Summarize text"},
                                      {"arguments",
                                       Json::array({
                                           Json{{"name", "text"},
                                                {"description", "Source text"},
                                                {"required", true}},
                                       })},
                                  },
                              })},
              },
      };
    }
    if (request.method == "prompts/get") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::prompts_get_result_to_json(
              mcp::protocol::PromptsGetResult{
                  .description = "Summarize text",
                  .messages = {mcp::protocol::PromptMessage{
                      .role = "user",
                      .content =
                          mcp::protocol::ContentBlock{
                              .type = "text",
                              .text =
                                  "Summarize " + request.params.at("arguments")
                                                     .at("text")
                                                     .get<std::string>(),
                              .data = Json::object(),
                          },
                  }},
              }),
      };
    }
    if (request.method == "resources/list") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{
                  {"resources", Json::array({
                                    Json{
                                        {"uri", "file:///tmp/readme.txt"},
                                        {"name", "Readme"},
                                        {"description", "Project readme"},
                                        {"mimeType", "text/plain"},
                                    },
                                })},
              },
      };
    }
    if (request.method == "resources/read") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::resources_read_result_to_json(
              mcp::protocol::ResourcesReadResult{
                  .contents = {mcp::protocol::ResourceContents{
                      .uri = request.params.at("uri").get<std::string>(),
                      .mime_type = "text/plain",
                      .text = "hello",
                      .blob = std::nullopt,
                  }},
              }),
      };
    }
    return mcp::protocol::JsonRpcResponse{
        .id = request.id,
        .error =
            mcp::protocol::ErrorObject{
                .code =
                    static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
                .message = "unexpected method",
            },
    };
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    notifications.push_back(notification);
    return mcp::core::Unit{};
  }

  void stop() noexcept override { stopped = true; }

  std::vector<mcp::protocol::JsonRpcRequest> requests;
  std::vector<mcp::protocol::JsonRpcNotification> notifications;
  Json initialize_result = Json{
      {"protocolVersion", std::string(mcp::protocol::McpProtocolVersion)},
      {"capabilities", Json::object()},
      {"serverInfo", Json{{"name", "FakeServer"}, {"version", "1"}}},
  };
  bool stopped = false;
};

class ConcurrentRequestIdTransport final : public mcp::client::Transport {
 public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse> send(
      const mcp::protocol::JsonRpcRequest& request) override {
    std::lock_guard<std::mutex> lock(mutex_);
    requests.push_back(request);
    return mcp::protocol::JsonRpcResponse{
        .id = request.id,
        .result = Json{{"ok", true}},
    };
  }

  std::size_t request_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests.size();
  }

  std::vector<mcp::protocol::JsonRpcRequest> requests;

 private:
  mutable std::mutex mutex_;
};

class RecordingServerTransport final : public mcp::server::Transport {
 public:
  mcp::core::Result<mcp::core::Unit> start(
      mcp::server::RequestHandler,
      mcp::server::NotificationHandler = {}) override {
    return mcp::core::Unit{};
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    notifications.push_back(notification);
    return mcp::core::Unit{};
  }

  void stop() noexcept override {}

  std::string_view name() const noexcept override { return "recording"; }

  std::vector<mcp::protocol::JsonRpcNotification> notifications;
};

class BlockingLifecycleServerTransport final : public mcp::server::Transport {
 public:
  mcp::core::Result<mcp::core::Unit> start(
      mcp::server::RequestHandler,
      mcp::server::NotificationHandler = {}) override {
    std::unique_lock<std::mutex> lock(mutex_);
    started_ = true;
    cv_.notify_all();
    cv_.wait(lock, [this] { return stopped_; });
    return mcp::core::Unit{};
  }

  void stop() noexcept override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    cv_.notify_all();
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification&) override {
    return mcp::core::Unit{};
  }

  std::string_view name() const noexcept override { return "blocking"; }

  void wait_until_started() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return started_; });
  }

  bool stopped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopped_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool started_ = false;
  bool stopped_ = false;
};

class QueuedRoleServerTransport final : public mcp::transport::ServerTransport {
 public:
  std::string_view name() const noexcept override { return "queued-role"; }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    sent.push_back(std::move(message));
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    if (next_inbound_ >= inbound.size()) {
      return std::nullopt;
    }
    auto message = std::move(inbound.at(next_inbound_));
    ++next_inbound_;
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    closed = true;
    next_inbound_ = inbound.size();
    return mcp::core::Unit{};
  }

  std::vector<RxMessage> inbound;
  std::vector<TxMessage> sent;
  bool closed = false;

 private:
  std::size_t next_inbound_ = 0;
};

class BlockingRoleServerTransport final
    : public mcp::transport::ServerTransport {
 public:
  std::string_view name() const noexcept override { return "blocking-role"; }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    std::lock_guard<std::mutex> lock(mutex_);
    sent.push_back(std::move(message));
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      receiving_ = true;
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return closed_; });
    return std::nullopt;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

  void wait_until_receiving() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return receiving_; });
  }

  bool closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

  std::vector<TxMessage> sent;

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool receiving_ = false;
  bool closed_ = false;
};

class BlockingRequestServerTransport final : public mcp::server::Transport {
 public:
  mcp::core::Result<mcp::core::Unit> start(
      mcp::server::RequestHandler,
      mcp::server::NotificationHandler = {}) override {
    return mcp::core::Unit{};
  }

  mcp::core::Result<mcp::protocol::JsonRpcResponse> send_request(
      const mcp::protocol::JsonRpcRequest& request) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      request_ = request;
      request_started_ = true;
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return released_; });
    request_finished_ = true;
    cv_.notify_all();

    if (request.method == std::string(mcp::protocol::RootsListMethod)) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::roots_list_result_to_json(
              mcp::protocol::RootsListResult{
                  .roots = {mcp::protocol::Root{
                      .uri = "file:///workspace",
                      .name = "workspace",
                  }},
              }),
      };
    }
    if (request.method ==
        std::string(mcp::protocol::SamplingCreateMessageMethod)) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::create_message_result_to_json(
              mcp::protocol::CreateMessageResult{
                  .role = "assistant",
                  .content =
                      mcp::protocol::ContentBlock{
                          .type = "text",
                          .text = "sample",
                          .data = Json::object(),
                      },
                  .model = "test-model",
                  .stop_reason = {},
              }),
      };
    }
    if (request.method == std::string(mcp::protocol::ElicitationCreateMethod)) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::create_elicitation_result_to_json(
              mcp::protocol::CreateElicitationResult{
                  .action = mcp::protocol::ElicitationAction::Accept,
                  .content = Json{{"value", 1}},
              }),
      };
    }

    return mcp::protocol::JsonRpcResponse{
        .id = request.id,
        .result = Json{{"ok", true}},
    };
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      notifications.push_back(notification);
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

  void stop() noexcept override {}

  std::string_view name() const noexcept override { return "blocking"; }

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      released_ = true;
    }
    cv_.notify_all();
  }

  void wait_until_request_started() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return request_started_; });
  }

  void wait_until_request_finished() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return request_finished_; });
  }

  void wait_until_notifications(std::size_t count) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this, count]() { return notifications.size() >= count; });
  }

  bool request_started() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return request_started_;
  }

  bool request_finished() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return request_finished_;
  }

  std::optional<mcp::protocol::JsonRpcRequest> request() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return request_;
  }

  std::vector<mcp::protocol::JsonRpcNotification> notifications;

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool request_started_ = false;
  bool request_finished_ = false;
  bool released_ = false;
  std::optional<mcp::protocol::JsonRpcRequest> request_;
};

class BlockingRequestClientTransport final : public mcp::client::Transport {
 public:
  mcp::core::Result<mcp::protocol::JsonRpcResponse> send(
      const mcp::protocol::JsonRpcRequest& request) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      request_ = request;
      request_started_ = true;
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return released_; });
    request_finished_ = true;
    cv_.notify_all();

    return mcp::protocol::JsonRpcResponse{
        .id = request.id,
        .result = Json{{"ok", true}},
    };
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification& notification) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      notifications.push_back(notification);
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

  void stop() noexcept override {}

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      released_ = true;
    }
    cv_.notify_all();
  }

  void wait_until_request_started() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return request_started_; });
  }

  void wait_until_request_finished() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return request_finished_; });
  }

  void wait_until_notifications(std::size_t count) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this, count]() { return notifications.size() >= count; });
  }

  std::vector<mcp::protocol::JsonRpcNotification> notifications;

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool request_started_ = false;
  bool request_finished_ = false;
  bool released_ = false;
  std::optional<mcp::protocol::JsonRpcRequest> request_;
};

mcp::server::Server make_server() {
  mcp::server::ServerOptions options;
  options.capabilities.tools.list_changed = true;
  options.capabilities.experimental = Json{{"beta", true}};
  options.capabilities.extensions =
      Json{{"vendor/feature", Json{{"enabled", true}}}};
  options.server_name = "TestServer";
  options.server_version = "1.0.0";

  mcp::server::Server server(options);

  const auto added = server.tools().add(
      mcp::protocol::ToolDefinition{
          .title = "Echo Tool",
          .name = "echo",
          .description = "Echo the incoming payload",
          .input_schema = Json::object(),
          .output_schema = Json{{"type", "object"}},
          .streaming = false,
          .annotations = Json{{"beta", true}},
          .meta = Json{{"source", "unit-test"}},
      },
      [](const mcp::server::ToolContext& context)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        mcp::protocol::ToolResult result;
        result.content.push_back(mcp::protocol::ContentBlock{
            .type = "text",
            .text = context.arguments.dump(),
            .data = Json::object(),
        });
        return result;
      });
  require(added.has_value(), "failed to register echo tool");

  const auto prompt_added = server.prompts().add(
      mcp::protocol::Prompt{
          .title = "Summarize Prompt",
          .name = "summarize",
          .description = "Summarize input",
          .arguments =
              {
                  mcp::protocol::PromptArgument{
                      .title = "Text Argument",
                      .name = "text",
                      .description = "Input text",
                      .required = true,
                      .annotations = Json{{"beta", true}},
                      .meta = Json{{"source", "unit-test"}},
                  },
              },
          .annotations = Json{{"beta", true}},
          .meta = Json{{"source", "unit-test"}},
      },
      [](const mcp::server::PromptContext& context)
          -> mcp::core::Result<mcp::protocol::PromptsGetResult> {
        mcp::protocol::PromptsGetResult result;
        result.description = "Summarize input";
        result.messages.push_back(mcp::protocol::PromptMessage{
            .role = "user",
            .content =
                mcp::protocol::ContentBlock{
                    .type = "text",
                    .text = context.arguments.at("text").get<std::string>(),
                },
        });
        return result;
      });
  require(prompt_added.has_value(), "failed to register summarize prompt");

  const auto resource_added = server.resources().add(
      mcp::protocol::Resource{
          .title = "Readme",
          .uri = "file:///tmp/readme.txt",
          .name = "Readme",
          .description = "Project readme",
          .mime_type = "text/plain",
          .size = std::int64_t{42},
          .annotations = Json{{"beta", true}},
          .meta = Json{{"source", "unit-test"}},
      },
      [](const mcp::server::ResourceContext& context)
          -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
        mcp::protocol::ResourcesReadResult result;
        result.contents.push_back(mcp::protocol::ResourceContents{
            .uri = context.uri,
            .mime_type = "text/plain",
            .text = "hello",
        });
        return result;
      });
  require(resource_added.has_value(), "failed to register readme resource");

  const auto template_added =
      server.resource_templates().add(mcp::protocol::ResourceTemplate{
          .title = "Temp file",
          .uri_template = "file:///tmp/{name}.txt",
          .name = "Temp file",
          .description = "A temporary file",
          .mime_type = "text/plain",
          .size = std::int64_t{12},
          .annotations = Json{{"beta", true}},
          .meta = Json{{"source", "unit-test"}},
      });
  require(template_added.has_value(), "failed to register resource template");

  return server;
}

void test_list_tools_round_trip() {
  auto server = make_server();
  mcp::client::Client client(std::make_unique<LoopbackTransport>(server));

  const auto tools = client.list_tools();
  require(tools.has_value(), "list_tools failed");
  require(tools->size() == 1, "unexpected tool count");
  require(tools->front().title == "Echo Tool", "tool title mismatch");
  require(tools->front().name == "echo", "tool name mismatch");
  require(tools->front().description == "Echo the incoming payload",
          "tool description mismatch");
}

void test_get_tool_round_trip() {
  auto server = make_server();

  const auto tool = server.get_tool("echo");
  require(tool.has_value(), "get_tool failed");
  require(tool->title == "Echo Tool", "get_tool title mismatch");
  require(tool->name == "echo", "get_tool name mismatch");
  require(tool->description == "Echo the incoming payload",
          "get_tool description mismatch");

  const auto response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "tools/get",
          .params = Json{{"name", "echo"}},
          .id = std::int64_t{10},
      },
      mcp::server::SessionContext{.session_id = "test-session",
                                  .remote_address = "127.0.0.1"});
  require(response.has_value(), "tools/get request failed");
  require(response->result.has_value(), "tools/get result missing");
  require(response->result->at("name") == "echo",
          "tools/get response name mismatch");
}

void test_server_direct_facade_round_trip() {
  auto server = make_server();

  const auto tools = server.list_tools();
  require(tools.size() == 1, "direct list_tools size mismatch");
  require(tools.front().title == "Echo Tool",
          "direct list_tools title mismatch");
  require(tools.front().name == "echo", "direct list_tools name mismatch");

  const auto prompts = server.list_prompts();
  require(prompts.size() == 1, "direct list_prompts size mismatch");
  require(prompts.front().title == "Summarize Prompt",
          "direct list_prompts title mismatch");
  require(prompts.front().name == "summarize",
          "direct list_prompts name mismatch");

  const auto resources = server.list_resources();
  require(resources.size() == 1, "direct list_resources size mismatch");
  require(resources.front().title == "Readme",
          "direct list_resources title mismatch");
  require(resources.front().uri == "file:///tmp/readme.txt",
          "direct list_resources uri mismatch");

  const auto templates = server.list_resource_templates();
  require(templates.size() == 1,
          "direct list_resource_templates size mismatch");
  require(templates.front().title == "Temp file",
          "direct list_resource_templates title mismatch");
  require(templates.front().uri_template == "file:///tmp/{name}.txt",
          "direct list_resource_templates uriTemplate mismatch");

  const auto tool = server.get_tool("echo");
  require(tool.has_value(), "direct get_tool failed");
  const auto call = server.call_tool("echo", Json{{"value", 7}}, "session-1");
  require(call.has_value(), "direct call_tool failed");
  require(call->content.front().text == "{\"value\":7}",
          "direct call_tool text mismatch");

  const auto prompt =
      server.get_prompt("summarize", Json{{"text", "hello"}}, "session-1");
  require(prompt.has_value(), "direct get_prompt failed");
  require(prompt->messages.front().content.text == "hello",
          "direct get_prompt text mismatch");

  const auto resource = server.read_resource("file:///tmp/readme.txt",
                                             Json::object(), "session-1");
  require(resource.has_value(), "direct read_resource failed");
  require(resource->contents.front().text == "hello",
          "direct read_resource text mismatch");
}

void test_role_aware_peer_facades_forward_to_client_and_server() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::ClientPeer client_peer(std::move(transport));

  const auto tools = client_peer.list_tools();
  require(tools.has_value(), "client peer list_tools failed");
  require(tools->size() == 1, "client peer tool count mismatch");
  require(tools->front().name == "fake-tool", "client peer tool name mismatch");
  require(recording->requests.front().method == "tools/list",
          "client peer method mismatch");

  require(client_peer.notify_cancelled(std::int64_t{7}, "stopped").has_value(),
          "client peer notify_cancelled failed");
  require(client_peer.notify_progress(std::int64_t{8}, 0.5, 1.0, "halfway")
              .has_value(),
          "client peer notify_progress failed");
  require(client_peer.notify_roots_list_changed().has_value(),
          "client peer notify_roots_list_changed failed");
  require(recording->notifications.size() == 3,
          "client peer notifications mismatch");
  require(recording->notifications.at(0).method ==
              mcp::protocol::CancelledNotificationMethod,
          "client peer cancelled method mismatch");
  require(recording->notifications.at(1).method ==
              mcp::protocol::ProgressNotificationMethod,
          "client peer progress method mismatch");
  require(recording->notifications.at(2).method ==
              mcp::protocol::RootsListChangedNotificationMethod,
          "client peer roots method mismatch");

  client_peer.set_roots({mcp::client::Client::Root{.uri = "file:///workspace",
                                                   .name = "workspace"}});
  const auto peer_roots = client_peer.list_roots();
  require(peer_roots.size() == 1, "client peer roots size mismatch");
  require(peer_roots.front().uri == "file:///workspace",
          "client peer roots uri mismatch");

  auto server = std::make_unique<mcp::server::Server>(make_server());
  mcp::ServerPeer server_peer(std::move(server));

  const auto server_tools = server_peer.list_tools();
  require(server_tools.size() == 1, "server peer list_tools size mismatch");
  require(server_tools.front().name == "echo",
          "server peer tool name mismatch");

  const auto called =
      server_peer.call_tool("echo", Json{{"value", 9}}, "session-1");
  require(called.has_value(), "server peer call_tool failed");
  require(called->content.front().text == "{\"value\":9}",
          "server peer call result mismatch");

  require(server_peer.notify_roots_list_changed().has_value(),
          "server peer notify_roots_list_changed failed");
  require(server_peer
              .notify_logging_message(
                  mcp::protocol::LoggingMessageNotificationParams{
                      .level = mcp::protocol::LoggingLevel::Info,
                      .logger = "test",
                      .data = Json{{"message", "hello"}},
                  })
              .has_value(),
          "server peer notify_logging_message failed");
  require(server_peer.notify_elicitation_complete("elicitation-1").has_value(),
          "server peer notify_elicitation_complete failed");
}

void test_client_peer_typed_async_helpers() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::ClientPeer peer(std::move(transport));

  mcp::RequestOptions options;
  options.timeout = std::chrono::seconds(1);
  options.meta = Json{{"traceId", "typed-async"}};

  const auto listed_handle = peer.list_tools_async(options);
  const auto listed = listed_handle.await_response();
  require(listed.has_value(), "typed async list_tools failed");
  require(listed->front().name == "fake-tool",
          "typed async list_tools result mismatch");
  const auto listed_again = listed_handle.await_response();
  require(listed_again.has_value(),
          "typed async list_tools multi-await failed");
  require(listed_again->front().name == "fake-tool",
          "typed async list_tools multi-await result mismatch");

  const auto prompts = peer.list_prompts_async(options).await_response();
  require(prompts.has_value(), "typed async list_prompts failed");
  require(prompts->front().name == "summarize",
          "typed async list_prompts result mismatch");

  const auto resources = peer.list_resources_async(options).await_response();
  require(resources.has_value(), "typed async list_resources failed");
  require(resources->front().uri == "file:///tmp/readme.txt",
          "typed async list_resources result mismatch");

  const auto templates =
      peer.list_resource_templates_async(options).await_response();
  require(templates.has_value(), "typed async list_resource_templates failed");
  require(templates->front().uri_template == "file:///tmp/{name}.txt",
          "typed async list_resource_templates result mismatch");

  const auto called =
      peer.call_tool_async("fake-tool", Json{{"value", 7}}, options)
          .await_response();
  require(called.has_value(), "typed async call_tool failed");
  require(called->content.front().text == "called",
          "typed async call_tool result mismatch");

  const auto prompt =
      peer.get_prompt_async("summarize", Json{{"text", "hello"}}, options)
          .await_response();
  require(prompt.has_value(), "typed async get_prompt failed");
  require(prompt->messages.front().content.text == "Summarize hello",
          "typed async get_prompt result mismatch");

  const auto resource =
      peer.read_resource_async("file:///tmp/readme.txt", options)
          .await_response();
  require(resource.has_value(), "typed async read_resource failed");
  require(resource->contents.front().text == "hello",
          "typed async read_resource result mismatch");

  const auto completed =
      peer.complete_async(
              mcp::protocol::CompleteParams{
                  .ref =
                      mcp::protocol::prompt_completion_reference("summarize"),
                  .argument =
                      mcp::protocol::CompletionArgument{
                          .name = "text",
                          .value = "he",
                      },
              },
              options)
          .await_response();
  require(completed.has_value(), "typed async complete failed");
  require(completed->completion.values.front() == "hello",
          "typed async complete result mismatch");

  const auto sampled = peer.create_message_async(
                               mcp::protocol::CreateMessageParams{
                                   .messages = {mcp::protocol::SamplingMessage{
                                       .role = "user",
                                       .content =
                                           mcp::protocol::ContentBlock{
                                               .type = "text",
                                               .text = "hello",
                                           },
                                   }},
                                   .max_tokens = 16,
                               },
                               options)
                           .await_response();
  require(sampled.has_value(), "typed async create_message failed");
  require(sampled->content.text == "sample",
          "typed async create_message result mismatch");

  const auto schema = mcp::protocol::ElicitationSchema::Builder()
                          .required_bool("accepted")
                          .build();
  require(schema.has_value(), "typed async elicitation schema failed");
  const auto elicited = peer.create_elicitation_async(
                                mcp::protocol::CreateElicitationRequestParam{
                                    .message = "choose",
                                    .requested_schema = *schema,
                                },
                                options)
                            .await_response();
  require(elicited.has_value(), "typed async create_elicitation failed");
  require(elicited->action == mcp::protocol::ElicitationAction::Accept,
          "typed async create_elicitation result mismatch");

  const auto task_call =
      peer.call_tool_task_async(
              mcp::protocol::ToolCall{
                  .name = "fake-tool",
                  .arguments = Json{{"value", 9}},
                  .task = mcp::protocol::TaskRequestParameters{.ttl = 60},
              },
              options)
          .await_response();
  require(task_call.has_value(), "typed async call_tool_task failed");
  require(task_call->task.task_id == "task-created",
          "typed async call_tool_task result mismatch");

  const auto requests_before_invalid_task_call = recording->requests.size();
  const auto invalid_task_call = peer.call_tool_task_async(
                                         mcp::protocol::ToolCall{
                                             .name = "fake-tool",
                                             .arguments = Json::object(),
                                         },
                                         options)
                                     .await_response();
  require(!invalid_task_call.has_value(),
          "invalid typed async call_tool_task should fail");
  require(invalid_task_call.error().message ==
              "task-aware tool call requires task parameters",
          "invalid typed async call_tool_task error mismatch");
  require(recording->requests.size() == requests_before_invalid_task_call,
          "invalid typed async call_tool_task should not send a request");

  const auto tasks = peer.list_tasks_async(options).await_response();
  require(tasks.has_value(), "typed async list_tasks failed");
  require(tasks->front().task_id == "task-1",
          "typed async list_tasks result mismatch");

  const auto task = peer.get_task_async("task-1", options).await_response();
  require(task.has_value(), "typed async get_task failed");
  require(task->task_id == "task-1", "typed async get_task result mismatch");

  const auto cancelled =
      peer.cancel_task_async("task-1", options).await_response();
  require(cancelled.has_value(), "typed async cancel_task failed");
  require(cancelled->status == mcp::protocol::TaskStatus::Cancelled,
          "typed async cancel_task result mismatch");

  const auto task_result =
      peer.task_result_async("task-1", options).await_response();
  require(task_result.has_value(), "typed async task_result failed");
  require(task_result->at("value") == "task-complete",
          "typed async task_result payload mismatch");

  const auto raw_listed =
      peer.request_async("tools/list", Json::object(), options)
          .await_response();
  require(raw_listed.has_value(), "raw async tools/list failed");
  require(raw_listed->at("tools").is_array(), "raw async tools/list mismatch");

  require(recording->requests.size() == 16,
          "typed async request count mismatch");
  require(recording->requests.at(0).method == "tools/list",
          "typed async list_tools method mismatch");
  require(recording->requests.at(1).method == "prompts/list",
          "typed async list_prompts method mismatch");
  require(recording->requests.at(2).method == "resources/list",
          "typed async list_resources method mismatch");
  require(recording->requests.at(3).method == "resources/templates/list",
          "typed async list_resource_templates method mismatch");
  require(recording->requests.at(4).method == "tools/call",
          "typed async call_tool method mismatch");
  require(recording->requests.at(5).method == "prompts/get",
          "typed async get_prompt method mismatch");
  require(recording->requests.at(6).method == "resources/read",
          "typed async read_resource method mismatch");
  require(recording->requests.at(7).method == "completion/complete",
          "typed async complete method mismatch");
  require(recording->requests.at(8).method == "sampling/createMessage",
          "typed async create_message method mismatch");
  require(recording->requests.at(9).method == "elicitation/create",
          "typed async create_elicitation method mismatch");
  require(recording->requests.at(10).method == "tools/call",
          "typed async call_tool_task method mismatch");
  require(recording->requests.at(11).method == "tasks/list",
          "typed async list_tasks method mismatch");
  require(recording->requests.at(12).method == "tasks/get",
          "typed async get_task method mismatch");
  require(recording->requests.at(13).method == "tasks/cancel",
          "typed async cancel_task method mismatch");
  require(recording->requests.at(14).method == "tasks/result",
          "typed async task_result method mismatch");
  require(recording->requests.at(15).method == "tools/list",
          "raw async request method mismatch");
  for (const auto& request : recording->requests) {
    require(request.meta.has_value(), "typed async request meta missing");
    require(request.meta->at("traceId") == "typed-async",
            "typed async request meta mismatch");
  }
}

void test_server_client_peer_request_handle_times_out_and_cancels() {
  BlockingRequestServerTransport transport;
  mcp::server::ClientPeer peer(&transport);

  mcp::RequestOptions options;
  options.timeout = std::chrono::milliseconds(1);

  auto handle = peer.request_async("tools/list", Json::object(), options);
  transport.wait_until_request_started();

  const auto response = handle.await_response();
  require(!response.has_value(), "request handle should time out");
  require(response.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "request handle timeout code mismatch");
  require(response.error().message == "request timed out",
          "request handle timeout message mismatch");
  require(response.error().detail == "1ms",
          "request handle timeout detail mismatch");

  const auto repeated_response = handle.await_response();
  require(!repeated_response.has_value(),
          "request handle repeated timeout should fail");
  require(repeated_response.error().code == response.error().code,
          "request handle repeated timeout code mismatch");
  require(repeated_response.error().message == response.error().message,
          "request handle repeated timeout message mismatch");
  require(repeated_response.error().detail == response.error().detail,
          "request handle repeated timeout detail mismatch");

  transport.wait_until_notifications(1);
  require(transport.notifications.size() == 1,
          "request handle timeout should send one cancellation");
  require(transport.notifications.front().method ==
              mcp::protocol::CancelledNotificationMethod,
          "request handle cancellation notification mismatch");

  const auto cancelled = mcp::protocol::cancelled_notification_params_from_json(
      transport.notifications.front().params);
  require(cancelled.has_value(), "cancelled params should parse");
  require(cancelled->reason == "request timeout", "cancelled reason mismatch");
  require(std::get<std::int64_t>(cancelled->request_id) ==
              std::get<std::int64_t>(handle.request_id()),
          "cancelled request id mismatch");

  transport.release();
  transport.wait_until_request_finished();
}

void test_server_client_peer_request_handle_explicit_cancel_notifies() {
  BlockingRequestServerTransport transport;
  mcp::server::ClientPeer peer(&transport);

  auto handle = peer.request_async("tools/list");
  transport.wait_until_request_started();

  const auto cancel_result = handle.cancel("request cancelled");
  require(cancel_result.has_value(),
          "server request handle explicit cancel should notify");

  transport.wait_until_notifications(1);
  require(transport.notifications.front().method ==
              mcp::protocol::CancelledNotificationMethod,
          "server explicit cancellation notification mismatch");

  const auto cancelled = mcp::protocol::cancelled_notification_params_from_json(
      transport.notifications.front().params);
  require(cancelled.has_value(),
          "server explicit cancelled params should parse");
  require(cancelled->reason == "request cancelled",
          "server explicit cancelled reason mismatch");
  require(std::get<std::int64_t>(cancelled->request_id) ==
              std::get<std::int64_t>(handle.request_id()),
          "server explicit cancelled request id mismatch");

  transport.release();
  const auto response = handle.await_response();
  require(response.has_value(), "server request should still complete");
  transport.wait_until_request_finished();
}

void test_client_peer_request_handle_times_out_and_cancels() {
  auto transport = std::make_unique<BlockingRequestClientTransport>();
  auto* recording = transport.get();
  mcp::Peer<mcp::RoleClient> peer(std::move(transport));

  mcp::RequestOptions options;
  options.timeout = std::chrono::milliseconds(1);

  auto handle = peer.request_async("tools/list", Json::object(), options);
  recording->wait_until_request_started();

  const auto response = handle.await_response();
  require(!response.has_value(), "client request handle should time out");
  require(response.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "client request handle timeout code mismatch");
  require(response.error().message == "request timed out",
          "client request handle timeout message mismatch");
  require(response.error().detail == "1ms",
          "client request handle timeout detail mismatch");

  const auto repeated_response = handle.await_response();
  require(!repeated_response.has_value(),
          "client request repeated timeout should fail");
  require(repeated_response.error().code == response.error().code,
          "client request repeated timeout code mismatch");
  require(repeated_response.error().message == response.error().message,
          "client request repeated timeout message mismatch");
  require(repeated_response.error().detail == response.error().detail,
          "client request repeated timeout detail mismatch");

  recording->wait_until_notifications(1);
  require(recording->notifications.size() == 1,
          "client request timeout should send one cancellation");
  require(recording->notifications.front().method ==
              mcp::protocol::CancelledNotificationMethod,
          "client request handle cancellation notification mismatch");

  const auto cancelled = mcp::protocol::cancelled_notification_params_from_json(
      recording->notifications.front().params);
  require(cancelled.has_value(), "client cancelled params should parse");
  require(cancelled->reason == "request timeout",
          "client cancelled reason mismatch");
  require(std::get<std::int64_t>(cancelled->request_id) ==
              std::get<std::int64_t>(handle.request_id()),
          "client cancelled request id mismatch");

  recording->release();
  recording->wait_until_request_finished();
}

void test_client_peer_request_handle_observes_cancellation_token() {
  auto transport = std::make_unique<BlockingRequestClientTransport>();
  auto* recording = transport.get();
  mcp::Peer<mcp::RoleClient> peer(std::move(transport));

  mcp::CancellationSource cancellation;
  mcp::RequestOptions options;
  options.cancellation_token = cancellation.token();

  auto handle = peer.list_tools_async(options);
  require(handle.cancellation_token().has_value(),
          "typed request handle should expose cancellation token");
  recording->wait_until_request_started();
  cancellation.cancel();

  const auto response = handle.await_response();
  require(!response.has_value(), "client request handle should cancel");
  require(response.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "client request handle cancellation code mismatch");
  require(response.error().message == "request cancelled",
          "client request handle cancellation message mismatch");

  const auto repeated_response = handle.await_response();
  require(!repeated_response.has_value(),
          "client request repeated cancellation should fail");
  require(repeated_response.error().code == response.error().code,
          "client request repeated cancellation code mismatch");
  require(repeated_response.error().message == response.error().message,
          "client request repeated cancellation message mismatch");

  recording->wait_until_notifications(1);
  require(recording->notifications.size() == 1,
          "client request cancellation should send one notification");
  require(recording->notifications.front().method ==
              mcp::protocol::CancelledNotificationMethod,
          "client request token cancellation notification mismatch");

  const auto cancelled = mcp::protocol::cancelled_notification_params_from_json(
      recording->notifications.front().params);
  require(cancelled.has_value(), "token cancelled params should parse");
  require(cancelled->reason == "request cancelled",
          "token cancelled reason mismatch");
  require(std::get<std::int64_t>(cancelled->request_id) ==
              std::get<std::int64_t>(handle.request_id()),
          "token cancelled request id mismatch");

  recording->release();
  recording->wait_until_request_finished();
}

void test_request_handle_errors_are_stable_and_structured() {
  mcp::RequestHandle<Json> empty_handle;
  const auto empty = empty_handle.await_response();
  require(!empty.has_value(), "empty request handle should fail");
  require(empty.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "empty request handle error code mismatch");
  require(empty.error().message == "request handle has no response state",
          "empty request handle error message mismatch");

  auto missing_task = mcp::RequestHandle<Json>::spawn(
      std::int64_t{401}, std::nullopt, std::nullopt, {},
      std::function<mcp::core::Result<Json>()>{});
  const auto missing = missing_task.await_response();
  require(!missing.has_value(), "missing request task should fail");
  require(missing.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "missing request task error code mismatch");
  require(missing.error().message == "request task is not configured",
          "missing request task error message mismatch");

  auto throwing_task = mcp::RequestHandle<Json>::spawn(
      std::int64_t{402}, std::nullopt, std::nullopt, {},
      []() -> mcp::core::Result<Json> { throw std::runtime_error("boom"); });
  const auto thrown = throwing_task.await_response();
  require(!thrown.has_value(), "throwing request task should fail");
  require(thrown.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "throwing request task error code mismatch");
  require(thrown.error().message == "request worker threw an exception",
          "throwing request task error message mismatch");
  require(thrown.error().detail == "boom",
          "throwing request task error detail mismatch");

  auto unknown_throwing_task = mcp::RequestHandle<Json>::spawn(
      std::int64_t{403}, std::nullopt, std::nullopt, {},
      []() -> mcp::core::Result<Json> { throw 7; });
  const auto unknown_thrown = unknown_throwing_task.await_response();
  require(!unknown_thrown.has_value(),
          "unknown throwing request task should fail");
  require(unknown_thrown.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "unknown throwing request task error code mismatch");
  require(unknown_thrown.error().message ==
              "request worker threw an unknown exception",
          "unknown throwing request task error message mismatch");
  require(unknown_thrown.error().detail.empty(),
          "unknown throwing request task detail mismatch");
}

void test_request_executor_can_be_configured_before_first_use() {
  const auto configured = mcp::configure_request_executor(
      mcp::RequestExecutorOptions{.worker_count = 8, .max_queue_size = 256});
  require(configured.has_value(),
          "request executor should configure before first use");

  auto handle = mcp::RequestHandle<Json>::spawn(
      std::int64_t{410}, std::nullopt, std::nullopt, {},
      []() -> mcp::core::Result<Json> { return Json{{"configured", true}}; });
  const auto response = handle.await_response();
  require(response.has_value(), "configured request executor handle failed");
  require(response->at("configured") == true,
          "configured request executor response mismatch");

  const auto reconfigured = mcp::configure_request_executor(
      mcp::RequestExecutorOptions{.worker_count = 3, .max_queue_size = 8});
  require(!reconfigured.has_value(),
          "live request executor reconfiguration should fail");
  require(
      reconfigured.error().message == "request executor is already initialized",
      "live request executor reconfiguration error mismatch");
}

void test_client_peer_cancel_timeout_race_stress() {
  for (int iteration = 0; iteration < 24; ++iteration) {
    auto transport = std::make_unique<BlockingRequestClientTransport>();
    auto* recording = transport.get();
    mcp::Peer<mcp::RoleClient> peer(std::move(transport));

    mcp::CancellationSource cancellation;
    mcp::RequestOptions options;
    options.timeout = std::chrono::milliseconds(2);
    options.cancellation_token = cancellation.token();

    auto handle = peer.request_async("tools/list", Json::object(), options);
    recording->wait_until_request_started();

    std::thread canceller([&cancellation, iteration]() {
      if (iteration % 2 == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      cancellation.cancel();
    });

    const auto response = handle.await_response();
    require(!response.has_value(),
            "client cancel/timeout race should fail terminally");
    require(response.error().message == "request cancelled" ||
                response.error().message == "request timed out",
            "client cancel/timeout race error mismatch");
    if (canceller.joinable()) {
      canceller.join();
    }

    recording->wait_until_notifications(1);
    require(recording->notifications.size() == 1,
            "client cancel/timeout race should send one cancellation");
    require(recording->notifications.front().method ==
                mcp::protocol::CancelledNotificationMethod,
            "client cancel/timeout race notification mismatch");
    const auto cancelled =
        mcp::protocol::cancelled_notification_params_from_json(
            recording->notifications.front().params);
    require(cancelled.has_value(),
            "client cancel/timeout race cancellation params should parse");
    require(std::get<std::int64_t>(cancelled->request_id) ==
                std::get<std::int64_t>(handle.request_id()),
            "client cancel/timeout race request id mismatch");

    recording->release();
    recording->wait_until_request_finished();
  }
}

void test_server_client_peer_cancel_timeout_race_stress() {
  for (int iteration = 0; iteration < 24; ++iteration) {
    BlockingRequestServerTransport transport;
    mcp::server::ClientPeer peer(&transport);

    mcp::CancellationSource cancellation;
    mcp::RequestOptions options;
    options.timeout = std::chrono::milliseconds(2);
    options.cancellation_token = cancellation.token();

    auto handle = peer.request_async("tools/list", Json::object(), options);
    transport.wait_until_request_started();

    std::thread canceller([&cancellation, iteration]() {
      if (iteration % 2 != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      cancellation.cancel();
    });

    const auto response = handle.await_response();
    require(!response.has_value(),
            "server cancel/timeout race should fail terminally");
    require(response.error().message == "request cancelled" ||
                response.error().message == "request timed out",
            "server cancel/timeout race error mismatch");
    if (canceller.joinable()) {
      canceller.join();
    }

    transport.wait_until_notifications(1);
    require(transport.notifications.size() == 1,
            "server cancel/timeout race should send one cancellation");
    require(transport.notifications.front().method ==
                mcp::protocol::CancelledNotificationMethod,
            "server cancel/timeout race notification mismatch");
    const auto cancelled =
        mcp::protocol::cancelled_notification_params_from_json(
            transport.notifications.front().params);
    require(cancelled.has_value(),
            "server cancel/timeout race cancellation params should parse");
    require(std::get<std::int64_t>(cancelled->request_id) ==
                std::get<std::int64_t>(handle.request_id()),
            "server cancel/timeout race request id mismatch");

    transport.release();
    transport.wait_until_request_finished();
  }
}

void test_client_request_ids_are_thread_safe_for_concurrent_async_requests() {
  auto transport = std::make_unique<ConcurrentRequestIdTransport>();
  auto* recording = transport.get();
  mcp::Peer<mcp::RoleClient> peer(std::move(transport));

  const auto warmup = peer.request_async("custom/warmup").await_response();
  require(warmup.has_value(), "warmup request should succeed");

  constexpr int kThreadCount = 8;
  constexpr int kRequestsPerThread = 4;
  constexpr int kRequestCount = kThreadCount * kRequestsPerThread;

  std::atomic<int> ready_threads{0};
  std::atomic_bool start{false};
  std::mutex handles_mutex;
  std::vector<mcp::RequestHandle<Json>> handles;
  handles.reserve(kRequestCount);
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
    threads.emplace_back([&, thread_index]() {
      std::vector<mcp::RequestHandle<Json>> local_handles;
      local_handles.reserve(kRequestsPerThread);
      ready_threads.fetch_add(1, std::memory_order_acq_rel);
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      for (int request_index = 0; request_index < kRequestsPerThread;
           ++request_index) {
        local_handles.push_back(peer.request_async(
            "custom/concurrent",
            Json{{"thread", thread_index}, {"request", request_index}}));
      }

      std::lock_guard<std::mutex> lock(handles_mutex);
      for (auto& handle : local_handles) {
        handles.push_back(std::move(handle));
      }
    });
  }

  wait_until(
      [&]() {
        return ready_threads.load(std::memory_order_acquire) == kThreadCount;
      },
      "concurrent request id workers did not start");
  start.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  require(handles.size() == static_cast<std::size_t>(kRequestCount),
          "concurrent request handle count mismatch");
  std::unordered_set<std::int64_t> ids;
  ids.reserve(handles.size());
  for (const auto& handle : handles) {
    ids.insert(std::get<std::int64_t>(handle.request_id()));
  }
  require(ids.size() == handles.size(),
          "concurrent request ids must be unique");

  for (const auto& handle : handles) {
    const auto response = handle.await_response();
    require(response.has_value(), "concurrent async request failed");
    require(response->at("ok") == true, "concurrent async response mismatch");
  }
  require(
      recording->request_count() == static_cast<std::size_t>(kRequestCount + 1),
      "concurrent transport request count mismatch");
}

void test_service_lifecycle_facades_start_and_stop() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  auto running_client = mcp::serve(mcp::ClientPeer(std::move(transport)));
  require(running_client.has_value(), "client service should start");
  require(running_client->running(), "client service should report running");
  std::atomic_bool client_wait_returned = false;
  std::thread client_waiter([&] {
    const auto waited = running_client->wait();
    require(waited.has_value(), "client service wait failed");
    client_wait_returned.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  require(!client_wait_returned.load(),
          "client service wait should block while running");

  const auto tools = running_client->peer().list_tools();
  require(tools.has_value(), "running client service list_tools failed");
  require(tools->front().name == "fake-tool",
          "running client service tool mismatch");

  const auto client_stopped = running_client->stop();
  require(client_stopped.has_value(), "client service stop failed");
  client_waiter.join();
  require(client_wait_returned.load(),
          "client service wait should return after stop");
  require(!running_client->running(), "client service should report stopped");
  require(recording->stopped, "client service should stop transport");

  auto server = std::make_unique<mcp::server::Server>(make_server());
  auto server_transport = std::make_unique<BlockingLifecycleServerTransport>();
  auto* server_transport_ptr = server_transport.get();
  require(server->add_transport(std::move(server_transport)).has_value(),
          "server service transport add failed");
  auto running_server = mcp::serve(mcp::ServerPeer(std::move(server)));
  require(running_server.has_value(), "server service should start");
  server_transport_ptr->wait_until_started();
  require(running_server->running(), "server service should report running");
  std::atomic_bool server_wait_returned = false;
  std::thread server_waiter([&] {
    const auto waited = running_server->wait();
    require(waited.has_value(), "server service wait failed");
    server_wait_returned.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  require(!server_wait_returned.load(),
          "server service wait should block while running");
  require(running_server->peer().list_tools().front().name == "echo",
          "running server service tool mismatch");

  const auto server_stopped = running_server->stop();
  require(server_stopped.has_value(), "server service stop failed");
  server_waiter.join();
  require(server_wait_returned.load(),
          "server service wait should return after stop");
  require(!running_server->running(), "server service should report stopped");
  require(server_transport_ptr->stopped(),
          "server service should stop transport");
}

void test_server_peer_serves_role_generic_transport_receive_loop() {
  auto server = std::make_unique<mcp::server::Server>(make_server());
  int raw_notifications = 0;
  std::string notification_session;
  server->set_raw_notification_handler(
      [&](const mcp::protocol::JsonRpcNotification& notification,
          const mcp::server::SessionContext& context)
          -> mcp::core::Result<mcp::core::Unit> {
        if (notification.method == "notifications/initialized") {
          ++raw_notifications;
          notification_session = context.session_id;
        }
        return mcp::core::Unit{};
      });

  mcp::ServerPeer peer(std::move(server));
  QueuedRoleServerTransport transport;
  transport.inbound.push_back(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = Json::object(),
      .id = std::int64_t{700},
  });
  transport.inbound.push_back(mcp::protocol::JsonRpcNotification{
      .method = "notifications/initialized",
      .params = Json::object(),
  });

  const auto served =
      peer.serve_transport(transport, mcp::server::SessionContext{
                                          .session_id = "peer-session",
                                          .remote_address = "role-generic-test",
                                      });
  require(served.has_value(), "server peer role transport loop failed");
  require(transport.sent.size() == 1,
          "server peer role transport should send one response");

  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.front());
  require(response != nullptr,
          "server peer role transport should send a response message");
  require(response->id.has_value(), "server peer response id missing");
  require(std::get<std::int64_t>(*response->id) == 700,
          "server peer response id mismatch");
  require(response->result.has_value(), "server peer response result missing");
  require(response->result->at("tools").front().at("name") == "echo",
          "server peer tools/list response mismatch");
  require(raw_notifications == 1,
          "server peer should dispatch transport notifications");
  require(notification_session == "peer-session",
          "server peer should pass transport session context");
}

void test_server_service_serves_role_generic_transport_receive_loop() {
  auto server = std::make_unique<mcp::server::Server>(make_server());
  int raw_notifications = 0;
  std::string notification_session;
  server->set_raw_notification_handler(
      [&](const mcp::protocol::JsonRpcNotification& notification,
          const mcp::server::SessionContext& context)
          -> mcp::core::Result<mcp::core::Unit> {
        if (notification.method == "notifications/initialized") {
          ++raw_notifications;
          notification_session = context.session_id;
        }
        return mcp::core::Unit{};
      });

  auto transport = std::make_unique<QueuedRoleServerTransport>();
  auto* transport_ptr = transport.get();
  transport->inbound.push_back(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = Json::object(),
      .id = std::int64_t{701},
  });
  transport->inbound.push_back(mcp::protocol::JsonRpcNotification{
      .method = "notifications/initialized",
      .params = Json::object(),
  });

  auto running = mcp::serve(
      mcp::ServerPeer(std::move(server)), std::move(transport),
      mcp::server::SessionContext{.session_id = "service-session",
                                  .remote_address = "role-service-test"});
  require(running.has_value(), "server service role transport should start");

  const auto waited = running->wait();
  require(waited.has_value(), "server service role transport wait failed");
  require(!running->running(),
          "server service role transport should stop after eof");
  require(transport_ptr->sent.size() == 1,
          "server service role transport should send one response");

  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport_ptr->sent.front());
  require(response != nullptr,
          "server service role transport should send a response message");
  require(response->id.has_value(), "server service response id missing");
  require(std::get<std::int64_t>(*response->id) == 701,
          "server service response id mismatch");
  require(response->result.has_value(),
          "server service response result missing");
  require(response->result->at("tools").front().at("name") == "echo",
          "server service tools/list response mismatch");
  require(raw_notifications == 1,
          "server service should dispatch transport notifications");
  require(notification_session == "service-session",
          "server service should pass transport session context");
}

void test_server_service_native_transport_stop_cancels_loop() {
  auto server = std::make_unique<mcp::server::Server>(make_server());
  auto transport = std::make_unique<BlockingRoleServerTransport>();
  auto* transport_ptr = transport.get();

  auto running =
      mcp::serve(mcp::ServerPeer(std::move(server)), std::move(transport));
  require(running.has_value(),
          "server service blocking role transport should start");
  transport_ptr->wait_until_receiving();
  require(running->running(),
          "server service blocking role transport should be running");
  require(!running->cancellation_token().cancelled(),
          "server service token should not start cancelled");

  std::atomic_bool wait_returned = false;
  std::thread waiter([&] {
    const auto waited = running->wait();
    require(waited.has_value(),
            "server service blocking role transport wait failed");
    wait_returned.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  require(!wait_returned.load(),
          "server service blocking role transport wait should block");

  const auto stopped = running->stop();
  require(stopped.has_value(),
          "server service blocking role transport stop failed");
  if (waiter.joinable()) {
    waiter.join();
  }
  require(wait_returned.load(),
          "server service blocking role transport wait should return");
  require(!running->running(),
          "server service blocking role transport should stop");
  require(running->cancellation_token().cancelled(),
          "server service token should be cancelled after stop");
  require(transport_ptr->closed(),
          "server service stop should close native role transport");
}

void test_server_non_task_tool_observes_cancelled_notification() {
  mcp::server::Server server(mcp::server::ServerOptions{});
  std::atomic_bool handler_started = false;
  std::atomic_bool handler_observed_cancel = false;
  const auto added = server.tools().add(
      mcp::protocol::ToolDefinition{
          .name = "cancellable",
          .input_schema = Json::object(),
      },
      [&](const mcp::server::ToolContext& context)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        handler_started.store(true);
        wait_until([&] { return context.cancelled(); },
                   "non-task tool should observe cancellation");
        handler_observed_cancel.store(true);
        return mcp::protocol::ToolResult{};
      });
  require(added.has_value(), "failed to register cancellable tool");

  std::optional<mcp::protocol::JsonRpcResponse> response;
  std::optional<mcp::core::Error> response_error;
  std::exception_ptr thread_error;
  const mcp::protocol::JsonRpcRequest request{
      .method = std::string(mcp::protocol::ToolsCallMethod),
      .params = Json{{"name", "cancellable"}, {"arguments", Json::object()}},
      .id = std::int64_t{909},
  };
  std::thread worker([&] {
    try {
      const auto handled = server.handle_request(
          request, mcp::server::SessionContext{.session_id = "cancel-session"});
      if (handled) {
        response = *handled;
      } else {
        response_error = handled.error();
      }
    } catch (...) {
      thread_error = std::current_exception();
    }
  });

  wait_until([&] { return handler_started.load(); },
             "non-task cancellation handler should start");
  const auto cancelled = server.handle_notification(
      mcp::protocol::JsonRpcNotification{
          .method = std::string(mcp::protocol::CancelledNotificationMethod),
          .params = mcp::protocol::cancelled_notification_params_to_json(
              mcp::protocol::CancelledNotificationParams{
                  .request_id = std::int64_t{909},
                  .reason = "client cancelled",
              }),
      },
      mcp::server::SessionContext{.session_id = "cancel-session"});
  require(cancelled.has_value(), "cancelled notification should be accepted");

  if (worker.joinable()) {
    worker.join();
  }
  if (thread_error) {
    std::rethrow_exception(thread_error);
  }
  require(!response_error.has_value(),
          "non-task cancellation request should not fail transport");
  require(response.has_value(), "non-task cancellation response missing");
  require(response->result.has_value(),
          "non-task cancellation response should contain a result");
  require(handler_observed_cancel.load(),
          "non-task tool did not observe cancellation");
}

void test_completion_and_sampling_handlers_observe_cancellation_token() {
  {
    auto server = make_server();
    std::atomic_bool started{false};
    std::atomic_bool observed_cancelled{false};

    server.set_completion_handler(
        mcp::server::Server::JsonRequestContextHandler{
            [&](const Json&, const mcp::server::SessionContext&,
                mcp::server::CancellationToken cancellation)
                -> mcp::core::Result<Json> {
              started.store(true);
              wait_until([&] { return cancellation.cancelled(); },
                         "completion handler should observe cancellation");
              observed_cancelled.store(cancellation.cancelled());
              return Json{{"cancelled", observed_cancelled.load()}};
            }});

    std::optional<mcp::core::Result<mcp::protocol::JsonRpcResponse>> response;
    std::thread worker([&] {
      response = server.handle_request(
          mcp::protocol::JsonRpcRequest{
              .method = std::string(mcp::protocol::CompletionCompleteMethod),
              .params = Json::object(),
              .id = std::int64_t{301},
          },
          mcp::server::SessionContext{.session_id = "completion"});
    });

    wait_until([&] { return started.load(); },
               "completion cancellation handler should start");
    const auto cancelled = server.handle_notification(
        mcp::protocol::JsonRpcNotification{
            .method = std::string(mcp::protocol::CancelledNotificationMethod),
            .params = mcp::protocol::cancelled_notification_params_to_json(
                mcp::protocol::CancelledNotificationParams{
                    .request_id = std::int64_t{301},
                    .reason = "test",
                }),
        },
        {});
    require(cancelled.has_value(),
            "completion cancellation notification failed");
    worker.join();

    require(response.has_value(), "completion cancellation response missing");
    require(response->has_value(), "completion cancellation request failed");
    require((*response)->result.has_value(),
            "completion cancellation result missing");
    require((*response)->result->at("cancelled") == true,
            "completion handler cancellation token mismatch");
    require(observed_cancelled.load(),
            "completion handler did not observe cancellation");
  }

  {
    std::atomic_bool started{false};
    std::atomic_bool observed_cancelled{false};
    auto built =
        mcp::server::App::builder()
            .sampling(
                [&](const Json&, mcp::server::CancellationToken cancellation) {
                  started.store(true);
                  wait_until([&] { return cancellation.cancelled(); },
                             "sampling handler should observe cancellation");
                  observed_cancelled.store(cancellation.cancelled());
                  return Json{{"cancelled", observed_cancelled.load()}};
                })
            .build();
    require(built.has_value(), "sampling cancellation server should build");

    std::optional<mcp::core::Result<mcp::protocol::JsonRpcResponse>> response;
    std::thread worker([&] {
      response = (*built)->handle_request(
          mcp::protocol::JsonRpcRequest{
              .method = std::string(mcp::protocol::SamplingCreateMessageMethod),
              .params = Json::object(),
              .id = std::int64_t{302},
          },
          mcp::server::SessionContext{.session_id = "sampling"});
    });

    wait_until([&] { return started.load(); },
               "sampling cancellation handler should start");
    const auto cancelled = (*built)->handle_notification(
        mcp::protocol::JsonRpcNotification{
            .method = std::string(mcp::protocol::CancelledNotificationMethod),
            .params = mcp::protocol::cancelled_notification_params_to_json(
                mcp::protocol::CancelledNotificationParams{
                    .request_id = std::int64_t{302},
                    .reason = "test",
                }),
        },
        {});
    require(cancelled.has_value(), "sampling cancellation notification failed");
    worker.join();

    require(response.has_value(), "sampling cancellation response missing");
    require(response->has_value(), "sampling cancellation request failed");
    require((*response)->result.has_value(),
            "sampling cancellation result missing");
    require((*response)->result->at("cancelled") == true,
            "sampling handler cancellation token mismatch");
    require(observed_cancelled.load(),
            "sampling handler did not observe cancellation");
  }
}

void test_contract_completion_sampling_handlers_receive_cancellation_token() {
  struct ContractCancellationHandler final
      : mcp::server::ServerHandlerInterface {
    std::atomic_bool* completion_started = nullptr;
    std::atomic_bool* completion_observed_cancelled = nullptr;
    std::atomic_bool* sampling_started = nullptr;
    std::atomic_bool* sampling_observed_cancelled = nullptr;

    std::optional<mcp::core::Result<Json>> on_completion(
        const Json&, const mcp::server::SessionContext& context,
        mcp::server::CancellationToken cancellation) const override {
      require(context.session_id == "completion-contract",
              "contract completion session mismatch");
      completion_started->store(true);
      wait_until([&] { return cancellation.cancelled(); },
                 "contract completion should observe cancellation");
      completion_observed_cancelled->store(cancellation.cancelled());
      return Json{{"cancelled", completion_observed_cancelled->load()}};
    }

    std::optional<mcp::core::Result<Json>> on_sampling(
        const Json&, const mcp::server::SessionContext& context,
        mcp::server::CancellationToken cancellation) const override {
      require(context.session_id == "sampling-contract",
              "contract sampling session mismatch");
      sampling_started->store(true);
      wait_until([&] { return cancellation.cancelled(); },
                 "contract sampling should observe cancellation");
      sampling_observed_cancelled->store(cancellation.cancelled());
      return Json{{"cancelled", sampling_observed_cancelled->load()}};
    }
  };

  std::atomic_bool completion_started{false};
  std::atomic_bool completion_observed_cancelled{false};
  std::atomic_bool sampling_started{false};
  std::atomic_bool sampling_observed_cancelled{false};
  ContractCancellationHandler handler;
  handler.completion_started = &completion_started;
  handler.completion_observed_cancelled = &completion_observed_cancelled;
  handler.sampling_started = &sampling_started;
  handler.sampling_observed_cancelled = &sampling_observed_cancelled;

  {
    auto server = make_server();
    server.set_handler(handler);

    std::optional<mcp::core::Result<mcp::protocol::JsonRpcResponse>> response;
    std::thread worker([&] {
      response = server.handle_request(
          mcp::protocol::JsonRpcRequest{
              .method = std::string(mcp::protocol::CompletionCompleteMethod),
              .params = Json::object(),
              .id = std::int64_t{303},
          },
          mcp::server::SessionContext{.session_id = "completion-contract"});
    });

    wait_until([&] { return completion_started.load(); },
               "contract completion handler should start");
    const auto cancelled = server.handle_notification(
        mcp::protocol::JsonRpcNotification{
            .method = std::string(mcp::protocol::CancelledNotificationMethod),
            .params = mcp::protocol::cancelled_notification_params_to_json(
                mcp::protocol::CancelledNotificationParams{
                    .request_id = std::int64_t{303},
                    .reason = "test",
                }),
        },
        {});
    require(cancelled.has_value(),
            "contract completion cancellation notification failed");
    worker.join();

    require(response.has_value(),
            "contract completion cancellation response missing");
    require(response->has_value(),
            "contract completion cancellation request failed");
    require((*response)->result.has_value(),
            "contract completion cancellation result missing");
    require((*response)->result->at("cancelled") == true,
            "contract completion cancellation token mismatch");
    require(completion_observed_cancelled.load(),
            "contract completion handler did not observe cancellation");
  }

  {
    auto server = make_server();
    server.set_handler(handler);

    std::optional<mcp::core::Result<mcp::protocol::JsonRpcResponse>> response;
    std::thread worker([&] {
      response = server.handle_request(
          mcp::protocol::JsonRpcRequest{
              .method = std::string(mcp::protocol::SamplingCreateMessageMethod),
              .params = Json::object(),
              .id = std::int64_t{304},
          },
          mcp::server::SessionContext{.session_id = "sampling-contract"});
    });

    wait_until([&] { return sampling_started.load(); },
               "contract sampling handler should start");
    const auto cancelled = server.handle_notification(
        mcp::protocol::JsonRpcNotification{
            .method = std::string(mcp::protocol::CancelledNotificationMethod),
            .params = mcp::protocol::cancelled_notification_params_to_json(
                mcp::protocol::CancelledNotificationParams{
                    .request_id = std::int64_t{304},
                    .reason = "test",
                }),
        },
        {});
    require(cancelled.has_value(),
            "contract sampling cancellation notification failed");
    worker.join();

    require(response.has_value(),
            "contract sampling cancellation response missing");
    require(response->has_value(),
            "contract sampling cancellation request failed");
    require((*response)->result.has_value(),
            "contract sampling cancellation result missing");
    require((*response)->result->at("cancelled") == true,
            "contract sampling cancellation token mismatch");
    require(sampling_observed_cancelled.load(),
            "contract sampling handler did not observe cancellation");
  }
}

void test_contract_handlers_override_client_and_server_requests() {
  struct ContractClientHandler final : mcp::client::ClientHandlerInterface {
    std::optional<mcp::core::Result<mcp::protocol::RootsListResult>>
    on_list_roots_request() const override {
      return mcp::protocol::RootsListResult{
          .roots = {mcp::protocol::Root{.uri = "file:///workspace",
                                        .name = "workspace"}},
      };
    }
  } client_handler;

  mcp::client::Client client(std::make_unique<RecordingTransport>());
  client.set_handler(client_handler);

  const auto client_response =
      client.handle_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::RootsListMethod),
          .params = mcp::protocol::Json::object(),
          .id = std::int64_t{100},
      });
  require(client_response.has_value(),
          "contract client roots request should succeed");
  require(client_response->result.has_value(),
          "contract client roots result missing");
  require(
      client_response->result->at("roots").front().at("name") == "workspace",
      "contract client roots result mismatch");

  mcp::ClientPeer client_peer(std::make_unique<RecordingTransport>());
  client_peer.set_handler(client_handler);
  const auto peer_client_message = client_peer.dispatch_message(
      mcp::protocol::JsonRpcMessage{mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::RootsListMethod),
          .params = mcp::protocol::Json::object(),
          .id = std::int64_t{102},
      }});
  require(peer_client_message.has_value(),
          "contract client peer roots dispatch should succeed");
  require(peer_client_message->has_value(),
          "contract client peer roots response missing");
  const auto* peer_client_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&**peer_client_message);
  require(peer_client_response != nullptr,
          "contract client peer roots response type mismatch");
  require(peer_client_response->result.has_value(),
          "contract client peer roots result missing");
  require(peer_client_response->result->at("roots").front().at("name") ==
              "workspace",
          "contract client peer roots result mismatch");

  struct ContractServerHandler final : mcp::server::ServerHandlerInterface {
    std::optional<mcp::core::Result<mcp::protocol::Json>> on_completion(
        const mcp::protocol::Json&) const override {
      return mcp::protocol::Json{
          {"values", mcp::protocol::Json::array({"hello"})}};
    }
  } server_handler;

  auto server = make_server();
  server.set_handler(server_handler);

  const auto server_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::CompletionCompleteMethod),
          .params =
              mcp::protocol::Json{
                  {"ref", mcp::protocol::Json{{"type", "ref/prompt"},
                                              {"name", "summarize"}}},
                  {"argument",
                   mcp::protocol::Json{{"name", "text"}, {"value", "he"}}},
              },
          .id = std::int64_t{101},
      },
      mcp::server::SessionContext{.session_id = "session-1",
                                  .remote_address = "127.0.0.1"});
  require(server_response.has_value(),
          "contract server completion request should succeed");
  require(server_response->result.has_value(),
          "contract server completion result missing");
  require(server_response->result->at("values").front() == "hello",
          "contract server completion result mismatch");

  mcp::ServerPeer server_peer(mcp::server::ServerOptions{});
  server_peer.set_handler(server_handler);
  const auto peer_server_response = server_peer.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::CompletionCompleteMethod),
          .params =
              mcp::protocol::Json{
                  {"ref", mcp::protocol::Json{{"type", "ref/prompt"},
                                              {"name", "summarize"}}},
                  {"argument",
                   mcp::protocol::Json{{"name", "text"}, {"value", "he"}}},
              },
          .id = std::int64_t{103},
      },
      mcp::server::SessionContext{.session_id = "session-1"});
  require(peer_server_response.has_value(),
          "contract server peer completion request should succeed");
  require(peer_server_response->result.has_value(),
          "contract server peer completion result missing");
  require(peer_server_response->result->at("values").front() == "hello",
          "contract server peer completion result mismatch");
}

void test_contract_discovery_handlers_override_registries() {
  struct ContractDiscoveryHandler final : mcp::server::ServerHandlerInterface {
    std::optional<mcp::core::Result<mcp::protocol::ToolsListResult>>
    on_list_tools(const mcp::server::SessionContext& context) const override {
      mcp::protocol::ToolDefinition tool;
      tool.name = context.session_id + "-tool";
      tool.input_schema = Json::object();
      mcp::protocol::ToolsListResult result;
      result.tools.push_back(std::move(tool));
      return result;
    }

    std::optional<mcp::core::Result<mcp::protocol::ToolDefinition>> on_get_tool(
        std::string_view name,
        const mcp::server::SessionContext& context) const override {
      mcp::protocol::ToolDefinition tool;
      tool.name = context.session_id + "-" + std::string(name);
      tool.input_schema = Json::object();
      return tool;
    }

    std::optional<mcp::core::Result<mcp::protocol::PromptsListResult>>
    on_list_prompts(const mcp::server::SessionContext& context) const override {
      mcp::protocol::PromptsListResult result;
      result.prompts.push_back(mcp::protocol::Prompt{
          .name = context.session_id + "-prompt",
      });
      return result;
    }

    std::optional<mcp::core::Result<mcp::protocol::ResourcesListResult>>
    on_list_resources(
        const mcp::server::SessionContext& context) const override {
      mcp::protocol::ResourcesListResult result;
      result.resources.push_back(mcp::protocol::Resource{
          .uri = "file:///" + context.session_id + "/resource.txt",
          .name = "resource",
      });
      return result;
    }

    std::optional<mcp::core::Result<mcp::protocol::ResourceTemplatesListResult>>
    on_list_resource_templates(
        const mcp::server::SessionContext& context) const override {
      mcp::protocol::ResourceTemplatesListResult result;
      result.resource_templates.push_back(mcp::protocol::ResourceTemplate{
          .uri_template = "file:///" + context.session_id + "/{name}.txt",
          .name = "template",
      });
      return result;
    }

    std::optional<mcp::core::Result<mcp::protocol::ToolResult>> on_call_tool(
        const mcp::protocol::ToolCall& call,
        const mcp::server::SessionContext& context,
        mcp::CancellationToken cancellation) const override {
      require(!cancellation.cancelled(),
              "contract tool cancellation should start open");
      return mcp::protocol::ToolResult::text(
          context.session_id + ":" + call.name + ":" +
          call.arguments.at("value").get<std::string>());
    }

    std::optional<mcp::core::Result<mcp::protocol::PromptsGetResult>>
    on_get_prompt(const mcp::protocol::PromptsGetParams& params,
                  const mcp::server::SessionContext& context,
                  mcp::CancellationToken cancellation) const override {
      require(!cancellation.cancelled(),
              "contract prompt cancellation should start open");
      mcp::protocol::PromptsGetResult result;
      result.messages.push_back(mcp::protocol::PromptMessage::text(
          "assistant", context.session_id + ":" + params.name + ":" +
                           params.arguments.at("topic").get<std::string>()));
      return result;
    }

    std::optional<mcp::core::Result<mcp::protocol::ResourcesReadResult>>
    on_read_resource(const mcp::protocol::ResourcesReadParams& params,
                     const mcp::server::SessionContext& context,
                     mcp::CancellationToken cancellation) const override {
      require(!cancellation.cancelled(),
              "contract resource cancellation should start open");
      mcp::protocol::ResourcesReadResult result;
      result.contents.push_back(mcp::protocol::ResourceContents{
          .uri = params.uri,
          .mime_type = "text/plain",
          .text = context.session_id + ":" + params.uri,
      });
      return result;
    }
  } handler;

  auto require_discovery = [](mcp::server::Server& server,
                              std::string_view session) {
    const mcp::server::SessionContext context{.session_id =
                                                  std::string(session)};
    const auto tools = server.handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ToolsListMethod),
            .params = Json::object(),
            .id = std::int64_t{211},
        },
        context);
    require(tools.has_value(), "contract tools/list failed");
    require(tools->result->at("tools").at(0).at("name") ==
                std::string(session) + "-tool",
            "contract tools/list should use handler");

    const auto tool = server.handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ToolsGetMethod),
            .params = Json{{"name", "lookup"}},
            .id = std::int64_t{212},
        },
        context);
    require(tool.has_value(), "contract tools/get failed");
    require(tool->result->at("name") == std::string(session) + "-lookup",
            "contract tools/get should use handler");

    const auto prompts = server.handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::PromptsListMethod),
            .params = Json::object(),
            .id = std::int64_t{213},
        },
        context);
    require(prompts.has_value(), "contract prompts/list failed");
    require(prompts->result->at("prompts").at(0).at("name") ==
                std::string(session) + "-prompt",
            "contract prompts/list should use handler");

    const auto resources = server.handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ResourcesListMethod),
            .params = Json::object(),
            .id = std::int64_t{214},
        },
        context);
    require(resources.has_value(), "contract resources/list failed");
    require(resources->result->at("resources").at(0).at("uri") ==
                "file:///" + std::string(session) + "/resource.txt",
            "contract resources/list should use handler");

    const auto templates = server.handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ResourcesTemplatesListMethod),
            .params = Json::object(),
            .id = std::int64_t{215},
        },
        context);
    require(templates.has_value(), "contract resources/templates/list failed");
    require(
        templates->result->at("resourceTemplates").at(0).at("uriTemplate") ==
            "file:///" + std::string(session) + "/{name}.txt",
        "contract resources/templates/list should use handler");

    const auto tool_call = server.handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ToolsCallMethod),
            .params =
                Json{{"name", "echo"}, {"arguments", Json{{"value", "hello"}}}},
            .id = std::int64_t{216},
        },
        context);
    require(tool_call.has_value(), "contract tools/call failed");
    require(tool_call->result->at("content").at(0).at("text") ==
                std::string(session) + ":echo:hello",
            "contract tools/call should use handler");

    const auto prompt_get = server.handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::PromptsGetMethod),
            .params =
                Json{{"name", "write"}, {"arguments", Json{{"topic", "sdk"}}}},
            .id = std::int64_t{217},
        },
        context);
    require(prompt_get.has_value(), "contract prompts/get failed");
    require(prompt_get->result->at("messages").at(0).at("content").at("text") ==
                std::string(session) + ":write:sdk",
            "contract prompts/get should use handler");

    const auto resource_read = server.handle_request(
        mcp::protocol::JsonRpcRequest{
            .method = std::string(mcp::protocol::ResourcesReadMethod),
            .params = Json{{"uri", "file:///contract.txt"}},
            .id = std::int64_t{218},
        },
        context);
    require(resource_read.has_value(), "contract resources/read failed");
    require(resource_read->result->at("contents").at(0).at("text") ==
                std::string(session) + ":file:///contract.txt",
            "contract resources/read should use handler");
  };

  auto server = make_server();
  server.set_handler(handler);
  require_discovery(server, "server-contract");

  mcp::ServerPeer peer(mcp::server::ServerOptions{});
  peer.set_handler(handler);

  const auto peer_tools = peer.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ToolsListMethod),
          .params = Json::object(),
          .id = std::int64_t{219},
      },
      mcp::server::SessionContext{.session_id = "peer-contract"});
  require(peer_tools.has_value(), "contract peer tools/list failed");
  require(
      peer_tools->result->at("tools").at(0).at("name") == "peer-contract-tool",
      "contract peer tools/list should use handler before registry");

  const auto peer_tool_call = peer.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ToolsCallMethod),
          .params =
              Json{{"name", "echo"}, {"arguments", Json{{"value", "peer"}}}},
          .id = std::int64_t{220},
      },
      mcp::server::SessionContext{.session_id = "peer-contract"});
  require(peer_tool_call.has_value(), "contract peer tools/call failed");
  require(peer_tool_call->result->at("content").at(0).at("text") ==
              "peer-contract:echo:peer",
          "contract peer tools/call should use handler before registry");

  const auto peer_prompt_get = peer.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::PromptsGetMethod),
          .params =
              Json{{"name", "write"}, {"arguments", Json{{"topic", "peer"}}}},
          .id = std::int64_t{221},
      },
      mcp::server::SessionContext{.session_id = "peer-contract"});
  require(peer_prompt_get.has_value(), "contract peer prompts/get failed");
  require(
      peer_prompt_get->result->at("messages").at(0).at("content").at("text") ==
          "peer-contract:write:peer",
      "contract peer prompts/get should use handler before registry");

  const auto peer_resource_read = peer.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ResourcesReadMethod),
          .params = Json{{"uri", "file:///peer.txt"}},
          .id = std::int64_t{222},
      },
      mcp::server::SessionContext{.session_id = "peer-contract"});
  require(peer_resource_read.has_value(),
          "contract peer resources/read failed");
  require(peer_resource_read->result->at("contents").at(0).at("text") ==
              "peer-contract:file:///peer.txt",
          "contract peer resources/read should use handler before registry");
}

void test_contract_task_handlers_receive_session_context() {
  struct ContractTaskHandler final : mcp::server::ServerHandlerInterface {
    mutable std::vector<std::string> sessions;

    std::optional<mcp::core::Result<mcp::protocol::TaskListResult>>
    on_task_list(const mcp::protocol::TaskListParams& params,
                 const mcp::server::SessionContext& context) const override {
      sessions.push_back(context.session_id);
      require(params.cursor == "page-1", "contract task list cursor mismatch");
      return mcp::protocol::TaskListResult{
          .tasks = {mcp::protocol::Task{
              .task_id = "task-1",
              .status = mcp::protocol::TaskStatus::Working,
              .created_at = "2026-05-24T00:00:00Z",
              .ttl = std::monostate{},
              .last_updated_at = "2026-05-24T00:00:05Z",
          }},
      };
    }

    std::optional<mcp::core::Result<mcp::protocol::Task>> on_task_get(
        const mcp::protocol::TaskGetParams& params,
        const mcp::server::SessionContext& context) const override {
      sessions.push_back(context.session_id);
      require(params.task_id == "task-1", "contract task get id mismatch");
      return mcp::protocol::Task{
          .task_id = params.task_id,
          .status = mcp::protocol::TaskStatus::Working,
          .created_at = "2026-05-24T00:00:00Z",
          .ttl = std::monostate{},
          .last_updated_at = "2026-05-24T00:00:05Z",
      };
    }

    std::optional<mcp::core::Result<mcp::protocol::Task>> on_task_cancel(
        const mcp::protocol::TaskCancelParams& params,
        const mcp::server::SessionContext& context) const override {
      sessions.push_back(context.session_id);
      require(params.task_id == "task-1", "contract task cancel id mismatch");
      return mcp::protocol::Task{
          .task_id = params.task_id,
          .status = mcp::protocol::TaskStatus::Cancelled,
          .created_at = "2026-05-24T00:00:00Z",
          .ttl = std::monostate{},
          .last_updated_at = "2026-05-24T00:00:10Z",
      };
    }

    std::optional<mcp::core::Result<mcp::protocol::Json>> on_task_result(
        const mcp::protocol::TaskResultParams& params,
        const mcp::server::SessionContext& context) const override {
      sessions.push_back(context.session_id);
      require(params.task_id == "task-1", "contract task result id mismatch");
      return Json{{"value", context.remote_address}};
    }
  } handler;

  auto server = make_server();
  server.set_handler(handler);

  const mcp::server::SessionContext context{
      .session_id = "session-1",
      .remote_address = "127.0.0.1",
  };

  const auto list_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::TasksListMethod),
          .params = Json{{"cursor", "page-1"}},
          .id = std::int64_t{201},
      },
      context);
  require(list_response.has_value(), "contract task list failed");
  require(list_response->result.has_value(), "contract task list missing");

  const auto get_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::TasksGetMethod),
          .params = Json{{"taskId", "task-1"}},
          .id = std::int64_t{202},
      },
      context);
  require(get_response.has_value(), "contract task get failed");
  require(get_response->result.has_value(), "contract task get missing");

  const auto cancel_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::TasksCancelMethod),
          .params = Json{{"taskId", "task-1"}},
          .id = std::int64_t{203},
      },
      context);
  require(cancel_response.has_value(), "contract task cancel failed");
  require(cancel_response->result.has_value(), "contract task cancel missing");

  const auto result_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::TasksResultMethod),
          .params = Json{{"taskId", "task-1"}},
          .id = std::int64_t{204},
      },
      context);
  require(result_response.has_value(), "contract task result failed");
  require(result_response->result.has_value(), "contract task result missing");
  require(result_response->result->at("value") == "127.0.0.1",
          "contract task result context mismatch");

  require(handler.sessions.size() == 4, "contract task session count mismatch");
  require(std::all_of(handler.sessions.begin(), handler.sessions.end(),
                      [](const std::string& session) {
                        return session == "session-1";
                      }),
          "contract task session context mismatch");
}

void test_contract_notification_handlers_receive_session_context() {
  struct ContractNotificationHandler final
      : mcp::server::ServerHandlerInterface {
    mutable std::vector<std::string> events;

    std::optional<mcp::core::Result<mcp::core::Unit>> on_progress(
        const mcp::protocol::ProgressNotificationParams& params,
        const mcp::server::SessionContext& context) const override {
      require(params.progress == 0.5,
              "contract progress notification value mismatch");
      events.push_back("progress:" + context.session_id);
      return mcp::core::Unit{};
    }

    std::optional<mcp::core::Result<mcp::core::Unit>> on_roots_list_changed(
        const mcp::server::SessionContext& context) const override {
      events.push_back("roots:" + context.remote_address);
      return mcp::core::Unit{};
    }

    std::optional<mcp::core::Result<mcp::core::Unit>> on_tool_list_changed(
        const mcp::server::SessionContext& context) const override {
      events.push_back("tools:" + context.session_id);
      return mcp::core::Unit{};
    }

    std::optional<mcp::core::Result<mcp::core::Unit>> on_prompt_list_changed(
        const mcp::server::SessionContext& context) const override {
      events.push_back("prompts:" + context.session_id);
      return mcp::core::Unit{};
    }

    std::optional<mcp::core::Result<mcp::core::Unit>> on_resource_list_changed(
        const mcp::server::SessionContext& context) const override {
      events.push_back("resources:" + context.session_id);
      return mcp::core::Unit{};
    }

    std::optional<mcp::core::Result<mcp::core::Unit>> on_resource_updated(
        const std::string& uri,
        const mcp::server::SessionContext& context) const override {
      require(uri == "file:///tmp/data.txt",
              "contract resource updated uri mismatch");
      events.push_back("updated:" + context.remote_address);
      return mcp::core::Unit{};
    }
  } handler;

  auto server = make_server();
  server.set_handler(handler);

  const mcp::server::SessionContext context{
      .session_id = "session-1",
      .remote_address = "127.0.0.1",
  };
  const std::vector<mcp::protocol::JsonRpcNotification> notifications = {
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::RootsListChangedNotificationMethod),
          .params = Json::object(),
      },
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ToolsListChangedNotificationMethod),
          .params = Json::object(),
      },
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::PromptsListChangedNotificationMethod),
          .params = Json::object(),
      },
      mcp::protocol::JsonRpcNotification{
          .method = std::string(
              mcp::protocol::ResourcesListChangedNotificationMethod),
          .params = Json::object(),
      },
      mcp::protocol::JsonRpcNotification{
          .method = std::string(mcp::protocol::ProgressNotificationMethod),
          .params = mcp::protocol::progress_notification_params_to_json(
              mcp::protocol::ProgressNotificationParams{
                  .progress_token = std::int64_t{1},
                  .progress = 0.5,
              }),
      },
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ResourcesUpdatedNotificationMethod),
          .params = Json{{"uri", "file:///tmp/data.txt"}},
      },
  };

  for (const auto& notification : notifications) {
    require(server.handle_notification(notification, context).has_value(),
            "contract notification dispatch failed");
  }

  const std::vector<std::string> expected = {
      "roots:127.0.0.1",     "tools:session-1",    "prompts:session-1",
      "resources:session-1", "progress:session-1", "updated:127.0.0.1"};
  require(handler.events == expected,
          "contract notification context events mismatch");
}

void test_server_notification_facade_round_trip() {
  auto server = make_server();

  int raw_notifications = 0;
  int roots_notifications = 0;
  int tools_notifications = 0;
  int prompts_notifications = 0;
  int resources_notifications = 0;
  int progress_notifications = 0;
  std::string updated_uri;
  double progress_value = 0.0;
  server.set_raw_notification_handler(
      [&](const mcp::protocol::JsonRpcNotification& notification,
          const mcp::server::SessionContext& context) {
        ++raw_notifications;
        require(!notification.method.empty(),
                "raw notification method mismatch");
        require(context.session_id == "session-1",
                "raw notification session mismatch");
        return mcp::core::Unit{};
      });
  server.set_roots_list_changed_handler(
      [&](const mcp::server::SessionContext& context) {
        ++roots_notifications;
        require(context.remote_address == "127.0.0.1",
                "roots notification remote mismatch");
        return mcp::core::Unit{};
      });
  server.set_tool_list_changed_handler(
      [&](const mcp::server::SessionContext& context) {
        ++tools_notifications;
        require(context.session_id == "session-1",
                "tool notification session mismatch");
        return mcp::core::Unit{};
      });
  server.set_prompt_list_changed_handler(
      [&](const mcp::server::SessionContext& context) {
        ++prompts_notifications;
        require(context.session_id == "session-1",
                "prompt notification session mismatch");
        return mcp::core::Unit{};
      });
  server.set_resource_list_changed_handler(
      [&](const mcp::server::SessionContext& context) {
        ++resources_notifications;
        require(context.session_id == "session-1",
                "resource notification session mismatch");
        return mcp::core::Unit{};
      });
  server.set_progress_handler(
      [&](const mcp::protocol::ProgressNotificationParams& params,
          const mcp::server::SessionContext& context) {
        ++progress_notifications;
        progress_value = params.progress;
        require(context.session_id == "session-1",
                "progress notification session mismatch");
        return mcp::core::Unit{};
      });
  server.set_resource_updated_handler(
      [&](const std::string& uri, const mcp::server::SessionContext& context) {
        updated_uri = uri;
        require(context.remote_address == "127.0.0.1",
                "resource updated remote mismatch");
        return mcp::core::Unit{};
      });

  const auto handled = server.handle_notification(
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::RootsListChangedNotificationMethod),
          .params = Json::object(),
      },
      mcp::server::SessionContext{.session_id = "session-1",
                                  .remote_address = "127.0.0.1"});
  require(handled.has_value(), "server handle_notification failed");
  require(raw_notifications == 1, "raw notification count mismatch");
  require(roots_notifications == 1, "roots notification count mismatch");

  require(server
              .handle_notification(
                  mcp::protocol::JsonRpcNotification{
                      .method = std::string(
                          mcp::protocol::ToolsListChangedNotificationMethod),
                      .params = Json::object(),
                  },
                  mcp::server::SessionContext{.session_id = "session-1",
                                              .remote_address = "127.0.0.1"})
              .has_value(),
          "tool list notification failed");
  require(server
              .handle_notification(
                  mcp::protocol::JsonRpcNotification{
                      .method = std::string(
                          mcp::protocol::PromptsListChangedNotificationMethod),
                      .params = Json::object(),
                  },
                  mcp::server::SessionContext{.session_id = "session-1",
                                              .remote_address = "127.0.0.1"})
              .has_value(),
          "prompt list notification failed");
  require(
      server
          .handle_notification(
              mcp::protocol::JsonRpcNotification{
                  .method = std::string(
                      mcp::protocol::ResourcesListChangedNotificationMethod),
                  .params = Json::object(),
              },
              mcp::server::SessionContext{.session_id = "session-1",
                                          .remote_address = "127.0.0.1"})
          .has_value(),
      "resource list notification failed");
  require(
      server
          .handle_notification(
              mcp::protocol::JsonRpcNotification{
                  .method =
                      std::string(mcp::protocol::ProgressNotificationMethod),
                  .params = mcp::protocol::progress_notification_params_to_json(
                      mcp::protocol::ProgressNotificationParams{
                          .progress_token = std::int64_t{1},
                          .progress = 0.75,
                          .total = 1.0,
                          .message = "three quarters",
                      }),
              },
              mcp::server::SessionContext{.session_id = "session-1",
                                          .remote_address = "127.0.0.1"})
          .has_value(),
      "progress notification failed");
  require(server
              .handle_notification(
                  mcp::protocol::JsonRpcNotification{
                      .method = std::string(
                          mcp::protocol::ResourcesUpdatedNotificationMethod),
                      .params = Json{{"uri", "file:///tmp/data.txt"}},
                  },
                  mcp::server::SessionContext{.session_id = "session-1",
                                              .remote_address = "127.0.0.1"})
              .has_value(),
          "resource updated notification failed");
  require(server
              .handle_notification(
                  mcp::protocol::JsonRpcNotification{
                      .method = std::string(
                          mcp::protocol::CancelledNotificationMethod),
                      .params =
                          mcp::protocol::cancelled_notification_params_to_json(
                              mcp::protocol::CancelledNotificationParams{
                                  .request_id = std::int64_t{9},
                                  .reason = "done",
                              }),
                  },
                  mcp::server::SessionContext{.session_id = "session-1",
                                              .remote_address = "127.0.0.1"})
              .has_value(),
          "cancelled notification failed");
  require(updated_uri == "file:///tmp/data.txt",
          "resource updated uri mismatch");
  require(tools_notifications == 1, "tool notification count mismatch");
  require(prompts_notifications == 1, "prompt notification count mismatch");
  require(resources_notifications == 1, "resource notification count mismatch");
  require(progress_notifications == 1, "progress notification count mismatch");
  require(raw_notifications == 7, "raw notification count mismatch");
  require(progress_value == 0.75, "progress notification value mismatch");

  const auto invalid = server.handle_notification(
      mcp::protocol::JsonRpcNotification{
          .method =
              std::string(mcp::protocol::ResourcesUpdatedNotificationMethod),
          .params = Json::object(),
      },
      mcp::server::SessionContext{.session_id = "session-1",
                                  .remote_address = "127.0.0.1"});
  require(!invalid.has_value(),
          "invalid resource updated notification should fail");
}

void test_server_notify_facade_broadcasts_notifications() {
  auto server = make_server();
  auto transport = std::make_unique<RecordingServerTransport>();
  auto* transport_ptr = transport.get();
  require(server.add_transport(std::move(transport)).has_value(),
          "server transport add failed");

  require(server.notify_tool_list_changed().has_value(),
          "notify_tool_list_changed failed");
  require(server.notify_prompt_list_changed().has_value(),
          "notify_prompt_list_changed failed");
  require(server.notify_resource_list_changed().has_value(),
          "notify_resource_list_changed failed");
  require(server.notify_roots_list_changed().has_value(),
          "notify_roots_list_changed failed");
  require(server.notify_resource_updated("file:///tmp/data.txt").has_value(),
          "notify_resource_updated failed");
  require(server
              .notify_progress(mcp::protocol::ProgressNotificationParams{
                  .progress_token = std::int64_t{1},
                  .progress = 0.5,
                  .total = 1.0,
                  .message = "half",
              })
              .has_value(),
          "notify_progress failed");
  require(server
              .notify_logging_message(
                  mcp::protocol::LoggingMessageNotificationParams{
                      .level = mcp::protocol::LoggingLevel::Info,
                      .logger = "test",
                      .data = Json{{"message", "hello"}},
                  })
              .has_value(),
          "notify_logging_message failed");

  require(transport_ptr->notifications.size() == 7,
          "server notification broadcast count mismatch");
  require(transport_ptr->notifications.front().method ==
              mcp::protocol::ToolsListChangedNotificationMethod,
          "tool notification method mismatch");
  require(transport_ptr->notifications.back().method ==
              mcp::protocol::LoggingMessageNotificationMethod,
          "logging notification method mismatch");
}

void test_server_resource_subscriptions_scope_notifications() {
  mcp::server::ServerOptions options;
  options.capabilities.resources.subscribe = true;
  mcp::server::Server server(options);
  auto transport = std::make_unique<RecordingServerTransport>();
  auto* transport_ptr = transport.get();
  require(server.add_transport(std::move(transport)).has_value(),
          "server transport add failed");

  const auto subscribe = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ResourcesSubscribeMethod),
          .params = Json{{"uri", "file:///tmp/data.txt"}},
          .id = std::int64_t{1},
      },
      mcp::server::SessionContext{.session_id = "session-1",
                                  .remote_address = "127.0.0.1",
                                  .transport = transport_ptr});
  require(subscribe.has_value(), "resource subscribe request failed");

  require(server.notify_resource_updated("file:///tmp/data.txt").has_value(),
          "notify_resource_updated for subscribed uri failed");
  require(transport_ptr->notifications.size() == 1,
          "subscribed resource update count mismatch");
  require(transport_ptr->notifications.front().method ==
              mcp::protocol::ResourcesUpdatedNotificationMethod,
          "subscribed resource update method mismatch");
  require(transport_ptr->notifications.front().params.at("uri") ==
              "file:///tmp/data.txt",
          "subscribed resource update uri mismatch");

  const auto unsubscribe = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ResourcesUnsubscribeMethod),
          .params = Json{{"uri", "file:///tmp/data.txt"}},
          .id = std::int64_t{2},
      },
      mcp::server::SessionContext{.session_id = "session-1",
                                  .remote_address = "127.0.0.1",
                                  .transport = transport_ptr});
  require(unsubscribe.has_value(), "resource unsubscribe request failed");

  require(server.notify_resource_updated("file:///tmp/data.txt").has_value(),
          "notify_resource_updated after unsubscribe failed");
  require(transport_ptr->notifications.size() == 1,
          "unsubscribed resource update should not be delivered");
}

void test_call_tool_round_trip() {
  auto server = make_server();
  mcp::client::Client client(std::make_unique<LoopbackTransport>(server));

  const auto result = client.call_tool(mcp::protocol::ToolCall{
      .name = "echo",
      .arguments = Json{{"value", 42}},
  });
  if (!result) {
    throw std::runtime_error("call_tool failed: " + result.error().message +
                             " (" + std::to_string(result.error().code) + ")");
  }
  require(!result->is_error_result(), "tool result should not be an error");
  require(result->content.size() == 1, "tool result content size mismatch");
  require(result->content.front().type == "text",
          "tool result content type mismatch");
  require(result->content.front().text == "{\"value\":42}",
          "tool result text mismatch");
}

void test_client_call_tool_serializes_task_request() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::Client client(std::move(transport));

  const auto result = client.call_tool_task(mcp::protocol::ToolCall{
      .name = "fake-tool",
      .arguments = Json{{"value", 42}},
      .task = mcp::protocol::TaskRequestParameters{.ttl = std::int64_t{60}},
  });

  require(result.has_value(), "task-aware call_tool should create task");
  require(result->task.task_id == "task-created",
          "task-aware call_tool task id mismatch");
  require(result->task.status == mcp::protocol::TaskStatus::Working,
          "task-aware call_tool task status mismatch");
  require(recording->requests.size() == 1,
          "task-aware call_tool should send one request");
  require(recording->requests.front().method ==
              std::string(mcp::protocol::ToolsCallMethod),
          "task-aware call_tool method mismatch");
  require(recording->requests.front().params.at("name") == "fake-tool",
          "task-aware call_tool name mismatch");
  require(recording->requests.front().params.at("arguments").at("value") == 42,
          "task-aware call_tool arguments mismatch");
  require(recording->requests.front().params.at("task").at("ttl") == 60,
          "task-aware call_tool ttl mismatch");
}

void test_server_tool_task_support_validation() {
  mcp::server::ServerOptions options;
  options.capabilities.tools.enabled = true;
  options.capabilities.tasks = mcp::protocol::TaskCapabilities{
      .tools_call = true,
  };
  mcp::server::Server server(options);
  server.use_task_manager();

  int forbidden_calls = 0;
  const auto forbidden_added = server.tools().add(
      mcp::protocol::ToolDefinition{
          .name = "forbidden",
          .input_schema = Json::object(),
      },
      [&](const mcp::server::ToolContext& context) {
        ++forbidden_calls;
        require(!context.task.has_value(), "forbidden tool task mismatch");
        return mcp::protocol::ToolResult{};
      });
  require(forbidden_added.has_value(), "failed to add forbidden tool");

  int optional_calls = 0;
  const auto optional_added = server.tools().add(
      mcp::protocol::ToolDefinition{
          .name = "optional",
          .input_schema = Json::object(),
          .execution = mcp::protocol::ToolExecution{}.with_task_support(
              mcp::protocol::TaskSupport::Optional),
      },
      [&](const mcp::server::ToolContext& context) {
        ++optional_calls;
        require(context.task.has_value(), "optional tool task missing");
        require(context.task->ttl == 30, "optional tool ttl mismatch");
        return mcp::protocol::ToolResult{};
      });
  require(optional_added.has_value(), "failed to add optional tool");

  int required_calls = 0;
  const auto required_added = server.tools().add(
      mcp::protocol::ToolDefinition{
          .name = "required",
          .input_schema = Json::object(),
          .execution = mcp::protocol::ToolExecution{}.with_task_support(
              mcp::protocol::TaskSupport::Required),
      },
      [&](const mcp::server::ToolContext& context) {
        ++required_calls;
        require(context.task.has_value(), "required tool task missing");
        require(context.task->ttl == 90, "required tool ttl mismatch");
        return mcp::protocol::ToolResult{};
      });
  require(required_added.has_value(), "failed to add required tool");

  const auto forbidden_task = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ToolsCallMethod),
          .params = Json{{"name", "forbidden"},
                         {"arguments", Json::object()},
                         {"task", Json{{"ttl", 10}}}},
          .id = std::int64_t{1},
      },
      mcp::server::SessionContext{.session_id = "session-1"});
  require(forbidden_task.has_value(),
          "forbidden task request should produce a response");
  require(forbidden_task->error.has_value(),
          "forbidden task request should fail");
  require(forbidden_task->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "forbidden task error code mismatch");
  require(forbidden_calls == 0, "forbidden task should not call handler");

  const auto required_without_task =
      server.call_tool("required", Json::object(), "session-1");
  require(!required_without_task.has_value(),
          "required tool should reject non-task direct call");
  require(required_without_task.error().message ==
              "tool requires task-based invocation",
          "required tool direct-call error mismatch");
  require(required_calls == 0, "required non-task should not call handler");

  const auto optional_task = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ToolsCallMethod),
          .params = Json{{"name", "optional"},
                         {"arguments", Json::object()},
                         {"task", Json{{"ttl", 30}}}},
          .id = std::int64_t{2},
      },
      mcp::server::SessionContext{.session_id = "session-1"});
  require(optional_task.has_value(), "optional task request failed");
  require(optional_task->result.has_value(), "optional task result missing");
  require(optional_task->result->contains("task"),
          "optional task should create a task");
  wait_until([&] { return optional_calls == 1; },
             "optional task should call handler once");

  const auto required_task = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ToolsCallMethod),
          .params = Json{{"name", "required"},
                         {"arguments", Json::object()},
                         {"task", Json{{"ttl", 90}}}},
          .id = std::int64_t{3},
      },
      mcp::server::SessionContext{.session_id = "session-1"});
  require(required_task.has_value(), "required task request failed");
  require(required_task->result.has_value(), "required task result missing");
  require(required_task->result->contains("task"),
          "required task should create a task");
  wait_until([&] { return required_calls == 1; },
             "required task should call handler once");

  const auto optional_calls_before_invalid = optional_calls;
  const auto invalid_task_params = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ToolsCallMethod),
          .params = Json{{"name", "optional"},
                         {"arguments", Json::object()},
                         {"task", 42}},
          .id = std::int64_t{4},
      },
      mcp::server::SessionContext{.session_id = "session-1"});
  require(invalid_task_params.has_value(),
          "invalid task params should produce a response");
  require(invalid_task_params->error.has_value(),
          "invalid task params should fail");
  require(invalid_task_params->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid task params error code mismatch");
  require(optional_calls == optional_calls_before_invalid,
          "invalid task params should not call handler");

  const auto negative_ttl = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ToolsCallMethod),
          .params = Json{{"name", "optional"},
                         {"arguments", Json::object()},
                         {"task", Json{{"ttl", -1}}}},
          .id = std::int64_t{5},
      },
      mcp::server::SessionContext{.session_id = "session-1"});
  require(negative_ttl.has_value(),
          "negative task ttl should produce a response");
  require(negative_ttl->error.has_value(), "negative task ttl should fail");
  require(negative_ttl->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "negative task ttl error code mismatch");
  require(negative_ttl->error->message == "task ttl must be non-negative",
          "negative task ttl error message mismatch");
  require(optional_calls == optional_calls_before_invalid,
          "negative task ttl should not call handler");
}

void test_server_task_processor_tool_call_lifecycle() {
  mcp::server::ServerBuilder builder;
  builder.name("TaskServer")
      .with_task_manager(mcp::server::TaskOperationProcessorOptions{
          .worker_count = 1,
          .queue_size = 8,
          .poll_interval = std::int64_t{1},
      });
  builder.add_tool(
      mcp::protocol::ToolDefinition{
          .name = "long.echo",
          .input_schema = Json::object(),
          .execution = mcp::protocol::ToolExecution{}.with_task_support(
              mcp::protocol::TaskSupport::Optional),
      },
      [](const mcp::server::ToolContext& context)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        require(context.session_id == "test-session",
                "task tool session mismatch");
        require(context.task.has_value(), "task tool context missing task");
        require(context.task->ttl == 60, "task tool ttl mismatch");
        mcp::protocol::ToolResult result;
        result.structured_content =
            Json{{"echo", context.arguments.at("value")}};
        result.content.push_back(mcp::protocol::ContentBlock{
            .type = "text",
            .text = context.arguments.at("value").get<std::string>(),
            .data = Json::object(),
        });
        return result;
      });

  auto built = builder.build();
  require(built.has_value(), "task processor server build failed");
  auto& server = **built;
  mcp::client::Client client(std::make_unique<LoopbackTransport>(server));

  const auto created = client.call_tool_task(mcp::protocol::ToolCall{
      .name = "long.echo",
      .arguments = Json{{"value", "hello"}},
      .task = mcp::protocol::TaskRequestParameters{.ttl = std::int64_t{60}},
  });
  require(created.has_value(), "task-aware tool call should create task");
  require(!created->task.task_id.empty(), "created task id missing");
  require(created->task.status == mcp::protocol::TaskStatus::Working,
          "created task should start as working");
  require(created->task.poll_interval == 1, "created task poll mismatch");
  require(std::holds_alternative<std::int64_t>(created->task.ttl),
          "created task ttl missing");
  require(std::get<std::int64_t>(created->task.ttl) == 60,
          "created task ttl mismatch");

  wait_until(
      [&] {
        const auto task = client.get_task(created->task.task_id);
        return task.has_value() &&
               task->status == mcp::protocol::TaskStatus::Completed;
      },
      "task-aware tool call should complete");

  const auto listed = client.list_tasks();
  require(listed.has_value(), "list tasks after tool call failed");
  require(!listed->empty(), "created task should be listed");

  const auto payload = client.task_result(created->task.task_id);
  require(payload.has_value(), "completed task result failed");
  require(payload->at("content").at(0).at("text") == "hello",
          "completed task content mismatch");
  require(payload->at("structuredContent").at("echo") == "hello",
          "completed task structured content mismatch");
}

void test_server_task_processor_failed_and_cancelled_tasks() {
  mcp::server::ServerBuilder builder;
  builder.with_task_manager(mcp::server::TaskOperationProcessorOptions{
      .worker_count = 1,
      .queue_size = 8,
  });
  std::atomic_bool slow_observed_cancel{false};
  builder.add_tool(
      mcp::protocol::ToolDefinition{
          .name = "fail",
          .input_schema = Json::object(),
          .execution = mcp::protocol::ToolExecution{}.with_task_support(
              mcp::protocol::TaskSupport::Optional),
      },
      [](const mcp::server::ToolContext&)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        return std::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::InternalError),
            "planned failure",
            {},
        });
      });
  builder.add_tool(
      mcp::protocol::ToolDefinition{
          .name = "slow",
          .input_schema = Json::object(),
          .execution = mcp::protocol::ToolExecution{}.with_task_support(
              mcp::protocol::TaskSupport::Optional),
      },
      [&](const mcp::server::ToolContext& context)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        for (int attempt = 0; attempt < 20; ++attempt) {
          if (context.cancelled()) {
            slow_observed_cancel = true;
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        mcp::protocol::ToolResult result;
        result.content.push_back(mcp::protocol::ContentBlock{
            .type = "text",
            .text = "late",
            .data = Json::object(),
        });
        return result;
      });

  auto built = builder.build();
  require(built.has_value(), "failed/cancel task server build failed");
  auto& server = **built;
  mcp::client::Client client(std::make_unique<LoopbackTransport>(server));

  const auto failed = client.call_tool_task(mcp::protocol::ToolCall{
      .name = "fail",
      .arguments = Json::object(),
      .task = mcp::protocol::TaskRequestParameters{},
  });
  require(failed.has_value(), "failing task should be created");
  wait_until(
      [&] {
        const auto task = client.get_task(failed->task.task_id);
        return task.has_value() &&
               task->status == mcp::protocol::TaskStatus::Failed;
      },
      "failing task should become failed");
  const auto failed_result = client.task_result(failed->task.task_id);
  require(!failed_result.has_value(), "failed task result should fail");
  require(failed_result.error().message == "planned failure",
          "failed task error mismatch");

  const auto slow = client.call_tool_task(mcp::protocol::ToolCall{
      .name = "slow",
      .arguments = Json::object(),
      .task = mcp::protocol::TaskRequestParameters{},
  });
  require(slow.has_value(), "slow task should be created");
  const auto cancelled = client.cancel_task(slow->task.task_id);
  require(cancelled.has_value(), "cancel task should succeed");
  require(cancelled->status == mcp::protocol::TaskStatus::Cancelled,
          "cancel task status mismatch");
  wait_until([&] { return slow_observed_cancel.load(); },
             "slow task should observe cancellation token");
  const auto after_cancel = client.get_task(slow->task.task_id);
  require(after_cancel.has_value(), "cancelled task should remain queryable");
  require(after_cancel->status == mcp::protocol::TaskStatus::Cancelled,
          "late slow result should not overwrite cancellation");
  const auto cancelled_result = client.task_result(slow->task.task_id);
  require(!cancelled_result.has_value(), "cancelled task result should fail");
  require(cancelled_result.error().message == "Operation cancelled",
          "cancelled task error mismatch");
}

void test_server_task_processor_timeout_and_retention() {
  mcp::server::ServerBuilder builder;
  builder.with_task_manager(mcp::server::TaskOperationProcessorOptions{
      .worker_count = 1,
      .queue_size = 8,
      .completed_task_ttl = std::chrono::milliseconds{25},
  });
  builder.add_tool(
      mcp::protocol::ToolDefinition{
          .name = "quick",
          .input_schema = Json::object(),
          .execution = mcp::protocol::ToolExecution{}.with_task_support(
              mcp::protocol::TaskSupport::Optional),
      },
      [](const mcp::server::ToolContext&)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        mcp::protocol::ToolResult result;
        result.content.push_back(mcp::protocol::ContentBlock{
            .type = "text",
            .text = "done",
            .data = Json::object(),
        });
        return result;
      });
  builder.add_tool(
      mcp::protocol::ToolDefinition{
          .name = "timeout",
          .input_schema = Json::object(),
          .execution = mcp::protocol::ToolExecution{}.with_task_support(
              mcp::protocol::TaskSupport::Optional),
      },
      [](const mcp::server::ToolContext& context)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        wait_until([&] { return context.cancelled(); },
                   "timeout task should receive cancellation");
        return mcp::protocol::ToolResult{};
      });

  auto built = builder.build();
  require(built.has_value(), "timeout/retention task server build failed");
  auto& server = **built;
  mcp::client::Client client(std::make_unique<LoopbackTransport>(server));

  const auto timed_out = client.call_tool_task(mcp::protocol::ToolCall{
      .name = "timeout",
      .arguments = Json::object(),
      .task = mcp::protocol::TaskRequestParameters{.ttl = std::int64_t{0}},
  });
  require(timed_out.has_value(), "timeout task should be created");
  const auto timeout_state = client.get_task(timed_out->task.task_id);
  require(timeout_state.has_value(), "timeout task should remain queryable");
  require(timeout_state->status == mcp::protocol::TaskStatus::Failed,
          "timeout task status mismatch");
  const auto timeout_result = client.task_result(timed_out->task.task_id);
  require(!timeout_result.has_value(), "timeout task result should fail");
  require(timeout_result.error().message == "Operation timed out",
          "timeout task error mismatch");

  const auto retained = client.call_tool_task(mcp::protocol::ToolCall{
      .name = "quick",
      .arguments = Json::object(),
      .task = mcp::protocol::TaskRequestParameters{},
  });
  require(retained.has_value(), "retained task should be created");
  wait_until(
      [&] {
        const auto task = client.get_task(retained->task.task_id);
        return task.has_value() &&
               task->status == mcp::protocol::TaskStatus::Completed;
      },
      "retained task should complete");
  const auto retained_result = client.task_result(retained->task.task_id);
  require(retained_result.has_value(), "retained task result should exist");

  std::this_thread::sleep_for(std::chrono::milliseconds{40});
  const auto after_ttl = client.list_tasks();
  require(after_ttl.has_value(), "list tasks after retention ttl failed");
  const auto expired = client.get_task(retained->task.task_id);
  require(!expired.has_value(), "completed task should expire after ttl");
  require(expired.error().message == "task not found",
          "expired task error mismatch");
}

void test_task_processor_cancellation_before_start() {
  mcp::server::TaskOperationProcessor processor(
      mcp::server::TaskOperationProcessorOptions{
          .worker_count = 1,
          .queue_size = 4,
      });

  std::mutex mutex;
  std::condition_variable cv;
  bool first_started = false;
  bool release_first = false;
  const auto blocker = processor.submit_operation(
      mcp::server::TaskOperationDescriptor{
          .name = "blocking.operation",
          .task = mcp::protocol::TaskRequestParameters{},
      },
      [&](const mcp::server::CancellationToken&) -> mcp::core::Result<Json> {
        {
          std::lock_guard<std::mutex> lock(mutex);
          first_started = true;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return release_first; });
        return Json{{"kind", "blocking"}};
      });
  require(blocker.has_value(), "blocking task should be created");

  wait_until(
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        return first_started;
      },
      "blocking task should start");

  std::atomic_bool queued_started{false};
  std::atomic_bool queued_saw_cancelled{false};
  const auto queued = processor.submit_operation(
      mcp::server::TaskOperationDescriptor{
          .name = "queued.operation",
          .task = mcp::protocol::TaskRequestParameters{},
      },
      [&](const mcp::server::CancellationToken& cancellation)
          -> mcp::core::Result<Json> {
        queued_saw_cancelled = cancellation.cancelled();
        queued_started = true;
        return Json{{"late", true}};
      });
  require(queued.has_value(), "queued task should be created");

  const auto cancelled = processor.cancel_task(
      mcp::protocol::TaskCancelParams{.task_id = queued->task.task_id});
  require(cancelled.has_value(), "queued task cancellation should succeed");
  require(cancelled->status == mcp::protocol::TaskStatus::Cancelled,
          "queued task should be cancelled before start");

  {
    std::lock_guard<std::mutex> lock(mutex);
    release_first = true;
  }
  cv.notify_all();

  wait_until([&] { return queued_started.load(); },
             "queued task should eventually observe pre-start cancellation");
  require(queued_saw_cancelled.load(),
          "queued task token should be cancelled before handler body");

  const auto after_handler = processor.get_task(
      mcp::protocol::TaskGetParams{.task_id = queued->task.task_id});
  require(after_handler.has_value(), "cancelled queued task should remain");
  require(after_handler->status == mcp::protocol::TaskStatus::Cancelled,
          "late queued result should not overwrite cancellation");

  const auto late_result = processor.task_result(
      mcp::protocol::TaskResultParams{.task_id = queued->task.task_id});
  require(!late_result.has_value(), "pre-start cancelled result should fail");
  require(late_result.error().message == "Operation cancelled",
          "pre-start cancelled result error mismatch");
}

void test_task_processor_retention_count_limit() {
  mcp::server::TaskOperationProcessor processor(
      mcp::server::TaskOperationProcessorOptions{
          .worker_count = 1,
          .queue_size = 4,
          .max_completed_tasks = 1,
      });

  const auto first = processor.submit_operation(
      mcp::server::TaskOperationDescriptor{.name = "first"},
      [](const mcp::server::CancellationToken&) -> mcp::core::Result<Json> {
        return Json{{"value", "first"}};
      });
  require(first.has_value(), "first retained task should be created");
  wait_until(
      [&] {
        const auto task = processor.get_task(
            mcp::protocol::TaskGetParams{.task_id = first->task.task_id});
        return task.has_value() &&
               task->status == mcp::protocol::TaskStatus::Completed;
      },
      "first retained task should complete");

  const auto second = processor.submit_operation(
      mcp::server::TaskOperationDescriptor{.name = "second"},
      [](const mcp::server::CancellationToken&) -> mcp::core::Result<Json> {
        return Json{{"value", "second"}};
      });
  require(second.has_value(), "second retained task should be created");
  wait_until(
      [&] {
        const auto task = processor.get_task(
            mcp::protocol::TaskGetParams{.task_id = second->task.task_id});
        return task.has_value() &&
               task->status == mcp::protocol::TaskStatus::Completed;
      },
      "second retained task should complete");

  const auto trimmed = processor.get_task(
      mcp::protocol::TaskGetParams{.task_id = first->task.task_id});
  require(!trimmed.has_value(), "oldest completed task should be trimmed");
  require(trimmed.error().message == "task not found",
          "trimmed task error mismatch");

  const auto retained = processor.task_result(
      mcp::protocol::TaskResultParams{.task_id = second->task.task_id});
  require(retained.has_value(), "newest completed task should be retained");
  require(retained->at("value") == "second",
          "newest retained task result mismatch");
}

void test_task_processor_generic_operation() {
  mcp::server::TaskOperationProcessor processor(
      mcp::server::TaskOperationProcessorOptions{
          .worker_count = 1,
          .queue_size = 4,
      });

  const auto created = processor.submit_operation(
      mcp::server::TaskOperationDescriptor{
          .name = "generic.operation",
          .task = mcp::protocol::TaskRequestParameters{.ttl = std::int64_t{60}},
      },
      [](const mcp::server::CancellationToken& cancellation)
          -> mcp::core::Result<Json> {
        require(!cancellation.cancelled(),
                "generic operation should not start cancelled");
        return Json{{"kind", "generic"}, {"ok", true}};
      });
  require(created.has_value(), "generic operation should create task");
  require(std::holds_alternative<std::int64_t>(created->task.ttl),
          "generic operation ttl missing");
  require(std::get<std::int64_t>(created->task.ttl) == 60,
          "generic operation ttl mismatch");

  wait_until(
      [&] {
        const auto task = processor.get_task(
            mcp::protocol::TaskGetParams{.task_id = created->task.task_id});
        return task.has_value() &&
               task->status == mcp::protocol::TaskStatus::Completed;
      },
      "generic operation should complete");
  const auto result = processor.task_result(
      mcp::protocol::TaskResultParams{.task_id = created->task.task_id});
  require(result.has_value(), "generic operation result missing");
  require(result->at("kind") == "generic", "generic operation result mismatch");
}

void test_ping_raw_request() {
  auto server = make_server();
  mcp::client::Client client(std::make_unique<LoopbackTransport>(server));

  const auto ping = client.ping();
  require(ping.has_value(), "client ping failed");

  const auto response = client.raw_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PingMethod),
      .params = Json::object(),
      .id = std::int64_t{99},
  });
  require(response.has_value(), "ping request failed");
  require(response->is_object(), "ping response must be an object");
  require(response->empty(), "ping response should be empty");
}

void test_server_info_accessors_and_ping() {
  auto server = make_server();

  const auto info = server.get_info();
  require(info.name == "TestServer", "server info name mismatch");
  require(info.version == "1.0.0", "server info version mismatch");
  require(info.instructions.empty(), "server info instructions mismatch");
  require(server.capabilities().tools.list_changed,
          "server capabilities mismatch");

  const auto ping = server.ping();
  require(ping.has_value(), "server ping helper failed");
  require(ping->is_object(), "server ping helper result must be an object");
  require(ping->empty(), "server ping helper result should be empty");
}

void test_initialize_handshake_shape() {
  auto server = make_server();

  const auto response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::InitializeMethod),
          .params =
              Json{
                  {"protocolVersion",
                   std::string(mcp::protocol::McpProtocolVersion)},
                  {"capabilities", Json::object()},
                  {"clientInfo", Json{{"name", "tester"}, {"version", "1"}}},
              },
          .id = std::string("init-1"),
      },
      mcp::server::SessionContext{.session_id = "test-session",
                                  .remote_address = "127.0.0.1"});

  require(response.has_value(), "initialize request failed");
  require(response->result.has_value(), "initialize result missing");
  require(response->result->is_object(), "initialize result must be an object");
  require(response->result->at("protocolVersion") ==
              std::string(mcp::protocol::McpProtocolVersion),
          "protocol version mismatch");
  require(response->result->at("serverInfo").at("name") == "TestServer",
          "server name mismatch");
  const auto& capabilities = response->result->at("capabilities");
  require(capabilities.at("tools").at("listChanged") == true,
          "server tools capability mismatch");
  require(!capabilities.contains("resources"),
          "inactive resources capability should be omitted");
  require(!capabilities.contains("prompts"),
          "inactive prompts capability should be omitted");
  require(!capabilities.contains("logging"),
          "inactive logging capability should be omitted");
  require(!capabilities.contains("completions"),
          "inactive completions capability should be omitted");
  require(response->result->at("capabilities").at("experimental").at("beta"),
          "server experimental mismatch");
  require(response->result->at("capabilities")
              .at("extensions")
              .at("vendor/feature")
              .at("enabled"),
          "server extension mismatch");

  auto legacy_request = mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params =
          Json{
              {"protocolVersion",
               std::string(mcp::protocol::McpProtocolVersion2025_06_18)},
              {"capabilities", Json::object()},
              {"clientInfo", Json{{"name", "tester"}, {"version", "1"}}},
          },
      .id = std::string("init-legacy"),
  };
  const auto legacy_response =
      server.handle_request(legacy_request, mcp::server::SessionContext{});
  require(legacy_response.has_value(), "legacy initialize request failed");
  require(legacy_response->result.has_value(),
          "legacy initialize result missing");
  require(legacy_response->result->at("protocolVersion") ==
              std::string(mcp::protocol::McpProtocolVersion2025_06_18),
          "server should echo a known client protocol version");
}

void test_server_initialize_rejects_invalid_protocol_versions() {
  auto server = make_server();
  const auto base_request = [] {
    return mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::InitializeMethod),
        .params =
            Json{
                {"protocolVersion",
                 std::string(mcp::protocol::McpProtocolVersion)},
                {"capabilities", Json::object()},
                {"clientInfo", Json{{"name", "tester"}, {"version", "1"}}},
            },
        .id = std::string("init-invalid"),
    };
  };

  auto missing = base_request();
  missing.params.erase("protocolVersion");
  const auto missing_response =
      server.handle_request(missing, mcp::server::SessionContext{});
  require(missing_response.has_value(),
          "missing initialize protocolVersion should produce response");
  require(missing_response->error.has_value(),
          "missing initialize protocolVersion should be rejected");
  require(missing_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "missing initialize protocolVersion error code mismatch");

  auto non_string = base_request();
  non_string.params["protocolVersion"] = 42;
  const auto non_string_response =
      server.handle_request(non_string, mcp::server::SessionContext{});
  require(non_string_response.has_value(),
          "non-string initialize protocolVersion should produce response");
  require(non_string_response->error.has_value(),
          "non-string initialize protocolVersion should be rejected");
  require(non_string_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "non-string initialize protocolVersion error code mismatch");

  auto non_object_params = base_request();
  non_object_params.params = Json::array();
  const auto non_object_response =
      server.handle_request(non_object_params, mcp::server::SessionContext{});
  require(non_object_response.has_value(),
          "non-object initialize params should produce response");
  require(non_object_response->error.has_value(),
          "non-object initialize params should be rejected");
  require(non_object_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "non-object initialize params error code mismatch");

  auto unknown = base_request();
  unknown.params["protocolVersion"] = "1900-01-01";
  const auto unknown_response =
      server.handle_request(unknown, mcp::server::SessionContext{});
  require(unknown_response.has_value(),
          "unknown initialize protocolVersion should produce response");
  require(unknown_response->result.has_value(),
          "unknown initialize protocolVersion should fall back");
  require(unknown_response->result->at("protocolVersion") ==
              std::string(mcp::protocol::McpProtocolVersion),
          "unknown initialize protocolVersion should negotiate to latest");
}

void test_default_server_initialize_omits_inactive_capabilities() {
  mcp::server::Server server(mcp::server::ServerOptions{});

  const auto initialized = server.initialize();
  require(initialized.has_value(), "default server initialize failed");

  const auto& capabilities = initialized->at("capabilities");
  require(capabilities.is_object(), "default capabilities must be an object");
  require(capabilities.empty(),
          "default server should not advertise inactive capabilities");
}

void test_server_app_builder_registers_parity_surface() {
  int logging_calls = 0;
  auto built =
      mcp::server::App::builder()
          .name("FacadeServer")
          .version("2.1.0")
          .instructions("test instructions")
          .tool<Json, Json>(
              "echo-json",
              [](const Json& input) { return Json{{"echo", input}}; })
          .prompt(
              mcp::protocol::Prompt{
                  .name = "summarize",
                  .description = "Summarize text",
              },
              [](const mcp::server::PromptContext& context)
                  -> mcp::core::Result<mcp::protocol::PromptsGetResult> {
                mcp::protocol::PromptsGetResult result;
                result.description = "Summarize text";
                result.messages.push_back(mcp::protocol::PromptMessage{
                    .role = "user",
                    .content =
                        mcp::protocol::ContentBlock{
                            .type = "text",
                            .text =
                                context.arguments.at("text").get<std::string>(),
                        },
                });
                return result;
              })
          .prompt(
              "session-summary",
              [](std::string text, const mcp::server::PromptContext& context) {
                return context.session_id + ":" + text;
              })
          .prompt("cancel-aware",
                  [](std::string text,
                     const mcp::server::CancellationToken& cancellation) {
                    return cancellation.cancelled() ? "cancelled"
                                                    : text + ":active";
                  })
          .resource(
              mcp::protocol::Resource{
                  .uri = "file:///tmp/readme.txt",
                  .name = "Readme",
                  .description = "Project readme",
                  .mime_type = "text/plain",
              },
              [](const mcp::server::ResourceContext& context)
                  -> mcp::core::Result<mcp::protocol::ResourcesReadResult> {
                mcp::protocol::ResourcesReadResult result;
                result.contents.push_back(mcp::protocol::ResourceContents{
                    .uri = context.uri,
                    .mime_type = "text/plain",
                    .text = "hello",
                });
                return result;
              })
          .resource(
              "file:///tmp/session.txt",
              [](std::string uri, const mcp::server::ResourceContext& context) {
                return context.session_id + ":" + uri + ":" +
                       context.params.value("section", std::string("default"));
              })
          .resource("file:///tmp/cancel.txt",
                    [](std::string uri,
                       const mcp::server::CancellationToken& cancellation) {
                      return cancellation.cancelled() ? "cancelled"
                                                      : uri + ":active";
                    })
          .resource_template(mcp::protocol::ResourceTemplate{
              .uri_template = "file:///tmp/{name}.txt",
              .name = "Tmp file",
              .description = "A tmp file",
              .mime_type = "text/plain",
          })
          .completion([](const Json& request,
                         const mcp::server::SessionContext& context) {
            return Json{{"completion",
                         context.session_id + ":" +
                             request.at("prefix").get<std::string>() + "llo"}};
          })
          .sampling([](const mcp::server::SessionContext& context,
                       const Json& request) {
            return Json{
                {"sample", context.session_id + ":" +
                               request.at("prompt").get<std::string>()}};
          })
          .logging([&](std::string_view level, std::string_view message) {
            require(level == "debug", "logging level mismatch");
            require(message == "logging level changed",
                    "logging message mismatch");
            ++logging_calls;
          })
          .raw_request([](const mcp::protocol::JsonRpcRequest& request)
                           -> std::optional<mcp::protocol::JsonRpcResponse> {
            if (request.method == "custom/echo") {
              return mcp::protocol::make_response(request.id,
                                                  Json{{"ok", true}});
            }
            return std::nullopt;
          })
          .build();
  require(built.has_value(), "facade builder should build");
  auto& server = **built;
  const auto info = server.get_info();
  require(info.name == "FacadeServer", "facade server info name mismatch");
  require(info.version == "2.1.0", "facade server info version mismatch");
  require(info.instructions == "test instructions",
          "facade server info instructions mismatch");
  require(server.capabilities().logging.enabled,
          "facade server capabilities mismatch");
  const auto context = mcp::server::SessionContext{
      .session_id = "facade", .remote_address = "127.0.0.1"};

  const auto initialized = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "initialize",
          .params =
              Json{
                  {"protocolVersion",
                   std::string(mcp::protocol::McpProtocolVersion)},
                  {"capabilities", Json::object()},
                  {"clientInfo", Json{{"name", "tester"}, {"version", "1"}}},
              },
          .id = std::int64_t{1},
      },
      context);
  require(initialized.has_value(), "facade initialize failed");
  require(initialized->result.has_value(), "facade initialize result missing");
  require(initialized->result->at("serverInfo").at("name") == "FacadeServer",
          "facade name mismatch");
  require(initialized->result->at("instructions") == "test instructions",
          "facade instructions mismatch");
  require(initialized->result->at("capabilities").contains("completions"),
          "completions capability missing");
  require(initialized->result->at("capabilities").at("completions").is_object(),
          "completions capability should be an object");
  require(initialized->result->at("capabilities").at("completions").empty(),
          "completions capability should use object presence");
  require(initialized->result->at("capabilities").contains("logging"),
          "logging capability missing");
  require(initialized->result->at("capabilities").at("logging").is_object(),
          "logging capability should be an object");
  require(initialized->result->at("capabilities").at("logging").empty(),
          "logging capability should use object presence");

  const auto tools = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "tools/call",
          .params =
              Json{{"name", "echo-json"}, {"arguments", Json{{"value", 7}}}},
          .id = std::int64_t{2},
      },
      context);
  require(tools.has_value(), "facade tool call failed");
  require(tools->result->at("structuredContent").at("echo").at("value") == 7,
          "facade tool result mismatch");

  const auto prompts = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "prompts/get",
          .params = Json{{"name", "summarize"},
                         {"arguments", Json{{"text", "hello"}}}},
          .id = std::int64_t{3},
      },
      context);
  require(prompts.has_value(), "facade prompt get failed");
  require(
      prompts->result->at("messages").at(0).at("content").at("text") == "hello",
      "facade prompt mismatch");

  const auto context_prompt = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "prompts/get",
          .params = Json{{"name", "session-summary"},
                         {"arguments", Json{{"text", "hello"}}}},
          .id = std::int64_t{30},
      },
      context);
  require(context_prompt.has_value(), "facade prompt context get failed");
  require(
      context_prompt->result->at("messages").at(0).at("content").at("text") ==
          "facade:hello",
      "facade prompt context mismatch");

  const auto cancellation_prompt = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "prompts/get",
          .params = Json{{"name", "cancel-aware"},
                         {"arguments", Json{{"text", "hello"}}}},
          .id = std::int64_t{31},
      },
      context);
  require(cancellation_prompt.has_value(),
          "facade prompt cancellation token get failed");
  require(cancellation_prompt->result->at("messages")
                  .at(0)
                  .at("content")
                  .at("text") == "hello:active",
          "facade prompt cancellation token mismatch");

  const auto resources = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "resources/read",
          .params = Json{{"uri", "file:///tmp/readme.txt"}},
          .id = std::int64_t{4},
      },
      context);
  require(resources.has_value(), "facade resource read failed");
  require(resources->result->at("contents").at(0).at("text") == "hello",
          "facade resource mismatch");

  const auto context_resource = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "resources/read",
          .params =
              Json{{"uri", "file:///tmp/session.txt"}, {"section", "api"}},
          .id = std::int64_t{40},
      },
      context);
  require(context_resource.has_value(), "facade resource context read failed");
  require(context_resource->result->at("contents").at(0).at("text") ==
              "facade:file:///tmp/session.txt:api",
          "facade resource context mismatch");

  const auto cancellation_resource = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "resources/read",
          .params = Json{{"uri", "file:///tmp/cancel.txt"}},
          .id = std::int64_t{41},
      },
      context);
  require(cancellation_resource.has_value(),
          "facade resource cancellation token read failed");
  require(cancellation_resource->result->at("contents").at(0).at("text") ==
              "file:///tmp/cancel.txt:active",
          "facade resource cancellation token mismatch");

  const auto templates = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "resources/templates/list",
          .params = Json::object(),
          .id = std::int64_t{5},
      },
      context);
  require(templates.has_value(), "facade resource templates list failed");
  require(templates->result->at("resourceTemplates").at(0).at("uriTemplate") ==
              "file:///tmp/{name}.txt",
          "facade template mismatch");

  const auto completion = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "completion/complete",
          .params = Json{{"prefix", "he"}},
          .id = std::int64_t{6},
      },
      context);
  require(completion.has_value(), "facade completion failed");
  require(completion->result->at("completion") == "facade:hello",
          "facade completion mismatch");

  const auto sampling = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "sampling/createMessage",
          .params = Json{{"prompt", "write"}},
          .id = std::int64_t{7},
      },
      context);
  require(sampling.has_value(), "facade sampling failed");
  require(sampling->result->at("sample") == "facade:write",
          "facade sampling mismatch");

  const auto logging = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "logging/setLevel",
          .params = Json{{"level", "debug"}},
          .id = std::int64_t{8},
      },
      context);
  require(logging.has_value(), "facade logging failed");
  require(logging_calls == 1, "facade logging callback mismatch");

  const auto invalid_tool_get = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ToolsGetMethod),
          .params = Json{{"name", 7}},
          .id = std::int64_t{80},
      },
      context);
  require(invalid_tool_get.has_value(),
          "invalid tools/get params should produce a response");
  require(invalid_tool_get->error.has_value(),
          "invalid tools/get params should fail");
  require(invalid_tool_get->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid tools/get params error code mismatch");

  const auto invalid_logging = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "logging/setLevel",
          .params = Json{{"level", 7}},
          .id = std::int64_t{81},
      },
      context);
  require(invalid_logging.has_value(),
          "invalid logging params should produce a response");
  require(invalid_logging->error.has_value(),
          "invalid logging params should fail");
  require(invalid_logging->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid logging params error code mismatch");

  const auto raw = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "custom/echo",
          .params = Json::object(),
          .id = std::int64_t{9},
      },
      context);
  require(raw.has_value(), "facade raw request failed");
  require(raw->result->at("ok"), "facade raw request mismatch");
}

void test_server_app_builder_registers_typed_completion() {
  auto built =
      mcp::server::App::builder()
          .completion([](const mcp::protocol::CompleteParams& params,
                         const mcp::server::CompletionContext& context) {
            require(!context.cancelled(),
                    "typed completion should receive live cancellation token");
            mcp::protocol::CompletionResult result;
            result.values = {context.session_id + ":" + params.argument.value +
                             "-one"};
            result.total = 1;
            result.has_more = false;
            return result;
          })
          .build();
  require(built.has_value(), "typed completion server should build");

  mcp::protocol::CompleteParams params;
  params.ref = mcp::protocol::prompt_completion_reference("summarize");
  params.argument = mcp::protocol::CompletionArgument{"text", "he"};

  const auto response = (*built)->handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::CompletionCompleteMethod),
          .params = mcp::protocol::complete_params_to_json(params),
          .id = std::int64_t{1},
      },
      mcp::server::SessionContext{.session_id = "typed"});
  require(response.has_value(), "typed completion request should succeed");
  require(response->result.has_value(), "typed completion result missing");
  require(
      response->result->at("completion").at("values").at(0) == "typed:he-one",
      "typed completion value mismatch");
  require(response->result->at("completion").at("total") == 1,
          "typed completion total mismatch");
  require(response->result->at("completion").at("hasMore") == false,
          "typed completion hasMore mismatch");

  auto argument_built =
      mcp::server::App::builder()
          .completion([](const mcp::server::CompletionContext& context,
                         const mcp::protocol::CompletionArgument& argument) {
            return std::vector<std::string>{context.session_id + ":" +
                                            argument.value + "-arg"};
          })
          .build();
  require(argument_built.has_value(),
          "argument completion server should build");

  const auto argument_response =
      (*argument_built)
          ->handle_request(
              mcp::protocol::JsonRpcRequest{
                  .method =
                      std::string(mcp::protocol::CompletionCompleteMethod),
                  .params = mcp::protocol::complete_params_to_json(params),
                  .id = std::int64_t{2},
              },
              mcp::server::SessionContext{.session_id = "argument"});
  require(argument_response.has_value(),
          "argument completion request should succeed");
  require(argument_response->result->at("completion").at("values").at(0) ==
              "argument:he-arg",
          "argument completion value mismatch");

  auto value_built =
      mcp::server::App::builder()
          .completion([](std::string value) { return value + "-value"; })
          .build();
  require(value_built.has_value(), "value completion server should build");
  const auto value_response =
      (*value_built)
          ->handle_request(
              mcp::protocol::JsonRpcRequest{
                  .method =
                      std::string(mcp::protocol::CompletionCompleteMethod),
                  .params = mcp::protocol::complete_params_to_json(params),
                  .id = std::int64_t{3},
              },
              {});
  require(value_response.has_value(),
          "value completion request should succeed");
  require(
      value_response->result->at("completion").at("values").at(0) == "he-value",
      "value completion mismatch");

  const auto invalid = (*built)->handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::CompletionCompleteMethod),
          .params = Json{{"ref",
                          Json{{"type", "ref/prompt"}, {"name", "summarize"}}}},
          .id = std::int64_t{4},
      },
      {});
  require(invalid.has_value(), "invalid typed completion should respond");
  require(invalid->error.has_value(), "invalid typed completion should fail");
  require(invalid->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid typed completion code mismatch");
}

void test_server_app_builder_registers_typed_prompt_and_resource() {
  using typed_tool_fixture::PromptArgs;
  using typed_tool_fixture::ResourceArgs;

  auto built =
      mcp::server::App::builder()
          .prompt<PromptArgs>(
              "typed-summary",
              [](PromptArgs args, const mcp::server::PromptContext& context) {
                return mcp::protocol::PromptMessage::text(
                    "user", context.session_id + ":" + args.text);
              })
          .resource<ResourceArgs>(
              "file:///tmp/typed.txt",
              [](ResourceArgs args,
                 const mcp::server::ResourceContext& context) {
                mcp::protocol::ResourceContents contents;
                contents.uri = context.uri;
                contents.mime_type = "text/plain";
                contents.text = context.session_id + ":" + args.section;
                return contents;
              })
          .build();
  require(built.has_value(), "typed prompt/resource server should build");

  const auto prompt = (*built)->handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::PromptsGetMethod),
          .params = Json{{"name", "typed-summary"},
                         {"arguments", Json{{"text", "hello"}}}},
          .id = std::int64_t{1},
      },
      mcp::server::SessionContext{.session_id = "typed"});
  require(prompt.has_value(), "typed prompt request should succeed");
  require(prompt->result.has_value(), "typed prompt result missing");
  require(prompt->result->at("messages").at(0).at("content").at("text") ==
              "typed:hello",
          "typed prompt result mismatch");

  const auto resource = (*built)->handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ResourcesReadMethod),
          .params =
              Json{{"uri", "file:///tmp/typed.txt"}, {"section", "intro"}},
          .id = std::int64_t{2},
      },
      mcp::server::SessionContext{.session_id = "typed"});
  require(resource.has_value(), "typed resource request should succeed");
  require(resource->result.has_value(), "typed resource result missing");
  require(resource->result->at("contents").at(0).at("text") == "typed:intro",
          "typed resource result mismatch");

  const auto invalid_prompt = (*built)->handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::PromptsGetMethod),
          .params =
              Json{{"name", "typed-summary"}, {"arguments", Json{{"text", 7}}}},
          .id = std::int64_t{3},
      },
      {});
  require(invalid_prompt.has_value(), "invalid typed prompt should respond");
  require(invalid_prompt->error.has_value(),
          "invalid typed prompt should fail");
  require(invalid_prompt->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid typed prompt code mismatch");

  const auto invalid_resource = (*built)->handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ResourcesReadMethod),
          .params = Json{{"uri", "file:///tmp/typed.txt"},
                         {"section", Json::array()}},
          .id = std::int64_t{4},
      },
      {});
  require(invalid_resource.has_value(),
          "invalid typed resource should respond");
  require(invalid_resource->error.has_value(),
          "invalid typed resource should fail");
  require(invalid_resource->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid typed resource code mismatch");
}

void test_server_app_builder_registers_typed_tool() {
  using typed_tool_fixture::SumArgs;
  using typed_tool_fixture::SumResult;

  auto built =
      mcp::server::App::builder()
          .tool(mcp::server::tool<SumArgs, SumResult>("sum")
                    .description("Add two integers")
                    .task_support(mcp::protocol::TaskSupport::Optional)
                    .handler([](SumArgs args) {
                      return SumResult{.sum = args.a + args.b};
                    }))
          .tool<SumArgs, SumResult>(
              "sum-with-context",
              [](SumArgs args, const mcp::server::ToolContext& context) {
                require(context.session_id == "typed-tool-session",
                        "typed tool context session mismatch");
                require(!context.cancelled(),
                        "typed tool context should not be cancelled");
                return SumResult{.sum = args.a + args.b + 1};
              })
          .tool(mcp::server::tool<Json, Json>("z-configured")
                    .input<SumArgs>()
                    .output<SumResult>()
                    .streaming()
                    .execution(mcp::protocol::ToolExecution{}.with_task_support(
                        mcp::protocol::TaskSupport::Required))
                    .annotations(Json{{"category", "math"}})
                    .handler([](const Json& args) {
                      return Json{{"sum", args.at("a").get<int>() +
                                              args.at("b").get<int>()}};
                    }))
          .build();
  require(built.has_value(), "typed tool server should build");
  auto& server = **built;

  const auto listed = server.list_tools();
  require(listed.size() == 3, "typed tool count mismatch");
  require(listed.front().name == "sum", "typed tool name mismatch");
  require(listed.front().description == "Add two integers",
          "typed tool description mismatch");
  require(listed.front().input_schema.at("properties").contains("a"),
          "typed tool input schema missing a");
  require(listed.front().input_schema.at("properties").contains("b"),
          "typed tool input schema missing b");
  require(listed.front().output_schema.at("properties").contains("sum"),
          "typed tool output schema missing sum");
  require(listed.front().task_support() == mcp::protocol::TaskSupport::Optional,
          "typed tool task support mismatch");
  const auto configured_it =
      std::find_if(listed.begin(), listed.end(),
                   [](const mcp::protocol::ToolDefinition& tool) {
                     return tool.name == "z-configured";
                   });
  require(configured_it != listed.end(), "configured typed tool missing");
  require(configured_it->input_schema.at("properties").contains("a"),
          "configured typed tool input<T> missing a");
  require(configured_it->output_schema.at("properties").contains("sum"),
          "configured typed tool output<T> missing sum");
  require(configured_it->streaming, "configured typed tool streaming mismatch");
  require(configured_it->task_support() == mcp::protocol::TaskSupport::Required,
          "configured typed tool execution mismatch");
  require(configured_it->annotations.at("category") == "math",
          "configured typed tool annotations mismatch");

  const auto result =
      server.call_tool("sum", Json{{"a", 2}, {"b", 3}}, "typed-tool-session");
  require(result.has_value(), "typed tool call failed");
  require(result->structured_content.has_value(),
          "typed tool structured content missing");
  require(result->structured_content->at("sum") == 5,
          "typed tool structured content mismatch");

  const auto context_result = server.call_tool(
      "sum-with-context", Json{{"a", 2}, {"b", 3}}, "typed-tool-session");
  require(context_result.has_value(), "typed context tool call failed");
  require(context_result->structured_content.has_value(),
          "typed context tool structured content missing");
  require(context_result->structured_content->at("sum") == 6,
          "typed context tool structured content mismatch");

  const auto invalid =
      server.call_tool("sum", Json{{"a", 2}}, "typed-tool-session");
  require(!invalid.has_value(), "typed tool invalid args should fail");
  require(invalid.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "typed tool invalid args error code mismatch");
}

void test_server_tool_schema_validator_hooks() {
  using typed_tool_fixture::SumArgs;
  using typed_tool_fixture::SumResult;

  auto validator = std::make_shared<RecordingSchemaValidator>();
  auto built =
      mcp::server::App::builder()
          .schema_validator(validator)
          .tool<SumArgs, SumResult>(
              "sum",
              [](SumArgs args) { return SumResult{.sum = args.a + args.b}; })
          .build();
  require(built.has_value(), "schema validator server should build");

  const auto result =
      (*built)->call_tool("sum", Json{{"a", 2}, {"b", 3}}, "schema-session");
  require(result.has_value(), "schema validator tool call should succeed");
  require(validator->contexts.size() == 2,
          "schema validator should see input and output");
  require(validator->contexts.at(0).target ==
              mcp::server::SchemaValidationTarget::ToolInput,
          "schema validator input target mismatch");
  require(validator->contexts.at(0).tool_name == "sum",
          "schema validator input tool mismatch");
  require(validator->instances.at(0).at("a") == 2,
          "schema validator input instance mismatch");
  require(validator->contexts.at(1).target ==
              mcp::server::SchemaValidationTarget::ToolOutput,
          "schema validator output target mismatch");
  require(validator->instances.at(1).at("sum") == 5,
          "schema validator output instance mismatch");

  auto input_rejector = std::make_shared<RecordingSchemaValidator>();
  input_rejector->reject_input = true;
  auto input_built =
      mcp::server::App::builder()
          .schema_validator(input_rejector)
          .tool<SumArgs, SumResult>(
              "sum",
              [](SumArgs args) { return SumResult{.sum = args.a + args.b}; })
          .build();
  require(input_built.has_value(),
          "input schema validator server should build");
  const auto input_response =
      (*input_built)
          ->handle_request(
              mcp::protocol::JsonRpcRequest{
                  .method = std::string(mcp::protocol::ToolsCallMethod),
                  .params = Json{{"name", "sum"},
                                 {"arguments", Json{{"a", 2}, {"b", 3}}}},
                  .id = std::int64_t{1},
              },
              {});
  require(input_response.has_value(), "input schema failure should respond");
  require(input_response->error.has_value(),
          "input schema failure should be an error response");
  require(input_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "input schema failure code mismatch");

  auto output_rejector = std::make_shared<RecordingSchemaValidator>();
  output_rejector->reject_output = true;
  auto output_built =
      mcp::server::App::builder()
          .schema_validator(output_rejector)
          .tool<SumArgs, SumResult>(
              "sum",
              [](SumArgs args) { return SumResult{.sum = args.a + args.b}; })
          .build();
  require(output_built.has_value(),
          "output schema validator server should build");
  const auto output_response =
      (*output_built)
          ->handle_request(
              mcp::protocol::JsonRpcRequest{
                  .method = std::string(mcp::protocol::ToolsCallMethod),
                  .params = Json{{"name", "sum"},
                                 {"arguments", Json{{"a", 2}, {"b", 3}}}},
                  .id = std::int64_t{2},
              },
              {});
  require(output_response.has_value(), "output schema failure should respond");
  require(output_response->error.has_value(),
          "output schema failure should be an error response");
  require(output_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "output schema failure code mismatch");
}

void test_server_app_builder_typed_scalar_tool_rejects_empty_args() {
  auto built = mcp::server::App::builder()
                   .tool<std::string, std::string>(
                       "shout", [](std::string text) { return text + "!"; })
                   .build();
  require(built.has_value(), "typed scalar tool server should build");

  auto& server = **built;
  const auto result = server.call_tool("shout", Json::object());
  require(!result.has_value(), "typed scalar empty args should fail");
  require(result.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "typed scalar empty args error code mismatch");
}

void test_client_session_initialize_and_mark_initialized() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::McpClientSession session(std::move(transport));

  const auto initialized = session.initialize();
  require(initialized.has_value(), "session initialize failed");
  require(recording->requests.size() == 1,
          "initialize should send one request");
  require(recording->requests.front().method == "initialize",
          "initialize request method mismatch");
  require(recording->requests.front().params.at("protocolVersion") ==
              std::string(mcp::protocol::McpProtocolVersion),
          "initialize protocol version mismatch");
  require(recording->requests.front().params.at("clientInfo").at("name") ==
              "cxxmcp",
          "initialize client name mismatch");
  require(recording->requests.front()
                  .params.at("capabilities")
                  .at("roots")
                  .at("listChanged") == true,
          "initialize roots capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("sampling")
              .is_object(),
          "initialize sampling capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("elicitation")
              .at("form")
              .is_object(),
          "initialize elicitation capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("elicitation")
              .at("url")
              .is_object(),
          "initialize url elicitation capability mismatch");
  require(
      !recording->requests.front().params.at("capabilities").contains("tasks"),
      "initialize should not advertise tasks by default");

  const auto marked = session.mark_initialized();
  require(marked.has_value(), "mark_initialized failed");
  require(recording->notifications.size() == 1,
          "mark_initialized should send one notification");
  // Depends on protocol::InitializedMethod matching the MCP
  // notifications/initialized method name.
  require(
      recording->notifications.front().method == "notifications/initialized",
      "initialized notification method mismatch");
}

void test_client_initialize_with_empty_explicit_capabilities() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::Client client(std::move(transport));
  client.set_capabilities(mcp::protocol::ClientCapabilities{});

  const auto initialized = client.initialize("tester", "1");
  require(initialized.has_value(),
          "client initialize with empty capabilities failed");
  require(recording->requests.size() == 1,
          "empty capabilities initialize should send one request");

  const auto& capabilities =
      recording->requests.front().params.at("capabilities");
  require(capabilities.is_object(), "client capabilities must be an object");
  require(capabilities.empty(),
          "explicit empty client capabilities should stay empty");
}

void test_client_initialize_with_explicit_task_capabilities() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::Client client(std::move(transport));

  mcp::protocol::ClientCapabilities capabilities;
  capabilities.roots.list_changed = true;
  capabilities.sampling.enabled = true;
  capabilities.elicitation.form = true;
  capabilities.elicitation.url = true;
  capabilities.tasks = mcp::protocol::TaskCapabilities{
      .list = true,
      .cancel = true,
      .tools_call = true,
      .sampling_create_message = true,
      .elicitation_create = true,
  };
  capabilities.experimental = Json{{"beta", true}};
  capabilities.extensions = Json{{"vendor/feature", Json{{"enabled", true}}}};
  client.set_capabilities(capabilities);

  const auto initialized = client.initialize("tester", "1");
  require(initialized.has_value(),
          "client initialize with explicit tasks failed");
  require(recording->requests.size() == 1,
          "initialize should send one request");
  require(recording->requests.front().params.at("clientInfo").at("name") ==
              "tester",
          "explicit initialize client name mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("tasks")
              .at("list")
              .is_object(),
          "tasks list capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("tasks")
              .at("cancel")
              .is_object(),
          "tasks cancel capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("tasks")
              .at("requests")
              .at("tools")
              .at("call")
              .is_object(),
          "tasks tools call capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("tasks")
              .at("requests")
              .at("sampling")
              .at("createMessage")
              .is_object(),
          "tasks sampling createMessage capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("tasks")
              .at("requests")
              .at("elicitation")
              .at("create")
              .is_object(),
          "tasks elicitation create capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("experimental")
              .at("beta"),
          "experimental capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("extensions")
              .at("vendor/feature")
              .at("enabled"),
          "extensions capability mismatch");
}

void test_client_initialize_rejects_invalid_protocol_versions() {
  auto missing_transport = std::make_unique<RecordingTransport>();
  missing_transport->initialize_result.erase("protocolVersion");
  mcp::client::Client missing_client(std::move(missing_transport));
  const auto missing = missing_client.initialize("tester", "1");
  require(!missing.has_value(),
          "client should reject missing initialize protocolVersion");
  require(missing.error().message ==
              "initialize response requires a string protocolVersion",
          "missing initialize protocolVersion client error mismatch");

  auto non_string_transport = std::make_unique<RecordingTransport>();
  non_string_transport->initialize_result["protocolVersion"] = 42;
  mcp::client::Client non_string_client(std::move(non_string_transport));
  const auto non_string = non_string_client.initialize("tester", "1");
  require(!non_string.has_value(),
          "client should reject non-string initialize protocolVersion");
  require(non_string.error().message ==
              "initialize response requires a string protocolVersion",
          "non-string initialize protocolVersion client error mismatch");

  auto unknown_transport = std::make_unique<RecordingTransport>();
  unknown_transport->initialize_result["protocolVersion"] = "1900-01-01";
  mcp::client::Client unknown_client(std::move(unknown_transport));
  const auto unknown = unknown_client.initialize("tester", "1");
  require(unknown.has_value(),
          "client should accept unknown string initialize protocolVersion");
}

void test_client_session_discover_tools_uses_client_list_tools() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::McpClientSession session(std::move(transport));

  const auto tools = session.discover_tools();
  require(tools.has_value(), "session discover_tools failed");
  require(recording->requests.size() == 1,
          "discover_tools should send one request");
  require(recording->requests.front().method == "tools/list",
          "discover_tools should call tools/list");
  require(tools->size() == 1, "unexpected session tool count");
  require(tools->front().name == "fake-tool", "session tool name mismatch");
  require(tools->front().description == "A fake tool",
          "session tool description mismatch");
}

void test_client_list_all_tools_follows_cursors() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::Client client(std::move(transport));

  const auto tools = client.list_all_tools();
  require(tools.has_value(), "list_all_tools failed");
  require(tools->size() == 2, "list_all_tools should collect both pages");
  require(tools->at(0).name == "fake-tool", "first paged tool mismatch");
  require(tools->at(1).name == "paged-tool-2", "second paged tool mismatch");
  require(recording->requests.size() == 2,
          "list_all_tools should request two pages");
  require(recording->requests.at(1).params.at("cursor") == "page-2",
          "list_all_tools cursor mismatch");
}

void test_client_task_helpers_round_trip() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::Client client(std::move(transport));

  const auto tasks = client.list_all_tasks();
  require(tasks.has_value(), "list_all_tasks failed");
  require(tasks->size() == 2, "list_all_tasks should collect both pages");
  require(tasks->at(0).task_id == "task-1", "first task id mismatch");
  require(tasks->at(1).status == mcp::protocol::TaskStatus::Completed,
          "second task status mismatch");

  const auto task = client.get_task("task-1");
  require(task.has_value(), "get_task failed");
  require(task->task_id == "task-1", "get_task task id mismatch");

  const auto cancelled = client.cancel_task("task-1");
  require(cancelled.has_value(), "cancel_task failed");
  require(cancelled->status == mcp::protocol::TaskStatus::Cancelled,
          "cancel_task status mismatch");

  const auto task_result = client.task_result("task-1");
  require(task_result.has_value(), "task_result failed");
  require(task_result->at("value") == "task-complete",
          "task_result payload mismatch");

  require(recording->requests.size() == 5,
          "task helpers should send five requests");
  require(recording->requests.at(0).method == "tasks/list",
          "task list method mismatch");
  require(recording->requests.at(1).params.at("cursor") == "page-2",
          "task list cursor mismatch");
  require(recording->requests.at(2).method == "tasks/get",
          "task get method mismatch");
  require(recording->requests.at(2).params.at("taskId") == "task-1",
          "task get id mismatch");
  require(recording->requests.at(3).method == "tasks/cancel",
          "task cancel method mismatch");
  require(recording->requests.at(4).method == "tasks/result",
          "task result method mismatch");
}

void test_task_status_notification_round_trip() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::Client client(std::move(transport));

  std::string observed_task_id;
  client.on_task_status([&](const mcp::protocol::Task& task) {
    observed_task_id = task.task_id;
  });

  const auto task = mcp::protocol::Task{
      .task_id = "task-1",
      .status = mcp::protocol::TaskStatus::Working,
      .created_at = "2026-05-24T00:00:00Z",
      .ttl = std::monostate{},
      .last_updated_at = "2026-05-24T00:00:05Z",
  };
  const auto notification_result =
      client.handle_notification(mcp::protocol::JsonRpcNotification{
          .method = std::string(mcp::protocol::TasksStatusNotificationMethod),
          .params = mcp::protocol::task_to_json(task),
      });
  require(notification_result.has_value(),
          "task status notification should parse");
  require(observed_task_id == "task-1", "task status callback mismatch");

  mcp::server::ServerOptions options;
  options.capabilities.tasks = mcp::protocol::TaskCapabilities{
      .list = true,
      .cancel = true,
      .tools_call = true,
  };
  mcp::server::Server server(options);
  auto server_transport = std::make_unique<RecordingServerTransport>();
  auto* server_recording = server_transport.get();
  require(server.add_transport(std::move(server_transport)).has_value(),
          "server transport add failed");

  const auto notified = server.notify_task_status(task);
  require(notified.has_value(),
          "server task status notification should succeed");
  require(server_recording->notifications.size() == 1,
          "server task status notification count mismatch");
  require(server_recording->notifications.front().method ==
              mcp::protocol::TasksStatusNotificationMethod,
          "server task status method mismatch");
  require(
      server_recording->notifications.front().params.at("taskId") == "task-1",
      "server task status payload mismatch");
  require(recording->requests.empty(),
          "task status notification should not send requests");
}

void test_server_task_request_round_trip() {
  mcp::server::ServerOptions options;
  options.capabilities.tasks = mcp::protocol::TaskCapabilities{
      .list = true,
      .cancel = true,
      .tools_call = true,
      .sampling_create_message = true,
      .elicitation_create = true,
  };
  mcp::server::Server server(options);

  const auto initialize_result = server.initialize();
  require(initialize_result.has_value(), "task server initialize failed");
  require(
      initialize_result->at("capabilities").at("tasks").at("list").is_object(),
      "server task list capability should use object presence");
  require(initialize_result->at("capabilities")
              .at("tasks")
              .at("cancel")
              .is_object(),
          "server task cancel capability should use object presence");
  require(initialize_result->at("capabilities")
              .at("tasks")
              .at("requests")
              .at("tools")
              .at("call")
              .is_object(),
          "server task tools capability should use object presence");
  require(initialize_result->at("capabilities")
              .at("tasks")
              .at("requests")
              .at("sampling")
              .at("createMessage")
              .is_object(),
          "server task sampling capability should use object presence");
  require(initialize_result->at("capabilities")
              .at("tasks")
              .at("requests")
              .at("elicitation")
              .at("create")
              .is_object(),
          "server task elicitation capability should use object presence");

  mcp::server::ServerHandler handler;
  handler.on_task_list = [](const mcp::protocol::TaskListParams& params,
                            const mcp::server::SessionContext& context)
      -> mcp::core::Result<mcp::protocol::TaskListResult> {
    require(context.session_id == "session-1", "task list session mismatch");
    require(params.cursor.has_value(), "task list cursor missing");
    require(*params.cursor == "page-1", "task list cursor mismatch");
    return mcp::protocol::TaskListResult{
        .tasks = {mcp::protocol::Task{
            .task_id = "task-1",
            .status = mcp::protocol::TaskStatus::Working,
            .created_at = "2026-05-24T00:00:00Z",
            .ttl = std::monostate{},
            .last_updated_at = "2026-05-24T00:00:05Z",
        }},
        .next_cursor = "page-2",
    };
  };
  handler.on_task_get = [](const mcp::protocol::TaskGetParams& params,
                           const mcp::server::SessionContext& context)
      -> mcp::core::Result<mcp::protocol::Task> {
    require(context.session_id == "session-1", "task get session mismatch");
    require(params.task_id == "task-1", "task get id mismatch");
    return mcp::protocol::Task{
        .task_id = params.task_id,
        .status = mcp::protocol::TaskStatus::Working,
        .created_at = "2026-05-24T00:00:00Z",
        .ttl = std::monostate{},
        .last_updated_at = "2026-05-24T00:00:05Z",
    };
  };
  handler.on_task_cancel = [](const mcp::protocol::TaskCancelParams& params,
                              const mcp::server::SessionContext& context)
      -> mcp::core::Result<mcp::protocol::Task> {
    require(context.session_id == "session-1", "task cancel session mismatch");
    require(params.task_id == "task-1", "task cancel id mismatch");
    return mcp::protocol::Task{
        .task_id = params.task_id,
        .status = mcp::protocol::TaskStatus::Cancelled,
        .created_at = "2026-05-24T00:00:00Z",
        .ttl = std::monostate{},
        .last_updated_at = "2026-05-24T00:00:10Z",
    };
  };
  handler.on_task_result = [](const mcp::protocol::TaskResultParams& params,
                              const mcp::server::SessionContext& context)
      -> mcp::core::Result<mcp::protocol::Json> {
    require(context.session_id == "session-1", "task result session mismatch");
    require(params.task_id == "task-1", "task result id mismatch");
    return Json{{"value", "task-complete"}};
  };
  server.set_handler(handler);

  const mcp::server::SessionContext context{
      .session_id = "session-1",
      .remote_address = "127.0.0.1",
  };

  const auto list_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::TasksListMethod),
          .params = Json{{"cursor", "page-1"}},
          .id = std::int64_t{11},
      },
      context);
  require(list_response.has_value(), "tasks/list request failed");
  require(list_response->result.has_value(), "tasks/list result missing");
  require(list_response->result->at("tasks").size() == 1,
          "tasks/list result count mismatch");
  require(list_response->result->at("nextCursor") == "page-2",
          "tasks/list next cursor mismatch");

  const auto get_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::TasksGetMethod),
          .params = Json{{"taskId", "task-1"}},
          .id = std::int64_t{12},
      },
      context);
  require(get_response.has_value(), "tasks/get request failed");
  require(get_response->result.has_value(), "tasks/get result missing");
  require(get_response->result->at("taskId") == "task-1",
          "tasks/get task id mismatch");

  const auto cancel_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::TasksCancelMethod),
          .params = Json{{"taskId", "task-1"}},
          .id = std::int64_t{13},
      },
      context);
  require(cancel_response.has_value(), "tasks/cancel request failed");
  require(cancel_response->result.has_value(), "tasks/cancel result missing");
  require(cancel_response->result->at("status") == "cancelled",
          "tasks/cancel status mismatch");

  const auto result_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::TasksResultMethod),
          .params = Json{{"taskId", "task-1"}},
          .id = std::int64_t{14},
      },
      context);
  require(result_response.has_value(), "tasks/result request failed");
  require(result_response->result.has_value(), "tasks/result result missing");
  require(result_response->result->at("value") == "task-complete",
          "tasks/result payload mismatch");
}

void test_client_resource_templates_and_json_helpers() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::Client client(std::move(transport));

  const auto templates = client.list_all_resource_templates();
  require(templates.has_value(), "list_all_resource_templates failed");
  require(templates->size() == 1, "resource template count mismatch");
  require(templates->front().uri_template == "file:///tmp/{name}.txt",
          "resource template uri mismatch");

  const auto completion = client.complete(
      Json{{"ref", Json{{"type", "prompt"}, {"name", "summarize"}}}});
  require(completion.has_value(), "completion helper failed");
  require(completion->at("completion").at("values").at(0) == "hello",
          "completion payload mismatch");

  const auto sample = client.create_message(Json{{"messages", Json::array()}});
  require(sample.has_value(), "create_message helper failed");
  require(sample->at("content").at("text") == "sample",
          "sample payload mismatch");

  const auto elicitation_schema = mcp::protocol::ElicitationSchema::Builder()
                                      .required_email("email")
                                      .optional_bool("remember", true)
                                      .build();
  require(elicitation_schema.has_value(), "elicitation schema builder failed");

  const auto typed_elicitation =
      client.create_elicitation(mcp::protocol::CreateElicitationRequestParam{
          .message = "choose",
          .requested_schema = *elicitation_schema,
      });
  require(typed_elicitation.has_value(),
          "create_elicitation typed helper failed");
  require(typed_elicitation->action == mcp::protocol::ElicitationAction::Accept,
          "elicitation action mismatch");
  require(typed_elicitation->content.has_value(),
          "elicitation typed content missing");
  require(typed_elicitation->content->at("value") == 1,
          "elicitation typed content mismatch");

  const auto raw_elicitation =
      client.create_elicitation(Json{{"message", "choose"}});
  require(raw_elicitation.has_value(), "create_elicitation raw helper failed");
  require(raw_elicitation->at("action") == "accept",
          "elicitation raw payload mismatch");

  const auto set_level = client.set_level("debug");
  require(set_level.has_value(), "set_level failed");
  const auto subscribed = client.subscribe("file:///tmp/readme.txt");
  require(subscribed.has_value(), "subscribe failed");
  const auto unsubscribed = client.unsubscribe("file:///tmp/readme.txt");
  require(unsubscribed.has_value(), "unsubscribe failed");

  require(recording->requests.at(1).method == "completion/complete",
          "completion method mismatch");
  require(recording->requests.at(2).method == "sampling/createMessage",
          "sample method mismatch");
  require(recording->requests.at(3).method == "elicitation/create",
          "elicitation typed method mismatch");
  require(recording->requests.at(4).method == "elicitation/create",
          "elicitation raw method mismatch");
  require(recording->requests.at(5).method == "logging/setLevel",
          "logging method mismatch");
  require(recording->requests.at(6).method == "resources/subscribe",
          "subscribe method mismatch");
  require(recording->requests.at(7).method == "resources/unsubscribe",
          "unsubscribe method mismatch");
}

void test_client_completion_helpers_cover_prompt_and_resource_refs() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::Client client(std::move(transport));

  const auto prompt_completion =
      client.complete_prompt_argument("summarize", "text", "he");
  require(prompt_completion.has_value(), "prompt completion helper failed");
  require(prompt_completion->values.front() == "hello",
          "prompt completion values mismatch");

  const auto prompt_simple =
      client.complete_prompt_simple("summarize", "text", "he");
  require(prompt_simple.has_value(), "prompt simple helper failed");
  require(prompt_simple->front() == "hello",
          "prompt simple completion mismatch");

  const auto resource_completion =
      client.complete_resource_argument("file:///tmp/{name}.txt", "name", "re");
  require(resource_completion.has_value(), "resource completion helper failed");
  require(resource_completion->values.front() == "hello",
          "resource completion values mismatch");

  const auto resource_simple =
      client.complete_resource_simple("file:///tmp/{name}.txt", "name", "re");
  require(resource_simple.has_value(), "resource simple helper failed");
  require(resource_simple->front() == "hello",
          "resource simple completion mismatch");

  require(recording->requests.size() == 4,
          "completion helpers should emit four requests");
  require(recording->requests.at(0).params.at("ref").at("type") == "ref/prompt",
          "prompt completion ref type mismatch");
  require(recording->requests.at(0).params.at("ref").at("name") == "summarize",
          "prompt completion ref name mismatch");
  require(
      recording->requests.at(2).params.at("ref").at("type") == "ref/resource",
      "resource completion ref type mismatch");
  require(recording->requests.at(2).params.at("ref").at("uri") ==
              "file:///tmp/{name}.txt",
          "resource completion ref uri mismatch");
}

void test_client_peer_initialize_uses_explicit_capabilities_and_roots() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::ClientPeer client_peer(std::move(transport));

  mcp::protocol::ClientCapabilities capabilities;
  capabilities.roots.list_changed = true;
  capabilities.sampling.enabled = true;
  capabilities.elicitation.form = true;
  client_peer.set_capabilities(capabilities);
  client_peer.set_roots({mcp::client::Client::Root{.uri = "file:///workspace",
                                                   .name = "workspace"}});

  const auto initialized = client_peer.initialize("peer-client", "1.2.3");
  require(initialized.has_value(), "client peer initialize failed");
  require(recording->requests.size() == 1,
          "client peer initialize should send one request");
  require(recording->requests.front().method == "initialize",
          "client peer initialize method mismatch");
  require(recording->requests.front().params.at("clientInfo").at("name") ==
              "peer-client",
          "client peer initialize client name mismatch");
  require(recording->requests.front()
                  .params.at("capabilities")
                  .at("roots")
                  .at("listChanged") == true,
          "client peer initialize roots capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("sampling")
              .is_object(),
          "client peer initialize sampling capability mismatch");
  require(recording->requests.front()
              .params.at("capabilities")
              .at("elicitation")
              .at("form")
              .is_object(),
          "client peer initialize elicitation capability mismatch");
  const auto roots = client_peer.list_roots();
  require(roots.size() == 1, "client peer roots size mismatch");
  require(roots.front().uri == "file:///workspace",
          "client peer roots uri mismatch");
}

void test_client_roots_and_notification_callbacks() {
  auto transport = std::make_unique<RecordingTransport>();
  mcp::client::Client client(std::move(transport));

  int roots_changed = 0;
  int tools_changed = 0;
  int prompts_changed = 0;
  int resources_changed = 0;
  int progress_notifications = 0;
  int elicitation_completed = 0;
  int raw_notifications = 0;
  int initialized_notifications = 0;
  int cancelled_notifications = 0;
  std::string logged_level;
  std::string logged_message;
  std::string updated_uri;
  std::string cancelled_reason;
  std::string elicitation_completion_id;
  double progress_value = 0.0;

  client.on_initialized([&] { ++initialized_notifications; })
      .on_cancelled(
          [&](const mcp::protocol::RequestId&, std::string_view reason) {
            ++cancelled_notifications;
            cancelled_reason = std::string(reason);
          })
      .on_roots_list_changed([&] { ++roots_changed; })
      .on_tool_list_changed([&] { ++tools_changed; })
      .on_prompt_list_changed([&] { ++prompts_changed; })
      .on_resource_list_changed([&] { ++resources_changed; })
      .on_resource_updated([&](const std::string& uri) { updated_uri = uri; })
      .on_progress(
          [&](const mcp::protocol::ProgressNotificationParams& params) {
            ++progress_notifications;
            progress_value = params.progress;
          })
      .on_elicitation_complete([&](std::string_view elicitation_id) {
        ++elicitation_completed;
        elicitation_completion_id = std::string(elicitation_id);
      })
      .on_logging_message(
          [&](std::string_view level, std::string_view message) {
            logged_level = std::string(level);
            logged_message = std::string(message);
          })
      .on_raw_notification([&](const mcp::protocol::JsonRpcNotification&) {
        ++raw_notifications;
      });

  client.set_roots({mcp::client::Client::Root{.uri = "file:///workspace",
                                              .name = "workspace"}});
  const auto roots = client.list_roots();
  require(roots.size() == 1, "root count mismatch");
  require(roots.front().uri == "file:///workspace", "root uri mismatch");
  require(roots_changed == 1, "set_roots should invoke roots changed callback");

  require(
      client.handle_notification({.method = "notifications/tools/list_changed"})
          .has_value(),
      "tool notification failed");
  require(client.handle_notification({.method = "notifications/initialized"})
              .has_value(),
          "initialized notification failed");
  require(client
              .handle_notification(
                  {.method = "notifications/cancelled",
                   .params = Json{{"requestId", 1}, {"reason", "done"}}})
              .has_value(),
          "cancelled notification failed");
  require(
      client
          .handle_notification({.method = "notifications/prompts/list_changed"})
          .has_value(),
      "prompt notification failed");
  require(client
              .handle_notification(
                  {.method = "notifications/resources/list_changed"})
              .has_value(),
          "resource list notification failed");
  require(
      client
          .handle_notification(
              {.method = std::string(mcp::protocol::ProgressNotificationMethod),
               .params = mcp::protocol::progress_notification_params_to_json(
                   mcp::protocol::ProgressNotificationParams{
                       .progress_token = std::int64_t{1},
                       .progress = 0.5,
                       .total = 1.0,
                       .message = "halfway",
                   })})
          .has_value(),
      "progress notification failed");
  require(client
              .handle_notification(
                  {.method = "notifications/resources/updated",
                   .params = Json{{"uri", "file:///workspace/README.md"}}})
              .has_value(),
          "resource updated notification failed");
  require(client
              .handle_notification(
                  {.method = std::string(
                       mcp::protocol::ElicitationCompleteNotificationMethod),
                   .params = mcp::protocol::
                       elicitation_complete_notification_params_to_json(
                           mcp::protocol::ElicitationCompleteNotificationParams{
                               .elicitation_id = "elicitation-1",
                           })})
              .has_value(),
          "elicitation completion notification failed");
  require(client
              .handle_notification(
                  {.method = "notifications/message",
                   .params = Json{{"level", "info"}, {"data", "ready"}}})
              .has_value(),
          "logging notification failed");

  require(tools_changed == 1, "tool list callback count mismatch");
  require(prompts_changed == 1, "prompt list callback count mismatch");
  require(resources_changed == 1, "resource list callback count mismatch");
  require(progress_notifications == 1, "progress notification count mismatch");
  require(progress_value == 0.5, "progress value mismatch");
  require(updated_uri == "file:///workspace/README.md",
          "resource updated uri mismatch");
  require(logged_level == "info", "logged level mismatch");
  require(logged_message == "ready", "logged message mismatch");
  require(initialized_notifications == 1,
          "initialized notification count mismatch");
  require(cancelled_notifications == 1,
          "cancelled notification count mismatch");
  require(elicitation_completed == 1, "elicitation completion count mismatch");
  require(elicitation_completion_id == "elicitation-1",
          "elicitation completion id mismatch");
  require(cancelled_reason == "done", "cancelled reason mismatch");
  require(raw_notifications == 9, "raw notification callback count mismatch");
}

void test_client_elicitation_defaults_to_decline_without_handler() {
  const Json form_request =
      Json{{"message", "choose"},
           {"requestedSchema",
            Json{{"type", "object"},
                 {"properties", Json{{"name", Json{{"type", "string"}}}}}}}};

  mcp::client::Client client(std::make_unique<RecordingTransport>());
  const auto response = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ElicitationCreateMethod),
      .params = form_request,
      .id = std::int64_t{30},
  });
  require(response.has_value(),
          "default elicitation decline should produce a response");
  require(response->result.has_value(),
          "default elicitation decline should return result");
  require(response->result->at("action") == "decline",
          "default elicitation action mismatch");

  const auto invalid = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ElicitationCreateMethod),
      .params = Json{{"message", "choose"}},
      .id = std::int64_t{31},
  });
  require(invalid.has_value(), "invalid elicitation should produce response");
  require(invalid->error.has_value(),
          "invalid elicitation should still validate params before decline");
  require(invalid->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid elicitation params error code mismatch");

  mcp::client::Client validating_client(std::make_unique<RecordingTransport>());
  validating_client.on_create_elicitation_request(
      [](const mcp::protocol::CreateElicitationRequestParam&)
          -> mcp::core::Result<mcp::protocol::CreateElicitationResult> {
        mcp::protocol::CreateElicitationResult result;
        result.action = mcp::protocol::ElicitationAction::Accept;
        result.content = Json{{"name", 7}};
        return result;
      });
  const auto invalid_result =
      validating_client.handle_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ElicitationCreateMethod),
          .params = form_request,
          .id = std::int64_t{32},
      });
  require(invalid_result.has_value(),
          "invalid elicitation result should produce a response");
  require(invalid_result->error.has_value(),
          "invalid elicitation result should fail");
  require(invalid_result->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "invalid elicitation result error code mismatch");

  mcp::ClientPeer peer(std::make_unique<RecordingTransport>());
  const auto peer_message = peer.dispatch_message(
      mcp::protocol::JsonRpcMessage{mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ElicitationCreateMethod),
          .params = form_request,
          .id = std::int64_t{33},
      }});
  require(peer_message.has_value(),
          "peer default elicitation decline dispatch failed");
  require(peer_message->has_value(),
          "peer default elicitation decline response missing");
  const auto* peer_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&**peer_message);
  require(peer_response != nullptr,
          "peer default elicitation decline response type mismatch");
  require(peer_response->result.has_value(),
          "peer default elicitation decline result missing");
  require(peer_response->result->at("action") == "decline",
          "peer default elicitation action mismatch");

  const auto peer_invalid = peer.dispatch_message(
      mcp::protocol::JsonRpcMessage{mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ElicitationCreateMethod),
          .params = Json{{"message", "choose"}},
          .id = std::int64_t{34},
      }});
  require(peer_invalid.has_value(),
          "peer invalid elicitation dispatch should produce response");
  require(peer_invalid->has_value(),
          "peer invalid elicitation response missing");
  const auto* peer_invalid_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&**peer_invalid);
  require(peer_invalid_response != nullptr,
          "peer invalid elicitation response type mismatch");
  require(peer_invalid_response->error.has_value(),
          "peer invalid elicitation should fail");
  require(peer_invalid_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "peer invalid elicitation params error code mismatch");

  mcp::ClientPeer validating_peer(std::make_unique<RecordingTransport>());
  validating_peer.on_create_elicitation_request(
      [](const mcp::protocol::CreateElicitationRequestParam&)
          -> mcp::core::Result<mcp::protocol::CreateElicitationResult> {
        mcp::protocol::CreateElicitationResult result;
        result.action = mcp::protocol::ElicitationAction::Accept;
        result.content = Json{{"name", 7}};
        return result;
      });
  const auto peer_invalid_result = validating_peer.dispatch_message(
      mcp::protocol::JsonRpcMessage{mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ElicitationCreateMethod),
          .params = form_request,
          .id = std::int64_t{35},
      }});
  require(peer_invalid_result.has_value(),
          "peer invalid elicitation result dispatch failed");
  require(peer_invalid_result->has_value(),
          "peer invalid elicitation result response missing");
  const auto* peer_invalid_result_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&**peer_invalid_result);
  require(peer_invalid_result_response != nullptr,
          "peer invalid elicitation result response type mismatch");
  require(peer_invalid_result_response->error.has_value(),
          "peer invalid elicitation result should fail");
  require(peer_invalid_result_response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InternalError),
          "peer invalid elicitation result code mismatch");
}

void test_client_request_callbacks() {
  auto transport = std::make_unique<RecordingTransport>();
  mcp::client::Client client(std::move(transport));

  client.set_roots({mcp::client::Client::Root{.uri = "file:///workspace",
                                              .name = "workspace"}});

  std::string sampling_prompt;
  std::string elicitation_message;
  std::string elicitation_url;
  client
      .on_list_roots_request(
          [&]() -> mcp::core::Result<mcp::protocol::RootsListResult> {
            return mcp::protocol::RootsListResult{
                .roots = {mcp::protocol::Root{
                    .uri = "file:///workspace",
                    .name = "workspace",
                }},
            };
          })
      .on_create_message_request(
          [&](const mcp::protocol::CreateMessageParams& params)
              -> mcp::core::Result<mcp::protocol::CreateMessageResult> {
            sampling_prompt = params.messages.front().content.text;
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
          })
      .on_create_elicitation_request(
          [&](const mcp::protocol::CreateElicitationRequestParam& request)
              -> mcp::core::Result<mcp::protocol::CreateElicitationResult> {
            elicitation_message = request.message;
            if (request.message == "decline" ||
                request.message == "decline-url") {
              return mcp::protocol::CreateElicitationResult{
                  .action = mcp::protocol::ElicitationAction::Decline,
              };
            }
            if (request.message == "cancel" ||
                request.message == "cancel-url") {
              return mcp::protocol::CreateElicitationResult{
                  .action = mcp::protocol::ElicitationAction::Cancel,
              };
            }
            if (request.mode == mcp::protocol::ElicitationMode::Url) {
              elicitation_url = request.url.value_or("");
              return mcp::protocol::CreateElicitationResult{
                  .action = mcp::protocol::ElicitationAction::Accept,
              };
            }
            return mcp::protocol::CreateElicitationResult{
                .action = mcp::protocol::ElicitationAction::Accept,
                .content = Json{{"chosen", true}},
            };
          })
      .on_custom_request([&](const mcp::protocol::JsonRpcRequest& request)
                             -> mcp::core::Result<Json> {
        if (request.method == "custom/echo") {
          return Json{{"ok", true}};
        }
        return std::unexpected(mcp::core::Error{
            static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
            "custom request not handled",
            std::string(request.method),
        });
      });

  const auto roots = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::RootsListMethod),
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(roots.has_value(), "roots/list request failed");
  require(roots->result->at("roots").at(0).at("uri") == "file:///workspace",
          "roots/list response mismatch");

  const auto ping = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PingMethod),
      .params = Json::object(),
      .id = std::int64_t{1},
  });
  require(ping.has_value(), "ping request failed");
  require(ping->result->empty(), "ping response mismatch");

  const auto sampled = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::SamplingCreateMessageMethod),
      .params =
          Json{
              {"messages",
               Json::array({Json{
                   {"role", "user"},
                   {"content", Json{{"type", "text"}, {"text", "hi"}}}}})},
              {"maxTokens", 16},
          },
      .id = std::int64_t{2},
  });
  require(sampled.has_value(), "sampling/createMessage request failed");
  require(sampled->result->at("role") == "assistant",
          "sampling response role mismatch");
  require(sampled->result->at("content").at("text") == "sampled",
          "sampling response content mismatch");
  require(sampling_prompt == "hi", "sampling request handler mismatch");

  const auto invalid_sampled =
      client.handle_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::SamplingCreateMessageMethod),
          .params = Json{{"messages", Json::array()}},
          .id = std::int64_t{20},
      });
  require(invalid_sampled.has_value(),
          "invalid sampling params should produce a response");
  require(invalid_sampled->error.has_value(),
          "invalid sampling params should fail");
  require(invalid_sampled->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidParams),
          "invalid sampling params error code mismatch");

  const Json form_schema =
      Json{{"type", "object"},
           {"properties", Json{{"name", Json{{"type", "string"}}}}}};

  const auto elicited = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ElicitationCreateMethod),
      .params =
          Json{
              {"message", "choose"},
              {"requestedSchema", form_schema},
          },
      .id = std::int64_t{3},
  });
  require(elicited.has_value(), "elicitation/create request failed");
  require(elicited->result->at("action") == "accept",
          "elicitation response action mismatch");
  require(elicitation_message == "choose",
          "elicitation request handler mismatch");

  const auto declined = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ElicitationCreateMethod),
      .params = Json{{"message", "decline"}, {"requestedSchema", form_schema}},
      .id = std::int64_t{4},
  });
  require(declined.has_value(), "form elicitation decline request failed");
  require(declined->result->at("action") == "decline",
          "form elicitation decline action mismatch");

  const auto cancelled = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ElicitationCreateMethod),
      .params = Json{{"message", "cancel"}, {"requestedSchema", form_schema}},
      .id = std::int64_t{5},
  });
  require(cancelled.has_value(), "form elicitation cancel request failed");
  require(cancelled->result->at("action") == "cancel",
          "form elicitation cancel action mismatch");

  const auto url_elicited = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ElicitationCreateMethod),
      .params =
          Json{
              {"message", "open"},
              {"mode", "url"},
              {"elicitationId", "elicitation-1"},
              {"url", "https://example.test/elicitation/1"},
              {"requestState", Json{{"step", 1}}},
          },
      .id = std::int64_t{6},
  });
  require(url_elicited.has_value(), "url elicitation/create request failed");
  require(url_elicited->result->at("action") == "accept",
          "url elicitation response action mismatch");
  require(elicitation_url == "https://example.test/elicitation/1",
          "url elicitation request handler mismatch");

  const auto url_declined = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ElicitationCreateMethod),
      .params =
          Json{
              {"message", "decline-url"},
              {"mode", "url"},
              {"elicitationId", "elicitation-2"},
              {"url", "https://example.test/elicitation/2"},
          },
      .id = std::int64_t{7},
  });
  require(url_declined.has_value(), "url elicitation decline request failed");
  require(url_declined->result->at("action") == "decline",
          "url elicitation decline action mismatch");

  const auto url_cancelled =
      client.handle_request(mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::ElicitationCreateMethod),
          .params =
              Json{
                  {"message", "cancel-url"},
                  {"mode", "url"},
                  {"elicitationId", "elicitation-3"},
                  {"url", "https://example.test/elicitation/3"},
              },
          .id = std::int64_t{8},
      });
  require(url_cancelled.has_value(), "url elicitation cancel request failed");
  require(url_cancelled->result->at("action") == "cancel",
          "url elicitation cancel action mismatch");

  const auto custom = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = "custom/echo",
      .params = Json::object(),
      .id = std::int64_t{9},
  });
  require(custom.has_value(), "custom request should return a response");
  require(custom->result->at("ok") == true, "custom request result mismatch");
}

void test_client_session_discover_prompts_uses_client_list_prompts() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::McpClientSession session(std::move(transport));

  const auto prompts = session.discover_prompts();
  require(prompts.has_value(), "session discover_prompts failed");
  require(recording->requests.size() == 1,
          "discover_prompts should send one request");
  require(recording->requests.front().method == "prompts/list",
          "discover_prompts should call prompts/list");
  require(prompts->size() == 1, "unexpected session prompt count");
  require(prompts->front().name == "summarize", "session prompt name mismatch");
  require(prompts->front().description == "Summarize text",
          "session prompt description mismatch");
  require(prompts->front().arguments.size() == 1,
          "session prompt arguments mismatch");
  require(prompts->front().arguments.front().required,
          "session prompt argument required mismatch");
}

void test_client_session_discover_resources_uses_client_list_resources() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::McpClientSession session(std::move(transport));

  const auto resources = session.discover_resources();
  require(resources.has_value(), "session discover_resources failed");
  require(recording->requests.size() == 1,
          "discover_resources should send one request");
  require(recording->requests.front().method == "resources/list",
          "discover_resources should call resources/list");
  require(resources->size() == 1, "unexpected session resource count");
  require(resources->front().uri == "file:///tmp/readme.txt",
          "session resource uri mismatch");
  require(resources->front().name == "Readme",
          "session resource name mismatch");
  require(resources->front().mime_type == "text/plain",
          "session resource mime type mismatch");
}

void test_client_session_get_prompt_uses_client_prompts_get() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::McpClientSession session(std::move(transport));

  const auto prompt = session.get_prompt(mcp::protocol::PromptsGetParams{
      .name = "summarize",
      .arguments = Json{{"text", "hello"}},
  });
  require(prompt.has_value(), "session get_prompt failed");
  require(recording->requests.size() == 1,
          "get_prompt should send one request");
  require(recording->requests.front().method == "prompts/get",
          "get_prompt should call prompts/get");
  require(recording->requests.front().params.at("name") == "summarize",
          "get_prompt name param mismatch");
  require(prompt->messages.size() == 1, "get_prompt message count mismatch");
  require(prompt->messages.front().content.text == "Summarize hello",
          "get_prompt text mismatch");
}

void test_client_session_read_resource_uses_client_resources_read() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  mcp::client::McpClientSession session(std::move(transport));

  const auto resource =
      session.read_resource(mcp::protocol::ResourcesReadParams{
          .uri = "file:///tmp/readme.txt",
      });
  require(resource.has_value(), "session read_resource failed");
  require(recording->requests.size() == 1,
          "read_resource should send one request");
  require(recording->requests.front().method == "resources/read",
          "read_resource should call resources/read");
  require(
      recording->requests.front().params.at("uri") == "file:///tmp/readme.txt",
      "read_resource uri param mismatch");
  require(resource->contents.size() == 1,
          "read_resource content count mismatch");
  require(resource->contents.front().text == "hello",
          "read_resource text mismatch");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, void (*)()>> tests = {
      {"request executor can be configured before first use",
       test_request_executor_can_be_configured_before_first_use},
      {"list tools round trip", test_list_tools_round_trip},
      {"get tool round trip", test_get_tool_round_trip},
      {"server direct facade round trip", test_server_direct_facade_round_trip},
      {"role-aware peer facades",
       test_role_aware_peer_facades_forward_to_client_and_server},
      {"client peer typed async helpers", test_client_peer_typed_async_helpers},
      {"server client-peer request handle timeout",
       test_server_client_peer_request_handle_times_out_and_cancels},
      {"server client-peer request handle explicit cancel",
       test_server_client_peer_request_handle_explicit_cancel_notifies},
      {"client peer request handle timeout",
       test_client_peer_request_handle_times_out_and_cancels},
      {"client peer request handle cancellation token",
       test_client_peer_request_handle_observes_cancellation_token},
      {"request handle errors are stable and structured",
       test_request_handle_errors_are_stable_and_structured},
      {"client peer cancel timeout race stress",
       test_client_peer_cancel_timeout_race_stress},
      {"server client-peer cancel timeout race stress",
       test_server_client_peer_cancel_timeout_race_stress},
      {"client request ids are thread safe for concurrent async requests",
       test_client_request_ids_are_thread_safe_for_concurrent_async_requests},
      {"service lifecycle facades",
       test_service_lifecycle_facades_start_and_stop},
      {"server peer role-generic transport receive loop",
       test_server_peer_serves_role_generic_transport_receive_loop},
      {"server service role-generic transport receive loop",
       test_server_service_serves_role_generic_transport_receive_loop},
      {"server service native transport stop cancels loop",
       test_server_service_native_transport_stop_cancels_loop},
      {"server non-task tool observes cancelled notification",
       test_server_non_task_tool_observes_cancelled_notification},
      {"completion and sampling handlers observe cancellation token",
       test_completion_and_sampling_handlers_observe_cancellation_token},
      {"contract completion and sampling handlers receive cancellation token",
       test_contract_completion_sampling_handlers_receive_cancellation_token},
      {"contract handlers override client and server requests",
       test_contract_handlers_override_client_and_server_requests},
      {"contract discovery handlers override registries",
       test_contract_discovery_handlers_override_registries},
      {"contract task handlers receive session context",
       test_contract_task_handlers_receive_session_context},
      {"contract notification handlers receive session context",
       test_contract_notification_handlers_receive_session_context},
      {"server notification facade round trip",
       test_server_notification_facade_round_trip},
      {"server notify facade broadcasts notifications",
       test_server_notify_facade_broadcasts_notifications},
      {"call tool round trip", test_call_tool_round_trip},
      {"client call tool serializes task request",
       test_client_call_tool_serializes_task_request},
      {"server tool task support validation",
       test_server_tool_task_support_validation},
      {"server task processor tool call lifecycle",
       test_server_task_processor_tool_call_lifecycle},
      {"server task processor failed and cancelled tasks",
       test_server_task_processor_failed_and_cancelled_tasks},
      {"server task processor timeout and retention",
       test_server_task_processor_timeout_and_retention},
      {"task processor cancellation before start",
       test_task_processor_cancellation_before_start},
      {"task processor retention count limit",
       test_task_processor_retention_count_limit},
      {"task processor generic operation",
       test_task_processor_generic_operation},
      {"ping raw request", test_ping_raw_request},
      {"server info accessors and ping", test_server_info_accessors_and_ping},
      {"initialize handshake shape", test_initialize_handshake_shape},
      {"server initialize rejects invalid protocol versions",
       test_server_initialize_rejects_invalid_protocol_versions},
      {"default server initialize omits inactive capabilities",
       test_default_server_initialize_omits_inactive_capabilities},
      {"server app builder registers parity surface",
       test_server_app_builder_registers_parity_surface},
      {"server app builder registers typed completion",
       test_server_app_builder_registers_typed_completion},
      {"server app builder registers typed prompt and resource",
       test_server_app_builder_registers_typed_prompt_and_resource},
      {"server app builder registers typed tool",
       test_server_app_builder_registers_typed_tool},
      {"server tool schema validator hooks",
       test_server_tool_schema_validator_hooks},
      {"server app builder typed scalar tool rejects empty args",
       test_server_app_builder_typed_scalar_tool_rejects_empty_args},
      {"client session initialize and mark initialized",
       test_client_session_initialize_and_mark_initialized},
      {"client initialize with empty explicit capabilities",
       test_client_initialize_with_empty_explicit_capabilities},
      {"client initialize with explicit task capabilities",
       test_client_initialize_with_explicit_task_capabilities},
      {"client initialize rejects invalid protocol versions",
       test_client_initialize_rejects_invalid_protocol_versions},
      {"client session discover prompts",
       test_client_session_discover_prompts_uses_client_list_prompts},
      {"client session discover resources",
       test_client_session_discover_resources_uses_client_list_resources},
      {"client session get prompt",
       test_client_session_get_prompt_uses_client_prompts_get},
      {"client session read resource",
       test_client_session_read_resource_uses_client_resources_read},
      {"client session discover tools",
       test_client_session_discover_tools_uses_client_list_tools},
      {"client list all tools follows cursors",
       test_client_list_all_tools_follows_cursors},
      {"client task helpers round trip", test_client_task_helpers_round_trip},
      {"task status notification round trip",
       test_task_status_notification_round_trip},
      {"server task request round trip", test_server_task_request_round_trip},
      {"client resource templates and json helpers",
       test_client_resource_templates_and_json_helpers},
      {"client completion helpers cover prompt and resource refs",
       test_client_completion_helpers_cover_prompt_and_resource_refs},
      {"client peer initialize uses explicit capabilities and roots",
       test_client_peer_initialize_uses_explicit_capabilities_and_roots},
      {"client roots and notification callbacks",
       test_client_roots_and_notification_callbacks},
      {"client elicitation defaults to decline without handler",
       test_client_elicitation_defaults_to_decline_without_handler},
      {"client request callbacks", test_client_request_callbacks},
  };

  std::size_t failures = 0;
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "[PASS] " << name << std::endl;
    } catch (const std::exception& ex) {
      ++failures;
      std::cerr << "[FAIL] " << name << ": " << ex.what() << std::endl;
    }
  }

  if (failures != 0) {
    std::cerr << failures << " test(s) failed" << std::endl;
    return 1;
  }

  std::cout << tests.size() << " test(s) passed" << std::endl;
  return 0;
}
