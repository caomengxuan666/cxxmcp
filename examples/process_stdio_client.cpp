// Copyright (c) 2025 [caomengxuan666]

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cxxmcp/client.hpp"
#include "cxxmcp/peer.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/service.hpp"

#ifndef MCP_EXAMPLE_CHILD_EXE
#define MCP_EXAMPLE_CHILD_EXE ""
#endif

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

}  // namespace

int main() {
  try {
    require(std::string_view(MCP_EXAMPLE_CHILD_EXE).size() != 0,
            "child server executable is not configured");

    auto peer =
        mcp::ClientPeer(std::make_unique<mcp::client::ProcessStdioTransport>(
            mcp::client::ProcessStdioTransportOptions{
                .command = MCP_EXAMPLE_CHILD_EXE,
                .args = {},
                .cwd = {},
                .env = {},
            }));

    auto running = mcp::serve(std::move(peer));
    require(running.has_value(), "process peer service failed to start");
    require(running->running(), "process peer service should report running");

    require(running->peer().initialize().has_value(),
            "process peer initialize failed");
    require(running->peer().notify_initialized().has_value(),
            "process peer initialized notification failed");

    const auto tools = running->peer().list_tools();
    require(tools.has_value() && tools->size() == 2,
            "process peer list_tools failed");

    const auto prompts = running->peer().list_prompts();
    require(prompts.has_value() && prompts->size() == 1,
            "process peer list_prompts failed");

    const auto resources = running->peer().list_resources();
    require(resources.has_value() && resources->size() == 1,
            "process peer list_resources failed");

    const auto templates = running->peer().list_resource_templates();
    require(templates.has_value() && templates->size() == 1,
            "process peer list_resource_templates failed");

    const auto prompt = running->peer().get_prompt(
        prompts->front().name, mcp::protocol::Json{{"text", "hello"}});
    require(prompt.has_value() && !prompt->messages.empty(),
            "process peer get_prompt failed");

    const auto resource = running->peer().read_resource(resources->front().uri);
    require(resource.has_value() && !resource->contents.empty(),
            "process peer read_resource failed");

    const auto shout = running->peer().call_tool(
        "shout", mcp::protocol::Json{{"value", "hello"}});
    require(shout.has_value(), "process peer call_tool failed");

    const auto completion =
        running->peer().complete(mcp::protocol::Json{{"prefix", "pr"}});
    require(completion.has_value(), "process peer complete failed");
    require(completion->at("completion").at("values").size() == 2,
            "process peer completion payload mismatch");

    const auto sampling = running->peer().create_message(
        mcp::protocol::Json{{"prompt", "write a summary"}});
    require(sampling.has_value(), "process peer create_message failed");
    require(sampling->at("role") == "assistant",
            "process peer sampling role mismatch");

    const auto health =
        running->peer().raw_request(mcp::protocol::JsonRpcRequest{
            .method = "example/health",
            .params = mcp::protocol::Json::object(),
            .id = std::int64_t{42},
        });
    require(health.has_value() && health->value("ok", false),
            "process peer raw_request failed");

    require(running->peer().set_level("info").has_value(),
            "process peer set_level failed");

    std::cout << "process stdio client example passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "process stdio client example failed: " << ex.what() << '\n';
    return 1;
  }
}
