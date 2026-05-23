# cxxmcp

Lightweight cxxmcp runtime / gateway for local tools, internal services, and upstream cxxmcp servers.

## What it provides

- `protocol` for cxxmcp / JSON-RPC types and serialization
- `client` and `server` libraries for embedding
- `app` for shared registry, exposure profiles, and gateway state
- `cli` for the command-line entrypoint
- `gui` for the desktop manager

## Build

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

## State

- Default state directory: `.\.mcp-runtime`
- Override with `--state-dir <path>`
- Or set `MCP_RUNTIME_HOME`
- `--help`, `--version`, and `--json` are supported as global flags
- `--json` produces structured output for list, inspect, onboarding, and management commands

Imported cxxmcp servers start as `untrusted`. Trust a server before discovery or gateway routing.
For one-shot setup, `cxxmcp gateway init --trust --discover ...` explicitly trusts the server, discovers capabilities, and binds them.
For first-run setup that also registers the upstream server, use `cxxmcp gateway init-stdio --trust --discover ...` for local stdio servers or `cxxmcp gateway init-http --trust --discover ...` for HTTP cxxmcp servers.
Gateway init commands accept `--instructions <text>` to set profile instructions during one-shot setup.
`cxxmcp gateway init-all ...` creates or refreshes one HTTP gateway profile per executable server with discovered capabilities, assigning ports from the provided base port. Add `--trust --discover` when you want the command to explicitly trust and discover all configured servers first.
`cxxmcp gateway init-all ...` and `cxxmcp gateway import-config ...` accept `--profile-prefix <prefix>` to control generated profile ids.
`cxxmcp gateway import-config ...` combines client-config import with the same batch gateway initialization flow.
After batch initialization, use `cxxmcp gateway status`, then prefer `client-config-all --ready-only` and `serve-all --ready-only` for the operational path.
`cxxmcp gateway init-http ...` accepts repeated `--header <name> <value>` options for upstream HTTP authentication.
`cxxmcp servers add-http ...` also accepts repeated `--header <name> <value>` options when registering an HTTP upstream manually.
`cxxmcp servers add-stdio ...` accepts `--cwd <cwd>` and repeated `--env <name> <value>` options when registering a local stdio upstream manually.
`cxxmcp servers add-stdio ...` and `cxxmcp servers add-http ...` accept `--trust` when you explicitly want to trust the upstream at registration time.
They also accept `--discover` to immediately discover and store upstream tools, prompts, and resources after registration.
For manual setup, use `cxxmcp servers trust <server-id>` and `cxxmcp servers discover <server-id>` before binding.
When importing an existing client config, `cxxmcp servers import --trust --discover <path>` imports, trusts, and discovers the listed servers in one step.
`gateway serve-http` rejects profiles that fail `gateway check`; `gateway serve-stdio` only requires binding readiness because it does not use the HTTP endpoint.
Readiness issues include command hints in human output and `suggestion` fields in `--json` output.
Use `cxxmcp doctor` for an aggregate runtime check across servers, discovered capabilities, exposure profiles, and HTTP gateway endpoint readiness.
Use `cxxmcp gateway status` when you specifically need to see which gateway profiles are ready for HTTP clients, including endpoint problems and the next `client-config-all --ready-only` / `serve-all --ready-only` steps.
Use `cxxmcp gateway check-all` to check every gateway profile, including HTTP endpoint readiness, from the gateway command group.
`cxxmcp exposures inspect <profile-id>` and `cxxmcp gateway inspect <profile-id>` include binding readiness plus HTTP gateway status; JSON output keeps `readiness` and adds `gatewayStatus`.
`cxxmcp gateway list` exposes the same profile list from the gateway command group.
`cxxmcp servers inspect <server-id>` includes discovered capability counts and exposure profile usage.
`cxxmcp gateway init --discover ...` explicitly runs discovery before creating and binding the gateway profile.
Discovery failures for untrusted, disabled, or blocked servers include command hints.
Repeated `cxxmcp gateway init ...` calls reuse the existing profile, update its endpoint/bindings, and report added vs refreshed bindings.
Use `cxxmcp exposures prune <profile-id>` after rediscovery to remove stale bindings whose upstream capabilities no longer exist.
Use `cxxmcp gateway client-config ...` for one HTTP-ready gateway profile, `cxxmcp gateway client-config-all ...` when every configured HTTP gateway profile must be ready, or `cxxmcp gateway client-config-stdio ...` for a binding-ready profile when the client should launch the gateway process itself. Add `--ready-only` to `client-config-all` when you want to skip unready profiles and export only usable HTTP gateways.
Use `cxxmcp gateway serve-all` to host all configured HTTP gateway profiles in one process. Add `--ready-only` to skip unready profiles and host only the profiles that can actually accept HTTP clients.

## Tests

The current test suite covers protocol, client/server transport, app services, CLI, GUI, and tool management.

