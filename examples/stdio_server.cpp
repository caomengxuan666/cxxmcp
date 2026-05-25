// Copyright (c) 2025 [caomengxuan666]

#include <optional>
#include <string>
#include <string_view>

#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server.hpp"

namespace {

mcp::protocol::Json make_completion_result(std::string_view prefix) {
  return mcp::protocol::Json{
      {"completion",
       mcp::protocol::Json{
           {"values", mcp::protocol::Json::array({
                          std::string(prefix) + "alpha",
                          std::string(prefix) + "beta",
                      })},
           {"total", 2},
           {"hasMore", false},
       }},
  };
}

mcp::protocol::Json make_sampling_result(std::string_view text) {
  return mcp::protocol::Json{
      {"role", "assistant"},
      {"content",
       mcp::protocol::Json{
           {"type", "text"},
           {"text", std::string("Sampled: ") + std::string(text)},
       }},
      {"model", "cxxmcp-example"},
      {"stopReason", "endTurn"},
  };
}

}  // namespace

int main() {
  return mcp::server::App::builder()
      .name("cxxmcp-example-stdio-server")
      .version("1.0.0")
      .instructions("Example stdio MCP server.")
      .stdio()
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
            const auto text = context.arguments.at("text").get<std::string>();
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
      .resource(
          "file:///workspace/session.txt",
          [](std::string uri, const mcp::server::ResourceContext& context) {
            return context.session_id + ":" + uri;
          })
      .resource_template("file:///workspace/{path}",
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
        return make_completion_result(prefix);
      })
      .sampling([](const mcp::protocol::Json& request) {
        return make_sampling_result(
            request.value("prompt", std::string{"hello"}));
      })
      .logging([](std::string_view level, std::string_view message) {
        (void)level;
        (void)message;
      })
      .raw_request([](const mcp::protocol::JsonRpcRequest& request)
                       -> std::optional<mcp::protocol::JsonRpcResponse> {
        if (request.method == "example/health") {
          return mcp::protocol::make_response(
              request.id, mcp::protocol::Json{
                              {"ok", true},
                              {"server", "cxxmcp-example-stdio-server"},
                          });
        }
        return std::nullopt;
      })
      .run();
}
