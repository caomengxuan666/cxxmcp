# cxxmcp

`cxxmcp` is a C++ MCP SDK.

The core package is intentionally narrow: `protocol`, `transport`, `handler`, and `peer` form the SDK shape, while `client` and `server` stay as embeddable compatibility wrappers and convenience entry points. Gateway and CLI code are optional runtime tools built on top of the SDK, not the main product surface.
Optional extension layers are also first-class package targets: `cxxmcp::plugin_sdk` and `cxxmcp::adapters`.

## Features

- Typed MCP protocol models and JSON-RPC serialization
- Client SDK for HTTP, stdio, and process-based connections
- Server SDK with typed tool, prompt, resource, and transport handlers
- Streamable HTTP support, with legacy SSE compatibility where needed
- Optional gateway and CLI tools for local runtime management
- Raw JSON-RPC request and notification escape hatches

## Using as a Library

Installed-package usage should look like a normal CMake SDK:

```cmake
find_package(cxxmcp CONFIG REQUIRED)

target_link_libraries(my_client PRIVATE cxxmcp::client)
target_link_libraries(my_server PRIVATE cxxmcp::server)
```

Use `cxxmcp::sdk` only when you want the aggregate protocol/client/server SDK target.

Public headers use the `cxxmcp/` prefix.

## Build

Requirements:

- CMake 3.23+
- A C++20 compiler

Default SDK build:

```powershell
cmake -S . -B build
cmake --build build
```

Build the client and server SDKs:

```powershell
cmake -S . -B build-sdk -DCXXMCP_BUILD_CLIENT=ON -DCXXMCP_BUILD_SERVER=ON
cmake --build build-sdk
```

Build the gateway and CLI:

```powershell
cmake -S . -B build-cli -DCXXMCP_BUILD_CLI=ON
cmake --build build-cli
```

Build examples:

```powershell
cmake --preset examples
cmake --build --preset examples
```

That preset builds the stdio server, server peer, client peer, client loopback, process stdio client, and gateway runtime examples when their dependencies are enabled.

Run tests:

```powershell
cmake -S . -B build-tests -DCXXMCP_BUILD_SDK=ON -DCXXMCP_BUILD_CLIENT=ON -DCXXMCP_BUILD_SERVER=ON -DCXXMCP_BUILD_RUNTIME=ON -DCXXMCP_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Quick Start

### Server Peer

```cpp
#include <cxxmcp/peer.hpp>
#include <cxxmcp/server.hpp>

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
    return 0;
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

### Runtime Server

This is the higher-level runtime builder example. It stays useful for product-style servers, but it is not the core peer/handler surface.

```cpp
#include <cxxmcp/server.hpp>

int main() {
    return mcp::server::App::builder()
        .name("demo-server")
        .version("1.0.0")
        .instructions("Expose local tools over MCP.")
        .stdio()
        .tool<std::string, std::string>("echo", [](const std::string& text) {
            return text;
        })
        .run();
}
```

### Client

```cpp
#include <cxxmcp/peer.hpp>

int main() {
    auto peer = mcp::Peer<mcp::RoleClient>::connect_streamable_http({
        .host = "127.0.0.1",
        .port = 3000,
        .path = "/mcp",
    });

    peer.initialize();
    peer.list_all_tools();
    peer.call_tool("echo", mcp::protocol::Json{{"value", "hello"}});
}
```

## Documentation

- [High-level API](docs/high_level_api.md)
- [Release policy](docs/release_policy.md)
- [Changelog](CHANGELOG.md)
- [SDK guidance](docs/rmcp_like_sdk_guidance.md)
- [De facto standard roadmap](docs/de_facto_standard_roadmap.md)
- [Capability matrix](docs/capability_matrix.md)
- [Transport strategy](docs/httplib_async_transport_strategy.md)
- [Project layout notes](docs/recommended_project_layout.md)

## Package Shape

The intended public split is:

- core SDK: `cxxmcp::protocol`, `cxxmcp::transport`, `cxxmcp::peer`, `cxxmcp::client`, `cxxmcp::server`, `cxxmcp::handler`, `cxxmcp::service`, `cxxmcp::sdk`
- runtime tools: `cxxmcp::runtime`, `cxxmcp::gateway`, `cxxmcp::cli`
- internal/reference: tests, examples, and local reference source used for compatibility checks

Public SDK headers are under `cxxmcp/`. Runtime state, gateway profiles, policy, and CLI defaults are not part of the core SDK contract.

## CMake Options

| Option | Default | Description |
|---|---:|---|
| `CXXMCP_BUILD_PROTOCOL` | `ON` | Build the MCP protocol library |
| `CXXMCP_BUILD_CLIENT` | `OFF` | Build the MCP client library |
| `CXXMCP_BUILD_SERVER` | `OFF` | Build the MCP server library |
| `CXXMCP_BUILD_RUNTIME` | `OFF` | Build the runtime application layer |
| `CXXMCP_BUILD_APP` | `OFF` | Build the application service library |
| `CXXMCP_BUILD_GATEWAY` | `OFF` | Build the gateway service library |
| `CXXMCP_BUILD_CLI` | `OFF` | Build the command-line application |
| `CXXMCP_BUILD_EXAMPLES` | `OFF` | Build example executables |
| `CXXMCP_BUILD_TESTS` | `BUILD_TESTING` | Build tests for enabled layers |

`CXXMCP_BUILD_CLI` enables the gateway, runtime, server, client, and protocol layers it needs.
