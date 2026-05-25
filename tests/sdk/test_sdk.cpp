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
                  {"capabilities", Json::object()},
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
                  {"capabilities", Json::object()},
                  {"serverInfo",
                   Json{{"name", "sdk-test-server"}, {"version", "1"}}},
              },
      });
    } else if (request->method == "ping") {
      received.push_back(mcp::protocol::JsonRpcResponse{
          .id = request->id,
          .result = Json::object(),
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
    return std::nullopt;
  }

  mcp::core::Result<mcp::core::Unit> close() override {
    stopped = true;
    return mcp::core::Unit{};
  }

  std::vector<TxMessage> sent;
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
  contract_client_peer.client().stop();
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
  peer.client().on_logging_message(
      [&](std::string_view level, std::string_view message) {
        logging_seen = level == "info" && message == "ready";
      });

  const auto response = peer.raw_request(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
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

  mcp::server::Server server;
  server.set_custom_request_handler(
      [](const mcp::protocol::JsonRpcRequest&,
         const mcp::server::SessionContext&)
          -> std::optional<mcp::protocol::JsonRpcResponse> {
        throw std::runtime_error("server boom");
      });
  const auto server_response = server.handle_request(
      mcp::protocol::JsonRpcRequest{.method = "custom/server",
                                    .params = Json::object(),
                                    .id = std::int64_t{502}});
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
  const auto parse = mcp::errors::parse("bad json");
  require(parse.category == "protocol", "parse category mismatch");
  require(parse.detail == "bad json", "parse detail mismatch");

  const auto handler = mcp::errors::handler_failed("boom");
  require(handler.category == "handler", "handler category mismatch");
  require(handler.message == "handler failed", "handler message mismatch");

  const auto closed = mcp::errors::transport_closed("stdio");
  require(closed.category == "transport", "transport category mismatch");
  require(closed.message == "transport closed",
          "transport closed message mismatch");

  const auto timed_out =
      mcp::errors::request_timed_out(std::chrono::milliseconds(25));
  require(timed_out.category == "timeout", "timeout category mismatch");
  require(timed_out.detail == "25ms", "timeout detail mismatch");

  const auto cancelled = mcp::errors::request_cancelled();
  require(cancelled.category == "cancellation",
          "cancellation category mismatch");
}

}  // namespace

int main() {
  try {
    test_sdk_peer_and_service_surface();
    test_running_service_moved_from_is_inert();
    test_peer_serve_transport_observes_precancelled_token();
    test_client_peer_native_raw_request_dispatches_interleaved_messages();
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
