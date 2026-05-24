// Copyright (c) 2025 [caomengxuan666]

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/client.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server.hpp"

namespace {

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
    notifications_.push_back(notification);
    return mcp::core::Unit{};
  }

  const std::vector<mcp::protocol::JsonRpcNotification>& notifications() const {
    return notifications_;
  }

 private:
  mcp::server::Server& server_;
  mcp::server::SessionContext context_{
      .session_id = "example",
      .remote_address = "127.0.0.1",
  };
  std::vector<mcp::protocol::JsonRpcNotification> notifications_;
};

}  // namespace

int main() {
  try {
    auto server =
        mcp::server::App::builder()
            .name("cxxmcp-example-loopback-server")
            .version("1.0.0")
            .instructions("Example server used by the client facade example.")
            .tool<mcp::protocol::Json, mcp::protocol::Json>(
                "echo",
                [](const mcp::protocol::Json& input) {
                  return mcp::protocol::Json{{"echo", input}};
                })
            .tool<std::string, std::string>(
                "shout", [](std::string text) { return text + "!"; })
            .prompt(
                mcp::protocol::Prompt{
                    .name = "summarize",
                    .description = "Summarize text",
                    .arguments = {mcp::protocol::PromptArgument{
                        .name = "text",
                        .description = "Text to summarize",
                        .required = true,
                    }},
                },
                [](const mcp::server::PromptContext& context) {
                  const auto text =
                      context.arguments.at("text").get<std::string>();
                  mcp::protocol::PromptsGetResult result;
                  result.description = "Summarize text";
                  result.messages.push_back(mcp::protocol::PromptMessage{
                      .role = "user",
                      .content =
                          mcp::protocol::ContentBlock{
                              .type = "text",
                              .text = "Summarize " + text,
                          },
                  });
                  return result;
                })
            .resource("file:///workspace/README.md",
                      [] {
                        return mcp::protocol::ResourceContents{
                            .uri = "file:///workspace/README.md",
                            .mime_type = "text/markdown",
                            .text = "hello from example",
                        };
                      })
            .resource_template(
                "file:///workspace/{path}",
                [] {
                  return mcp::protocol::ResourceTemplate{
                      .uri_template = "file:///workspace/{path}",
                      .name = "Workspace file",
                      .description = "Address a file in the workspace",
                      .mime_type = "text/plain",
                  };
                })
            .completion([](const mcp::protocol::Json& request) {
              const auto prefix = request.value("prefix", std::string{});
              return mcp::protocol::Json{
                  {"completion",
                   mcp::protocol::Json{
                       {"values", mcp::protocol::Json::array({
                                      prefix + "alpha",
                                      prefix + "beta",
                                  })},
                       {"total", 2},
                   }},
              };
            })
            .sampling([](const mcp::protocol::Json& request) {
              return mcp::protocol::Json{
                  {"role", "assistant"},
                  {"content",
                   mcp::protocol::Json{
                       {"type", "text"},
                       {"text",
                        "Sampled: " +
                            request.value("prompt", std::string{"hello"})},
                   }},
                  {"model", "cxxmcp-example"},
              };
            })
            .logging([](std::string_view level, std::string_view message) {
              (void)level;
              (void)message;
            })
            .raw_request([](const mcp::protocol::JsonRpcRequest& request)
                             -> std::optional<mcp::protocol::JsonRpcResponse> {
              if (request.method == "example/health") {
                return mcp::protocol::make_response(
                    request.id, mcp::protocol::Json{{"ok", true}});
              }
              return std::nullopt;
            })
            .build();
    require(server.has_value(), "failed to build example server");

    auto transport = std::make_unique<LoopbackTransport>(**server);
    auto* transport_ptr = transport.get();
    mcp::client::Client client(std::move(transport));

    std::size_t logging_messages = 0;
    std::size_t tool_notifications = 0;
    std::size_t prompt_notifications = 0;
    std::size_t resource_notifications = 0;
    std::size_t root_notifications = 0;
    std::vector<std::string> updated_uris;
    std::vector<std::string> raw_methods;

    client.on_logging_message(
        [&](std::string_view level, std::string_view message) {
          ++logging_messages;
          require(!level.empty(), "logging level should be populated");
          require(!message.empty(), "logging message should be populated");
        });
    client.on_tool_list_changed([&] { ++tool_notifications; });
    client.on_prompt_list_changed([&] { ++prompt_notifications; });
    client.on_resource_list_changed([&] { ++resource_notifications; });
    client.on_resource_updated(
        [&](const std::string& uri) { updated_uris.push_back(uri); });
    client.on_roots_list_changed([&] { ++root_notifications; });
    client.on_raw_notification(
        [&](const mcp::protocol::JsonRpcNotification& notification) {
          raw_methods.push_back(notification.method);
        });

    require(client.initialize().has_value(), "client initialize failed");

    const auto tools = client.list_tools();
    require(tools.has_value() && tools->size() == 2,
            "client list_tools failed");

    const auto all_tools = client.list_all_tools();
    require(all_tools.has_value() && all_tools->size() == 2,
            "client list_all_tools failed");

    const auto echoed =
        client.call_raw("echo", mcp::protocol::Json{{"value", "hello"}});
    require(echoed.has_value(), "client call_tool failed");

    const auto shouted =
        client.call_raw("shout", mcp::protocol::Json{{"value", "hello"}});
    require(shouted.has_value(), "client typed call_tool failed");

    const auto prompts = client.list_prompts();
    require(prompts.has_value() && prompts->size() == 1,
            "client list_prompts failed");

    const auto all_prompts = client.list_all_prompts();
    require(all_prompts.has_value() && all_prompts->size() == 1,
            "client list_all_prompts failed");

    const auto prompt =
        client.get_prompt("summarize", mcp::protocol::Json{{"text", "hello"}});
    require(prompt.has_value() && !prompt->messages.empty(),
            "client get_prompt failed");

    const auto resources = client.list_resources();
    require(resources.has_value() && resources->size() == 1,
            "client list_resources failed");

    const auto all_resources = client.list_all_resources();
    require(all_resources.has_value() && all_resources->size() == 1,
            "client list_all_resources failed");

    const auto resource = client.read_resource("file:///workspace/README.md");
    require(resource.has_value() && !resource->contents.empty(),
            "client read_resource failed");

    const auto templates = client.list_resource_templates();
    require(templates.has_value() && templates->size() == 1,
            "client list_resource_templates failed");

    const auto all_templates = client.list_all_resource_templates();
    require(all_templates.has_value() && all_templates->size() == 1,
            "client list_all_resource_templates failed");

    const auto completion =
        client.complete(mcp::protocol::Json{{"prefix", "pr"}});
    require(completion.has_value(), "client complete failed");
    require(completion->at("completion").at("values").size() == 2,
            "client complete payload mismatch");

    const auto sample = client.create_message(
        mcp::protocol::Json{{"prompt", "write a summary"}});
    require(sample.has_value(), "client create_message failed");
    require(sample->at("role") == "assistant",
            "client create_message role mismatch");

    const auto health = client.raw_request(mcp::protocol::JsonRpcRequest{
        .method = "example/health",
        .params = mcp::protocol::Json::object(),
        .id = std::int64_t{9},
    });
    require(health.has_value() && health->value("ok", false),
            "client raw_request failed");

    require(client.set_level("info").has_value(), "client set_level failed");

    require(client.notify_initialized().has_value(),
            "client notify_initialized failed");
    require(client.notify_cancelled(std::int64_t{7}, "done").has_value(),
            "client notify_cancelled failed");
    require(client.notify_progress(std::int64_t{8}, 0.5, 1.0).has_value(),
            "client notify_progress failed");
    require(client.notify_roots_list_changed().has_value(),
            "client notify_roots_list_changed failed");
    require(transport_ptr->notifications().size() == 4,
            "client notifications were not recorded");

    require(client
                .handle_notification(mcp::protocol::JsonRpcNotification{
                    .method = "notifications/message",
                    .params = mcp::protocol::Json{{"level", "info"},
                                                  {"data", "example log"}},
                })
                .has_value(),
            "client logging notification failed");
    require(client
                .handle_notification(mcp::protocol::JsonRpcNotification{
                    .method = "notifications/tools/list_changed",
                    .params = mcp::protocol::Json::object(),
                })
                .has_value(),
            "client tools list notification failed");
    require(client
                .handle_notification(mcp::protocol::JsonRpcNotification{
                    .method = "notifications/prompts/list_changed",
                    .params = mcp::protocol::Json::object(),
                })
                .has_value(),
            "client prompts list notification failed");
    require(client
                .handle_notification(mcp::protocol::JsonRpcNotification{
                    .method = "notifications/resources/list_changed",
                    .params = mcp::protocol::Json::object(),
                })
                .has_value(),
            "client resources list notification failed");
    require(
        client
            .handle_notification(mcp::protocol::JsonRpcNotification{
                .method = "notifications/resources/updated",
                .params =
                    mcp::protocol::Json{{"uri", "file:///workspace/README.md"}},
            })
            .has_value(),
        "client resource updated notification failed");
    require(client
                .handle_notification(mcp::protocol::JsonRpcNotification{
                    .method = "notifications/roots/list_changed",
                    .params = mcp::protocol::Json::object(),
                })
                .has_value(),
            "client roots list notification failed");

    require(logging_messages == 1, "client logging handler not called");
    require(tool_notifications == 1,
            "client tool notification handler not called");
    require(prompt_notifications == 1,
            "client prompt notification handler not called");
    require(resource_notifications == 1,
            "client resource notification handler not called");
    require(root_notifications == 1,
            "client roots notification handler not called");
    require(updated_uris.size() == 1 &&
                updated_uris.front() == "file:///workspace/README.md",
            "client resource updated handler mismatch");
    require(raw_methods.size() == 6,
            "client raw notification handler mismatch");

    std::cout << "client facade example passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "client facade example failed: " << ex.what() << '\n';
    return 1;
  }
}
