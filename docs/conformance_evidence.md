# Conformance Evidence

Date: 2026-05-29

This document records the current local `modelcontextprotocol/conformance`
evidence used by README and release-candidate documentation. Headline
comparisons use `--suite all` for both server and client so server subsets,
client auth subsets, and dated compatibility subsets are not mixed into the
same score.

## C++ SDK Results

The downstream harness lives in `../cxxmcp-examples`.

| Target | Command shape | Result |
|---|---|---|
| Server latest all | `npm start -- server --url http://127.0.0.1:3100/mcp --suite all` | 109 passed, 1 failed |
| Client latest all (OpenSSL) | `npm start -- client --command ...build-auth-openssl/cxxmcp_conformance_everything_client.exe --suite all --timeout 30000` | 447 passed, 0 failed |
| Server `2025-11-25` all | `npm start -- server --url http://127.0.0.1:3000/mcp --suite all --spec-version 2025-11-25` | 47 passed, 0 failed |
| Client `2025-11-25` all | `npm start -- client --command ...build-auth-openssl/cxxmcp_conformance_everything_client.exe --suite all --spec-version 2025-11-25 --timeout 30000` | 224 passed, 1 failed |
| Client tier auth | `npm start -- client --command ...build-auth-openssl/cxxmcp_conformance_everything_client.exe --suite auth --timeout 30000` | 217 passed, 0 failed |

The OpenSSL client build is configured with:

```powershell
cmake -S . -B build-auth-openssl -G Ninja `
  -DCMAKE_SKIP_INSTALL_RULES=ON `
  -DCXXMCP_ENABLE_AUTH=ON `
  -DCXXMCP_ENABLE_HTTP=ON `
  -DCXXMCP_AUTH_CRYPTO=OpenSSL `
  -DCMAKE_TOOLCHAIN_FILE=C:\Users\cmx\repo\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

## Known Exceptions

- Server latest all has one SEP-2243 failure:
  `http-header-validation / ServerRejectsMissingMethodHeader`.
  cxxmcp 服务器将 `Mcp-Method` 头设为可选：有则验证一致性，无则放行。严格拒绝
  缺少 `Mcp-Method` 的请求会导致 TypeScript SDK 客户端的 32 个场景失败。
  cxxmcp 客户端已正确发送 `Mcp-Method` 和 `Mcp-Name` 头。上游修复进行中：
  - [typescript-sdk#2176](https://github.com/modelcontextprotocol/typescript-sdk/issues/2176) — TypeScript SDK 客户端缺少 `Mcp-Method` 头
  - [typescript-sdk#2178](https://github.com/modelcontextprotocol/typescript-sdk/pull/2178) — 修复 PR（开放中）
  - [conformance#323](https://github.com/modelcontextprotocol/conformance/issues/323) — conformance 套件自相矛盾
  - SEP-2243 规范：客户端 MUST 发送 `Mcp-Method`，服务器 SHOULD 验证
  等 PR #2178 合并后，cxxmcp 可默认启用严格 SEP-2243 验证，达到 110/110。
- The no-OpenSSL auth build intentionally does not support private_key_jwt.
  The OpenSSL auth build passes `auth/client-credentials-jwt`.

## RMCP Comparison

The fair headline comparison uses `--suite all` only.

| Target | C++ result | RMCP result |
|---|---:|---:|
| Server latest all | 109 passed, 1 failed | 48 passed, 47 failed |
| Client latest all | 447 passed, 0 failed | no summary; runner crashed after RMCP returned empty/non-JSON response |

Sub-suite RMCP results are useful for debugging but are not used as headline
all-suite comparisons. Locally, RMCP server active was 40 passed / 2 failed and
RMCP client auth was 190 passed / 17 failed / 2 warnings.

The full run notes are maintained in
`../cxxmcp-examples/CONFORMANCE_STATUS.md`.
