#include "mcp/client.hpp"
#include "mcp/protocol/serialization.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef MCP_EXAMPLE_CHILD_EXE
#define MCP_EXAMPLE_CHILD_EXE ""
#endif

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

} // namespace

int main() {
    try {
        require(std::string_view(MCP_EXAMPLE_CHILD_EXE).size() != 0, "child server executable is not configured");

        mcp::client::McpClientSession session(std::make_unique<mcp::client::ProcessStdioTransport>(
            mcp::client::ProcessStdioTransportOptions{
                .command = MCP_EXAMPLE_CHILD_EXE,
                .args = {},
                .cwd = {},
                .env = {},
            }));

        require(session.initialize().has_value(), "process session initialize failed");
        require(session.mark_initialized().has_value(), "process session initialized notification failed");

        const auto tools = session.discover_tools();
        require(tools.has_value() && tools->size() == 2, "process session discover_tools failed");

        const auto prompts = session.discover_prompts();
        require(prompts.has_value() && prompts->size() == 1, "process session discover_prompts failed");

        const auto resources = session.discover_resources();
        require(resources.has_value() && resources->size() == 1, "process session discover_resources failed");

        const auto templates = session.discover_resource_templates();
        require(templates.has_value() && templates->size() == 1, "process session discover_resource_templates failed");

        const auto prompt = session.get_prompt(mcp::protocol::PromptsGetParams{
            .name = prompts->front().name,
            .arguments = mcp::protocol::Json{{"text", "hello"}},
        });
        require(prompt.has_value() && !prompt->messages.empty(), "process session get_prompt failed");

        const auto resource = session.read_resource(mcp::protocol::ResourcesReadParams{
            .uri = resources->front().uri,
        });
        require(resource.has_value() && !resource->contents.empty(), "process session read_resource failed");

        const auto shout = session.call_tool(mcp::protocol::ToolCall{
            .name = "shout",
            .arguments = mcp::protocol::Json{{"value", "hello"}},
        });
        require(shout.has_value(), "process session call_tool failed");

        const auto completion = session.client().complete(mcp::protocol::Json{{"prefix", "pr"}});
        require(completion.has_value(), "process session complete failed");
        require(completion->at("completion").at("values").size() == 2, "process session completion payload mismatch");

        const auto sampling = session.client().create_message(mcp::protocol::Json{{"prompt", "write a summary"}});
        require(sampling.has_value(), "process session create_message failed");
        require(sampling->at("role") == "assistant", "process session sampling role mismatch");

        const auto health = session.client().raw_request(mcp::protocol::JsonRpcRequest{
            .method = "example/health",
            .params = mcp::protocol::Json::object(),
            .id = std::int64_t{42},
        });
        require(health.has_value() && health->value("ok", false), "process session raw_request failed");

        require(session.client().set_level("info").has_value(), "process session set_level failed");

        std::cout << "process stdio client example passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "process stdio client example failed: " << ex.what() << '\n';
        return 1;
    }
}
