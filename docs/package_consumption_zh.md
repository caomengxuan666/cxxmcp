# 包消费方式

这份文档覆盖中心包仓库之外的轻量消费路径。这里的路径都是 SDK-only：
runtime、gateway、CLI、GUI 以及它们的依赖不属于 SDK package contract。

## 依赖策略

SDK 支持两种依赖模式：

- 默认源码包 / archive 构建使用仓库内 bundled header-only SDK 依赖，让
  FetchContent、CPM.cmake 和直接源码安装不依赖包管理器。安装树会包含
  `tl/expected.hpp`、`nlohmann/json.hpp`，以及
  `cxxmcp/third_party/jsonrpcpp/jsonrpcpp.hpp` 下的 jsonrpcpp 实现头。
- 注册表包构建应设置 `CXXMCP_USE_SYSTEM_DEPS=ON`，并使用包管理器提供的
  `tl-expected`、`nlohmann-json` 和 `cpp-httplib`。这种模式下安装树不能再
  vendor `tl` 或 `nlohmann` 头文件。

`jsonrpcpp` 仍然是仓库内实现细节，因为当前项目维护的是一个小的 patched
single-header copy，而不是依赖外部 registry package。它安装在
`cxxmcp/third_party` include 前缀下，供导出的 CMake targets 使用，但不占用顶层
public include namespace。

`cpp-httplib` 是 transport 实现依赖，不作为 public SDK header 安装。下游代码应
包含 `cxxmcp/transport/http_transport.hpp`、
`cxxmcp/client/http_transport.hpp` 或 `cxxmcp/server/http_transport.hpp`，而不是
直接包含 `httplib.h`。

spdlog、CLI11 等 runtime/tooling 依赖不属于 SDK package contract。面向
vcpkg/Conan 的 SDK 包应保持 runtime、gateway、CLI、examples、tests 和 docs
关闭，除非后续创建单独的 tools 包。

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

## 窄 SDK Targets

按当前二进制实际需要选择最窄的 public target。这样可以让传递依赖更可控，也避免
把未使用的 SDK 层带进下游构建。

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

- `cxxmcp::protocol` 只用于协议模型、JSON-RPC envelope 和序列化辅助函数。
- `cxxmcp::client` 用于嵌入式 MCP client；它会带上 client transport 所需的
  protocol 和 transport SDK 层。
- `cxxmcp::server` 用于嵌入式 MCP server；它会带上 server transport 所需的
  protocol 和 transport SDK 层。
- `cxxmcp::sdk` 只在一个 target 明确同时需要 protocol、client、server API
  时使用，比如 loopback 测试或 SDK 示例。

## xmake-repo

xmake-repo recipe 草案在：

```text
packaging/xmake/packages/c/cxxmcp/xmake.lua
```

它构建同一个 SDK source archive，并关闭 runtime、gateway、CLI、examples、
tests 和 docs。等 package interface 稳定后，可以把它提交到 xmake-repo。
