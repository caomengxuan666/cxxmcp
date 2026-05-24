# Recommended Project Layout

This document proposes a project layout that makes the repository usable as both:

- a third-party MCP SDK library
- a runtime/gateway product
- a CLI application

The goal is to make the SDK clean and installable without forcing third-party users to depend on gateway, CLI, runtime state, or repository-local paths.

## Principle

The repository should be split into three clear layers:

1. SDK core
2. Runtime and gateway
3. CLI and tools

Dependency direction must be one-way:

```text
cli -> runtime/gateway -> sdk
```

The SDK must not depend on the runtime, gateway, or CLI.

## Target Layout

```text
MCPServer.cpp/
  sdk/
    include/
      mcp/
        protocol/
        transport/
        peer/
        handler/
        client/
        server/
        task/
        protocol.hpp
        client.hpp
        server.hpp
        transport.hpp
        peer.hpp
    src/
      protocol/
      transport/
        stdio/
        process/
        http_httplib/
      peer/
      client/
      server/
      task/

  runtime/
    include/
      mcp/
        runtime/
        gateway/
        registry/
        policy/
        state/
    src/
      gateway/
      registry/
      policy/
      state/
      discovery/
      observability/

  cli/
    src/

  adapters/
    filesystem/
    shell/
    git/
    internal_rest/

  plugin-sdk/
    include/
    src/

  examples/
    sdk/
    gateway/
    cli/

  tests/
    sdk/
    runtime/
    cli/
    integration/

  docs/
  reference/
  third_party/
  scripts/
  CMakeLists.txt
```

## SDK Layer

The SDK layer is what third-party users should consume.

It should contain:

- protocol models
- JSON-RPC serialization
- transport abstraction
- stdio transport
- process stdio transport
- HTTP transport adapter
- peer abstraction
- client and server facade
- handler interfaces
- task protocol models

It should not contain:

- gateway profiles
- trust policy
- exposure binding
- CLI state
- import/export workflow
- runtime state directory defaults
- product-specific registry logic

The public include surface should be stable:

```cpp
#include <mcp/protocol.hpp>
#include <mcp/client.hpp>
#include <mcp/server.hpp>
#include <mcp/transport.hpp>
#include <mcp/peer.hpp>
```

Third-party users should not need to know the repository's internal source layout.

## Runtime and Gateway Layer

The runtime layer is product-specific and can be heavier than the SDK.

It should contain:

- upstream server registry
- gateway profiles
- exposure bindings
- discovery
- trust and approval policy
- auth and rate limiting
- persistent state
- readiness checks
- observability

The runtime can depend on the SDK.

The SDK must not depend on the runtime.

## CLI Layer

The CLI should be a consumer of the runtime layer.

It can provide:

- server management commands
- gateway commands
- import/export commands
- profile inspection
- readiness checks
- diagnostic output

The CLI may default to a local state directory such as `.mcp-runtime`.

The SDK must not do that.

## Adapters

Adapters are tool or service integrations.

Examples:

- filesystem
- shell
- git
- internal REST services

Adapters should not be part of the SDK core dependency chain.

They can be used by the gateway/runtime layer or examples.

## Reference Code

The `reference/` directory is for source comparison only.

It should not:

- be installed
- be included in default builds
- be linked into SDK targets
- affect third-party consumers

Recommended behavior:

```cmake
CXXMCP_BUILD_REFERENCE=OFF
```

## CMake Targets

Recommended SDK targets:

```text
cxxmcp::protocol
cxxmcp::transport
cxxmcp::peer
cxxmcp::client
cxxmcp::server
cxxmcp::sdk
```

Recommended runtime targets:

```text
cxxmcp::runtime
cxxmcp::gateway
```

Recommended CLI target:

```text
cxxmcp::cli
```

Example third-party usage:

```cmake
find_package(cxxmcp CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE cxxmcp::client)
```

That should not link gateway or CLI code.

## Build Options

Recommended build options:

```cmake
CXXMCP_BUILD_SDK=ON
CXXMCP_BUILD_RUNTIME=ON
CXXMCP_BUILD_CLI=ON
CXXMCP_BUILD_EXAMPLES=OFF
CXXMCP_BUILD_TESTS=OFF
CXXMCP_BUILD_REFERENCE=OFF
```

For third-party library consumption, users should be able to configure:

```cmake
CXXMCP_BUILD_SDK=ON
CXXMCP_BUILD_RUNTIME=OFF
CXXMCP_BUILD_CLI=OFF
CXXMCP_BUILD_EXAMPLES=OFF
CXXMCP_BUILD_TESTS=OFF
CXXMCP_BUILD_REFERENCE=OFF
```

## Install Layout

The installed SDK should look like:

```text
include/
  mcp/
    protocol.hpp
    client.hpp
    server.hpp
    transport.hpp
    peer.hpp
    protocol/
    client/
    server/
    transport/
    handler/
    task/

lib/
  cmake/
    cxxmcp/
      cxxmcpConfig.cmake
      cxxmcpTargets.cmake
```

The installed SDK should not include:

- `reference/`
- CLI-only headers
- gateway-only private headers
- test fixtures
- examples as required runtime files

## Current Working Directory Rule

The SDK must not depend on the process current working directory.

The SDK should not:

- write `.mcp-runtime`
- read default gateway config from the current directory
- resolve resources relative to the repository root
- require callers to run from a specific directory

The runtime and CLI may provide current-directory defaults, but those defaults must be explicit product behavior and overrideable through options.

For process stdio transports:

- `cwd` should be explicit when possible
- if omitted, inheriting the parent process cwd should be documented
- SDK code should not silently assume the repository root

## Migration Plan

### Phase 1: Create SDK Boundary

- introduce `sdk/`
- move or mirror public SDK headers under `sdk/include/mcp`
- keep existing include paths working through compatibility forwarding headers

### Phase 2: Split Runtime

- move gateway, registry, policy, state, and discovery code under `runtime/`
- make runtime depend on SDK targets only
- remove runtime dependencies from SDK headers

### Phase 3: Split CLI

- make CLI depend on runtime and SDK
- keep CLI state-dir defaults out of SDK code
- keep CLI-only formatting and command dependencies out of SDK targets

### Phase 4: Export CMake Targets

- export SDK targets separately from runtime and CLI targets
- add install rules for SDK headers and libraries
- add `find_package(cxxmcp CONFIG REQUIRED)` support

### Phase 5: Clean Public Includes

- keep public includes stable
- move private headers out of installed include trees
- document third-party usage examples

## Compatibility Strategy

Avoid breaking existing code immediately.

Use forwarding headers where needed:

```cpp
// old path
#include <mcp/client.hpp>

// internally forwards to the new SDK layout
```

Keep current `Client` and `Server` classes as compatibility wrappers while introducing the RMCP-like peer and handler layer.

## Summary

The project should become:

```text
sdk core
  reusable third-party library

runtime/gateway
  product orchestration over the SDK

cli
  command-line product surface over runtime/gateway
```

This keeps the SDK clean for external users while preserving the richer gateway and CLI product layers.
