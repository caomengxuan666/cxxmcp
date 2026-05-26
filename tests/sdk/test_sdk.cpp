// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/core/executor.hpp"
#include "cxxmcp/sdk.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

Json full_server_capabilities_json() {
  mcp::protocol::TaskCapabilities tasks;
  tasks.list = true;
  tasks.cancel = true;
  tasks.tools_call = true;
  const auto capabilities = mcp::protocol::server_capabilities()
                                .logging()
                                .completions()
                                .resources(false, true)
                                .tasks(tasks)
                                .build();
  return mcp::protocol::server_capabilities_to_json(capabilities);
}

class RecordingClientTransport final : public mcp::client::Transport {
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
                  {"capabilities", server_capabilities},
                  {"serverInfo",
                   Json{{"name", "sdk-test-server"}, {"version", "1"}}},
              },
      };
    }
    if (request.method == "ping") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = Json::object(),
      };
    }
    if (request.method == "tools/list") {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{
                  {"tools", Json::array({
                                Json{
                                    {"name", "echo"},
                                    {"description", "Echo payload"},
                                    {"inputSchema", Json{{"type", "object"}}},
                                    {"streaming", false},
                                },
                            })},
              },
      };
    }
    if (request.method == "tools/call") {
      if (request.params.is_object() && request.params.contains("task")) {
        mcp::protocol::CreateTaskResult task;
        task.task.task_id = "recorded-task";
        task.task.status = mcp::protocol::TaskStatus::Working;
        task.task.created_at = "2025-01-01T00:00:00Z";
        task.task.last_updated_at = "2025-01-01T00:00:00Z";
        return mcp::protocol::JsonRpcResponse{
            .id = request.id,
            .result = mcp::protocol::create_task_result_to_json(task),
        };
      }
      const auto arguments = request.params.value("arguments", Json::object());
      mcp::protocol::ToolResult result;
      result.content.push_back(mcp::protocol::ContentBlock{
          .type = "text",
          .text = arguments.dump(),
          .data = Json::object(),
      });
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = mcp::protocol::tool_result_to_json(result),
      };
    }
    if (request.method == std::string(mcp::protocol::LoggingSetLevelMethod) ||
        request.method ==
            std::string(mcp::protocol::ResourcesSubscribeMethod) ||
        request.method ==
            std::string(mcp::protocol::ResourcesUnsubscribeMethod)) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = Json::object(),
      };
    }
    if (request.method ==
        std::string(mcp::protocol::CompletionCompleteMethod)) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = Json{{"completion", Json{{"values", Json::array({"ok"})}}}},
      };
    }
    if (request.method == std::string(mcp::protocol::TasksListMethod)) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{{"tasks", Json::array({Json{
                                 {"taskId", "recorded-task"},
                                 {"status", "working"},
                                 {"createdAt", "2025-01-01T00:00:00Z"},
                                 {"lastUpdatedAt", "2025-01-01T00:00:00Z"},
                                 {"ttl", nullptr},
                             }})}},
      };
    }
    if (request.method == std::string(mcp::protocol::TasksGetMethod) ||
        request.method == std::string(mcp::protocol::TasksCancelMethod)) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result = Json{{"taskId", request.params.at("taskId")},
                         {"status",
                          request.method ==
                                  std::string(mcp::protocol::TasksCancelMethod)
                              ? "cancelled"
                              : "completed"},
                         {"createdAt", "2025-01-01T00:00:00Z"},
                         {"lastUpdatedAt", "2025-01-01T00:00:00Z"},
                         {"ttl", nullptr}},
      };
    }
    if (request.method == std::string(mcp::protocol::TasksResultMethod)) {
      return mcp::protocol::JsonRpcResponse{
          .id = request.id,
          .result =
              Json{{"taskId", request.params.at("taskId")}, {"value", 99}},
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
      const mcp::protocol::JsonRpcNotification&) override {
    return mcp::core::Unit{};
  }

  void stop() noexcept override { stopped = true; }

  std::vector<mcp::protocol::JsonRpcRequest> requests;
  Json server_capabilities = Json::object();
  bool stopped = false;
};

class RecordingClientContractTransport final
    : public mcp::transport::ClientTransport {
 public:
  std::string_view name() const noexcept override {
    return "recording-client-contract";
  }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    sent.push_back(message);
    const auto* request = std::get_if<mcp::protocol::JsonRpcRequest>(&message);
    if (request == nullptr) {
      return mcp::core::Unit{};
    }
    if (request->method == "initialize") {
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result =
              Json{
                  {"protocolVersion",
                   std::string(mcp::protocol::McpProtocolVersion)},
                  {"capabilities", server_capabilities},
                  {"serverInfo",
                   Json{{"name", "sdk-test-server"}, {"version", "1"}}},
              },
      });
    } else if (request->method == "ping") {
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result = Json::object(),
      });
    } else if (request->method ==
                   std::string(mcp::protocol::LoggingSetLevelMethod) ||
               request->method ==
                   std::string(mcp::protocol::ResourcesSubscribeMethod) ||
               request->method ==
                   std::string(mcp::protocol::ResourcesUnsubscribeMethod)) {
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result = Json::object(),
      });
    } else if (request->method ==
               std::string(mcp::protocol::CompletionCompleteMethod)) {
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result = Json{{"completion", Json{{"values", Json::array({"ok"})}}}},
      });
    } else if (request->method == std::string(mcp::protocol::ToolsCallMethod) &&
               request->params.is_object() &&
               request->params.contains("task")) {
      mcp::protocol::CreateTaskResult task;
      task.task.task_id = "native-task";
      task.task.status = mcp::protocol::TaskStatus::Working;
      task.task.created_at = "2025-01-01T00:00:00Z";
      task.task.last_updated_at = "2025-01-01T00:00:00Z";
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result = mcp::protocol::create_task_result_to_json(task),
      });
    } else if (request->method == std::string(mcp::protocol::ToolsListMethod)) {
      const bool has_cursor =
          request->params.is_object() && request->params.contains("cursor");
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result = has_cursor
                        ? Json{{"tools",
                                Json::array({Json{
                                    {"name", "native-next"},
                                    {"description", "Native peer next page"},
                                    {"inputSchema", Json{{"type", "object"}}},
                                }})}}
                        : Json{{"tools",
                                Json::array({Json{
                                    {"name", "native-echo"},
                                    {"description", "Native peer echo"},
                                    {"inputSchema", Json{{"type", "object"}}},
                                }})},
                               {"nextCursor", "page-2"}},
      });
    } else if (request->method ==
               std::string(mcp::protocol::PromptsListMethod)) {
      const bool has_cursor =
          request->params.is_object() && request->params.contains("cursor");
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result =
              has_cursor
                  ? Json{{"prompts",
                          Json::array({Json{
                              {"name", "native-prompt-next"},
                              {"description", "Native peer prompt next page"},
                          }})}}
                  : Json{{"prompts", Json::array({Json{
                                         {"name", "native-prompt"},
                                         {"description", "Native peer prompt"},
                                     }})},
                         {"nextCursor", "page-2"}},
      });
    } else if (request->method ==
               std::string(mcp::protocol::ResourcesListMethod)) {
      const bool has_cursor =
          request->params.is_object() && request->params.contains("cursor");
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result =
              has_cursor
                  ? Json{{"resources", Json::array({Json{
                                           {"uri", "file:///native-next.txt"},
                                           {"name", "native-resource-next"},
                                       }})}}
                  : Json{{"resources", Json::array({Json{
                                           {"uri", "file:///native.txt"},
                                           {"name", "native-resource"},
                                       }})},
                         {"nextCursor", "page-2"}},
      });
    } else if (request->method ==
               std::string(mcp::protocol::ResourcesTemplatesListMethod)) {
      const bool has_cursor =
          request->params.is_object() && request->params.contains("cursor");
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result = has_cursor
                        ? Json{{"resourceTemplates",
                                Json::array({Json{
                                    {"uriTemplate", "file:///{name}-next.txt"},
                                    {"name", "native-template-next"},
                                }})}}
                        : Json{{"resourceTemplates",
                                Json::array({Json{
                                    {"uriTemplate", "file:///{name}.txt"},
                                    {"name", "native-template"},
                                }})},
                               {"nextCursor", "page-2"}},
      });
    } else if (request->method == std::string(mcp::protocol::TasksListMethod)) {
      const bool has_cursor =
          request->params.is_object() && request->params.contains("cursor");
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result = has_cursor
                        ? Json{{"tasks",
                                Json::array({Json{
                                    {"taskId", "task-next"},
                                    {"status", "completed"},
                                    {"createdAt", "2025-01-01T00:00:00Z"},
                                    {"lastUpdatedAt", "2025-01-01T00:00:01Z"},
                                    {"ttl", nullptr},
                                }})}}
                        : Json{{"tasks",
                                Json::array({Json{
                                    {"taskId", "task-first"},
                                    {"status", "working"},
                                    {"createdAt", "2025-01-01T00:00:00Z"},
                                    {"lastUpdatedAt", "2025-01-01T00:00:00Z"},
                                    {"ttl", nullptr},
                                }})},
                               {"nextCursor", "page-2"}},
      });
    } else if (request->method == std::string(mcp::protocol::TasksGetMethod) ||
               request->method ==
                   std::string(mcp::protocol::TasksCancelMethod)) {
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result = Json{{"taskId", request->params.at("taskId")},
                         {"status",
                          request->method ==
                                  std::string(mcp::protocol::TasksCancelMethod)
                              ? "cancelled"
                              : "completed"},
                         {"createdAt", "2025-01-01T00:00:00Z"},
                         {"lastUpdatedAt", "2025-01-01T00:00:01Z"},
                         {"ttl", nullptr}},
      });
    } else if (request->method ==
               std::string(mcp::protocol::TasksResultMethod)) {
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result =
              Json{{"taskId", request->params.at("taskId")}, {"value", 100}},
      });
    }
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    ++receive_count;
    if (received.empty()) {
      return std::nullopt;
    }
    auto message = std::move(received.front());
    received.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    stopped = true;
    return mcp::core::Unit{};
  }

  std::vector<TxMessage> sent;
  std::deque<RxMessage> received;
  Json server_capabilities = full_server_capabilities_json();
  int receive_count = 0;
  bool stopped = false;
};

class RecordingServerContractTransport final
    : public mcp::transport::ServerTransport {
 public:
  std::string_view name() const noexcept override {
    return "recording-server-contract";
  }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    sent.push_back(std::move(message));
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    ++receive_count;
    if (received.empty()) {
      return std::nullopt;
    }
    auto message = std::move(received.front());
    received.pop_front();
    return message;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    stopped = true;
    return mcp::core::Unit{};
  }

  std::vector<TxMessage> sent;
  std::deque<RxMessage> received;
  int receive_count = 0;
  bool stopped = false;
};

class BlockingServerContractTransport final
    : public mcp::transport::ServerTransport {
 public:
  std::string_view name() const noexcept override {
    return "blocking-server-contract";
  }

  mcp::core::Result<mcp::core::Unit> send(TxMessage message) override {
    std::lock_guard lock(mutex_);
    sent.push_back(std::move(message));
    return mcp::core::Unit{};
  }

  mcp::core::Result<std::optional<RxMessage>> receive() override {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return stopped; });
    return std::nullopt;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    {
      std::lock_guard lock(mutex_);
      stopped = true;
    }
    cv_.notify_all();
    return mcp::core::Unit{};
  }

  std::vector<TxMessage> sent;
  bool stopped = false;

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
};

class CapabilityClientPeerTransport final : public mcp::server::Transport {
 public:
  explicit CapabilityClientPeerTransport(
      std::optional<mcp::protocol::ClientCapabilities> capabilities)
      : capabilities_(std::move(capabilities)) {}

  mcp::core::Result<mcp::core::Unit> start(
      mcp::server::RequestHandler,
      mcp::server::NotificationHandler = {}) override {
    return mcp::core::Unit{};
  }

  mcp::core::Result<mcp::protocol::JsonRpcResponse> send_request_to_session(
      std::string_view session_id,
      const mcp::protocol::JsonRpcRequest& request) override {
    session_ids.push_back(std::string(session_id));
    requests.push_back(request);

    Json result = Json::object();
    if (request.method == std::string(mcp::protocol::RootsListMethod)) {
      result = mcp::protocol::roots_list_result_to_json(
          mcp::protocol::RootsListResult{
              .roots = {mcp::protocol::Root{.uri = "file:///cap-root"}}});
    } else if (request.method ==
               std::string(mcp::protocol::SamplingCreateMessageMethod)) {
      mcp::protocol::CreateMessageResult message;
      message.role = "assistant";
      message.content = mcp::protocol::ContentBlock{
          .type = "text",
          .text = "sampled",
          .data = Json::object(),
      };
      result = mcp::protocol::create_message_result_to_json(message);
    } else if (request.method ==
               std::string(mcp::protocol::ElicitationCreateMethod)) {
      mcp::protocol::CreateElicitationResult elicitation;
      elicitation.action = mcp::protocol::ElicitationAction::Accept;
      elicitation.content = Json{{"ok", true}};
      result = mcp::protocol::create_elicitation_result_to_json(elicitation);
    } else if (request.method == std::string(mcp::protocol::TasksListMethod)) {
      result = Json{{"tasks", Json::array({Json{
                                  {"taskId", "cap-task"},
                                  {"status", "working"},
                                  {"createdAt", "2025-01-01T00:00:00Z"},
                                  {"lastUpdatedAt", "2025-01-01T00:00:01Z"},
                                  {"ttl", nullptr},
                              }})}};
    } else if (request.method == std::string(mcp::protocol::TasksGetMethod) ||
               request.method ==
                   std::string(mcp::protocol::TasksCancelMethod)) {
      result =
          Json{{"taskId", request.params.at("taskId")},
               {"status",
                request.method == std::string(mcp::protocol::TasksCancelMethod)
                    ? "cancelled"
                    : "completed"},
               {"createdAt", "2025-01-01T00:00:00Z"},
               {"lastUpdatedAt", "2025-01-01T00:00:01Z"},
               {"ttl", nullptr}};
    } else if (request.method ==
               std::string(mcp::protocol::TasksResultMethod)) {
      result = Json{{"taskId", request.params.at("taskId")}, {"value", 7}};
    }

    return mcp::protocol::JsonRpcResponse{.id = request.id, .result = result};
  }

  std::optional<mcp::protocol::ClientCapabilities>
  client_capabilities_for_session(std::string_view session_id) const override {
    last_capability_session = std::string(session_id);
    return capabilities_;
  }

  mcp::core::Result<mcp::core::Unit> send_notification(
      const mcp::protocol::JsonRpcNotification&) override {
    return mcp::core::Unit{};
  }

  void stop() noexcept override {}

  std::string_view name() const noexcept override {
    return "capability-client-peer";
  }

  std::optional<mcp::protocol::ClientCapabilities> capabilities_;
  std::vector<mcp::protocol::JsonRpcRequest> requests;
  std::vector<std::string> session_ids;
  mutable std::string last_capability_session;
};

void test_sdk_peer_and_service_surface() {
  mcp::CancellationSource cancellation_source;
  const auto cancellation_token = cancellation_source.token();
  require(!cancellation_token.cancelled(),
          "cancellation token should start active");
  cancellation_source.cancel();
  require(cancellation_token.cancelled(),
          "cancellation token should observe cancel");

  auto transport = std::make_unique<RecordingClientTransport>();
  auto* transport_ptr = transport.get();
  mcp::ClientPeer client_peer(std::move(transport));

  auto running_client = mcp::serve(std::move(client_peer));
  require(running_client.has_value(), "client service should start");
  require(running_client->running(), "client service should report running");

  require(running_client->peer().initialize().has_value(),
          "client initialize failed");

  const auto tools = running_client->peer().list_tools();
  require(tools.has_value() && tools->size() == 1, "client tools list failed");
  require(tools->front().name == "echo", "client tool name mismatch");

  const auto call =
      running_client->peer().call_tool("echo", Json{{"value", "client"}});
  require(call.has_value(), "client tool call failed");
  require(!call->content.empty(), "client tool call content missing");
  require(call->content.front().text == "{\"value\":\"client\"}",
          "client tool call result mismatch");

  const auto client_service_token = running_client->cancellation_token();
  require(!client_service_token.cancelled(),
          "client service token should start active");
  require(running_client->close().has_value(), "client service close failed");
  require(client_service_token.cancelled(),
          "client service token should cancel on close");
  require(running_client->wait().has_value(), "client service wait failed");
  require(transport_ptr->stopped, "client transport should stop");

  auto contract_transport =
      std::make_unique<RecordingClientContractTransport>();
  auto* contract_transport_ptr = contract_transport.get();
  mcp::ClientPeer contract_client_peer(std::move(contract_transport));
  require(contract_client_peer.initialize().has_value(),
          "client peer should accept role-generic transport");
  require(contract_client_peer.ping().has_value(),
          "client peer generic transport ping failed");
  require(contract_client_peer.notify_initialized().has_value(),
          "client peer generic transport initialized notification failed");
  require(contract_client_peer.set_level("debug").has_value(),
          "client peer generic transport set_level failed");
  require(!contract_client_peer.set_level("not-a-level").has_value(),
          "client peer generic transport should reject invalid logging level");
  require(contract_client_peer.subscribe("file:///native.txt").has_value(),
          "client peer generic transport subscribe failed");
  require(contract_client_peer.unsubscribe("file:///native.txt").has_value(),
          "client peer generic transport unsubscribe failed");
  const auto contract_tools = contract_client_peer.list_tools();
  require(contract_tools.has_value() && contract_tools->size() == 1 &&
              contract_tools->front().name == "native-echo",
          "client peer generic transport list_tools failed");
  const auto contract_all_tools = contract_client_peer.list_all_tools();
  require(contract_all_tools.has_value() && contract_all_tools->size() == 2 &&
              contract_all_tools->back().name == "native-next",
          "client peer generic transport list_all_tools failed");
  const auto contract_all_prompts = contract_client_peer.list_all_prompts();
  require(contract_all_prompts.has_value() &&
              contract_all_prompts->size() == 2 &&
              contract_all_prompts->back().name == "native-prompt-next",
          "client peer generic transport list_all_prompts failed");
  const auto contract_all_resources = contract_client_peer.list_all_resources();
  require(contract_all_resources.has_value() &&
              contract_all_resources->size() == 2 &&
              contract_all_resources->back().name == "native-resource-next",
          "client peer generic transport list_all_resources failed");
  const auto contract_all_templates =
      contract_client_peer.list_all_resource_templates();
  require(contract_all_templates.has_value() &&
              contract_all_templates->size() == 2 &&
              contract_all_templates->back().name == "native-template-next",
          "client peer generic transport list_all_resource_templates failed");
  const auto contract_all_tasks = contract_client_peer.list_all_tasks();
  require(contract_all_tasks.has_value() && contract_all_tasks->size() == 2 &&
              contract_all_tasks->back().task_id == "task-next",
          "client peer generic transport list_all_tasks failed");
  contract_client_peer.stop();
  require(contract_transport_ptr->stopped,
          "client peer generic transport should close");

  mcp::server::ServerBuilder builder;
  builder.name("sdk-test-server")
      .version("1.0.0")
      .add_tool(
          mcp::protocol::ToolDefinition{
              .name = "echo",
              .description = "Echo payload",
              .input_schema = Json{{"type", "object"}},
          },
          [](const mcp::server::ToolContext& context)
              -> mcp::core::Result<mcp::protocol::ToolResult> {
            mcp::protocol::ToolResult result;
            result.structured_content = context.arguments;
            result.content.push_back(mcp::protocol::ContentBlock{
                .type = "text",
                .text = context.arguments.dump(),
                .data = Json::object(),
            });
            return result;
          });

  auto server = builder.build();
  require(server.has_value(), "server builder failed");

  mcp::ServerPeer server_peer(std::move(*server));
  const auto server_tools = server_peer.list_tools();
  require(server_tools.size() == 1, "server tools list mismatch");
  require(server_tools.front().name == "echo", "server tool name mismatch");

  const auto server_call =
      server_peer.call_tool("echo", Json{{"value", "server"}});
  require(server_call.has_value(), "server tool call failed");
  require(!server_call->content.empty(), "server tool content missing");
  require(server_call->content.front().text == "{\"value\":\"server\"}",
          "server tool call result mismatch");

  auto server_service_transport =
      std::make_unique<BlockingServerContractTransport>();
  auto* server_service_transport_ptr = server_service_transport.get();
  require(server_peer.add_transport(std::move(server_service_transport))
              .has_value(),
          "server peer service transport should be accepted");
  require(server_peer.notify_tool_list_changed().has_value(),
          "server peer should notify role-generic transports");
  require(!server_service_transport_ptr->sent.empty(),
          "role-generic server transport should receive server notifications");

  auto running_server = mcp::serve(std::move(server_peer));
  require(running_server.has_value(), "server service should start");
  require(running_server->running(), "server service should report running");
  const auto server_service_token = running_server->cancellation_token();
  require(!server_service_token.cancelled(),
          "server service token should start active");
  require(running_server->close().has_value(), "server service close failed");
  require(server_service_token.cancelled(),
          "server service token should cancel on close");
  require(running_server->wait().has_value(), "server service wait failed");
  require(server_service_transport_ptr->stopped,
          "server service transport should close");

  mcp::ServerPeer contract_server_peer;
  auto server_contract_transport =
      std::make_unique<BlockingServerContractTransport>();
  auto* server_contract_transport_ptr = server_contract_transport.get();
  require(
      contract_server_peer.add_transport(std::move(server_contract_transport))
          .has_value(),
      "server peer should accept role-generic transport");
  auto running_contract_server = mcp::serve(std::move(contract_server_peer));
  require(running_contract_server.has_value(),
          "server peer generic transport service should start");
  require(running_contract_server->close().has_value(),
          "server peer generic transport service should close");
  require(running_contract_server->wait().has_value(),
          "server peer generic transport service should wait");
  require(server_contract_transport_ptr->stopped,
          "server peer generic transport should close");
}

void test_running_service_moved_from_is_inert() {
  auto client_transport = std::make_unique<RecordingClientTransport>();
  auto* client_transport_ptr = client_transport.get();
  auto running_client =
      mcp::serve(mcp::ClientPeer(std::move(client_transport)));
  require(running_client.has_value(), "client service should start");
  auto moved_client = std::move(*running_client);
  require(!running_client->running(),
          "moved-from client service should report stopped");
  require(running_client->stop().has_value(),
          "moved-from client service stop should be a no-op");
  require(running_client->wait().has_value(),
          "moved-from client service wait should be a no-op");
  require(!client_transport_ptr->stopped,
          "moved-from client service should not stop moved peer");
  require(moved_client.running(), "moved client service should remain running");
  require(moved_client.close().has_value(),
          "moved client service close should succeed");
  require(client_transport_ptr->stopped,
          "moved client service should stop transport");

  mcp::server::ServerOptions options;
  options.server_name = "moved-server";
  options.server_version = "1";
  auto server = std::make_unique<mcp::server::Server>(std::move(options));
  mcp::ServerPeer server_peer(std::move(server));
  auto server_transport = std::make_unique<BlockingServerContractTransport>();
  auto* server_transport_ptr = server_transport.get();
  require(server_peer.add_transport(std::move(server_transport)).has_value(),
          "moved server service transport should be accepted");
  auto running_server = mcp::serve(std::move(server_peer));
  require(running_server.has_value(), "server service should start");
  auto moved_server = std::move(*running_server);
  require(!running_server->running(),
          "moved-from server service should report stopped");
  require(running_server->stop().has_value(),
          "moved-from server service stop should be a no-op");
  require(running_server->wait().has_value(),
          "moved-from server service wait should be a no-op");
  require(moved_server.running(), "moved server service should remain running");
  require(moved_server.close().has_value(),
          "moved server service close should succeed");
  require(server_transport_ptr->stopped,
          "moved server service should stop transport");
}

void test_peer_serve_transport_observes_precancelled_token() {
  mcp::CancellationSource cancellation;
  cancellation.cancel();

  auto client_transport = std::make_unique<RecordingClientTransport>();
  mcp::ClientPeer client_peer(std::move(client_transport));
  RecordingClientContractTransport client_contract_transport;
  const auto client_served = client_peer.serve_transport(
      client_contract_transport, cancellation.token());
  require(client_served.has_value(),
          "pre-cancelled client serve loop should succeed");
  require(client_contract_transport.receive_count == 0,
          "pre-cancelled client serve loop must not receive");

  mcp::ServerPeer server_peer;
  RecordingServerContractTransport server_contract_transport;
  const auto server_served = server_peer.serve_transport(
      server_contract_transport, mcp::server::SessionContext{},
      cancellation.token());
  require(server_served.has_value(),
          "pre-cancelled server serve loop should succeed");
  require(server_contract_transport.receive_count == 0,
          "pre-cancelled server serve loop must not receive");
}

void test_server_peer_initialize_dispatches_on_peer_boundary() {
  mcp::server::ServerOptions options;
  options.server_name = "peer-init-server";
  options.server_version = "9.8.7";
  options.instructions = "peer-owned initialize";
  options.capabilities =
      mcp::protocol::server_capabilities().tools(true).logging().build();
  mcp::ServerPeer peer(std::move(options));

  RecordingServerContractTransport transport;
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params =
          Json{
              {"protocolVersion",
               std::string(mcp::protocol::McpProtocolVersion)},
              {"clientInfo",
               Json{{"name", "sdk-test-client"}, {"version", "1"}}},
              {"capabilities", Json::object()},
          },
      .id = mcp::protocol::RequestId{std::int64_t{1}},
  });

  const auto served = peer.serve_transport(transport);
  require(served.has_value(),
          "server peer initialize transport dispatch should succeed");
  require(transport.sent.size() == 1,
          "server peer initialize should send one response");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.front());
  require(response != nullptr, "server peer initialize response missing");
  require(response->result.has_value(),
          "server peer initialize should return result");
  require(response->result->at("protocolVersion") ==
              std::string(mcp::protocol::McpProtocolVersion),
          "server peer initialize protocol version mismatch");
  require(response->result->at("serverInfo").at("name") == "peer-init-server",
          "server peer initialize server name mismatch");
  require(response->result->at("serverInfo").at("version") == "9.8.7",
          "server peer initialize server version mismatch");
  require(response->result->at("instructions") == "peer-owned initialize",
          "server peer initialize instructions mismatch");
  require(response->result->at("capabilities").contains("tools"),
          "server peer initialize should advertise tools");
  require(response->result->at("capabilities").contains("logging"),
          "server peer initialize should advertise logging");

  RecordingServerContractTransport fallback_transport;
  fallback_transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params =
          Json{
              {"protocolVersion", "1900-01-01"},
              {"clientInfo",
               Json{{"name", "sdk-test-client"}, {"version", "1"}}},
              {"capabilities", Json::object()},
          },
      .id = mcp::protocol::RequestId{std::int64_t{2}},
  });

  const auto fallback_served = peer.serve_transport(fallback_transport);
  require(fallback_served.has_value(),
          "server peer fallback initialize dispatch should succeed");
  require(fallback_transport.sent.size() == 1,
          "server peer fallback initialize should send one response");
  const auto* fallback_response = std::get_if<mcp::protocol::JsonRpcResponse>(
      &fallback_transport.sent.front());
  require(fallback_response != nullptr,
          "server peer fallback initialize response missing");
  require(fallback_response->result.has_value(),
          "server peer fallback initialize should return result");
  require(fallback_response->result->at("protocolVersion") ==
              std::string(mcp::protocol::McpProtocolVersion),
          "server peer unknown protocol version should negotiate to latest");
}

void test_server_peer_tool_discovery_dispatches_on_peer_boundary() {
  auto server = mcp::server::App::builder()
                    .tool(
                        mcp::protocol::ToolDefinition{
                            .name = "echo",
                            .description = "Echo payload",
                            .input_schema = Json{{"type", "object"}},
                        },
                        [](const mcp::server::ToolContext&)
                            -> mcp::core::Result<mcp::protocol::ToolResult> {
                          mcp::protocol::ToolResult result;
                          result.content.push_back(mcp::protocol::ContentBlock{
                              .type = "text",
                              .text = "ok",
                              .data = Json::object(),
                          });
                          return result;
                        })
                    .build();
  require(server.has_value(), "tool discovery server build failed");
  mcp::ServerPeer peer(std::move(*server));

  RecordingServerContractTransport transport;
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ToolsListMethod),
      .params = Json::object(),
      .id = mcp::protocol::RequestId{std::int64_t{10}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ToolsGetMethod),
      .params = Json{{"name", "echo"}},
      .id = mcp::protocol::RequestId{std::int64_t{11}},
  });

  const auto served = peer.serve_transport(transport);
  require(served.has_value(),
          "server peer tool discovery dispatch should succeed");
  require(transport.sent.size() == 2,
          "server peer tool discovery should send two responses");

  const auto* list_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(0));
  require(list_response != nullptr, "tools/list response missing");
  require(list_response->result.has_value(), "tools/list result missing");
  require(list_response->result->at("tools").size() == 1,
          "tools/list tool count mismatch");
  require(list_response->result->at("tools").at(0).at("name") == "echo",
          "tools/list tool name mismatch");

  const auto* get_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(1));
  require(get_response != nullptr, "tools/get response missing");
  require(get_response->result.has_value(), "tools/get result missing");
  require(get_response->result->at("name") == "echo",
          "tools/get tool name mismatch");

  mcp::ServerPeer raw_peer;
  raw_peer.set_raw_request_handler(
      [](const mcp::protocol::JsonRpcRequest& request,
         const mcp::server::SessionContext&)
          -> std::optional<mcp::protocol::JsonRpcResponse> {
        if (request.method == mcp::protocol::ToolsListMethod) {
          return mcp::protocol::make_response(request.id,
                                              Json{{"rawOverride", true}});
        }
        return std::nullopt;
      });

  RecordingServerContractTransport raw_transport;
  raw_transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ToolsListMethod),
      .params = Json::object(),
      .id = mcp::protocol::RequestId{std::int64_t{12}},
  });
  const auto raw_served = raw_peer.serve_transport(raw_transport);
  require(raw_served.has_value(),
          "server peer raw override dispatch should succeed");
  require(raw_transport.sent.size() == 1,
          "server peer raw override should send one response");
  const auto* raw_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&raw_transport.sent.front());
  require(raw_response != nullptr, "raw override response missing");
  require(raw_response->result.has_value(), "raw override result missing");
  require(raw_response->result->at("rawOverride") == true,
          "raw request handler should run before peer tool discovery");
}

void test_server_peer_tool_call_dispatches_on_peer_boundary() {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false;
  auto server =
      mcp::server::App::builder()
          .tool(
              mcp::protocol::ToolDefinition{
                  .name = "wait",
                  .description = "Cancellation-aware tool",
                  .input_schema = Json{{"type", "object"}},
              },
              [&](const mcp::server::ToolContext& context)
                  -> mcp::core::Result<mcp::protocol::ToolResult> {
                {
                  std::lock_guard lock(mutex);
                  entered = true;
                }
                cv.notify_one();

                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(250);
                while (!context.cancelled() &&
                       std::chrono::steady_clock::now() < deadline) {
                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                mcp::protocol::ToolResult result;
                result.content.push_back(mcp::protocol::ContentBlock{
                    .type = "text",
                    .text = context.cancelled() ? "cancelled"
                                                : context.arguments.dump(),
                    .data = Json::object(),
                });
                return result;
              })
          .build();
  require(server.has_value(), "tool call server build failed");
  mcp::ServerPeer peer(std::move(*server));

  std::optional<mcp::core::Result<mcp::protocol::JsonRpcResponse>> response;
  std::thread worker([&] {
    response = peer.handle_request(mcp::protocol::JsonRpcRequest{
        .method = std::string(mcp::protocol::ToolsCallMethod),
        .params =
            Json{{"name", "wait"}, {"arguments", Json{{"value", "peer"}}}},
        .id = mcp::protocol::RequestId{std::int64_t{50}},
    });
  });

  {
    std::unique_lock lock(mutex);
    cv.wait_for(lock, std::chrono::milliseconds(250), [&] { return entered; });
  }
  if (!entered) {
    worker.join();
    require(false, "tool call handler should start");
  }

  mcp::protocol::CancelledNotificationParams cancelled;
  cancelled.request_id = mcp::protocol::RequestId{std::int64_t{50}};
  cancelled.reason = "test cancellation";
  const auto cancelled_notification =
      peer.handle_notification(mcp::protocol::JsonRpcNotification{
          .method = std::string(mcp::protocol::CancelledNotificationMethod),
          .params =
              mcp::protocol::cancelled_notification_params_to_json(cancelled),
      });
  worker.join();
  require(cancelled_notification.has_value(),
          "server peer cancellation notification should succeed");

  require(response.has_value(), "tool call response missing");
  require(response->has_value(), "tool call response should succeed");
  require((*response)->result.has_value(), "tool call result missing");
  require((*response)->result->at("content").at(0).at("text") == "cancelled",
          "tool call should receive peer cancellation token");
}

void test_server_peer_task_aware_tool_call_dispatches_on_peer_boundary() {
  auto server =
      std::make_unique<mcp::server::Server>(mcp::server::ServerOptions{});
  server->use_task_manager();
  std::atomic<int> calls{0};
  const auto added = server->tools().add(
      mcp::protocol::ToolDefinition{
          .name = "task-tool",
          .description = "Task-aware tool",
          .input_schema = Json{{"type", "object"}},
          .execution = mcp::protocol::ToolExecution{}.with_task_support(
              mcp::protocol::TaskSupport::Optional),
      },
      [&](const mcp::server::ToolContext& context) {
        calls.fetch_add(1, std::memory_order_relaxed);
        require(context.task.has_value(), "task-aware tool task missing");
        require(context.task->ttl == 15, "task-aware tool ttl mismatch");
        mcp::protocol::ToolResult result;
        result.content.push_back(mcp::protocol::ContentBlock{
            .type = "text",
            .text = context.session_id,
            .data = Json::object(),
        });
        return result;
      });
  require(added.has_value(), "task-aware tool add failed");
  mcp::ServerPeer peer(std::move(server));

  RecordingServerContractTransport transport;
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ToolsCallMethod),
      .params = Json{{"name", "task-tool"},
                     {"arguments", Json::object()},
                     {"task", Json{{"ttl", 15}}}},
      .id = mcp::protocol::RequestId{std::int64_t{51}},
  });
  const auto served = peer.serve_transport(
      transport, mcp::server::SessionContext{.session_id = "task-session"});
  require(served.has_value(),
          "server peer task-aware tool call dispatch should succeed");
  require(transport.sent.size() == 1,
          "task-aware tool call should send one response");
  const auto* response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.front());
  require(response != nullptr, "task-aware tool response missing");
  require(response->result.has_value(), "task-aware tool result missing");
  require(response->result->contains("task"),
          "task-aware tool call should create a task");
}

void test_server_peer_prompt_resource_dispatches_on_peer_boundary() {
  auto server =
      mcp::server::App::builder()
          .prompt(
              "session-summary",
              [](std::string text, const mcp::server::PromptContext& context) {
                return context.session_id + ":" + text;
              })
          .resource(
              "file:///tmp/session.txt",
              [](std::string uri, const mcp::server::ResourceContext& context) {
                return context.session_id + ":" + uri + ":" +
                       context.params.value("section", std::string("default"));
              })
          .resource_template(mcp::protocol::ResourceTemplate{
              .uri_template = "file:///tmp/{name}.txt",
              .name = "Tmp file",
              .description = "A tmp file",
              .mime_type = "text/plain",
          })
          .build();
  require(server.has_value(), "prompt/resource server build failed");
  mcp::ServerPeer peer(std::move(*server));

  RecordingServerContractTransport transport;
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PromptsListMethod),
      .params = Json::object(),
      .id = mcp::protocol::RequestId{std::int64_t{20}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PromptsGetMethod),
      .params = Json{{"name", "session-summary"},
                     {"arguments", Json{{"text", "hello"}}}},
      .id = mcp::protocol::RequestId{std::int64_t{21}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ResourcesListMethod),
      .params = Json::object(),
      .id = mcp::protocol::RequestId{std::int64_t{22}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ResourcesReadMethod),
      .params = Json{{"uri", "file:///tmp/session.txt"}, {"section", "intro"}},
      .id = mcp::protocol::RequestId{std::int64_t{23}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ResourcesTemplatesListMethod),
      .params = Json::object(),
      .id = mcp::protocol::RequestId{std::int64_t{24}},
  });

  mcp::server::SessionContext context;
  context.session_id = "peer-session";
  const auto served = peer.serve_transport(transport, context);
  require(served.has_value(),
          "server peer prompt/resource dispatch should succeed");
  require(transport.sent.size() == 5,
          "server peer prompt/resource dispatch response count mismatch");

  const auto* prompts_list =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(0));
  require(prompts_list != nullptr, "prompts/list response missing");
  require(
      prompts_list->result->at("prompts").at(0).at("name") == "session-summary",
      "prompts/list prompt name mismatch");

  const auto* prompt_get =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(1));
  require(prompt_get != nullptr, "prompts/get response missing");
  require(prompt_get->result->at("messages").at(0).at("content").at("text") ==
              "peer-session:hello",
          "prompts/get should preserve session context");

  const auto* resources_list =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(2));
  require(resources_list != nullptr, "resources/list response missing");
  require(resources_list->result->at("resources").at(0).at("uri") ==
              "file:///tmp/session.txt",
          "resources/list uri mismatch");

  const auto* resources_read =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(3));
  require(resources_read != nullptr, "resources/read response missing");
  require(resources_read->result->at("contents").at(0).at("text") ==
              "peer-session:file:///tmp/session.txt:intro",
          "resources/read should preserve session context and params");

  const auto* templates_list =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(4));
  require(templates_list != nullptr,
          "resources/templates/list response missing");
  require(
      templates_list->result->at("resourceTemplates").at(0).at("uriTemplate") ==
          "file:///tmp/{name}.txt",
      "resources/templates/list template mismatch");
}

void test_server_peer_resource_subscriptions_use_native_transport_identity() {
  mcp::server::ServerOptions options;
  options.capabilities.resources.enabled = true;
  options.capabilities.resources.subscribe = true;
  mcp::ServerPeer peer(std::move(options));

  auto transport = std::make_unique<RecordingServerContractTransport>();
  auto* transport_ptr = transport.get();
  require(peer.add_transport(std::move(transport)).has_value(),
          "server peer should accept subscription transport");

  transport_ptr->received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::ResourcesSubscribeMethod),
      .params = Json{{"uri", "file:///tmp/data.txt"}},
      .id = mcp::protocol::RequestId{std::int64_t{25}},
  });
  const auto subscribed = peer.serve_transport(*transport_ptr);
  require(subscribed.has_value(),
          "server peer resource subscribe dispatch should succeed");
  require(transport_ptr->sent.size() == 1,
          "resource subscribe should send one response");
  const auto* subscribe_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport_ptr->sent.front());
  require(subscribe_response != nullptr, "resource subscribe response missing");
  require(subscribe_response->result.has_value(),
          "resource subscribe result missing");

  transport_ptr->sent.clear();
  require(peer.notify_resource_updated("file:///tmp/other.txt").has_value(),
          "unmatched resource update should succeed");
  require(transport_ptr->sent.empty(),
          "unmatched resource update should not reach subscribed transport");

  require(peer.notify_resource_updated("file:///tmp/data.txt").has_value(),
          "matched resource update should succeed");
  require(transport_ptr->sent.size() == 1,
          "matched resource update should reach subscribed transport");
  const auto* update = std::get_if<mcp::protocol::JsonRpcNotification>(
      &transport_ptr->sent.front());
  require(update != nullptr, "resource update notification missing");
  require(update->method == mcp::protocol::ResourcesUpdatedNotificationMethod,
          "resource update notification method mismatch");
  require(update->params.at("uri") == "file:///tmp/data.txt",
          "resource update notification uri mismatch");
}

void test_server_peer_handler_requests_dispatch_on_peer_boundary() {
  mcp::ServerPeer peer;
  std::string logged_level;
  peer.set_completion_handler([](const Json& params) {
        return mcp::core::Result<Json>{Json{
            {"completion", params.value("prefix", std::string{}) + "llo"}}};
      })
      .set_sampling_handler([](const Json& params) {
        return mcp::core::Result<Json>{
            Json{{"sampled", params.at("messages").size()}}};
      })
      .set_logging_handler(
          [&](std::string_view level, std::string_view reason) {
            logged_level = std::string(level) + ":" + std::string(reason);
          });

  RecordingServerContractTransport transport;
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::CompletionCompleteMethod),
      .params = Json{{"prefix", "he"}},
      .id = mcp::protocol::RequestId{std::int64_t{30}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::SamplingCreateMessageMethod),
      .params = Json{{"messages", Json::array({Json{{"role", "user"}}})}},
      .id = mcp::protocol::RequestId{std::int64_t{31}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::LoggingSetLevelMethod),
      .params = Json{{"level", "debug"}},
      .id = mcp::protocol::RequestId{std::int64_t{32}},
  });

  const auto served = peer.serve_transport(transport);
  require(served.has_value(),
          "server peer handler request dispatch should succeed");
  require(transport.sent.size() == 3,
          "server peer handler request response count mismatch");

  const auto* completion =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(0));
  require(completion != nullptr, "completion response missing");
  require(completion->result->at("completion") == "hello",
          "completion result mismatch");

  const auto* sampling =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(1));
  require(sampling != nullptr, "sampling response missing");
  require(sampling->result->at("sampled") == 1, "sampling result mismatch");

  const auto* logging =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(2));
  require(logging != nullptr, "logging response missing");
  require(logging->result.has_value(), "logging result missing");
  require(logged_level == "debug:logging level changed",
          "logging handler should run at peer boundary");
}

void test_server_peer_task_handlers_dispatch_on_peer_boundary() {
  auto make_task = [](std::string id, mcp::protocol::TaskStatus status) {
    mcp::protocol::Task task;
    task.task_id = std::move(id);
    task.status = status;
    task.created_at = "2025-01-01T00:00:00Z";
    task.last_updated_at = "2025-01-01T00:00:01Z";
    return task;
  };

  mcp::ServerPeer peer;
  peer.set_task_list_handler([&](const mcp::protocol::TaskListParams&,
                                 const mcp::server::SessionContext& context) {
        mcp::protocol::TaskListResult result;
        result.tasks.push_back(make_task(context.session_id + "-listed",
                                         mcp::protocol::TaskStatus::Working));
        return mcp::core::Result<mcp::protocol::TaskListResult>{result};
      })
      .set_task_get_handler([&](const mcp::protocol::TaskGetParams& params,
                                const mcp::server::SessionContext& context) {
        return mcp::core::Result<mcp::protocol::Task>{
            make_task(context.session_id + "-" + params.task_id,
                      mcp::protocol::TaskStatus::Completed)};
      })
      .set_task_cancel_handler(
          [&](const mcp::protocol::TaskCancelParams& params,
              const mcp::server::SessionContext&) {
            return mcp::core::Result<mcp::protocol::Task>{make_task(
                params.task_id, mcp::protocol::TaskStatus::Cancelled)};
          })
      .set_task_result_handler(
          [&](const mcp::protocol::TaskResultParams& params,
              const mcp::server::SessionContext& context) {
            return mcp::core::Result<Json>{Json{{"taskId", params.task_id},
                                                {"session", context.session_id},
                                                {"value", 42}}};
          });

  RecordingServerContractTransport transport;
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::TasksListMethod),
      .params = Json::object(),
      .id = mcp::protocol::RequestId{std::int64_t{40}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::TasksGetMethod),
      .params = Json{{"taskId", "task-a"}},
      .id = mcp::protocol::RequestId{std::int64_t{41}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::TasksCancelMethod),
      .params = Json{{"taskId", "task-b"}},
      .id = mcp::protocol::RequestId{std::int64_t{42}},
  });
  transport.received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::TasksResultMethod),
      .params = Json{{"taskId", "task-c"}},
      .id = mcp::protocol::RequestId{std::int64_t{43}},
  });

  mcp::server::SessionContext context;
  context.session_id = "peer-task-session";
  const auto served = peer.serve_transport(transport, context);
  require(served.has_value(),
          "server peer task handler dispatch should succeed");
  require(transport.sent.size() == 4,
          "server peer task handler response count mismatch");

  const auto* list_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(0));
  require(list_response != nullptr, "tasks/list response missing");
  require(list_response->result->at("tasks").at(0).at("taskId") ==
              "peer-task-session-listed",
          "tasks/list result mismatch");

  const auto* get_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(1));
  require(get_response != nullptr, "tasks/get response missing");
  require(get_response->result->at("taskId") == "peer-task-session-task-a",
          "tasks/get should preserve session context");
  require(get_response->result->at("status") == "completed",
          "tasks/get status mismatch");

  const auto* cancel_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(2));
  require(cancel_response != nullptr, "tasks/cancel response missing");
  require(cancel_response->result->at("taskId") == "task-b",
          "tasks/cancel task id mismatch");
  require(cancel_response->result->at("status") == "cancelled",
          "tasks/cancel status mismatch");

  const auto* result_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport.sent.at(3));
  require(result_response != nullptr, "tasks/result response missing");
  require(result_response->result->at("taskId") == "task-c",
          "tasks/result task id mismatch");
  require(result_response->result->at("session") == "peer-task-session",
          "tasks/result should preserve session context");
}

void test_server_client_peer_gates_helpers_on_client_capabilities() {
  CapabilityClientPeerTransport unsupported_transport(
      mcp::protocol::ClientCapabilities{});
  mcp::server::ClientPeer unsupported_peer(&unsupported_transport, "session-a");

  require(!unsupported_peer.supports_roots(),
          "empty client capabilities should not support roots");
  require(!unsupported_peer.list_roots().has_value(),
          "roots helper should reject missing roots capability");
  require(!unsupported_peer.list_roots_async().await_response().has_value(),
          "async roots helper should reject missing roots capability");

  mcp::protocol::CreateMessageParams sampling_params;
  require(!unsupported_peer.create_message(sampling_params).has_value(),
          "sampling helper should reject missing sampling capability");
  require(!unsupported_peer.create_message_async(sampling_params)
               .await_response()
               .has_value(),
          "async sampling helper should reject missing sampling capability");

  mcp::protocol::CreateElicitationRequestParam form_elicitation;
  form_elicitation.message = "Need value";
  require(
      !unsupported_peer.create_elicitation(form_elicitation).has_value(),
      "form elicitation helper should reject missing elicitation capability");
  require(!unsupported_peer.create_elicitation_async(form_elicitation)
               .await_response()
               .has_value(),
          "async form elicitation helper should reject missing capability");

  mcp::protocol::CreateElicitationRequestParam url_elicitation;
  url_elicitation.message = "Open URL";
  url_elicitation.mode = mcp::protocol::ElicitationMode::Url;
  url_elicitation.elicitation_id = "elicit-1";
  url_elicitation.url = "https://example.test";
  const auto rejected_url =
      unsupported_peer.create_elicitation(url_elicitation);
  require(!rejected_url.has_value(),
          "url elicitation helper should reject missing url capability");
  require(
      rejected_url.error().code ==
          static_cast<int>(mcp::protocol::ErrorCode::UrlElicitationRequired),
      "url elicitation rejection should use protocol url-required error");

  require(!unsupported_peer.list_tasks().has_value(),
          "task list helper should reject missing task capability");
  require(!unsupported_peer.get_task("missing").has_value(),
          "task get helper should reject missing task capability");
  require(!unsupported_peer.cancel_task("missing").has_value(),
          "task cancel helper should reject missing task cancel capability");
  require(!unsupported_peer.task_result("missing").has_value(),
          "task result helper should reject missing task capability");
  require(unsupported_transport.requests.empty(),
          "unsupported capability helpers should not send requests");

  auto capabilities = mcp::protocol::client_capabilities()
                          .roots(false)
                          .sampling()
                          .elicitation_form()
                          .elicitation_url()
                          .task_list()
                          .task_cancel()
                          .build();
  CapabilityClientPeerTransport supported_transport(capabilities);
  mcp::server::ClientPeer supported_peer(&supported_transport, "session-b");

  const auto roots = supported_peer.list_roots();
  require(roots.has_value() && roots->roots.size() == 1 &&
              roots->roots.front().uri == "file:///cap-root",
          "roots helper should send when roots capability is present");

  const auto sampled = supported_peer.create_message(sampling_params);
  require(sampled.has_value() && sampled->content.text == "sampled",
          "sampling helper should send when sampling capability is present");

  const auto elicited = supported_peer.create_elicitation(form_elicitation);
  require(elicited.has_value() &&
              elicited->action == mcp::protocol::ElicitationAction::Accept,
          "elicitation helper should send when form capability is present");

  const auto url_result = supported_peer.create_elicitation(url_elicitation);
  require(url_result.has_value(),
          "url elicitation helper should send when url capability is present");

  const auto tasks = supported_peer.list_tasks();
  require(tasks.has_value() && tasks->size() == 1 &&
              tasks->front().task_id == "cap-task",
          "task list helper should send when task list capability is present");
  require(supported_peer.get_task("task-1").has_value(),
          "task get helper should send when tasks are advertised");
  const auto cancelled = supported_peer.cancel_task("task-2");
  require(cancelled.has_value() &&
              cancelled->status == mcp::protocol::TaskStatus::Cancelled,
          "task cancel helper should send when task cancel is advertised");
  const auto task_result = supported_peer.task_result("task-3");
  require(task_result.has_value() && task_result->at("value") == 7,
          "task result helper should send when tasks are advertised");
  require(!supported_transport.requests.empty(),
          "supported capability helpers should send requests");
  require(supported_transport.session_ids.front() == "session-b",
          "client peer helper should route through the requested session");
}

void test_client_helpers_gate_on_server_capabilities() {
  auto make_task_call = [] {
    mcp::protocol::ToolCall call;
    call.name = "slow-tool";
    call.task = mcp::protocol::TaskRequestParameters{};
    return call;
  };

  auto concrete_transport = std::make_unique<RecordingClientTransport>();
  auto* concrete_transport_ptr = concrete_transport.get();
  mcp::client::Client concrete_client(std::move(concrete_transport));
  require(concrete_client.initialize().has_value(),
          "concrete client initialize should succeed");
  const auto concrete_request_count = concrete_transport_ptr->requests.size();

  require(!concrete_client.complete(Json::object()).has_value(),
          "concrete client should gate completion on server capabilities");
  require(!concrete_client.complete_async(Json::object())
               .await_response()
               .has_value(),
          "concrete client should gate async completion on capabilities");
  require(!concrete_client.set_level("debug").has_value(),
          "concrete client should gate logging on server capabilities");
  require(!concrete_client.subscribe("file:///x").has_value(),
          "concrete client should gate resource subscribe capability");
  require(!concrete_client.unsubscribe("file:///x").has_value(),
          "concrete client should gate resource unsubscribe capability");
  require(!concrete_client.list_tasks().has_value(),
          "concrete client should gate task listing capability");
  require(!concrete_client.list_tasks_async().await_response().has_value(),
          "concrete client should gate async task listing capability");
  require(!concrete_client.get_task("task-a").has_value(),
          "concrete client should gate task get when tasks are absent");
  require(!concrete_client.cancel_task("task-a").has_value(),
          "concrete client should gate task cancel capability");
  require(!concrete_client.task_result("task-a").has_value(),
          "concrete client should gate task result when tasks are absent");
  require(!concrete_client.call_tool_task(make_task_call()).has_value(),
          "concrete client should gate task-aware tool calls");
  require(concrete_transport_ptr->requests.size() == concrete_request_count,
          "gated concrete client helpers should not send requests");

  auto concrete_supported_transport =
      std::make_unique<RecordingClientTransport>();
  concrete_supported_transport->server_capabilities =
      full_server_capabilities_json();
  auto* concrete_supported_ptr = concrete_supported_transport.get();
  mcp::client::Client concrete_supported(
      std::move(concrete_supported_transport));
  require(concrete_supported.initialize().has_value(),
          "supported concrete client initialize should succeed");
  require(concrete_supported.complete(Json::object()).has_value(),
          "supported concrete client completion should send");
  require(concrete_supported.set_level("debug").has_value(),
          "supported concrete client logging should send");
  require(concrete_supported.subscribe("file:///x").has_value(),
          "supported concrete client subscribe should send");
  require(concrete_supported.list_tasks().has_value(),
          "supported concrete client task list should send");
  require(concrete_supported.get_task("task-a").has_value(),
          "supported concrete client task get should send");
  require(concrete_supported.cancel_task("task-a").has_value(),
          "supported concrete client task cancel should send");
  require(concrete_supported.task_result("task-a").has_value(),
          "supported concrete client task result should send");
  require(concrete_supported.call_tool_task(make_task_call()).has_value(),
          "supported concrete client task-aware tool call should send");
  require(concrete_supported_ptr->requests.size() > 1,
          "supported concrete client helpers should send after initialize");

  auto native_transport = std::make_unique<RecordingClientContractTransport>();
  native_transport->server_capabilities = Json::object();
  auto* native_transport_ptr = native_transport.get();
  mcp::ClientPeer native_peer(std::move(native_transport));
  require(native_peer.initialize().has_value(),
          "native client peer initialize should succeed");
  const auto native_send_count = native_transport_ptr->sent.size();

  require(!native_peer.complete(Json::object()).has_value(),
          "native client peer should gate completion on server capabilities");
  require(
      !native_peer.complete_async(Json::object()).await_response().has_value(),
      "native client peer should gate async completion on capabilities");
  require(!native_peer.set_level("debug").has_value(),
          "native client peer should gate logging on server capabilities");
  require(!native_peer.subscribe("file:///x").has_value(),
          "native client peer should gate resource subscribe capability");
  require(!native_peer.unsubscribe("file:///x").has_value(),
          "native client peer should gate resource unsubscribe capability");
  require(!native_peer.list_tasks().has_value(),
          "native client peer should gate task listing capability");
  require(!native_peer.list_tasks_async().await_response().has_value(),
          "native client peer should gate async task listing capability");
  require(!native_peer.get_task("task-a").has_value(),
          "native client peer should gate task get when tasks are absent");
  require(!native_peer.cancel_task("task-a").has_value(),
          "native client peer should gate task cancel capability");
  require(!native_peer.task_result("task-a").has_value(),
          "native client peer should gate task result when tasks are absent");
  require(!native_peer.call_tool_task(make_task_call()).has_value(),
          "native client peer should gate task-aware tool calls");
  require(native_transport_ptr->sent.size() == native_send_count,
          "gated native client peer helpers should not send requests");

  auto native_supported_transport =
      std::make_unique<RecordingClientContractTransport>();
  auto* native_supported_ptr = native_supported_transport.get();
  mcp::ClientPeer native_supported(std::move(native_supported_transport));
  require(native_supported.initialize().has_value(),
          "supported native client peer initialize should succeed");
  require(native_supported.complete(Json::object()).has_value(),
          "supported native client peer completion should send");
  require(native_supported.set_level("debug").has_value(),
          "supported native client peer logging should send");
  require(native_supported.subscribe("file:///x").has_value(),
          "supported native client peer subscribe should send");
  require(native_supported.list_tasks().has_value(),
          "supported native client peer task list should send");
  require(native_supported.get_task("task-a").has_value(),
          "supported native client peer task get should send");
  require(native_supported.cancel_task("task-a").has_value(),
          "supported native client peer task cancel should send");
  require(native_supported.task_result("task-a").has_value(),
          "supported native client peer task result should send");
  require(native_supported.call_tool_task(make_task_call()).has_value(),
          "supported native client peer task-aware tool call should send");
  require(native_supported_ptr->sent.size() > 1,
          "supported native client peer helpers should send after initialize");
}

void test_client_peer_native_raw_request_dispatches_interleaved_messages() {
  auto transport = std::make_unique<RecordingClientContractTransport>();
  auto* transport_ptr = transport.get();
  transport_ptr->received.push_back(mcp::protocol::JsonRpcNotification{
      .method = std::string(mcp::protocol::LoggingMessageNotificationMethod),
      .params = Json{{"level", "info"}, {"data", "ready"}},
  });
  transport_ptr->received.push_back(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::RootsListMethod),
      .params = Json::object(),
      .id = std::string("server-roots"),
  });
  transport_ptr->received.push_back(mcp::protocol::JsonRpcResponse{
      .id = mcp::protocol::RequestId{std::int64_t{77}},
      .result = Json{{"ok", true}},
  });

  mcp::ClientPeer peer(std::move(transport));
  peer.set_roots({mcp::protocol::Root{.uri = "file:///workspace"}});

  bool logging_seen = false;
  peer.on_logging_message(
      [&](std::string_view level, std::string_view message) {
        logging_seen = level == "info" && message == "ready";
      });

  const auto response = peer.raw_request(mcp::protocol::JsonRpcRequest{
      .method = "custom/raw",
      .params = Json::object(),
      .id = std::int64_t{77},
  });
  require(response.has_value(),
          "native client peer raw_request should succeed");
  require(response->at("ok") == true,
          "native client peer raw_request response mismatch");
  require(logging_seen,
          "native client peer should dispatch interleaved notification");
  require(transport_ptr->sent.size() == 2,
          "native client peer should send request and interleaved response");
  require(std::holds_alternative<mcp::protocol::JsonRpcRequest>(
              transport_ptr->sent.front()),
          "native client peer first send should be outbound request");
  const auto* roots_response =
      std::get_if<mcp::protocol::JsonRpcResponse>(&transport_ptr->sent.back());
  require(roots_response != nullptr,
          "native client peer should answer interleaved request");
  require(roots_response->result.has_value(),
          "native client peer interleaved response should contain result");

  auto tools = peer.list_tools_async().await_response();
  require(tools.has_value(), "native client peer list_tools_async failed");
  require(tools->size() == 1 && tools->front().name == "native-echo",
          "native client peer list_tools_async result mismatch");
}

void test_client_peer_native_raw_request_reports_transport_failures() {
  auto unexpected_transport =
      std::make_unique<RecordingClientContractTransport>();
  unexpected_transport->received.push_back(mcp::protocol::JsonRpcResponse{
      .id = mcp::protocol::RequestId{std::int64_t{999}},
      .result = Json::object(),
  });
  mcp::ClientPeer unexpected_peer(std::move(unexpected_transport));
  const auto unexpected = unexpected_peer.raw_request(
      mcp::protocol::JsonRpcRequest{.method = "custom/unexpected",
                                    .params = Json::object(),
                                    .id = std::int64_t{77}});
  require(!unexpected.has_value(),
          "native client peer should reject unexpected response ids");
  require(unexpected.error().message ==
              "client peer transport received unexpected response id",
          "native client peer unexpected response message mismatch");
  require(unexpected.error().category == "transport",
          "native client peer unexpected response category mismatch");

  auto closed_transport = std::make_unique<RecordingClientContractTransport>();
  mcp::ClientPeer closed_peer(std::move(closed_transport));
  const auto closed = closed_peer.raw_request(mcp::protocol::JsonRpcRequest{
      .method = "custom/closed",
      .params = Json::object(),
      .id = std::int64_t{78},
  });
  require(!closed.has_value(),
          "native client peer should fail when transport closes");
  require(
      closed.error().message == "client peer transport closed before response",
      "native client peer closed transport message mismatch");
  require(closed.error().category == "transport",
          "native client peer closed transport category mismatch");
}

void test_request_handle_rejects_invalid_state() {
  mcp::RequestHandle<Json> default_handle;
  const auto default_result = default_handle.await_response();
  require(!default_result.has_value(),
          "default request handle should not have a response");
  require(
      default_result.error().message == "request handle has no response state",
      "default request handle should report invalid state");

  std::function<mcp::core::Result<Json>()> empty_task;
  auto empty_task_handle = mcp::RequestHandle<Json>::spawn(
      std::int64_t{7}, std::nullopt, std::nullopt, {}, std::move(empty_task));
  const auto empty_task_result = empty_task_handle.await_response();
  require(!empty_task_result.has_value(),
          "empty request task should not produce a value");
  require(empty_task_result.error().message == "request task is not configured",
          "empty request task should report configuration error");
}

void test_public_dispatch_boundaries_translate_handler_exceptions() {
  auto client_transport = std::make_unique<RecordingClientTransport>();
  mcp::client::Client client(std::move(client_transport));
  client.on_custom_request(
      [](const mcp::protocol::JsonRpcRequest&) -> mcp::core::Result<Json> {
        throw std::runtime_error("client boom");
      });
  const auto client_response = client.handle_request(
      mcp::protocol::JsonRpcRequest{.method = "custom/client",
                                    .params = Json::object(),
                                    .id = std::int64_t{501}});
  require(client_response.has_value(),
          "client thrown handler should become an error response");
  require(client_response->error.has_value(),
          "client thrown handler response should contain error");
  require(client_response->error->message == "handler failed",
          "client thrown handler error message mismatch");
  require(client_response->error->data.has_value() &&
              *client_response->error->data == "client boom",
          "client thrown handler error detail mismatch");

  mcp::server::Server server(mcp::server::ServerOptions{});
  server.set_custom_request_handler(
      [](const mcp::protocol::JsonRpcRequest&,
         const mcp::server::SessionContext&)
          -> std::optional<mcp::protocol::JsonRpcResponse> {
        throw std::runtime_error("server boom");
      });
  const auto server_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{.method = "custom/server",
                                    .params = Json::object(),
                                    .id = std::int64_t{502}},
      mcp::server::SessionContext{});
  require(server_response.has_value(),
          "server thrown handler should become an error response");
  require(server_response->error.has_value(),
          "server thrown handler response should contain error");
  require(server_response->error->message == "handler failed",
          "server thrown handler error message mismatch");
  require(server_response->error->data.has_value() &&
              *server_response->error->data == "server boom",
          "server thrown handler error detail mismatch");
}

void test_request_handle_latches_terminal_timeout() {
  std::atomic<int> cancel_count{0};
  auto handle = mcp::RequestHandle<Json>::spawn(
      std::int64_t{42}, std::chrono::milliseconds(1), std::nullopt,
      [&](std::string reason) -> mcp::core::Result<mcp::core::Unit> {
        require(reason == "request timeout", "timeout cancel reason mismatch");
        cancel_count.fetch_add(1);
        return mcp::core::Unit{};
      },
      []() -> mcp::core::Result<Json> {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return Json{{"late", true}};
      });

  const auto first = handle.await_response();
  require(!first.has_value(), "first await should time out");
  require(first.error().message == "request timed out",
          "first timeout message mismatch");

  std::this_thread::sleep_for(std::chrono::milliseconds(75));
  const auto second = handle.await_response();
  require(!second.has_value(), "late response should remain timed out");
  require(second.error().message == "request timed out",
          "second timeout message mismatch");
  require(cancel_count.load() == 1,
          "timeout cancellation notification should be sent once");
}

void test_request_handle_latches_terminal_cancellation() {
  mcp::CancellationSource cancellation;
  std::atomic<int> cancel_count{0};
  auto handle = mcp::RequestHandle<Json>::spawn(
      std::int64_t{43}, std::nullopt, cancellation.token(),
      [&](std::string reason) -> mcp::core::Result<mcp::core::Unit> {
        require(reason == "request cancelled", "token cancel reason mismatch");
        cancel_count.fetch_add(1);
        return mcp::core::Unit{};
      },
      []() -> mcp::core::Result<Json> {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return Json{{"late", true}};
      });

  cancellation.cancel();
  const auto first = handle.await_response();
  require(!first.has_value(), "first await should cancel");
  require(first.error().message == "request cancelled",
          "first cancellation message mismatch");

  std::this_thread::sleep_for(std::chrono::milliseconds(75));
  const auto second = handle.await_response();
  require(!second.has_value(), "late response should remain cancelled");
  require(second.error().message == "request cancelled",
          "second cancellation message mismatch");
  require(cancel_count.load() == 1,
          "token cancellation notification should be sent once");
}

void test_request_handle_cancellation_race_stress() {
  constexpr int kRequestCount = 24;
  std::atomic<int> ready_waiters{0};
  std::atomic<bool> release_tasks{false};
  std::atomic<int> cancel_count{0};
  std::atomic<int> cancelled_results{0};
  std::atomic<int> unexpected_results{0};
  mcp::CancellationSource cancellation;

  std::vector<mcp::RequestHandle<Json>> handles;
  handles.reserve(kRequestCount);
  for (int i = 0; i < kRequestCount; ++i) {
    handles.push_back(mcp::RequestHandle<Json>::spawn(
        std::int64_t{1000 + i}, std::chrono::milliseconds(1000),
        cancellation.token(),
        [&](std::string reason) -> mcp::core::Result<mcp::core::Unit> {
          if (reason != "request cancelled") {
            unexpected_results.fetch_add(1);
          }
          cancel_count.fetch_add(1);
          return mcp::core::Unit{};
        },
        [&]() -> mcp::core::Result<Json> {
          while (!release_tasks.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          return Json{{"late", true}};
        }));
  }

  std::vector<std::thread> waiters;
  waiters.reserve(handles.size());
  for (const auto& handle : handles) {
    waiters.emplace_back([&, handle]() {
      ready_waiters.fetch_add(1, std::memory_order_acq_rel);
      const auto result = handle.await_response();
      if (!result.has_value() &&
          result.error().message == "request cancelled") {
        cancelled_results.fetch_add(1);
      } else {
        unexpected_results.fetch_add(1);
      }
    });
  }

  while (ready_waiters.load(std::memory_order_acquire) != kRequestCount) {
    std::this_thread::yield();
  }
  cancellation.cancel();
  for (auto& waiter : waiters) {
    waiter.join();
  }
  release_tasks.store(true, std::memory_order_release);

  require(cancelled_results.load() == kRequestCount,
          "all cancellation race waiters should observe cancellation");
  require(cancel_count.load() == kRequestCount,
          "each raced cancellation request should notify exactly once");
  require(unexpected_results.load() == 0,
          "cancellation race should not produce unexpected results");

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  for (const auto& handle : handles) {
    const auto repeated = handle.await_response();
    require(!repeated.has_value(),
            "late cancellation result should stay error");
    require(repeated.error().message == "request cancelled",
            "late cancellation result should stay cancelled");
  }
  require(cancel_count.load() == kRequestCount,
          "late cancellation awaits should not send another notification");
}

void test_request_handle_timeout_race_stress() {
  constexpr int kRequestCount = 24;
  std::atomic<bool> release_tasks{false};
  std::atomic<int> cancel_count{0};
  std::atomic<int> timeout_results{0};
  std::atomic<int> unexpected_results{0};

  std::vector<mcp::RequestHandle<Json>> handles;
  handles.reserve(kRequestCount);
  for (int i = 0; i < kRequestCount; ++i) {
    handles.push_back(mcp::RequestHandle<Json>::spawn(
        std::int64_t{2000 + i}, std::chrono::milliseconds(2), std::nullopt,
        [&](std::string reason) -> mcp::core::Result<mcp::core::Unit> {
          if (reason != "request timeout") {
            unexpected_results.fetch_add(1);
          }
          cancel_count.fetch_add(1);
          return mcp::core::Unit{};
        },
        [&]() -> mcp::core::Result<Json> {
          while (!release_tasks.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          return Json{{"late", true}};
        }));
  }

  std::vector<std::thread> waiters;
  waiters.reserve(handles.size());
  for (const auto& handle : handles) {
    waiters.emplace_back([&, handle]() {
      const auto result = handle.await_response();
      if (!result.has_value() &&
          result.error().message == "request timed out") {
        timeout_results.fetch_add(1);
      } else {
        unexpected_results.fetch_add(1);
      }
    });
  }

  for (auto& waiter : waiters) {
    waiter.join();
  }
  release_tasks.store(true, std::memory_order_release);

  require(timeout_results.load() == kRequestCount,
          "all timeout race waiters should observe timeout");
  require(cancel_count.load() == kRequestCount,
          "each raced timeout request should notify exactly once");
  require(unexpected_results.load() == 0,
          "timeout race should not produce unexpected results");

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  for (const auto& handle : handles) {
    const auto repeated = handle.await_response();
    require(!repeated.has_value(), "late timeout result should stay error");
    require(repeated.error().message == "request timed out",
            "late timeout result should stay timed out");
  }
  require(cancel_count.load() == kRequestCount,
          "late timeout awaits should not send another notification");
}

void test_app_builder_rejects_empty_std_function_handlers() {
  std::function<std::string()> empty_resource;
  bool threw = false;
  try {
    (void)mcp::server::App::builder().resource("file:///empty", empty_resource);
  } catch (const std::invalid_argument& ex) {
    threw = std::string_view(ex.what()) == "resource handler must not be empty";
  }
  require(threw, "empty resource std::function should be rejected");

  std::function<mcp::protocol::Json(const mcp::protocol::Json&)>
      empty_completion;
  threw = false;
  try {
    (void)mcp::server::App::builder().completion(empty_completion);
  } catch (const std::invalid_argument& ex) {
    threw =
        std::string_view(ex.what()) == "completion handler must not be empty";
  }
  require(threw, "empty completion std::function should be rejected");

  mcp::core::Result<mcp::protocol::Json> (*empty_sampling)(
      const mcp::protocol::Json&) = nullptr;
  threw = false;
  try {
    (void)mcp::server::App::builder().sampling(empty_sampling);
  } catch (const std::invalid_argument& ex) {
    threw = std::string_view(ex.what()) == "sampling handler must not be empty";
  }
  require(threw, "empty sampling function pointer should be rejected");
}

void test_executor_rejects_empty_task() {
  mcp::core::BoundedExecutor executor(1, 4);
  std::function<void()> empty_task;
  const auto queued = executor.enqueue(std::move(empty_task));
  require(!queued.has_value(), "empty executor task should be rejected");
  require(queued.error().message == "executor task must be callable",
          "empty executor task error mismatch");
}

void test_public_error_helpers_assign_stable_categories() {
  auto require_error_object = [](const mcp::core::Error& error,
                                 mcp::protocol::ErrorCode code,
                                 std::string_view message,
                                 std::string_view detail) {
    const auto object = mcp::errors::to_json_rpc_error(error);
    require(object.code == static_cast<int>(code),
            "json-rpc error code mismatch");
    require(object.message == message, "json-rpc error message mismatch");
    if (detail.empty()) {
      require(!object.data.has_value(), "json-rpc error data mismatch");
    } else {
      require(object.data.has_value(), "json-rpc error data missing");
      require(object.data->is_string(), "json-rpc error data type mismatch");
      require(object.data->get<std::string>() == detail,
              "json-rpc error data value mismatch");
    }
  };

  const auto parse = mcp::errors::parse("bad json");
  require(parse.category == "protocol", "parse category mismatch");
  require(parse.detail == "bad json", "parse detail mismatch");
  require_error_object(parse, mcp::protocol::ErrorCode::ParseError,
                       "parse error", "bad json");

  const auto invalid_request = mcp::errors::invalid_request("missing method");
  require(invalid_request.category == "protocol",
          "invalid request category mismatch");
  require_error_object(invalid_request,
                       mcp::protocol::ErrorCode::InvalidRequest,
                       "invalid request", "missing method");

  const auto invalid_params = mcp::errors::invalid_params("bad params");
  require(invalid_params.category == "protocol",
          "invalid params category mismatch");
  require_error_object(invalid_params, mcp::protocol::ErrorCode::InvalidParams,
                       "invalid params", "bad params");

  const auto method = mcp::errors::method_not_found("unknown/method");
  require(method.category == "protocol", "method-not-found category mismatch");
  require_error_object(method, mcp::protocol::ErrorCode::MethodNotFound,
                       "method not found", "unknown/method");

  const auto tool = mcp::errors::tool_not_found("missing_tool");
  require(tool.category == "tool", "tool-not-found category mismatch");
  require_error_object(tool, mcp::protocol::ErrorCode::ToolNotFound,
                       "tool not found", "missing_tool");

  const auto resource = mcp::errors::resource_not_found("file:///missing.txt");
  require(resource.category == "resource",
          "resource-not-found category mismatch");
  require_error_object(resource, mcp::protocol::ErrorCode::ResourceNotFound,
                       "resource not found", "file:///missing.txt");

  const auto permission = mcp::errors::permission_denied("filesystem");
  require(permission.category == "permission",
          "permission-denied category mismatch");
  require_error_object(permission, mcp::protocol::ErrorCode::PermissionDenied,
                       "permission denied", "filesystem");

  const auto limited = mcp::errors::rate_limited("tool quota");
  require(limited.category == "rate_limit", "rate-limited category mismatch");
  require_error_object(limited, mcp::protocol::ErrorCode::RateLimited,
                       "rate limited", "tool quota");

  const auto elicitation =
      mcp::errors::url_elicitation_required("https://example.test/auth");
  require(elicitation.category == "elicitation",
          "url elicitation category mismatch");
  require_error_object(elicitation,
                       mcp::protocol::ErrorCode::UrlElicitationRequired,
                       "url elicitation required", "https://example.test/auth");

  const auto handler = mcp::errors::handler_failed("boom");
  require(handler.category == "handler", "handler category mismatch");
  require(handler.message == "handler failed", "handler message mismatch");
  require_error_object(handler, mcp::protocol::ErrorCode::InternalError,
                       "handler failed", "boom");

  const auto closed = mcp::errors::transport_closed("stdio");
  require(closed.category == "transport", "transport category mismatch");
  require(closed.message == "transport closed",
          "transport closed message mismatch");
  require_error_object(closed, mcp::protocol::ErrorCode::InvalidRequest,
                       "transport closed", "stdio");

  const auto timed_out =
      mcp::errors::request_timed_out(std::chrono::milliseconds(25));
  require(timed_out.category == "timeout", "timeout category mismatch");
  require(timed_out.detail == "25ms", "timeout detail mismatch");
  require_error_object(timed_out, mcp::protocol::ErrorCode::InternalError,
                       "request timed out", "25ms");

  const auto cancelled = mcp::errors::request_cancelled();
  require(cancelled.category == "cancellation",
          "cancellation category mismatch");
  require_error_object(cancelled, mcp::protocol::ErrorCode::InternalError,
                       "request cancelled", "");
}

}  // namespace

int main() {
  try {
    test_sdk_peer_and_service_surface();
    test_running_service_moved_from_is_inert();
    test_peer_serve_transport_observes_precancelled_token();
    test_server_peer_initialize_dispatches_on_peer_boundary();
    test_server_peer_tool_discovery_dispatches_on_peer_boundary();
    test_server_peer_tool_call_dispatches_on_peer_boundary();
    test_server_peer_task_aware_tool_call_dispatches_on_peer_boundary();
    test_server_peer_prompt_resource_dispatches_on_peer_boundary();
    test_server_peer_resource_subscriptions_use_native_transport_identity();
    test_server_peer_handler_requests_dispatch_on_peer_boundary();
    test_server_peer_task_handlers_dispatch_on_peer_boundary();
    test_server_client_peer_gates_helpers_on_client_capabilities();
    test_client_helpers_gate_on_server_capabilities();
    test_client_peer_native_raw_request_dispatches_interleaved_messages();
    test_client_peer_native_raw_request_reports_transport_failures();
    test_request_handle_rejects_invalid_state();
    test_public_dispatch_boundaries_translate_handler_exceptions();
    test_request_handle_latches_terminal_timeout();
    test_request_handle_latches_terminal_cancellation();
    test_request_handle_cancellation_race_stress();
    test_request_handle_timeout_race_stress();
    test_app_builder_rejects_empty_std_function_handlers();
    test_executor_rejects_empty_task();
    test_public_error_helpers_assign_stable_categories();
    std::cout << "sdk peer/service test passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "sdk peer/service test failed: " << ex.what() << '\n';
    return 1;
  }
}
