# 包消费方式

这份文档覆盖中心包仓库之外的轻量消费路径。这里的路径都是 SDK-only：
gateway 等工具仓库不属于 SDK package contract。

## 依赖策略

SDK 支持两种依赖模式：

- 默认源码包 / archive 构建使用仓库内 bundled header-only SDK 依赖，让
  FetchContent、CPM.cmake 和直接源码安装不依赖包管理器。安装树会包含
  `tl/expected.hpp` 和 `nlohmann/json.hpp`。
- 注册表包构建应设置 `CXXMCP_USE_SYSTEM_DEPS=ON`，并使用包管理器提供的
  `tl-expected`、`nlohmann-json` 和 `cpp-httplib`。这种模式下安装树不能再
  vendor `tl` 或 `nlohmann` 头文件。

`cpp-httplib` 是 transport 实现依赖，不作为 public SDK header 安装。下游代码应
包含 `cxxmcp/transport/http_transport.hpp`、
`cxxmcp/client/http_transport.hpp` 或 `cxxmcp/server/http_transport.hpp`，而不是
直接包含 `httplib.h`。

spdlog、CLI11 等 tooling 依赖不属于 SDK package contract。面向 vcpkg/Conan
的 SDK 包应默认关闭 examples、tests 和 docs。

## vcpkg Overlay Port

`cxxmcp-sdk` 尚未进入 vcpkg curated registry。当前支持的 vcpkg 路径是本仓库里的
`packaging/vcpkg/ports/cxxmcp-sdk` overlay port，需要从这个仓库 checkout 消费。

一次性安装：

```powershell
vcpkg install cxxmcp-sdk --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
```

manifest mode 下，下游应用的 manifest 保持精简：

```json
{
  "dependencies": [
    "cxxmcp-sdk"
  ]
}
```

然后用 overlay path 安装：

```powershell
vcpkg install --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
```

示例 `vcpkg-configuration.json` 形状位于：

```text
packaging/vcpkg/vcpkg-configuration.overlay-example.json
```

把它放到下游 `vcpkg.json` 旁边后，需要把 `builtin-baseline` placeholder 替换成
你的项目锁定的 vcpkg commit。`overlay-ports` 路径相对于
`vcpkg-configuration.json` 所在目录；如果仓库 checkout 在其他位置，请同步调整。

这个 overlay port 只构建 C++17 SDK package targets。它设置
`CXXMCP_USE_SYSTEM_DEPS=ON`，关闭 examples、tests 和 docs，并使用 vcpkg 提供的
`tl-expected`、`nlohmann-json` 和 `cpp-httplib`。它不会把 spdlog、CLI11 或外部
gateway tooling 变成 SDK 包的默认消费面。

可选 auth scaffold 是显式 feature，不属于默认 vcpkg package 路径：

```powershell
vcpkg install "cxxmcp-sdk[auth]" --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
```

`auth` feature 会映射到 `CXXMCP_ENABLE_AUTH=ON`。它当前只启用
transport-neutral OAuth/DPoP contracts，不允许把 OpenSSL 拉入默认 package 路径。

## 后续 vcpkg Registry 路径

如果用户在 curated registry 接受之前需要 vcpkg 版本管理，下一步可以为同一个
SDK-only port 准备独立或仓库托管的 custom Git registry。未来配置形状示例在：

```text
packaging/vcpkg/vcpkg-configuration.git-registry-future-example.json
```

这个文件只是示例，不代表当前已经存在可用 registry。使用前需要把 repository URL
和两个 baseline placeholder 都替换成真实 registry commit。

重新提交 curated-registry PR 的条件由
`docs/ecosystem_maturity_evidence.md` 约束，不能只因为 overlay port 存在就提交。

未来提交 vcpkg curated-registry PR 时，应和当前本地 overlay port 做这些区别：

- 用 `vcpkg_from_github()` 从 release tag 和 SHA512 source archive hash 拉取源码，
  而不是把本地 checkout 当作 `SOURCE_PATH`；
  `packaging/vcpkg/curated-portfile.future.cmake` 是当前 review sketch；
- 在 SDK libraries 仍显式构建为 static、且不声明 shared-library ABI 支持时保留
  `vcpkg_check_linkage(ONLY_STATIC_LIBRARY)`；portfile 不再强制
  `-DBUILD_SHARED_LIBS=OFF`；
- 继续只启用 SDK 构建，并关闭 examples、tests 和 docs；
- 默认 `cpp-httplib` 仍按不带 TLS 的 loopback HTTP 消费，除非后续明确增加依赖
  `cpp-httplib[openssl]` 的 `ssl` 或 `https` feature；
- OAuth/DPoP auth 等 OpenSSL-backed 实现落地之后再作为 opt-in feature，不提前把
  OpenSSL 拉入默认 SDK package；
- package smoke 要覆盖两个状态：默认安装不能暴露 `cxxmcp::auth`，auth-enabled
  安装必须允许外部 consumer 显式链接 `cxxmcp::auth`。

## FetchContent

优先使用 release workflow 生成的 SDK source archive，而不是 GitHub 自动生成的
源码包。SDK archive 包含默认 bundled 构建需要的 header-only SDK 依赖；GitHub
自动源码包不包含 submodule 内容。

下面具体的 `v1.1.3` URL 是本文档目前记录的最新已发布 SDK source archive。
它适用于想固定到已发布默认 SDK surface 的 consumer。不要把它当成当前 worktree
可选 auth header surface 的证据；当前源码或 release candidate 验证必须使用那次
release-gates run 生成的精确 source archive 和 checksum。

```cmake
include(FetchContent)

FetchContent_Declare(
    cxxmcp
    URL https://github.com/caomengxuan666/cxxmcp/releases/download/v1.1.3/cxxmcp-sdk-source-v1.1.3.tar.gz
    URL_HASH SHA256=ebf256c24e806301b65749ff22960b717aef46bba625c5d8a7edf9e237ccf936
)

set(CXXMCP_BUILD_SDK ON CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cxxmcp)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE cxxmcp::server)
```

## CPM.cmake

`CPM.cmake` 可以消费同一个 SDK source archive。显式关闭 SDK 外选项，避免下游
构建意外启用 examples、tests 或 docs。

URL 和 hash 应来自你有意 pin 的那个 release。release candidate 验证要使用候选
run 生成的精确 source artifact，不要直接复制以前发布版本的示例。

cxxmcp 不会安装或导出 `CPM.cmake` helper。消费方项目必须自己提供它，例如把
`cmake/CPM.cmake` vendor 到自己的源码树，或者在 `include()` 前自行 bootstrap。
下面的路径是消费方自己的文件路径，不是 cxxmcp 发布包提供的文件。

```cmake
include(cmake/CPM.cmake)

set(CXXMCP_BUILD_SDK ON CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_DOCS OFF CACHE BOOL "" FORCE)

CPMAddPackage(
    NAME cxxmcp
    URL https://github.com/caomengxuan666/cxxmcp/releases/download/v1.1.3/cxxmcp-sdk-source-v1.1.3.tar.gz
    URL_HASH SHA256=ebf256c24e806301b65749ff22960b717aef46bba625c5d8a7edf9e237ccf936
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

## 外部消费者模板

最小的仓库外 CMake 项目位于：

```text
templates/external_consumer
```

它刻意保持很小：一次 `find_package(cxxmcp CONFIG REQUIRED)`，一个 executable，
以及一个窄 SDK target 链接。`package_smoke` 会用安装后的 SDK 产物配置并编译
这个模板，确保 release candidate 里的模板始终可用。

## Conan

根目录 `conanfile.py` 默认关闭 auth。需要可选 auth scaffold 的消费者必须显式启用：

```powershell
conan create . -o cxxmcp/*:with_auth=True
```

`with_auth=True` 会映射到 `CXXMCP_ENABLE_AUTH=ON` 并暴露 `cxxmcp::auth`
component。默认 Conan package 仍然是 SDK-only，不导出 auth headers 或 OpenSSL
requirements。

## xmake-repo

xmake-repo recipe 草案在：

```text
packaging/xmake/packages/c/cxxmcp/xmake.lua
```

它构建同一个 SDK source archive，并关闭 examples、tests 和 docs。recipe 有一个
opt-in `auth` config，会映射到
`CXXMCP_ENABLE_AUTH=ON`；默认仍然关闭 auth。等 package interface 稳定后，可以把它
提交到 xmake-repo。
