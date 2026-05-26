# 包消费方式

这份文档覆盖中心包仓库之外的轻量消费路径。这里的路径都是 SDK-only：
runtime、gateway、CLI、GUI 以及它们的依赖不属于 SDK package contract。

## FetchContent

优先使用 release workflow 生成的 SDK source archive，而不是 GitHub 自动生成的
源码包。SDK archive 包含默认 bundled 构建需要的 header-only SDK 依赖；GitHub
自动源码包不包含 submodule 内容。

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

`CPM.cmake` 可以消费同一个 SDK source archive。显式关闭工具层选项，避免下游
构建意外拉入 runtime 或 tools。

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

xmake-repo recipe 草案在：

```text
packaging/xmake/packages/c/cxxmcp/xmake.lua
```

它构建同一个 SDK source archive，并关闭 runtime、gateway、CLI、examples、
tests 和 docs。等 package interface 稳定后，可以把它提交到 xmake-repo。
