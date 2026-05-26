# Package Consumption

This document covers lightweight consumption paths that are useful before or
outside central package registries. They are SDK-only paths: runtime, gateway,
CLI, GUI, and their dependencies are not part of the SDK package contract.

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

## xmake-repo

The xmake-repo recipe draft lives at:

```text
packaging/xmake/packages/c/cxxmcp/xmake.lua
```

It builds the same SDK source archive and disables runtime, gateway, CLI,
examples, tests, and docs. Submit it to xmake-repo after the package interface
is stable enough for registry review.
