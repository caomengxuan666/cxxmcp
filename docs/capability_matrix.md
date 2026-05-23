# Capability Matrix

## Position
This project is a C++ MCP runtime / gateway with a thin public SDK on top of a shared app layer.

Parity target:
- official Rust SDK `rmcp` 1.7.0
- current upstream docs / release: docs.rs and GitHub release `rmcp-v1.7.0`

The public surface should not be smaller than the official Rust SDK. The core families are:
- tools
- prompts
- resources and resource templates
- roots
- logging
- completions
- sampling
- notifications and progress events
- subscriptions and change notifications
- raw JSON-RPC escape hatches
- stdio, Streamable HTTP, and legacy SSE compatibility

`elicitation` exists in the Rust SDK as a feature-gated surface, so we treat it as optional parity, not as the core baseline.

No custom protocol fork is allowed.

## Capability Tiers

### Tier 1: Core MCP parity
These are first-class and must remain stable.
- `initialize`
- `ping`
- `tools/list`
- `tools/call`
- `prompts/list`
- `prompts/get`
- `resources/list`
- `resources/read`
- `resource templates`
- `roots/list`
- `completion/complete`
- logging-level requests and log-message notifications
- request / response / notification handling
- progress notifications
- structured error mapping
- raw JSON-RPC request / notification escape hatches

### Tier 2: Extended parity
These are still core product behavior, not optional add-ons.
- resource subscriptions and update notifications
- client-side list-change notifications
- client-side roots list updates
- sampling / `createMessage`
- transport negotiation and compatibility handling
- stdio, Streamable HTTP, and legacy SSE transports
- feature-gated elicitation
- task / background job lifecycle
- cancellation and progress propagation

### Tier 3: Gateway / runtime behavior
These belong in `app` and `cli`, not in the protocol layer.
- upstream server registry
- discovery from stdio and HTTP MCP servers
- exposure profiles
- capability binding and namespacing
- trust / block / approval policy
- import / export
- endpoint readiness checks
- multi-profile hosting
- tasks / long-running job tracking

### Tier 4: Adapter-only behavior
These are implementation adapters, not the product identity.
- filesystem wrappers
- shell wrappers
- git wrappers
- internal REST wrappers
- process supervision glue

### Not core
- plugin marketplace
- protocol fork
- second HTTP stack
- second JSON stack
- second GUI toolkit
- custom RPC dialect

## Dependency Plan

| Layer | Required libraries |
|---|---|
| protocol | `nlohmann/json`, `jsonrpcpp`, standard library |
| client | `cpp-httplib`, `nlohmann/json`, `spdlog` |
| server | `cpp-httplib`, `OpenSSL`, `nlohmann/json`, `spdlog` |
| plugin-sdk | `nlohmann/json`, standard library |
| app | `nlohmann/json`, `spdlog`, standard library |
| cli | `CLI11`, `spdlog`, `app`, `client`, `server` |
| gui | deferred; no active CMake target |

## Notes
- `cpp-httplib` covers the HTTP, HTTPS, and SSE compatibility path we need without introducing Asio.
- `CLI11` is the right fit for a dense command surface with composable subcommands.
- `spdlog` keeps logging consistent across CLI and runtime code.
- GUI work is paused. When it returns, it should stay a thin shell over `app`.
- The facade should be written in a C++17 style even if the internal build uses C++23.
- The public SDK should still expose raw protocol and transport escape hatches, even when the fluent builder is the primary entry point.
