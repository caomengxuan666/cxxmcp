# cxxmcp

`cxxmcp` 是一个面向 MCP 的 C++ SDK，提供 `protocol`、`client`、`server`，并可按需启用 `app`、`gateway` 和 `CLI`。

## 特性

- `protocol`：JSON-RPC 与 MCP 模型序列化
- `client` / `server`：MCP 客户端和服务端库
- 传输支持：stdio、HTTP
- 可选运行层：上游 server 管理、发现、受控暴露
- 可选工具：`cxxmcp` 命令、examples、tests

## 构建

默认只构建 `protocol`。

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
