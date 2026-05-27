# Runtime And Gateway Tools

`cxxmcp` is presented first as a C++ MCP SDK. Runtime, gateway, CLI, app,
adapter, and plugin tooling are optional layers built on top of the SDK and are
not part of the core public SDK contract.

## Scope

Runtime and gateway code may compose SDK targets to manage local MCP servers,
profiles, policies, imports, exports, and hosted tool exposure. These concepts
must stay outside canonical public SDK headers under:

- `cxxmcp/protocol`
- `cxxmcp/transport`
- `cxxmcp/handler`
- `cxxmcp/peer`
- `cxxmcp/service`
- `cxxmcp/client`
- `cxxmcp/server`

The SDK boundary gate checks for runtime, gateway, profile, policy, discovery,
import/export, CLI, and observability leaks in canonical SDK headers.

## Package Boundary

SDK package-manager paths keep runtime and gateway disabled by default. Package
consumers should be able to install and link `cxxmcp::protocol`,
`cxxmcp::client`, `cxxmcp::server`, or `cxxmcp::sdk` without pulling runtime
tooling dependencies such as spdlog or CLI11.

Runtime and gateway targets can remain in this repository while they are useful
for local development and examples, but they are documented as tools, not as the
SDK's primary product surface.

## Example Boundary

`examples/gateway_runtime.cpp` is a non-canonical tooling example. It should not
be used as the first-choice SDK path in README, release notes, or package
documentation. First-choice examples should use `Peer`, `Service`, `client`,
`server`, and transport contracts directly.
