# High Level API Draft

This is the high-level C++20-friendly facade on top of the current SDK core.
The canonical public surface is `Peer` / `Service` plus the protocol layer.
`client` and `server` remain compatibility and convenience wrappers.

The examples below are the copy-pasteable part of this document. The later
interface sketch summarizes API families and naming only; exact signatures live
in the public headers and generated Doxygen.

## Canonical SDK examples

### Server Peer

```cpp
#include <cxxmcp/peer.hpp>
#include <cxxmcp/protocol.hpp>
#include <cxxmcp/server.hpp>
#include <cxxmcp/service.hpp>

int main() {
    mcp::server::ServerBuilder builder;
    builder.name("demo-server")
        .version("1.0.0")
        .instructions("Expose local tools over MCP.")
        .add_tool(
            mcp::protocol::tool_definition("echo")
                .description("Echo the incoming payload")
                .input_schema(
                    mcp::protocol::object_schema()
                        .required_property("value", mcp::protocol::JsonSchema::string())
                        .additional_properties(false)
                        .build())
                .build(),
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

    return running->peer().list_tools().empty() ? 1 : 0;
}
```

### Typed Tool

```cpp
struct SumArgs {
    int a;
    int b;
};

struct SumResult {
    int sum;
};

// Provide from_json/to_json for your types and specialize SchemaTraits<T>
// when you want field-level schemas.
auto server = mcp::server::App::builder()
    .tool(mcp::server::tool<SumArgs, SumResult>("sum")
        .description("Add two integers")
        .handler([](SumArgs args) {
            return SumResult{.sum = args.a + args.b};
        }))
    .build();
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

## Public API Families

This section is intentionally shape-level. Do not treat it as the authoritative
signature reference; use the headers under `sdk/include/cxxmcp` and generated
Doxygen for exact return types, overloads, and optional capability families.

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

### Client Peer

The client peer family covers:

- connection helpers for Streamable HTTP, legacy SSE, and stdio
- initialize, initialized notification, ping, cancellation, and progress
- tools, prompts, resources, resource templates, and roots
- completion, sampling, elicitation, logging, subscriptions, and tasks
- raw JSON-RPC requests and notifications
- request handles for asynchronous raw calls
- handler installation through `ClientHandler` / `ClientHandlerInterface`

### Server Peer

The server peer family covers:

- `ServerBuilder` for name, version, instructions, capabilities, and handlers
- tool, prompt, resource, resource-template, completion, task, and elicitation
  registration
- initialize, ping, request handling, notification handling, start, and stop
- server-to-client notifications for list changes, progress, logging,
  elicitation completion, and task status
- raw JSON-RPC handling through the server implementation layer
- handler installation through `ServerHandler` / `ServerHandlerInterface`

### Transports

The transport family covers:

- role-generic `mcp::transport::Transport<Role>` message contract
- native role-generic stdio stream transport
- client Streamable HTTP and legacy SSE transport
- client stdio and process-stdio transport
- server HTTP, stdio, and server transport interfaces
- one lightweight umbrella include, `cxxmcp/transport.hpp`, for SDK users who
  need the shared transport contract
- built-in concrete transports from `cxxmcp/client.hpp`, `cxxmcp/server.hpp`,
  or the component transport headers
- compatibility adapters that wrap existing client/server transports as
  `mcp::transport::Transport<Role>`

## Rules

- `Peer` / `Service` is the default SDK entry point.
- Typed handlers are the default registration path.
- Raw JSON-RPC stays as an escape hatch.
- Streamable HTTP is the default HTTP transport.
- Legacy SSE remains for compatibility only.
- The facade must stay modern and straightforward in examples and ownership style.
- The public surface must not drop any capability family exposed by the official Rust SDK.
- Gateway and runtime builders are optional tool-layer APIs, not canonical SDK APIs.

