# High Level API Draft

This is the target C++17-friendly facade on top of the current core.
The public surface should match the official Rust SDK parity set from `docs/rmcp_api_inventory.md`, with `elicitation` treated as an optional feature-gated extension.

## Canonical examples

### Server

```cpp
#include <mcp/server.hpp>

struct EchoArgs {
    std::string value;
};

struct EchoResult {
    std::string value;
};

int main() {
    return mcp::server::App::builder()
        .name("demo-server")
        .version("1.0.0")
        .instructions("Expose local tools over MCP.")
        .stdio()
        .tool<EchoArgs, EchoResult>("echo", [](const EchoArgs& input) {
            return EchoResult{input.value};
        })
        .tool<std::string, std::string>("shout", [](std::string text) {
            return text + "!";
        })
        .prompt(mcp::protocol::Prompt{
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
                .content = mcp::protocol::ContentBlock{.type = "text", .text = text},
            });
            return result;
        })
        .resource("readme", [] {
            return mcp::protocol::Resource{
                .uri = "file:///workspace/README.md",
                .name = "Readme",
                .description = "Workspace readme",
                .mime_type = "text/plain",
            };
        })
        .resource_template("file", [](std::string path) {
            return mcp::protocol::ResourceTemplate{
                .uri_template = "file:///workspace/{path}",
                .name = "Workspace file",
                .description = "Address a file in the workspace",
            };
        })
        .completion([](const mcp::protocol::Json& request) {
            return mcp::protocol::Json{
                {"completion", mcp::protocol::Json{
                                   {"values", mcp::protocol::Json::array({
                                       request.value("prefix", std::string{}) + "alpha",
                                       request.value("prefix", std::string{}) + "beta",
                                   })},
                               }},
            };
        })
        .sampling([](const mcp::protocol::Json& request) {
            return mcp::protocol::Json{
                {"role", "assistant"},
                {"content", mcp::protocol::Json{{"type", "text"},
                                                {"text", "Sampled: " + request.value("prompt", std::string{})}}},
                {"model", "demo-model"},
            };
        })
        .logging([](std::string_view level, std::string_view message) {
            (void)level;
            (void)message;
        })
        .raw_request([](const mcp::protocol::JsonRpcRequest& request) {
            if (request.method == "example/health") {
                return mcp::protocol::Json{{"ok", true}};
            }
            return mcp::protocol::Json{};
        })
        .run();
}
```

### Client

```cpp
#include <mcp/client.hpp>

int main() {
    auto client = mcp::client::Client::connect_streamable_http({
        .host = "127.0.0.1",
        .port = 3000,
        .path = "/mcp",
    });

    client.initialize();

    const auto tools = client.list_all_tools();
    const auto prompts = client.list_all_prompts();
    const auto resources = client.list_all_resources();
    const auto templates = client.list_all_resource_templates();
    const auto roots = client.list_roots();

    client.set_roots({
        {.uri = "file:///workspace", .name = "workspace"},
    });

    const auto prompt = client.get_prompt("summarize", {{"text", "hello"}});
    const auto resource = client.read_resource("file:///workspace/README.md");
    const auto tool = client.call_raw("echo", {{"value", "hello"}});
    const auto completion = client.complete({{"prefix", "he"}});
    const auto sample = client.create_message({{"prompt", "write a summary"}});

    client.on_logging_message([](std::string_view level, std::string_view message) {
        (void)level;
        (void)message;
    });
    client.on_tool_list_changed([] {});
    client.on_prompt_list_changed([] {});
    client.on_resource_list_changed([] {});
    client.on_resource_updated([](const std::string& uri) {
        (void)uri;
    });
    client.on_roots_list_changed([] {});
    client.notify_initialized();
    client.notify_progress(std::int64_t{1}, 0.5, 1.0);
    client.notify_roots_list_changed();
}
```

### Process stdio client

```cpp
#include <mcp/client.hpp>

int main() {
    mcp::client::McpClientSession session(std::make_unique<mcp::client::ProcessStdioTransport>(
        mcp::client::ProcessStdioTransportOptions{
            .command = "/path/to/cxxmcp-example-stdio-server",
        }));

    session.initialize();
    session.mark_initialized();

    const auto tools = session.discover_tools();
    const auto prompts = session.discover_prompts();
    const auto resources = session.discover_resources();
    const auto templates = session.discover_resource_templates();

    session.get_prompt(mcp::protocol::PromptsGetParams{
        .name = prompts->front().name,
        .arguments = {{"text", "hello"}},
    });
    session.read_resource(mcp::protocol::ResourcesReadParams{
        .uri = resources->front().uri,
    });

    session.client().complete({{"prefix", "pr"}});
    session.client().create_message({{"prompt", "write a summary"}});
    session.client().raw_request({
        .method = "example/health",
        .params = {},
        .id = 1,
    });
}
```

### Gateway

```cpp
#include <mcp/gateway.hpp>

int main() {
    auto gateway = mcp::gateway::Runtime::builder()
        .profile("profile.dev")
        .host("127.0.0.1")
        .port(39931)
        .path("/mcp/dev")
        .trust(true)
        .discover(true)
        .bind_server("filesystem")
        .instruction("Use reviewed workspace tools only.")
        .add_stdio_server("filesystem", "npx", {
            "-y",
            "@modelcontextprotocol/server-filesystem",
            "C:/workspace",
        })
        .build();

    if (!gateway) {
        return 1;
    }

    return gateway->run();
}
```

## Derived interface surface

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

### Gateway

```cpp
namespace mcp::gateway {

class Runtime {
public:
    class Builder {
    public:
        Builder& profile(std::string id);
        Builder& host(std::string host);
        Builder& port(std::uint16_t port);
        Builder& path(std::string path);
        Builder& trust(bool enabled);
        Builder& discover(bool enabled);
        Builder& bind_server(std::string server_id);
        Builder& instruction(std::string value);
        Builder& add_header(std::string name, std::string value);
        Builder& add_env(std::string name, std::string value);
        Builder& add_stdio_server(std::string id, std::string command, std::vector<std::string> args);
        Builder& add_http_server(std::string id, std::string url);
        Builder& add_capability_filter(std::string capability_id);
        Builder& export_client_config(std::string path);
        Runtime build();
    };

    static Builder builder();
    int run();
};

} // namespace mcp::gateway
```

## Rules

- The fluent builder is the default entry point.
- Typed handlers are the default registration path.
- Raw JSON-RPC stays as an escape hatch.
- Streamable HTTP is the default HTTP transport.
- Legacy SSE remains for compatibility only.
- The facade must stay C++17-friendly in examples and ownership style.
- The public surface must not drop any capability family exposed by the official Rust SDK.
