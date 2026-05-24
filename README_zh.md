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

公开头文件使用 `cxxmcp/` 前缀。

## 构建

默认构建 core SDK。

```powershell
cmake -S . -B build
cmake --build build
```

构建 client / server：

```powershell
cmake -S . -B build-sdk -DCXXMCP_BUILD_CLIENT=ON -DCXXMCP_BUILD_SERVER=ON
cmake --build build-sdk
```

构建 gateway / CLI：

```powershell
cmake -S . -B build-cli -DCXXMCP_BUILD_CLI=ON
cmake --build build-cli
```

构建 examples：

```powershell
cmake --preset examples
cmake --build --preset examples
```

这个预设会在依赖满足时构建 stdio server、server peer、client peer、client loopback、process stdio client 和 gateway runtime 示例。

运行测试：

```powershell
cmake -S . -B build-tests -DCXXMCP_BUILD_CLIENT=ON -DCXXMCP_BUILD_SERVER=ON -DCXXMCP_BUILD_APP=ON -DCXXMCP_BUILD_GATEWAY=ON -DCXXMCP_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## 快速开始

### Server Peer

```cpp
#include <cxxmcp/peer.hpp>
#include <cxxmcp/server.hpp>

int main() {
    mcp::server::ServerBuilder builder;
    builder.name("demo-server")
        .version("1.0.0")
        .instructions("Expose local tools over MCP.")
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

    auto running = mcp::serve(mcp::ServerPeer(std::move(*server)));
    if (!running) {
        return 1;
    }
    return 0;
}
```

### Client Peer

```cpp
#include <cxxmcp/peer.hpp>
#include <cxxmcp/service.hpp>

int main() {
    auto peer = mcp::ClientPeer::connect_streamable_http({
        .host = "127.0.0.1",
        .port = 3000,
        .path = "/mcp",
    });

    auto running = mcp::serve(std::move(peer));
    if (!running) {
        return 1;
    }

    return running->peer().initialize().has_value() ? 0 : 1;
}
```

### 运行时 Server

这是更高层的 runtime builder 示例，适合产品型 server，但它不是 core 的 peer/handler 入口。

```cpp
#include <cxxmcp/server.hpp>

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
#include <cxxmcp/peer.hpp>

int main() {
    auto peer = mcp::Peer<mcp::RoleClient>::connect_streamable_http({
        .host = "127.0.0.1",
        .port = 3000,
        .path = "/mcp",
    });

    peer.initialize();
    const auto tools = peer.list_all_tools();
    (void)tools;
}
```

## 架构文档

- [SDK 设计指导](docs/rmcp_like_sdk_guidance.md)
- [发布策略](docs/release_policy.md)
- [更新日志](CHANGELOG.md)
- [高层 API 草案](docs/high_level_api.md)
- [事实标准路线](docs/de_facto_standard_roadmap.md)
- [传输策略](docs/httplib_async_transport_strategy.md)
- [能力矩阵](docs/capability_matrix.md)
- [推荐项目布局](docs/recommended_project_layout.md)

## 发布形态

对外发布时按三层理解：

- core SDK：`cxxmcp::protocol`、`cxxmcp::transport`、`cxxmcp::peer`、`cxxmcp::client`、`cxxmcp::server`、`cxxmcp::handler`、`cxxmcp::service`、`cxxmcp::sdk`
- runtime tools：gateway / runtime services / `cxxmcp` CLI
- internal/reference：tests、examples、本地参考源码

公开 SDK 头文件位于 `cxxmcp/`。运行时状态、gateway profile、policy 和 CLI 默认目录不进入 core SDK 契约。
