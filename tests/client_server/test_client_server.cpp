// Copyright (c) 2025 [caomengxuan666]

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

namespace {

using mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
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
          .result =
              Json{
                  {"protocolVersion",
                   std::string(mcp::protocol::McpProtocolVersion)},
                  {"capabilities", Json::object()},
                  {"serverInfo",
                   Json{{"name", "FakeServer"}, {"version", "1"}}},
              },
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
                   {"content", Json{{"type", "text"}, {"text", "sample"}}}},
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
  bool stopped = false;
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

void test_server_client_peer_request_handle_times_out_and_cancels() {
  BlockingRequestServerTransport transport;
  mcp::server::ClientPeer peer(&transport);

  mcp::RequestOptions options;
  options.timeout = std::chrono::milliseconds(1);

  auto handle = peer.request_async("tools/list", Json::object(), options);
  transport.wait_until_request_started();

  const auto response = handle.await_response();
  require(!response.has_value(), "request handle should time out");
  require(response.error().message == "request timed out",
          "request handle timeout message mismatch");

  transport.wait_until_notifications(1);
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
  require(response.error().message == "request timed out",
          "client request handle timeout message mismatch");

  recording->wait_until_notifications(1);
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

void test_service_lifecycle_facades_start_and_stop() {
  auto transport = std::make_unique<RecordingTransport>();
  auto* recording = transport.get();
  auto running_client = mcp::serve(mcp::ClientPeer(std::move(transport)));
  require(running_client.has_value(), "client service should start");
  require(running_client->running(), "client service should report running");

  const auto tools = running_client->peer().list_tools();
  require(tools.has_value(), "running client service list_tools failed");
  require(tools->front().name == "fake-tool",
          "running client service tool mismatch");

  const auto client_stopped = running_client->stop();
  require(client_stopped.has_value(), "client service stop failed");
  require(!running_client->running(), "client service should report stopped");
  require(recording->stopped, "client service should stop transport");

  auto server = std::make_unique<mcp::server::Server>(make_server());
  auto running_server = mcp::serve(mcp::ServerPeer(std::move(server)));
  require(running_server.has_value(), "server service should start");
  require(running_server->running(), "server service should report running");
  require(running_server->peer().list_tools().front().name == "echo",
          "running server service tool mismatch");

  const auto server_stopped = running_server->stop();
  require(server_stopped.has_value(), "server service stop failed");
  require(!running_server->running(), "server service should report stopped");
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
  require(!result->is_error, "tool result should not be an error");
  require(result->content.size() == 1, "tool result content size mismatch");
  require(result->content.front().type == "text",
          "tool result content type mismatch");
  require(result->content.front().text == "{\"value\":42}",
          "tool result text mismatch");
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
  require(response->result->at("capabilities").at("experimental").at("beta"),
          "server experimental mismatch");
  require(response->result->at("capabilities")
              .at("extensions")
              .at("vendor/feature")
              .at("enabled"),
          "server extension mismatch");
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
          .resource_template(mcp::protocol::ResourceTemplate{
              .uri_template = "file:///tmp/{name}.txt",
              .name = "Tmp file",
              .description = "A tmp file",
              .mime_type = "text/plain",
          })
          .completion([](const Json& request) {
            return Json{{"completion",
                         request.at("prefix").get<std::string>() + "llo"}};
          })
          .sampling([](const Json& request) {
            return Json{{"sample", request.at("prompt").get<std::string>()}};
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
          .params = Json::object(),
          .id = std::int64_t{1},
      },
      context);
  require(initialized.has_value(), "facade initialize failed");
  require(initialized->result->at("serverInfo").at("name") == "FacadeServer",
          "facade name mismatch");
  require(initialized->result->at("instructions") == "test instructions",
          "facade instructions mismatch");
  require(initialized->result->at("capabilities").contains("completions"),
          "completions capability missing");
  require(
      initialized->result->at("capabilities").at("completions").at("enabled"),
      "completions capability should be enabled");
  require(initialized->result->at("capabilities").at("logging").at("enabled"),
          "logging capability missing");

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
  require(completion->result->at("completion") == "hello",
          "facade completion mismatch");

  const auto sampling = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "sampling/createMessage",
          .params = Json{{"prompt", "write"}},
          .id = std::int64_t{7},
      },
      context);
  require(sampling.has_value(), "facade sampling failed");
  require(sampling->result->at("sample") == "write",
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
  require(recording->requests.at(2).params.at("ref").at("name") ==
              "file:///tmp/{name}.txt",
          "resource completion ref name mismatch");
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

  const auto elicited = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ElicitationCreateMethod),
      .params =
          Json{
              {"message", "choose"},
              {"requestedSchema",
               Json{
                   {"type", "object"},
                   {"properties", Json{{"name", Json{{"type", "string"}}}}},
               }},
          },
      .id = std::int64_t{3},
  });
  require(elicited.has_value(), "elicitation/create request failed");
  require(elicited->result->at("action") == "accept",
          "elicitation response action mismatch");
  require(elicitation_message == "choose",
          "elicitation request handler mismatch");

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
      .id = std::int64_t{4},
  });
  require(url_elicited.has_value(), "url elicitation/create request failed");
  require(url_elicited->result->at("action") == "accept",
          "url elicitation response action mismatch");
  require(elicitation_url == "https://example.test/elicitation/1",
          "url elicitation request handler mismatch");

  const auto custom = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = "custom/echo",
      .params = Json::object(),
      .id = std::int64_t{5},
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
      {"list tools round trip", test_list_tools_round_trip},
      {"get tool round trip", test_get_tool_round_trip},
      {"server direct facade round trip", test_server_direct_facade_round_trip},
      {"role-aware peer facades",
       test_role_aware_peer_facades_forward_to_client_and_server},
      {"server client-peer request handle timeout",
       test_server_client_peer_request_handle_times_out_and_cancels},
      {"client peer request handle timeout",
       test_client_peer_request_handle_times_out_and_cancels},
      {"service lifecycle facades",
       test_service_lifecycle_facades_start_and_stop},
      {"contract handlers override client and server requests",
       test_contract_handlers_override_client_and_server_requests},
      {"server notification facade round trip",
       test_server_notification_facade_round_trip},
      {"server notify facade broadcasts notifications",
       test_server_notify_facade_broadcasts_notifications},
      {"call tool round trip", test_call_tool_round_trip},
      {"ping raw request", test_ping_raw_request},
      {"server info accessors and ping", test_server_info_accessors_and_ping},
      {"initialize handshake shape", test_initialize_handshake_shape},
      {"server app builder registers parity surface",
       test_server_app_builder_registers_parity_surface},
      {"client session initialize and mark initialized",
       test_client_session_initialize_and_mark_initialized},
      {"client initialize with explicit task capabilities",
       test_client_initialize_with_explicit_task_capabilities},
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
      {"client request callbacks", test_client_request_callbacks},
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
