# High Level API Draft

This is the target C++17-friendly facade on top of the current core.
The canonical public surface is `Peer` / `Service` plus the protocol layer.
`client` and `server` remain compatibility and convenience wrappers. The
public surface should match the official Rust SDK parity set from
`docs/rmcp_api_inventory.md`, with `elicitation` treated as an optional
feature-gated extension.

## Canonical SDK examples

### Server Peer

```cpp
#include <cxxmcp/peer.hpp>
#include <cxxmcp/server.hpp>
#include <cxxmcp/service.hpp>

int main() {
    mcp::server::ServerBuilder builder;
    builder.name("demo-server")
        .version("1.0.0")
        .instructions("Expose local tools over MCP.")
        .add_tool(
            mcp::protocol::ToolDefinition{
                .name = "echo",
                .description = "Echo the incoming payload",
                .input_schema = mcp::protocol::Json{{"type", "object"}},
            },
            [](const mcp::server::ToolContext& context) {
                mcp::protocol::ToolResult result;
                result.structured_content = context.arguments;
                return result;
            });

    auto server = builder.build();
    if (!server) {
        return 1;
    }

    auto running = mcp::serve(mcp::ServerPeer(std::move(*server)));
    if (!running) {
        return 1;
    }

    return running->peer().list_tools().has_value() ? 0 : 1;
}
```

### Client Peer

```cpp
#include <cxxmcp/peer.hpp>
#include <cxxmcp/service.hpp>

int main() {
    auto peer = mcp::ClientPeer::connect_streamable_http({
        .host = "127.0.0.1",
        .port = 3000,
        .path = "/mcp",
    });

    auto running = mcp::serve(std::move(peer));
    if (!running) {
        return 1;
    }

    return running->peer().initialize().has_value() ? 0 : 1;
}
```

### Process stdio client

```cpp
#include <cxxmcp/client.hpp>
#include <cxxmcp/peer.hpp>
#include <cxxmcp/service.hpp>

int main() {
    auto peer = mcp::ClientPeer(std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = "/path/to/cxxmcp-example-stdio-server",
        }));

    auto running = mcp::serve(std::move(peer));
    if (!running) {
        return 1;
    }

    running->peer().initialize();
    running->peer().notify_initialized();
    const auto tools = running->peer().list_tools();
    const auto prompts = running->peer().list_prompts();
    const auto resources = running->peer().list_resources();
    const auto templates = running->peer().list_resource_templates();

    running->peer().get_prompt(prompts->front().name, {{"text", "hello"}});
    running->peer().read_resource(resources->front().uri);
    running->peer().complete({{"prefix", "pr"}});
    running->peer().create_message({{"prompt", "write a summary"}});
    running->peer().raw_request({
        .method = "example/health",
        .params = {},
        .id = 1,
    });
}
```

## Derived interface surface

### Peer / Service

```cpp
namespace mcp {

struct RoleClient {};
struct RoleServer {};

template <class Role>
class Peer;

template <class Role>
class Service;

template <class Role>
class RunningService;

using ClientPeer = Peer<RoleClient>;
using ServerPeer = Peer<RoleServer>;

} // namespace mcp
```

### Protocol

```cpp
namespace mcp::protocol {

// common
// Json, RequestId, ErrorCode, ErrorObject, JsonRpcRequest, JsonRpcResponse, JsonRpcNotification

// capability models
// ClientCapabilities, ServerCapabilities, ResourceCapabilities, PromptCapabilities, ToolCapabilities

// content and model families
// ContentBlock, ToolDefinition, ToolCall, ToolResult
// Prompt, PromptArgument, PromptMessage, PromptsListResult, PromptsGetParams, PromptsGetResult
// Resource, ResourceContents, ResourcesListResult, ResourcesReadParams, ResourcesReadResult
// ResourceTemplate, ResourcesTemplateListResult
// Root, RootsListResult
// Completion request/result models
// Logging request/result models
// Sampling request/result models
// Elicitation request/result models

} // namespace mcp::protocol
```

### Client

```cpp
namespace mcp::client {

class Client {
public:
    struct StreamableHttpEndpoint {
        std::string host;
        std::uint16_t port;
        std::string path = "/mcp";
    };

    struct StdioEndpoint {
        std::string command;
        std::vector<std::string> args;
        std::string cwd;
    };

    struct Root {
        std::string uri;
        std::string name;
    };

    static Client connect_streamable_http(StreamableHttpEndpoint endpoint);
    static Client connect_stdio(StdioEndpoint endpoint);
    static Client connect_legacy_sse(StreamableHttpEndpoint endpoint);

    void initialize();
    void ping();
    std::vector<protocol::ToolDefinition> list_tools();
    std::vector<protocol::ToolDefinition> list_all_tools();
    std::vector<protocol::Prompt> list_prompts();
    std::vector<protocol::Prompt> list_all_prompts();
    std::vector<protocol::Resource> list_resources();
    std::vector<protocol::Resource> list_all_resources();
    std::vector<protocol::ResourceTemplate> list_resource_templates();
    std::vector<protocol::ResourceTemplate> list_all_resource_templates();
    std::vector<Root> list_roots();
    void set_roots(std::vector<Root> roots);

    protocol::Json complete(const protocol::Json& request);
    protocol::Json create_message(const protocol::Json& request);
    protocol::Json create_elicitation(const protocol::Json& request);
    protocol::Json request(const protocol::JsonRpcRequest& request);
    void notify(const protocol::JsonRpcNotification& notification);

    void on_initialized(std::function<void()> handler);
    void on_cancelled(std::function<void(const protocol::RequestId&, std::string_view)> handler);
    void on_logging_message(std::function<void(std::string_view, std::string_view)> handler);
    void on_tool_list_changed(std::function<void()> handler);
    void on_prompt_list_changed(std::function<void()> handler);
    void on_resource_list_changed(std::function<void()> handler);
    void on_resource_updated(std::function<void(const std::string& uri)> handler);
    void on_roots_list_changed(std::function<void()> handler);

    void subscribe(std::string uri);
    void unsubscribe(std::string uri);

    template <class Args, class Result>
    Result call(std::string_view name, const Args& args);

    protocol::ToolResult call_raw(std::string_view name, const protocol::Json& args);
    protocol::PromptsGetResult get_prompt(std::string_view name, const protocol::Json& arguments);
    protocol::ResourcesReadResult read_resource(std::string_view uri);
    protocol::Json request(const protocol::JsonRpcRequest& request);
    void notify(const protocol::JsonRpcNotification& notification);
};

} // namespace mcp::client
```

### Server

```cpp
namespace mcp::server {

class App {
public:
    class Builder {
    public:
        Builder& name(std::string value);
        Builder& version(std::string value);
        Builder& instructions(std::string value);

        Builder& stdio();
        Builder& streamable_http(std::string host, std::uint16_t port, std::string path = "/mcp");
        Builder& legacy_sse(std::string host, std::uint16_t port, std::string path = "/mcp");

        template <class Args, class Result, class Handler>
        Builder& tool(std::string name, Handler handler);
        template <class Handler>
        Builder& prompt(std::string name, Handler handler);
        template <class Handler>
        Builder& resource(std::string name, Handler handler);
        template <class Handler>
        Builder& resource_template(std::string name, Handler handler);
        template <class Handler>
        Builder& completion(Handler handler);
        template <class Handler>
        Builder& sampling(Handler handler);
        template <class Handler>
        Builder& logging(Handler handler);
        template <class Handler>
        Builder& raw_request(Handler handler);

        int run();
    };

    static Builder builder();
};

} // namespace mcp::server
```

## Rules

- `Peer` / `Service` is the default SDK entry point.
- Typed handlers are the default registration path.
- Raw JSON-RPC stays as an escape hatch.
- Streamable HTTP is the default HTTP transport.
- Legacy SSE remains for compatibility only.
- The facade must stay C++17-friendly in examples and ownership style.
- The public surface must not drop any capability family exposed by the official Rust SDK.
- Gateway and runtime builders are optional tool-layer APIs, not canonical SDK APIs.

