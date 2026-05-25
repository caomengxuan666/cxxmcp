// Copyright (c) 2025 [caomengxuan666]
//
// Compatibility example: keeps a concrete in-process loopback while exercising
// task-aware calls. New SDK application examples should start from
// Peer/Service and role-generic transports.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"

namespace {

using Json = mcp::protocol::Json;

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
      const mcp::protocol::JsonRpcNotification&) override {
    return mcp::core::Unit{};
  }

 private:
  mcp::server::Server& server_;
  mcp::server::SessionContext context_{
      .session_id = "task-example-session",
      .remote_address = "loopback",
  };
};

bool wait_for_task_status(mcp::ClientPeer& peer, std::string_view task_id,
                          mcp::protocol::TaskStatus status) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto task = peer.get_task(task_id);
    if (task.has_value() && task->status == status) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

}  // namespace

int main() {
  try {
    auto built =
        mcp::server::App::builder()
            .name("cxxmcp-example-task-server")
            .version("1.0.0")
            .tasks(mcp::server::TaskOperationProcessorOptions{
                .worker_count = 1,
                .queue_size = 8,
                .poll_interval = std::int64_t{1},
            })
            .tool(mcp::server::tool<Json, Json>("long.echo")
                      .description("Echo a value through the task lifecycle.")
                      .task_support(mcp::protocol::TaskSupport::Optional)
                      .handler([](const Json& args,
                                  const mcp::server::ToolContext& context) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(20));
                        return Json{
                            {"echo", args.at("value")},
                            {"session", context.session_id},
                        };
                      }))
            .build();
    require(built.has_value(), "task server build failed");

    mcp::ClientPeer peer(
        mcp::client::Client(std::make_unique<LoopbackTransport>(**built)));

    mcp::RequestOptions options;
    options.timeout = std::chrono::milliseconds(500);
    options.meta = Json{{"traceId", "task-example"}};

    auto handle = peer.call_tool_task_async(
        mcp::protocol::ToolCall{
            .name = "long.echo",
            .arguments = Json{{"value", "hello"}},
            .task =
                mcp::protocol::TaskRequestParameters{
                    .ttl = std::int64_t{60},
                },
        },
        options);

    const auto created = handle.await_response();
    require(created.has_value(), "task-aware tool call failed");
    require(!created->task.task_id.empty(), "created task id missing");

    require(wait_for_task_status(peer, created->task.task_id,
                                 mcp::protocol::TaskStatus::Completed),
            "task did not complete");

    const auto tasks = peer.list_tasks();
    require(tasks.has_value() && !tasks->empty(), "list_tasks failed");

    const auto payload = peer.task_result(created->task.task_id);
    require(payload.has_value(), "task_result failed");
    require(payload->at("structuredContent").at("echo") == "hello",
            "task structuredContent mismatch");
    require(payload->at("structuredContent").at("session") ==
                "task-example-session",
            "task session context mismatch");

    std::cout << "task async client/server example passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "task async client/server example failed: " << ex.what()
              << '\n';
    return 1;
  }
}
