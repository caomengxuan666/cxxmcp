# cxxmcp

`cxxmcp` 是一个 C++ MCP SDK。

核心包只强调稳定、窄、可嵌入的 SDK 能力：MCP 协议模型、客户端库、服务端库、传输适配，以及 RMCP 风格的 peer / handler facade。`app`、`gateway` 和 CLI 是构建在 SDK 之上的可选运行时工具，不是主叙事。

## 特性

- `protocol`：JSON-RPC 与 MCP 模型序列化
- `client` / `server`：MCP 客户端和服务端库
- 传输支持：stdio、HTTP
- 可选运行时工具：上游 server 管理、发现、受控暴露
- 可选工具：`cxxmcp` 命令、examples、tests

## 作为库使用

安装后的 CMake 使用方式应该像普通 SDK：

```cmake
find_package(cxxmcp CONFIG REQUIRED)

target_link_libraries(my_client PRIVATE cxxmcp::client)
target_link_libraries(my_server PRIVATE cxxmcp::server)
```

只有在同时需要 protocol / client / server 时，才使用聚合目标 `cxxmcp::sdk`。

## 构建

默认构建 core SDK。

```powershell
cmake -S . -B build
cmake --build build
```

构建 client / server：

```powershell
cmake -S . -B build-sdk -DMCP_BUILD_CLIENT=ON -DMCP_BUILD_SERVER=ON
cmake --build build-sdk
```

构建 gateway / CLI：

```powershell
cmake -S . -B build-cli -DMCP_BUILD_CLI=ON
cmake --build build-cli
```

运行测试：

```powershell
cmake -S . -B build-tests -DMCP_BUILD_CLIENT=ON -DMCP_BUILD_SERVER=ON -DMCP_BUILD_APP=ON -DMCP_BUILD_GATEWAY=ON -DMCP_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## 快速开始

### Server

```cpp
#include <mcp/server.hpp>

int main() {
    return mcp::server::App::builder()
        .name("demo-server")
        .version("1.0.0")
        .stdio()
        .tool<std::string, std::string>("echo", [](std::string text) {
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
    const auto tools = client.list_all_tools();
    (void)tools;
}
```

## 架构文档

- [SDK 设计指导](docs/rmcp_like_sdk_guidance.md)
- [高层 API 草案](docs/high_level_api.md)
- [传输策略](docs/httplib_async_transport_strategy.md)
- [能力矩阵](docs/capability_matrix.md)
- [推荐项目布局](docs/recommended_project_layout.md)

## 发布形态

对外发布时按三层理解：

- core SDK：`cxxmcp::protocol`、`cxxmcp::client`、`cxxmcp::server`、`cxxmcp::sdk`
- runtime tools：gateway / app services / `cxxmcp` CLI
- internal/reference：tests、examples、本地参考源码

公开 SDK 头文件位于 `mcp/`。运行时状态、gateway profile、policy 和 CLI 默认目录不进入 core SDK 契约。
