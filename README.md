# cxxmcp

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C.svg)](https://cmake.org/)
[![Release gates](https://github.com/caomengxuan666/cxxmcp/actions/workflows/release-gates.yml/badge.svg)](https://github.com/caomengxuan666/cxxmcp/actions/workflows/release-gates.yml)
[![Pages](https://github.com/caomengxuan666/cxxmcp/actions/workflows/pages.yml/badge.svg)](https://caomengxuan666.github.io/cxxmcp/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![MCP](https://img.shields.io/badge/protocol-Model%20Context%20Protocol-111827.svg)](https://modelcontextprotocol.io/)

A production-ready C++17 SDK for the [Model Context Protocol](https://modelcontextprotocol.io/) — build MCP servers and clients that embed directly into native C++ applications, with full protocol coverage and cross-SDK conformance validation.

Read this in [Chinese](README_zh.md).

## Quick Start

```cmake
find_package(cxxmcp CONFIG REQUIRED)
target_link_libraries(my_server PRIVATE cxxmcp::server)
```

```cpp
#include <cxxmcp/peer.hpp>
#include <cxxmcp/run.hpp>

int main() {
    return mcp::ServerPeer::builder()
        .name("demo-server")
        .version("1.0.0")
        .stdio()
        .tool<mcp::protocol::Json, mcp::protocol::Json>("echo",
            [](const mcp::protocol::Json& input) {
                return mcp::protocol::Json{{"echo", input}};
            })
        .run();
}
```

```cpp
// Client side
#include <cxxmcp/peer.hpp>
#include <cxxmcp/run.hpp>

int main() {
    return mcp::ClientPeer::builder()
        .streamable_http("http://127.0.0.1:3000/mcp")
        .run([](auto& svc) {
            svc.peer().initialize();
            svc.peer().list_all_tools();
            svc.peer().call_tool("echo",
                                 mcp::protocol::Json{{"value", "hello"}});
        });
}
```

## What It Covers

| Area | Status |
|---|---|
| Protocol & JSON-RPC | Typed models, serialization, initialize validation, raw escape hatches |
| Server SDK | Tool/prompt/resource registries, typed handlers, task-aware calls, notifications |
| Client SDK | HTTP, stdio, process-stdio, async helpers, roots, sampling, elicitation, tasks |
| Transports | stdio, process stdio, Streamable HTTP (stateful sessions), legacy SSE compat |
| Packaging | CMake `find_package`, Conan 2, vcpkg overlay, FetchContent / CPM |
| Peer/Service boundary | RMCP-style role-aware `Peer<Role>` and `Service<Role>` |

**Protocol coverage:** tool, prompt, resource, resource template, completion, logging, roots, sampling, elicitation, task lifecycle, progress, cancellation, and raw JSON-RPC escape hatches for vendor extensions.

**Conformance:** Validated against the official `modelcontextprotocol/conformance` runner (`--suite all`).

| | cxxmcp | RMCP |
|---|---|---|
| Server | **108/109** (99%) | 48/95 (51%) |
| Client | **428/436** (98%) | — (runner crashed) |

Full details in [conformance evidence](docs/conformance_evidence.md).

## Install

```cmake
find_package(cxxmcp CONFIG REQUIRED)

# Server
target_link_libraries(my_server PRIVATE cxxmcp::server)
# Client
target_link_libraries(my_client PRIVATE cxxmcp::client)
# Everything
target_link_libraries(my_app PRIVATE cxxmcp::sdk)
```

Build from source:

```powershell
cmake -S . -B build -DCXXMCP_BUILD_CLIENT=ON -DCXXMCP_BUILD_SERVER=ON
cmake --build build --config Release
cmake --install build --config Release --prefix out/install/cxxmcp
```

Package managers: `conanfile.py` (Conan 2), `packaging/vcpkg/ports/cxxmcp` (vcpkg overlay), `packaging/xmake/` (xmake). See [package consumption](docs/package_consumption.md).

## CMake Options

| Option | Default | Description |
|---|---:|---|
| `CXXMCP_BUILD_SDK` | `ON` | Build the aggregate SDK layer (protocol + client + server) |
| `CXXMCP_BUILD_CLIENT` | `OFF` | Build the MCP client library |
| `CXXMCP_BUILD_SERVER` | `OFF` | Build the MCP server library |
| `CXXMCP_BUILD_EXAMPLES` | `OFF` | Build example executables |
| `CXXMCP_BUILD_TESTS` | `BUILD_TESTING` | Build tests |
| `CXXMCP_ENABLE_AUTH` | `OFF` | Build the optional OAuth 2.1 / DPoP auth target |

## Package Targets

| Target | Purpose |
|---|---|
| `cxxmcp::protocol` | MCP protocol models and JSON-RPC serialization |
| `cxxmcp::transport` | Role-generic transport contracts |
| `cxxmcp::handler` | Client/server handler interfaces |
| `cxxmcp::peer` | Role-aware execution boundary |
| `cxxmcp::service` | Service lifecycle boundary |
| `cxxmcp::client` | Embeddable MCP client SDK |
| `cxxmcp::server` | Embeddable MCP server SDK |
| `cxxmcp::auth` | Optional OAuth 2.1 / DPoP contract (`CXXMCP_ENABLE_AUTH=ON`) |
| `cxxmcp::sdk` | Aggregate public SDK target |

## Why cxxmcp

- Standard CMake SDK consumption — `find_package` and go
- C++17 public API, no runtime dependencies beyond the standard library
- Full MCP protocol coverage with typed, capability-gated helpers
- Cross-SDK conformance validation against the official runner
- RMCP-style Peer/Service architecture designed for embedding
- stdio, process-stdio, and Streamable HTTP transports out of the box

## Examples

In-tree examples cover server/client peers, auth, tasks, elicitation, and transport adapters. Run them with:

```powershell
cmake --preset examples && cmake --build --preset examples && ctest --preset examples
```

See [examples.md](docs/examples.md) for the full list. The separate [cxxmcp-examples](https://github.com/caomengxuan666/cxxmcp-examples) repository exercises the SDK through an external CMake project with advanced scenarios.

## Documentation

- [GitHub Pages](https://caomengxuan666.github.io/cxxmcp/) — API reference
- [Conformance evidence](docs/conformance_evidence.md) — test results and known exceptions
- [Compatibility policy](docs/compatibility_policy.md) — versioning, compiler matrix, ABI
- [HTTP transport backend evidence](docs/compatibility_policy.md#http-transport-backend-evidence)
- [Release gates](docs/release_gates.md) — release-blocking checks
- [Runtime gateway](docs/runtime_gateway.md) — external gateway boundary
- [Examples](docs/examples.md) — full example list
- [Auth design](docs/auth_design.md) — OAuth 2.1 / DPoP direction
- [Request lifecycle](docs/request_lifecycle.md) — timeout, cancellation, progress, shutdown
- [Contributing](CONTRIBUTING.md) | [Security](SECURITY.md) | [Changelog](CHANGELOG.md)

## Status

cxxmcp is a community C++ MCP SDK preparing official SDK candidate evidence. The Peer/Service boundary, transport layer, and conformance gates are in release-candidate shape. It is not an official MCP SDK unless accepted by the MCP maintainers.

### Compiler Compatibility

MinGW UCRT64 GCC and MinGW CLANG64 Clang are tracked as provisional, best-effort compiler compatibility evidence. These targets are not release-supported. The `compiler-compat` workflow runs them with `continue-on-error: true` while they remain provisional.
