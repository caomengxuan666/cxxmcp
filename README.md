# cxxmcp

`cxxmcp` is a C++ MCP SDK.

The core package is intentionally narrow: `protocol`, `transport`, `handler`, and `peer` form the SDK shape, while `client` and `server` stay as embeddable compatibility wrappers and convenience entry points. Gateway and CLI code are optional runtime tools built on top of the SDK, not the main product surface.

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
- A C++17 compiler

Default SDK build:

```powershell
cmake -S . -B build
cmake --build build
```

Build the client and server SDKs:

```powershell
cmake -S . -B build-sdk -DMCP_BUILD_CLIENT=ON -DMCP_BUILD_SERVER=ON
cmake --build build-sdk
```

Build the gateway and CLI:

```powershell
cmake -S . -B build-cli -DMCP_BUILD_CLI=ON
cmake --build build-cli
```

Build examples:

```powershell
cmake --preset examples
cmake --build --preset examples
```

That preset builds the stdio server, server peer, client loopback, process stdio client, and gateway runtime examples when their dependencies are enabled.

Run tests:

```powershell
cmake -S . -B build-tests -DMCP_BUILD_CLIENT=ON -DMCP_BUILD_SERVER=ON -DMCP_BUILD_APP=ON -DMCP_BUILD_GATEWAY=ON -DMCP_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Quick Start

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
- [SDK guidance](docs/rmcp_like_sdk_guidance.md)
- [De facto standard roadmap](docs/de_facto_standard_roadmap.md)
- [Capability matrix](docs/capability_matrix.md)
- [Transport strategy](docs/httplib_async_transport_strategy.md)
- [Project layout notes](docs/recommended_project_layout.md)

## Package Shape

The intended public split is:

- core SDK: `cxxmcp::protocol`, `cxxmcp::client`, `cxxmcp::server`, `cxxmcp::sdk`
- runtime tools: gateway/runtime services and `cxxmcp` CLI
- internal/reference: tests, examples, and local reference source used for compatibility checks

Public SDK headers are under `cxxmcp/`. Runtime state, gateway profiles, policy, and CLI defaults are not part of the core SDK contract.

## CMake Options

| Option | Default | Description |
|---|---:|---|
| `MCP_BUILD_PROTOCOL` | `ON` | Build the MCP protocol library |
| `MCP_BUILD_CLIENT` | `OFF` | Build the MCP client library |
| `MCP_BUILD_SERVER` | `OFF` | Build the MCP server library |
| `MCP_BUILD_APP` | `OFF` | Build the application service library |
| `MCP_BUILD_GATEWAY` | `OFF` | Build the gateway service library |
| `MCP_BUILD_CLI` | `OFF` | Build the command-line application |
| `MCP_BUILD_EXAMPLES` | `OFF` | Build example executables |
| `MCP_BUILD_TESTS` | `BUILD_TESTING` | Build tests for enabled layers |

`MCP_BUILD_CLI` enables the gateway, runtime, server, client, and protocol layers it needs.
