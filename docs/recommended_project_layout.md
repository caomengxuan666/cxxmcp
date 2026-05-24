# Recommended Project Layout

This document proposes a project layout that makes the repository usable primarily as:

- a third-party MCP SDK library
- optional runtime/gateway tools
- an optional CLI application

The goal is to make the SDK clean and installable without forcing third-party users to depend on gateway, CLI, runtime state, or repository-local paths.

## Product Position

The external positioning should be:

```text
cxxmcp is a C++ MCP SDK.
```

The first public story should be `protocol`, `transport`, `handler`, and `peer`. `client` and `server` remain useful compatibility and convenience wrappers. `runtime`, `gateway`, `cli`, `adapters`, and `extensions/plugin-sdk` are useful layers, but they should be presented as optional tools or extensions built on top of the SDK.

This distinction matters more than the physical folder names. A user evaluating the project should quickly see:

- stable public headers
- installable CMake targets
- small examples that embed the SDK
- compatibility and versioning rules
- runtime/gateway tools as secondary consumers of the SDK

They should not have to understand gateway profiles, local state, policy, or CLI workflows before using `cxxmcp::client` or `cxxmcp::server`.

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

The public narrative should be even narrower:

```text
core SDK:      protocol / client / server
runtime tools: gateway / cli / adapters
internal:      runtime internals, tests, reference code
```

`runtime` and gateway internals may stay in the repository, but they should not be part of the main SDK package story.

## Practical Recommendation

The current repository layout is usable, but it should continue to read more like a clean SDK package from the outside.

The best short-term approach is:

1. Keep the SDK source tree under `sdk/` and do not reintroduce root-level `client/`, `server/`, `protocol/`, or `core/` folders.
2. Treat `protocol`, `client`, and `server` as the SDK core.
3. Treat `gateway` and `cli` as optional runtime tools, not part of the core SDK story.
4. Keep `runtime` as the home for product runtime logic and gateway internals.
5. Keep `extensions/plugin-sdk` as an optional extension layer until the plugin-loading story is complete.
6. Keep `reference/` out of install and default build paths.
7. Keep README first-screen content focused on library usage, not internal architecture.
8. Keep `find_package(cxxmcp)` and `target_link_libraries(... cxxmcp::client)` as the intended consumption experience.

In other words, do not do a large directory move first. Make the public package shape SDK-first first, then simplify the physical tree only if it still helps.

## Target Layout

```text
MCPServer.cpp/
  sdk/
    include/
      cxxmcp/
        protocol/
        transport/
        peer/
        handler/
        service/
        client/
        server/
        protocol.hpp
        request.hpp
        sdk.hpp
        client.hpp
        server.hpp
        transport.hpp
        peer.hpp
        service.hpp
    core/
      include/
        cxxmcp/
          core/
            result.hpp
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
      cxxmcp/
        runtime/
        gateway/
        registry/
        policy/
        state/
    observability/
      include/
    src/
      gateway/
      registry/
      policy/
      state/
      discovery/
      observability/

  tools/
    cli/
      src/
      include/

  extensions/
    adapters/
      filesystem/
      shell/
      git/
      internal_rest/
      include/
      src/
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
#include <cxxmcp/protocol.hpp>
#include <cxxmcp/request.hpp>
#include <cxxmcp/sdk.hpp>
#include <cxxmcp/client.hpp>
#include <cxxmcp/server.hpp>
#include <cxxmcp/transport.hpp>
#include <cxxmcp/peer.hpp>
#include <cxxmcp/service.hpp>
```

Third-party users should not need to know the repository's internal source layout.

Recommended CMake usage:

```cmake
find_package(cxxmcp CONFIG REQUIRED)

target_link_libraries(my_mcp_client PRIVATE cxxmcp::client)
target_link_libraries(my_mcp_server PRIVATE cxxmcp::server)
```

The aggregate `cxxmcp::sdk` target is convenient for examples and small applications. Library consumers that only need one side should prefer the narrower target.
Extension layers should also be linkable as first-class package targets, such as `cxxmcp::plugin_sdk` and `cxxmcp::adapters`, when a project needs to build on the SDK without depending on runtime tools.

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

The runtime should be documented as an optional tool layer. It is allowed to be product-shaped and opinionated because it is not the SDK contract.

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
cxxmcp::handler
cxxmcp::peer
cxxmcp::service
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

## Public Packaging Rules

The SDK should feel stable, narrow, and embeddable:

- public headers live under `cxxmcp/`
- installed targets use the `cxxmcp::` namespace
- `protocol`, `client`, and `server` can be linked independently
- `sdk` is an aggregate target, not a requirement
- gateway, CLI, tests, examples, and reference code are not installed as part of the core SDK
- API stability policy is documented before a public release

The repository can contain more than the SDK, but the package consumed by third parties should look like a library first.

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
  cxxmcp/
    protocol.hpp
    request.hpp
    sdk.hpp
    client.hpp
    server.hpp
    transport.hpp
    peer.hpp
    core/
    protocol/
    client/
    server/
    transport/
    handler/

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
- keep public SDK headers under `sdk/include/cxxmcp`
- keep `sdk/core/include/cxxmcp/core` for shared helpers
- avoid reintroducing root-level SDK folders

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
