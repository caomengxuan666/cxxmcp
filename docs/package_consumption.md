# Package Consumption

This document covers lightweight consumption paths that are useful before or
outside central package registries. They are SDK-only paths: runtime, gateway,
CLI, GUI, and their dependencies are not part of the SDK package contract.

## Dependency Policy

The SDK has two supported dependency modes:

- Default source/archive builds use bundled header-only SDK dependencies so
  FetchContent, CPM.cmake, and direct source installs work without a package
  manager. The install tree includes `tl/expected.hpp`, `nlohmann/json.hpp`,
  and the jsonrpcpp implementation header under
  `cxxmcp/third_party/jsonrpcpp/jsonrpcpp.hpp`.
- Registry builds should set `CXXMCP_USE_SYSTEM_DEPS=ON` and use package
  manager dependencies for `tl-expected`, `nlohmann-json`, and `cpp-httplib`.
  In this mode the install tree must not vendor `tl` or `nlohmann` headers.

`jsonrpcpp` remains an in-tree implementation detail because this project keeps
a small patched single-header copy rather than depending on an external package
registry entry. It is installed under the `cxxmcp/third_party` include prefix so
exported CMake targets can consume it without claiming a top-level public
include namespace.

`cpp-httplib` is a transport implementation dependency. It is intentionally not
installed as a public SDK header. Downstream code should use
`cxxmcp/transport/http_transport.hpp`, `cxxmcp/client/http_transport.hpp`, or
`cxxmcp/server/http_transport.hpp` instead of including `httplib.h`.

Runtime/tooling dependencies such as spdlog and CLI11 are outside the SDK
package contract. vcpkg/Conan package submissions for the SDK should keep
runtime, gateway, CLI, examples, tests, and docs disabled unless a separate
tools package is created.

## FetchContent

Prefer the SDK source release archive over GitHub's generated source archive.
The SDK archive includes the header-only SDK dependencies needed by the default
bundled build, while GitHub generated archives do not include submodule
contents.

```cmake
include(FetchContent)

FetchContent_Declare(
    cxxmcp
    URL https://github.com/caomengxuan666/cxxmcp/releases/download/v2.0.2/cxxmcp-sdk-source-v2.0.2.tar.gz
    URL_HASH SHA256=3c4ad678a8612183a4f2539973328b6a85dab360991a86e6328ca032cc5e2ba8
)

set(CXXMCP_BUILD_SDK ON CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_RUNTIME OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_APP OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_GATEWAY OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cxxmcp)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE cxxmcp::server)
```

## CPM.cmake

`CPM.cmake` can consume the same SDK source archive. Keep the SDK options
explicit so downstream builds do not accidentally pull runtime or tools.

```cmake
include(cmake/CPM.cmake)

set(CXXMCP_BUILD_SDK ON CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_RUNTIME OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_APP OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_GATEWAY OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_DOCS OFF CACHE BOOL "" FORCE)

CPMAddPackage(
    NAME cxxmcp
    URL https://github.com/caomengxuan666/cxxmcp/releases/download/v2.0.2/cxxmcp-sdk-source-v2.0.2.tar.gz
    URL_HASH SHA256=3c4ad678a8612183a4f2539973328b6a85dab360991a86e6328ca032cc5e2ba8
)

add_executable(my_client main.cpp)
target_link_libraries(my_client PRIVATE cxxmcp::client)
```

## Narrow SDK Targets

Choose the narrowest public target that matches the binary you are building.
This keeps transitive dependencies predictable and avoids pulling SDK layers
that the consumer does not use.

```cmake
find_package(cxxmcp CONFIG REQUIRED)

add_library(protocol_only protocol_only.cpp)
target_link_libraries(protocol_only PRIVATE cxxmcp::protocol)

add_executable(my_client client_main.cpp)
target_link_libraries(my_client PRIVATE cxxmcp::client)

add_executable(my_server server_main.cpp)
target_link_libraries(my_server PRIVATE cxxmcp::server)

add_executable(loopback loopback.cpp)
target_link_libraries(loopback PRIVATE cxxmcp::sdk)
```

- Use `cxxmcp::protocol` for protocol models, JSON-RPC envelopes, and
  serialization helpers only.
- Use `cxxmcp::client` for an embeddable MCP client. It brings the protocol
  and transport SDK layers needed by client transports.
- Use `cxxmcp::server` for an embeddable MCP server. It brings the protocol
  and transport SDK layers needed by server transports.
- Use `cxxmcp::sdk` only when one target intentionally needs protocol, client,
  and server APIs together, such as loopback tests or SDK examples.

## xmake-repo

The xmake-repo recipe draft lives at:

```text
packaging/xmake/packages/c/cxxmcp/xmake.lua
```

It builds the same SDK source archive and disables runtime, gateway, CLI,
examples, tests, and docs. Submit it to xmake-repo after the package interface
is stable enough for registry review.
