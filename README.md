# cxxmcp

`cxxmcp` is a practical C++ SDK for the Model Context Protocol (MCP), with protocol models, client and server libraries, and optional gateway and CLI layers.

## Features

- Typed MCP protocol models and JSON-RPC serialization
- Client SDK for HTTP, stdio, and process-based connections
- Server SDK with typed tool, prompt, resource, and transport handlers
- Streamable HTTP support, with legacy SSE compatibility where needed
- Gateway and CLI layers for local runtime management
- Raw JSON-RPC request and notification escape hatches

## Build

Requirements:

- CMake 3.23+
- A C++23 compiler

Default build:

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

Run tests:

```powershell
cmake -S . -B build-tests -DMCP_BUILD_CLIENT=ON -DMCP_BUILD_SERVER=ON -DMCP_BUILD_APP=ON -DMCP_BUILD_GATEWAY=ON -DMCP_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Quick Start

### Server

```cpp
#include <mcp/server.hpp>

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
#include <mcp/client.hpp>

int main() {
    auto client = mcp::client::Client::connect_streamable_http({
        .host = "127.0.0.1",
        .port = 3000,
        .path = "/mcp",
    });

    client.initialize();
    client.list_all_tools();
    client.call_raw("echo", {{"value", "hello"}});
}
```

## Documentation

- [High-level API](docs/high_level_api.md)
- [SDK guidance](docs/rmcp_like_sdk_guidance.md)
- [Capability matrix](docs/capability_matrix.md)
- [Transport strategy](docs/httplib_async_transport_strategy.md)
- [Project layout notes](docs/recommended_project_layout.md)

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

`MCP_BUILD_CLI` enables the gateway, app, server, client, and protocol layers it needs.
