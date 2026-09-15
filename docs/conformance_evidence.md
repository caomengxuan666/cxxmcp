# Conformance Evidence

Date: 2026-09-15

This document records the current local `modelcontextprotocol/conformance`
evidence used by README and release-candidate documentation. Headline
comparisons use `--suite all` for both server and client so server subsets,
client auth subsets, and dated compatibility subsets are not mixed into the
same score.

## C++ SDK Results

The downstream harness lives in `../cxxmcp-examples`.

| Target | Command shape | Result |
|---|---|---|
| Server latest all | `node dist/index.js server --url http://127.0.0.1:3100/mcp --suite all` | 272 passed, 0 failed |
| Client latest all (OpenSSL) | `node dist/index.js client --command ...build/cxxmcp_conformance_everything_client.exe --suite all --timeout 30000` | 501 passed, 0 failed, 0 warnings |
| Server `2025-11-25` all | `node dist/index.js server --url http://127.0.0.1:3101/mcp --suite all --spec-version 2025-11-25` | 80 passed, 0 failed |
| Client `2025-11-25` all | `node dist/index.js client --command ...build/cxxmcp_conformance_everything_client.exe --suite all --spec-version 2025-11-25 --timeout 30000` | 247 passed, 0 failed, 0 warnings |
| Client tier auth | `node dist/index.js client --command ...build/cxxmcp_conformance_everything_client.exe --suite auth --timeout 30000` | 227 passed, 0 failed, 0 warnings |

The OpenSSL client build is configured with auth enabled:

```powershell
cmake -S . -B build -G Ninja `
  -DCXXMCP_ENABLE_AUTH=ON `
  -DCXXMCP_ENABLE_HTTP=ON `
  -DCXXMCP_AUTH_CRYPTO=OpenSSL `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

## Covered Surface Since 2026-05-29

The 2026-09 runs additionally cover the draft conformance surface:

- SEP-2575 per-request stateless lifecycle: `MCP-Protocol-Version` on every
  POST (including `initialize`), `_meta` `protocolVersion`/`clientInfo`/
  `clientCapabilities` validation, header/_meta agreement, removed-method
  `404` + `-32601`, and error-code-to-HTTP-status mapping.
- SEP-2243 `Mcp-Method`/`Mcp-Name`/custom-header validation, including strict
  rejection of missing or mismatched standard headers on the stateless wire.
- SEP-2663 tasks: extension capability gating (`-32021`),
  `tasks/get|update|cancel`, task-augmented `tools/call`, and lifecycle
  states.
- SEP-2640 skills: directory/resources fixtures, digest/size/frontmatter
  verification, and `skills/*` methods.
- `subscriptions/listen` streaming and `publish` fan-out.
- `server/discover` `_meta.serverInfo` and cacheable `ttlMs`/`cacheScope`
  result fields.
- Client auth: DPoP (RFC 9449) proof-per-request with AS/RS `DPoP-Nonce`
  retry, WIF JWT-bearer grant, and SEP-2352 authorization-server migration
  with re-registration at the new AS.

## Known Exceptions

- The no-OpenSSL auth build intentionally does not support private_key_jwt.
  The OpenSSL auth build passes `auth/client-credentials-jwt`.
- The previously documented SEP-2243 exception
  (`http-header-validation / ServerRejectsMissingMethodHeader`) is resolved:
  strict standard-header validation is enforced on the stateless draft wire,
  so the check now passes while stateful TypeScript-SDK-backed scenarios that
  omit `Mcp-Method` continue to be tolerated.

## RMCP Comparison

The fair headline comparison uses `--suite all` only. RMCP numbers below are
from the 2026-05-29 run and are kept for reference.

| Target | C++ result (2026-09-15) | RMCP result (2026-05-29) |
|---|---:|---:|
| Server latest all | 272 passed, 0 failed | 48 passed, 47 failed |
| Client latest all | 501 passed, 0 failed | no summary; runner crashed after RMCP returned empty/non-JSON response |

Sub-suite RMCP results are useful for debugging but are not used as headline
all-suite comparisons. Locally, RMCP server active was 40 passed / 2 failed and
RMCP client auth was 190 passed / 17 failed / 2 warnings on 2026-05-29.

The full run notes are maintained in
`../cxxmcp-examples/CONFORMANCE_STATUS.md`.
