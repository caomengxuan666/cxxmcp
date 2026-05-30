# cxxmcp

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C.svg)](https://cmake.org/)
[![Release gates](https://github.com/caomengxuan666/cxxmcp/actions/workflows/release-gates.yml/badge.svg)](https://github.com/caomengxuan666/cxxmcp/actions/workflows/release-gates.yml)
[![Pages](https://github.com/caomengxuan666/cxxmcp/actions/workflows/pages.yml/badge.svg)](https://caomengxuan666.github.io/cxxmcp/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![MCP](https://img.shields.io/badge/protocol-Model%20Context%20Protocol-111827.svg)](https://modelcontextprotocol.io/)
[![Server Conformance](https://img.shields.io/badge/Server%20Conformance-109%2F110%20(99%25)-brightgreen.svg)](docs/conformance_evidence.md)
[![Client Conformance](https://img.shields.io/badge/Client%20Conformance-447%2F447%20(100%25)-brightgreen.svg)](docs/conformance_evidence.md)

生产就绪的 C++17 [Model Context Protocol](https://modelcontextprotocol.io/) SDK —— 直接在原生 C++ 应用中嵌入 MCP server 和 client，完整覆盖协议能力，通过跨 SDK conformance 验证。

English version: [README.md](README.md)

## 快速开始

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
// Client 端
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

## 能力覆盖

| 领域 | 状态 |
|---|---|
| Protocol / JSON-RPC | Typed models、序列化、initialize 校验、raw escape hatch |
| Server SDK | tool/prompt/resource registry、typed handler、task-aware call、notification |
| Client SDK | HTTP、stdio、process stdio、async helper、roots、sampling、elicitation、tasks |
| Transports | stdio、process stdio、Streamable HTTP（有状态 session）、legacy SSE 兼容 |
| Packaging | CMake `find_package`、Conan 2、vcpkg overlay、FetchContent / CPM |
| Peer/Service boundary | RMCP 风格 role-aware `Peer<Role>` 和 `Service<Role>` |

**协议覆盖：** tool、prompt、resource、resource template、completion、logging、roots、sampling、elicitation、task lifecycle、progress、cancellation，以及用于 vendor extension 的 raw JSON-RPC escape hatch。

**Conformance：** 基于官方 `modelcontextprotocol/conformance` runner（`--suite all`）验证。

| | cxxmcp | RMCP |
|---|---|---|
| Server | **109/110** (99%) | 48/95 (51%) |
| Client | **447/447** (100%) | — (runner 崩溃) |

完整结果见 [conformance evidence](docs/conformance_evidence.md)。

## 能力快照

cxxmcp 是具备完整协议覆盖、跨 SDK conformance 验证和多种 transport 选项的 release-candidate 质量 C++ MCP SDK。详见 [conformance evidence](docs/conformance_evidence.md) 和 [ecosystem maturity evidence](docs/ecosystem_maturity_evidence.md)。

## 安装

```cmake
find_package(cxxmcp CONFIG REQUIRED)

# Server
target_link_libraries(my_server PRIVATE cxxmcp::server)
# Client
target_link_libraries(my_client PRIVATE cxxmcp::client)
# 完整 SDK
target_link_libraries(my_app PRIVATE cxxmcp::sdk)
```

从源码构建：

```powershell
cmake -S . -B build -DCXXMCP_BUILD_CLIENT=ON -DCXXMCP_BUILD_SERVER=ON
cmake --build build --config Release
cmake --install build --config Release --prefix out/install/cxxmcp
```

包管理器：`conanfile.py`（Conan 2）、`packaging/vcpkg/ports/cxxmcp`（vcpkg overlay）、`packaging/xmake/`（xmake）。详见 [package consumption](docs/package_consumption_zh.md)。

## CMake Options

| Option | Default | 说明 |
|---|---:|---|
| `CXXMCP_BUILD_SDK` | `ON` | 构建聚合 SDK 层（protocol + client + server） |
| `CXXMCP_BUILD_CLIENT` | `OFF` | 构建 MCP client library |
| `CXXMCP_BUILD_SERVER` | `OFF` | 构建 MCP server library |
| `CXXMCP_BUILD_EXAMPLES` | `OFF` | 构建示例 |
| `CXXMCP_BUILD_TESTS` | `BUILD_TESTING` | 构建测试 |
| `CXXMCP_BUILD_BENCHMARKS` | `OFF` | 构建 benchmark 可执行文件 |
| `CXXMCP_ENABLE_AUTH` | `OFF` | 构建可选 OAuth 2.1 / DPoP auth target |

## Package Targets

| Target | 用途 |
|---|---|
| `cxxmcp::protocol` | MCP 协议模型和 JSON-RPC 序列化 |
| `cxxmcp::transport` | Role-generic transport contract |
| `cxxmcp::handler` | Client/server handler interface |
| `cxxmcp::peer` | Role-aware execution boundary |
| `cxxmcp::service` | Service lifecycle boundary |
| `cxxmcp::client` | 可嵌入 MCP client SDK |
| `cxxmcp::server` | 可嵌入 MCP server SDK |
| `cxxmcp::auth` | 可选 OAuth 2.1 / DPoP contract（`CXXMCP_ENABLE_AUTH=ON`） |
| `cxxmcp::sdk` | 聚合 public SDK target |

## 为什么选择 cxxmcp

- 标准 CMake SDK 体验 —— `find_package` 即用
- C++17 public API，除标准库外无运行时依赖
- 完整 MCP 协议覆盖，typed helper 自动按 capability gate
- 基于官方 runner 的跨 SDK conformance 验证
- RMCP 风格 Peer/Service 架构，为嵌入场景设计
- 开箱支持 stdio、process stdio 和 Streamable HTTP transport

## Examples

源码树内示例覆盖 server/client peer、auth、tasks、elicitation 和 transport adapter。运行：

```powershell
cmake --preset examples && cmake --build --preset examples && ctest --preset examples
```

完整列表见 [examples.md](docs/examples.md)。独立仓库 [cxxmcp-examples](https://github.com/caomengxuan666/cxxmcp-examples) 通过外部 CMake 项目消费 SDK，覆盖更多高级场景。

## 文档

- [GitHub Pages](https://caomengxuan666.github.io/cxxmcp/) — API 参考
- [Conformance evidence](docs/conformance_evidence.md) — 测试结果和已知例外
- [Compatibility policy](docs/compatibility_policy.md) — 版本策略、编译器矩阵、ABI
- [HTTP transport backend evidence](docs/compatibility_policy.md#http-transport-backend-evidence)
- [Release gates](docs/release_gates.md) — release-blocking 检查
- [Runtime gateway](docs/runtime_gateway.md) — 外部 gateway 边界
- [Examples](docs/examples.md) — 完整示例列表
- [Auth design](docs/auth_design.md) — OAuth 2.1 / DPoP 方向
- [Request lifecycle](docs/request_lifecycle.md) — 超时、取消、进度、关闭
- [Contributing](CONTRIBUTING.md) | [Security](SECURITY.md) | [Changelog](CHANGELOG.md)

## 项目状态

cxxmcp 是正在准备 official SDK candidate 证据的社区 C++ MCP SDK。Peer/Service boundary、transport 层和 conformance gates 已接近 release candidate 状态。除非被 MCP maintainers 接受，否则它不是官方 MCP SDK。

### 编译器兼容性

MinGW UCRT64 GCC 和 MinGW CLANG64 Clang 作为 provisional、best-effort 编译器兼容性证据进行跟踪。这些目标不是 release-supported。`compiler-compat` workflow 在它们保持 provisional 期间以 `continue-on-error: true` 运行。
