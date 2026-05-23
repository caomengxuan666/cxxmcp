#pragma once

#include "mcp/client/client.hpp"

#include <memory>
#include <string>
#include <vector>

namespace mcp::client {

struct McpClientSessionOptions {
    std::string client_name = "cxxmcp";
    std::string client_version = "0";
};

class McpClientSession {
public:
    explicit McpClientSession(std::unique_ptr<Transport> transport,
                              McpClientSessionOptions options = {});

    core::Result<protocol::Json> initialize();
    core::Result<core::Unit> mark_initialized();
    core::Result<std::vector<protocol::Prompt>> discover_prompts();
    core::Result<std::vector<protocol::Prompt>> discover_all_prompts();
    core::Result<protocol::PromptsGetResult> get_prompt(const protocol::PromptsGetParams& params);
    core::Result<std::vector<protocol::Resource>> discover_resources();
    core::Result<std::vector<protocol::Resource>> discover_all_resources();
    core::Result<protocol::ResourcesReadResult> read_resource(const protocol::ResourcesReadParams& params);
    core::Result<std::vector<protocol::ResourceTemplate>> discover_resource_templates();
    core::Result<std::vector<protocol::ResourceTemplate>> discover_all_resource_templates();
    core::Result<std::vector<protocol::ToolDefinition>> discover_tools();
    core::Result<std::vector<protocol::ToolDefinition>> discover_all_tools();
    core::Result<protocol::ToolResult> call_tool(const protocol::ToolCall& call);
    Client& client();
    const Client& client() const;

private:
    Client client_;
    McpClientSessionOptions options_;
};

} // namespace mcp::client
