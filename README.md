# cxxmcp

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C.svg)](https://cmake.org/)
[![MCP](https://img.shields.io/badge/protocol-Model%20Context%20Protocol-111827.svg)](https://modelcontextprotocol.io/)
[![SDK](https://img.shields.io/badge/package-C%2B%2B%20SDK-0F766E.svg)](#using-as-a-library)

`cxxmcp` is a modern C++17 SDK and optional C++20 runtime toolkit for the
[Model Context Protocol](https://modelcontextprotocol.io/). It is designed to
be the practical default choice for C++ applications that need to expose or
consume MCP servers.

The public SDK surface is intentionally narrow and package-friendly:
`protocol`, `transport`, `handler`, `peer`, `service`, `client`, and `server`
are the core library layers. Gateway, app, and CLI code are optional runtime
tools built on top of the SDK.

Read this in [Chinese](README_zh.md).

## Why cxxmcp

- C++17 SDK with CMake package targets and install smoke coverage
- Typed MCP protocol models with raw JSON-RPC escape hatches
- Client and server libraries for embedded C++ applications
- RMCP-style `Peer`, `Service`, and handler boundaries for SDK-first authoring
- stdio, process stdio, Streamable HTTP, and legacy SSE-compatible transport
  paths
- Typed tool, prompt, resource, completion, elicitation, sampling, task,
  progress, and cancellation surfaces
- Optional gateway and CLI runtime for local MCP server management
- Local RMCP interoperability and package-smoke tests used as release gates

## Capability Snapshot

| Area | Status |
|---|---|
| Protocol and JSON-RPC | Typed models, serialization helpers, initialize version validation, raw request/notification escape hatches |
| Client SDK | HTTP, stdio, process-stdio, request handles, typed async helpers, roots, sampling, elicitation, tasks |
| Server SDK | Registries, typed tool helpers, prompt/resource handlers, task-aware tool calls, notifications |
| Peer/service boundary | RMCP-like role-aware `Peer<Role>` and `Service<Role>` public shape |
| Transports | stdio, process stdio, Streamable HTTP, legacy SSE compatibility paths |
| Packaging | Exported CMake targets, install tree support, package-smoke fixture |
| Runtime tools | Optional app, gateway, and CLI layers above the SDK |

## Using As A Library

Installed-package usage should look like a normal CMake SDK:

`Peer` and `Service` remain the application entry points. The `client` and
`server` package targets provide the embeddable SDK layers behind those entry
points, not a separate first-choice architecture.

```cmake
find_package(cxxmcp CONFIG REQUIRED)

add_executable(my_client main.cpp)
target_link_libraries(my_client PRIVATE cxxmcp::client)

add_executable(my_server server.cpp)
target_link_libraries(my_server PRIVATE cxxmcp::server)
```

Use `cxxmcp::sdk` when one target should pull in the public protocol, client,
and server SDK layers.

Common public headers:

```cpp
#include <cxxmcp/protocol.hpp>
#include <cxxmcp/request.hpp>
#include <cxxmcp/transport.hpp>
#include <cxxmcp/handler.hpp>
#include <cxxmcp/peer.hpp>
#include <cxxmcp/service.hpp>
#include <cxxmcp/client.hpp>
#include <cxxmcp/server.hpp>
#include <cxxmcp/sdk.hpp>
```

## Build From Source

Requirements:

- CMake 3.23+
- A C++17 compiler for SDK targets
- A C++20 compiler when building optional runtime, CLI, examples, or tests

Default SDK build:

```powershell
cmake -S . -B build
cmake --build build
```

Build client and server SDKs explicitly:

```powershell
cmake -S . -B build-sdk -DCXXMCP_BUILD_CLIENT=ON -DCXXMCP_BUILD_SERVER=ON
cmake --build build-sdk
```

Build examples:

```powershell
cmake --preset examples
cmake --build --preset examples
```

Build and run the full smoke test set:

```powershell
cmake -S . -B build-smoke -DCXXMCP_BUILD_SDK=ON -DCXXMCP_BUILD_CLIENT=ON -DCXXMCP_BUILD_SERVER=ON -DCXXMCP_BUILD_RUNTIME=ON -DCXXMCP_BUILD_TESTS=ON
cmake --build build-smoke --config Debug
ctest --test-dir build-smoke -C Debug --output-on-failure
```

Install to a local prefix:

```powershell
cmake --install build-smoke --config Debug --prefix out/install/cxxmcp
```

## Quick Start

### Canonical Server Peer/Service

```cpp
#include <iostream>
#include <memory>
#include <utility>

#include <cxxmcp/peer.hpp>
#include <cxxmcp/server.hpp>
#include <cxxmcp/service.hpp>
#include <cxxmcp/transport/stdio_transport.hpp>

int main() {
    mcp::server::ServerBuilder builder;
    builder.name("demo-server")
        .version("1.0.0")
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

    mcp::ServerPeer peer(std::move(*server));
    peer.add_transport(
        std::make_unique<mcp::transport::ServerStdioTransport>(
            std::cin, std::cout));

    auto running = mcp::serve(std::move(peer));
    if (!running) {
        return 1;
    }

    return running->wait().has_value() ? 0 : 1;
}
```

### Canonical Client Peer/Service

```cpp
#include <memory>
#include <utility>

#include <cxxmcp/peer.hpp>
#include <cxxmcp/service.hpp>
#include <cxxmcp/transport/http_transport.hpp>

int main() {
    auto transport =
        std::make_unique<mcp::transport::StreamableHttpClientTransport>(
            mcp::transport::StreamableHttpClientTransportOptions{
                .host = "127.0.0.1",
                .port = 3000,
                .path = "/mcp",
            });
    mcp::ClientPeer peer(std::move(transport));

    auto running = mcp::serve(std::move(peer));
    if (!running) {
        return 1;
    }

    running->peer().initialize();
    running->peer().list_all_tools();
    running->peer().call_tool("echo", mcp::protocol::Json{{"value", "hello"}});
    running->stop();
}
```

### Compatibility App Builder

The `server::App::builder()` path is a convenience wrapper for compact demos and
legacy code. New SDK documentation and examples should use `Peer` and `Service`
first.

```cpp
#include <string>

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

## Package Targets

| Target | Purpose |
|---|---|
| `cxxmcp::protocol` | MCP protocol models and JSON-RPC serialization |
| `cxxmcp::transport` | Role-generic transport contracts and shared transport helpers |
| `cxxmcp::handler` | Client/server handler interfaces and aggregates |
| `cxxmcp::peer` | Role-aware client/server execution boundary |
| `cxxmcp::service` | Service lifecycle boundary around peers |
| `cxxmcp::client` | Embeddable MCP client SDK |
| `cxxmcp::server` | Embeddable MCP server SDK |
| `cxxmcp::sdk` | Aggregate public SDK target |
| `cxxmcp::runtime` | Optional runtime application layer |
| `cxxmcp::gateway` | Optional local gateway layer |
| `cxxmcp::cli` | Optional command-line tool |
| `cxxmcp::plugin_sdk` | Optional plugin authoring surface |
| `cxxmcp::adapters` | Optional adapter helpers |

Runtime state, gateway profiles, policy, and CLI defaults are not part of the
core SDK contract.

## Protocol Boundary

cxxmcp follows the MCP JSON-RPC wire shape. It does not define a custom MCP
dialect or alternate wire format. Normal application code should use typed
helpers for tools, prompts, resources, completion, roots, sampling,
elicitation, tasks, progress, and cancellation. Raw JSON-RPC request and
notification APIs remain available for vendor-specific methods, forward
compatibility, and conformance tests. Unusual runtime integrations should be
implemented through compatibility adapters over the public transport contracts,
not by extending the protocol.

Tasks and elicitation are exposed as typed SDK capabilities, but they remain
optional feature families. A milestone that targets core MCP parity must not
force applications to implement task or elicitation handlers unless that
milestone explicitly covers those capabilities. Capability negotiation and raw
JSON-RPC escape hatches remain the compatibility path for partial or future
feature support.

## Protocol Version Policy

cxxmcp tracks published MCP protocol snapshots and does not mint custom
versions. The SDK advertises and validates only versions listed by
`protocol::supported_protocol_versions()`.

When a new MCP snapshot is added, cxxmcp keeps the previous supported snapshot
for at least one minor release so clients and servers can overlap during
rollout. Dropping a snapshot is a breaking compatibility event and must be
called out in release notes and the public compatibility checklist.

Unsupported versions fail fast with a protocol or transport validation error;
the SDK does not silently negotiate down to an unadvertised dialect. HTTP
requests additionally require `MCP-Protocol-Version` after initialize, and
initialize requests with mismatched header/body versions are rejected.

## HTTP Transport Policy

Streamable HTTP is the default HTTP path. The server transport currently runs in
stateful mode: each successful `initialize` creates a distinct
`Mcp-Session-Id`, and later POST, GET/SSE, and DELETE requests must send that
session id plus `MCP-Protocol-Version`. Unknown or deleted sessions are treated
as stale and rejected.

Stateless server mode is not currently advertised by `server::HttpTransport`.
Servers that need Streamable HTTP in this SDK should use the stateful session
contract above. The client transport can still consume simple HTTP MCP
endpoints that do not return `Mcp-Session-Id`; in that case it omits session
headers and treats each POST response independently.

Server-to-client requests, notifications, client capabilities, replay windows,
and pending responses are tracked per session. `SessionContext::client()` binds
the returned `ClientPeer` to the active session, so roots, sampling,
elicitation, cancellation, and progress notifications are routed to the correct
HTTP client. One live real-time SSE stream is accepted per session; reconnects
with `Last-Event-ID` can replay retained events while an old stream is closing.

Legacy SSE compatibility is compatibility-only. New code should use the
Streamable HTTP POST/GET/DELETE behavior and treat raw SSE endpoints as an
adapter concern, not a separate SDK protocol.

## CMake Options

| Option | Default | Description |
|---|---:|---|
| `CXXMCP_BUILD_SDK` | `ON` | Build the aggregate public SDK layer |
| `CXXMCP_BUILD_PROTOCOL` | `ON` | Build the MCP protocol library |
| `CXXMCP_BUILD_CLIENT` | `OFF` | Build the MCP client library |
| `CXXMCP_BUILD_SERVER` | `OFF` | Build the MCP server library |
| `CXXMCP_BUILD_RUNTIME` | `OFF` | Build the runtime application layer |
| `CXXMCP_BUILD_APP` | `OFF` | Build the application service library |
| `CXXMCP_BUILD_GATEWAY` | `OFF` | Build the gateway service library |
| `CXXMCP_BUILD_CLI` | `OFF` | Build the command-line application |
| `CXXMCP_BUILD_EXAMPLES` | `OFF` | Build example executables |
| `CXXMCP_BUILD_TESTS` | `BUILD_TESTING` | Build tests for enabled layers |
| `CXXMCP_BUILD_DOCS` | `OFF` | Build Doxygen API documentation |

`CXXMCP_BUILD_SDK` enables the protocol, client, and server layers.
`CXXMCP_BUILD_CLI` enables the gateway, runtime, server, client, and protocol
layers it needs.

## Compatibility Contract

- Public SDK headers and package targets compile as C++17 by default. The
  configurable `CXXMCP_SDK_CXX_STANDARD` cache value may be raised by a
  downstream build, but public headers must not require it.
- The release compiler matrix is Windows/MSVC, Linux/GCC, Linux/Clang, and
  macOS/AppleClang. A release may only claim support for matrix entries that
  passed the public-header, package-smoke, and conformance gates for that
  release.
- Public include paths stay under `cxxmcp/...`; public targets are
  `cxxmcp::protocol`, `cxxmcp::transport`, `cxxmcp::handler`,
  `cxxmcp::peer`, `cxxmcp::service`, `cxxmcp::client`, `cxxmcp::server`, and
  `cxxmcp::sdk`.
- Source compatibility follows semantic versioning. Public renames must add the
  new name first, keep the old alias with `CXXMCP_DEPRECATED("message")`,
  document the migration, and remove the alias only in the next major release.
- ABI stability is explicitly out of scope while cxxmcp ships static libraries
  by default. A shared-library ABI policy must be defined before treating shared
  builds as stable release artifacts.
- Release review must include a public header diff, independent public-header
  compile tests, installed-tree `package_smoke`, and the conformance matrix
  available for that release.
- The full compatibility policy is tracked in
  [Compatibility policy](docs/compatibility_policy.md). Release-blocking tests,
  labels, and the supported compiler/generator/runtime matrix are tracked in
  [Release gates](docs/release_gates.md).

## Examples

The examples preset builds representative SDK entry points:

- First-choice Peer/Service examples: `server_peer`, `client_peer`,
  `process_stdio_client`
- Compatibility and low-level examples: `stdio_server`, `typed_stdio_server`,
  `client_loopback`, `task_async_client_server`
- Optional runtime tooling example: `gateway_runtime`

## Quality Bar

The repository keeps SDK-grade checks close to the source tree:

- protocol, client/server, transport, SDK, and public target tests
- package-smoke fixture for installed CMake target consumption
- local RMCP conformance coverage
- examples build preset
- formatting, cpplint, clang-tidy, Doxygen, and release-evidence scripts

Current standardization work is tracked in [Fact-standard TODO](todo.md).

## Documentation

- [Fact-standard TODO](todo.md)
- [Compatibility policy](docs/compatibility_policy.md)
- [Release gates](docs/release_gates.md)
- [Release candidate checklist](docs/release_candidate_checklist.md)
- [Release notes template](docs/release_notes_template.md)
- [Peer/Service migration guide](docs/sdk_peer_service_migration.md)
- [Changelog](CHANGELOG.md)

## Project Status

`cxxmcp` is an MCP C++ SDK with an RMCP-like public architecture and strong
standard-SDK potential. The SDK-first shape, Peer/Service boundary, built-in
transport behavior, and cross-SDK conformance gates are in strong release
candidate shape. Do not claim fact-standard status until the release-gates
matrix has produced auditable artifacts and the release candidate checklist has
been completed for the exact release commit.
