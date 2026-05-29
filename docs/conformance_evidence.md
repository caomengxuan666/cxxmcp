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
| Server latest all | `npm start -- server --url http://127.0.0.1:3100/mcp --suite all` | 108 passed, 1 failed |
| Client latest all | `npm start -- client --command ...build-auth-openssl/cxxmcp_conformance_everything_client.exe --suite all --timeout 30000` | 428 passed, 8 failed |
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
  Strictly rejecting requests without `Mcp-Method` would currently break the
  TypeScript SDK v1.29.0 conformance client, which does not send that required
  header. The C++ client sends `Mcp-Method`; strict server enforcement should
  be exposed as a compatibility-controlled mode rather than forced in the
  default server transport. Upstream tracking:
  [typescript-sdk#2176](https://github.com/modelcontextprotocol/typescript-sdk/issues/2176)
  and
  [conformance#323](https://github.com/modelcontextprotocol/conformance/issues/323).
- Client latest all still fails `sse-retry`, `http-custom-headers`, and
  `http-invalid-tool-headers`.
- The no-OpenSSL auth build intentionally does not support private_key_jwt.
  The OpenSSL auth build passes `auth/client-credentials-jwt`.

## RMCP Comparison

The fair headline comparison uses `--suite all` only.

| Target | C++ result | RMCP result |
|---|---:|---:|
| Server latest all | 108 passed, 1 failed | 48 passed, 47 failed |
| Client latest all | 428 passed, 8 failed | no summary; runner crashed after RMCP returned empty/non-JSON response |

Sub-suite RMCP results are useful for debugging but are not used as headline
all-suite comparisons. Locally, RMCP server active was 40 passed / 2 failed and
RMCP client auth was 190 passed / 17 failed / 2 warnings.

The full run notes are maintained in
`../cxxmcp-examples/CONFORMANCE_STATUS.md`.
