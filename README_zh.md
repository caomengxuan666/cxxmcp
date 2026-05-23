# cxxmcp

轻量级 cxxmcp runtime / gateway，用于本地工具、内部服务和上游 cxxmcp 服务器。

## 提供的内容

- `protocol`：cxxmcp / JSON-RPC 类型与序列化
- `client` 和 `server`：可嵌入库
- `app`：共享注册表、暴露配置和 gateway 状态
- `cli`：命令行入口
- `gui`：桌面管理器

## 构建

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI

```powershell
cxxmcp --help
cxxmcp --version
cxxmcp doctor
cxxmcp --state-dir .\.mcp-runtime tools list
cxxmcp --json servers list
cxxmcp servers add-stdio --trust --discover --cwd C:\workspace --env API_TOKEN secret filesystem node server.js --root C:\workspace
cxxmcp servers set-cwd filesystem C:\other-workspace
cxxmcp servers set-env filesystem MCP_PROFILE dev
cxxmcp servers unset-env filesystem API_TOKEN
cxxmcp servers add-http --trust --discover --header Authorization "Bearer token" remote http://127.0.0.1:3000/mcp
cxxmcp servers set-header remote X-Request-ID request-id
cxxmcp servers unset-header remote Authorization
cxxmcp servers import --trust --discover .\client-mcp-config.json
cxxmcp servers inspect filesystem
cxxmcp gateway init --trust --discover --instructions "Use reviewed workspace tools only." profile.dev filesystem 127.0.0.1 39931 /mcp/cli
cxxmcp gateway init-stdio --trust --discover --path /mcp/filesystem --instructions "Use filesystem tools only." profile.fs filesystem 127.0.0.1 39931 node server.js --root C:\workspace
cxxmcp gateway init-http --trust --discover --path /mcp/remote --instructions "Use remote tools only." --header Authorization "Bearer token" profile.remote remote 127.0.0.1 39932 http://127.0.0.1:3000/mcp
cxxmcp gateway init-all --trust --discover --instructions "Use imported tools only." 127.0.0.1 39940 /mcp/imported
cxxmcp gateway import-config --trust --discover --profile-prefix gateway. --instructions "Use imported tools only." .\client-mcp-config.json 127.0.0.1 39940 /mcp/imported
cxxmcp gateway list
cxxmcp gateway inspect profile.dev
cxxmcp gateway status
cxxmcp capabilities list
cxxmcp capabilities inspect filesystem:tool:read_file
cxxmcp exposures set-instructions profile.dev "Use this gateway for reviewed workspace tools only."
cxxmcp exposures bind profile.dev filesystem:tool:read_file dev.read_file
cxxmcp exposures disable profile.dev filesystem:tool:read_file
cxxmcp exposures enable profile.dev filesystem:tool:read_file
cxxmcp exposures prune profile.dev
cxxmcp exposures inspect profile.dev
cxxmcp gateway check profile.dev
cxxmcp gateway client-config profile.dev dev-gateway
cxxmcp gateway serve-http profile.dev
cxxmcp gateway client-config-all local
cxxmcp gateway client-config-all --ready-only local
cxxmcp gateway client-config-stdio profile.dev dev-gateway
cxxmcp gateway check-all
cxxmcp gateway preview profile.dev
cxxmcp gateway serve-stdio profile.dev
cxxmcp gateway serve-all --ready-only
cxxmcp exposures clear-instructions profile.dev
cxxmcp exposures unbind profile.dev filesystem:tool:read_file
cxxmcp exposures remove profile.dev
cxxmcp servers disable filesystem
cxxmcp servers remove filesystem
```

## 状态目录

- 默认状态目录：`.\.mcp-runtime`
- 使用 `--state-dir <path>` 覆盖
- 或设置 `MCP_RUNTIME_HOME`
- `--help`、`--version` 和 `--json` 作为全局参数支持
- `--json` 会为 list、inspect、onboarding 和管理类命令输出结构化数据

导入的 cxxmcp server 默认是 `untrusted`。需要先显式 trust，才能进行 discovery 或通过 gateway 路由调用。
一键初始化时可以使用 `cxxmcp gateway init --trust --discover ...`，它会显式 trust server、执行 discovery 并绑定能力。
如果是首次接入并且还需要注册上游 server，本地 stdio server 可以直接用 `cxxmcp gateway init-stdio --trust --discover ...`，HTTP cxxmcp server 可以用 `cxxmcp gateway init-http --trust --discover ...`。
gateway init 系列命令支持 `--instructions <text>`，可以在一键初始化时同时设置 profile instructions。
`cxxmcp gateway init-all ...` 会为所有可执行且已经完成能力发现的 server 创建或刷新 HTTP gateway profile，并从指定 base port 开始分配端口。如果希望该命令先显式 trust 并 discover 所有已配置 server，可以加 `--trust --discover`。
`cxxmcp gateway init-all ...` 和 `cxxmcp gateway import-config ...` 支持 `--profile-prefix <prefix>`，用于控制批量生成的 profile id。
`cxxmcp gateway import-config ...` 会把 client config 导入和批量 gateway 初始化合并为同一条命令。
批量初始化后先运行 `cxxmcp gateway status`，实际接入时优先使用 `client-config-all --ready-only` 和 `serve-all --ready-only`。
`cxxmcp gateway init-http ...` 支持重复传入 `--header <name> <value>`，用于上游 HTTP 鉴权。
手动注册 HTTP upstream 时，`cxxmcp servers add-http ...` 也支持重复传入 `--header <name> <value>`。
手动注册本地 stdio upstream 时，`cxxmcp servers add-stdio ...` 支持 `--cwd <cwd>` 和重复的 `--env <name> <value>`。
如果注册时已经明确信任该 upstream，`cxxmcp servers add-stdio ...` 和 `cxxmcp servers add-http ...` 可以传入 `--trust`。
它们也可以传入 `--discover`，在注册后立即发现并保存上游 tools、prompts 和 resources。
手动配置时，先执行 `cxxmcp servers trust <server-id>` 和 `cxxmcp servers discover <server-id>`，再绑定到 exposure profile。
导入已有客户端配置时，可以用 `cxxmcp servers import --trust --discover <path>` 一步完成导入、信任和能力发现。
`gateway serve-http` 会拒绝未通过 `gateway check` 的 profile；`gateway serve-stdio` 不使用 HTTP endpoint，因此只要求 binding readiness。
readiness 问题会在人类输出里附带命令提示，并在 `--json` 输出里提供 `suggestion` 字段。
可以用 `cxxmcp doctor` 聚合检查 servers、discovered capabilities、exposure profiles 和 HTTP gateway endpoint readiness 的当前状态。
如果只关心 gateway 能否被 HTTP 客户端接入，使用 `cxxmcp gateway status`，它会同时显示 endpoint 问题以及下一步 `client-config-all --ready-only` / `serve-all --ready-only` 命令。
可以用 `cxxmcp gateway check-all` 直接从 gateway 命令组检查所有 gateway profiles，包括 HTTP endpoint readiness。
`cxxmcp exposures inspect <profile-id>` 和 `cxxmcp gateway inspect <profile-id>` 会同时显示 binding readiness 与 HTTP gateway status；JSON 输出保留 `readiness`，并新增 `gatewayStatus`。
`cxxmcp gateway list` 可以直接从 gateway 命令组查看同一份 profile 列表。
`cxxmcp servers inspect <server-id>` 会显示 discovered capability 数量以及 exposure profile 使用情况。
`cxxmcp gateway init --discover ...` 会显式执行 discovery，然后创建并绑定 gateway profile。
如果 discovery 因 server 未 trust、disabled 或 blocked 失败，CLI 会给出对应命令提示。
重复执行 `cxxmcp gateway init ...` 会复用已有 profile，更新 endpoint/bindings，并报告新增与刷新的 binding 数量。
rediscovery 后如果上游能力已经消失，可以用 `cxxmcp exposures prune <profile-id>` 清理 stale bindings。
HTTP 客户端接入单个已 HTTP-ready 的 gateway profile 时使用 `cxxmcp gateway client-config ...`；只有所有已配置 HTTP gateway profiles 都 ready 时才使用 `cxxmcp gateway client-config-all ...`；如果希望客户端直接拉起 gateway 进程，对 binding-ready 的 profile 使用 `cxxmcp gateway client-config-stdio ...`。`client-config-all` 可以加 `--ready-only`，只导出当前可用的 HTTP gateway profiles，并跳过未就绪项。
可以用 `cxxmcp gateway serve-all` 在一个进程里托管所有已配置的 HTTP gateway profiles。加 `--ready-only` 可以跳过未就绪项，只托管确实能接收 HTTP 客户端的 profiles。

## 测试

当前测试覆盖 protocol、client/server 传输、app 服务、CLI、GUI 和 tool management。

