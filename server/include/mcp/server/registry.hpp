#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/prompt.hpp"
#include "mcp/protocol/resource.hpp"
#include "mcp/protocol/tool.hpp"
#include "mcp/server/peer.hpp"
#include "mcp/server/transport.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcp::server {

struct ToolContext : SessionContext {
    ClientPeer client() const noexcept {
        return client_peer(*this);
    }

    protocol::Json arguments = protocol::Json::object();
};

struct PromptContext : SessionContext {
    ClientPeer client() const noexcept {
        return client_peer(*this);
    }

    protocol::Json arguments = protocol::Json::object();
};

struct ResourceContext : SessionContext {
    ClientPeer client() const noexcept {
        return client_peer(*this);
    }

    std::string uri;
    protocol::Json params = protocol::Json::object();
};

using ToolHandler = std::function<core::Result<protocol::ToolResult>(const ToolContext&)>;
using PromptHandler = std::function<core::Result<protocol::PromptsGetResult>(const PromptContext&)>;
using ResourceReadHandler = std::function<core::Result<protocol::ResourcesReadResult>(const ResourceContext&)>;

class ToolRegistry {
public:
    core::Result<core::Unit> add(protocol::ToolDefinition definition, ToolHandler handler);
    core::Result<protocol::ToolDefinition> get(std::string_view name) const;
    core::Result<protocol::ToolResult> call(std::string_view name, protocol::Json arguments) const;
    core::Result<protocol::ToolResult> call(std::string_view name,
                                            protocol::Json arguments,
                                            const SessionContext& session_context) const;
    core::Result<protocol::ToolResult> call(std::string_view name,
                                            protocol::Json arguments,
                                            const std::string& session_id) const;
    std::vector<protocol::ToolDefinition> list() const;

private:
    struct Entry {
        protocol::ToolDefinition definition;
        ToolHandler handler;
    };

    std::unordered_map<std::string, Entry> tools_;
};

class PromptRegistry {
public:
    core::Result<core::Unit> add(protocol::Prompt prompt, PromptHandler handler);
    core::Result<protocol::PromptsGetResult> get(std::string_view name,
                                                 protocol::Json arguments,
                                                 const std::string& session_id) const;
    core::Result<protocol::PromptsGetResult> get(std::string_view name,
                                                 protocol::Json arguments,
                                                 const SessionContext& session_context) const;
    std::vector<protocol::Prompt> list() const;

private:
    struct Entry {
        protocol::Prompt prompt;
        PromptHandler handler;
    };

    std::unordered_map<std::string, Entry> prompts_;
};

class ResourceRegistry {
public:
    core::Result<core::Unit> add(protocol::Resource resource, ResourceReadHandler handler);
    core::Result<protocol::ResourcesReadResult> read(std::string_view uri,
                                                     protocol::Json params,
                                                     const std::string& session_id) const;
    core::Result<protocol::ResourcesReadResult> read(std::string_view uri,
                                                     protocol::Json params,
                                                     const SessionContext& session_context) const;
    std::vector<protocol::Resource> list() const;

private:
    struct Entry {
        protocol::Resource resource;
        ResourceReadHandler handler;
    };

    std::unordered_map<std::string, Entry> resources_;
};

class ResourceTemplateRegistry {
public:
    core::Result<core::Unit> add(protocol::ResourceTemplate resource_template);
    std::vector<protocol::ResourceTemplate> list() const;

private:
    std::unordered_map<std::string, protocol::ResourceTemplate> resource_templates_;
};

} // namespace mcp::server
