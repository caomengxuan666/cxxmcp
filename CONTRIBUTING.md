# Contributing

Thank you for helping improve cxxmcp. This project is SDK-first: protocol,
transport, handler, peer, service, client, and server APIs are the stable public
surface. Runtime, gateway, CLI, adapters, and plugin tooling are optional layers
above the SDK.

## Development Flow

Use topic branches from `master`. Keep changes scoped to one behavior,
capability family, or documentation concern.

Before opening a pull request, run the narrowest checks that cover your change.
For public SDK changes, prefer:

```powershell
pwsh -NoProfile -File scripts\install-githooks.ps1
pwsh -NoProfile -File scripts\format.ps1 -Check
cmake --build build-package-prep --target mcp_protocol_tests mcp_client_server_tests mcp_sdk_tests --parallel
ctest --test-dir build-package-prep -R "^(protocol|client_server|sdk|package_smoke)$" --output-on-failure --timeout 600
```

Transport changes should also run the relevant transport tests. Package changes
should run `package_smoke`, preferably for both bundled and
`CXXMCP_USE_SYSTEM_DEPS=ON` builds when those build directories are available.

Before creating a release tag, run:

```powershell
python -B scripts\prepare_release.py 1.2.3
```

The release preparation script updates package/version metadata, runs the
repository format script, and runs the short release metadata checks. It does
not commit, tag, or push.

## Public API Rules

Public headers live under `sdk/**/include/cxxmcp`. New public surface must state
one of these statuses in docs, comments, or release notes:

- core: stable SDK contract for normal use;
- optional: stable API for a negotiated or opt-in capability;
- experimental: available but not yet a stable compatibility promise.

Breaking public API changes require a design note or RFC-style issue before
implementation. Public renames must add the new spelling first, keep the old
spelling as an alias or forwarding wrapper, mark the old spelling with
`CXXMCP_DEPRECATED("message")` where possible, and document the migration.

Runtime, gateway, policy, discovery, profile, CLI defaults, and transport
backend details must not enter public SDK headers unless the pull request
explains why the concern belongs in the SDK contract.

## Protocol Compatibility

cxxmcp follows published MCP protocol snapshots and cross-checks behavior
against pinned reference SDKs. Do not add custom MCP dialects or alternate wire
formats. Unknown or future protocol fields should be preserved through typed
models when possible and exposed through raw JSON-RPC escape hatches when they
are not modeled yet.

## Tests And Evidence

Every new public behavior should have at least one focused test. Prefer
protocol tests for model/serialization behavior, client/server tests for SDK
request handling, transport tests for I/O behavior, and package smoke tests for
install/export concerns.

Release-candidate evidence is governed by `docs/release_gates.md` and
`docs/release_candidate_checklist.md`.
