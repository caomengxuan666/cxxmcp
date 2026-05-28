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
SDK's primary product surface. In the SDK package-manager routes, runtime,
gateway, and CLI targets are not part of the default installed SDK contract.
They are build-tree and source-checkout tooling targets until a separate tools
package/export set is created. Release notes must not imply that
`cxxmcp::runtime`, `cxxmcp::gateway`, or `cxxmcp::cli` are guaranteed by the
SDK-only vcpkg, Conan, or xmake package routes.

## Extension Surfaces

`cxxmcp::plugin_sdk` is a stable optional SDK-adjacent package surface for
declaring lightweight tool extensions over the protocol types. It is installed
only when `CXXMCP_ENABLE_PLUGIN_SDK=ON`, covered by package smoke, and may
evolve only under the public API compatibility rules.

`cxxmcp::adapters` is a stable optional adapter-helper surface for connecting
plugin-style declarations to the server SDK. It is installed only when
`CXXMCP_ENABLE_ADAPTERS=ON`, depends on `cxxmcp::server` and
`cxxmcp::plugin_sdk`, remains outside the core SDK narrative, and must not pull
runtime, gateway, profile, policy, discovery, or CLI concepts into canonical
SDK headers.

Experimental adapter ideas that need runtime policy, discovery, registry, or
managed gateway state must live outside these stable optional targets until a
design note promotes them.

## Example Boundary

`examples/gateway_runtime.cpp` is a non-canonical tooling example. It should not
be used as the first-choice SDK path in README, release notes, or package
documentation. First-choice examples should use `Peer`, `Service`, `client`,
`server`, and transport contracts directly.
