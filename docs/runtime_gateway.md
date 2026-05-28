# External Gateway Boundary

`cxxmcp` is presented first as a C++ MCP SDK. Gateway, runtime, CLI, app,
profile, policy, discovery, import/export, and hosted-tool management now live
outside this SDK repository and are not part of the core public SDK contract or
the SDK package contract.

## SDK Boundary

The canonical SDK include roots must stay focused on:

- `cxxmcp/protocol`
- `cxxmcp/transport`
- `cxxmcp/handler`
- `cxxmcp/peer`
- `cxxmcp/service`
- `cxxmcp/client`
- `cxxmcp/server`

The SDK boundary gate checks for gateway, runtime, profile, policy, discovery,
import/export, CLI, and observability leaks in canonical SDK headers.

## Package Boundary

SDK package-manager paths install and expose `cxxmcp::protocol`,
`cxxmcp::client`, `cxxmcp::server`, `cxxmcp::transport`, `cxxmcp::handler`,
`cxxmcp::peer`, `cxxmcp::service`, and `cxxmcp::sdk`. Optional
`cxxmcp::plugin_sdk`, `cxxmcp::adapters`, and `cxxmcp::auth` targets are
SDK-adjacent extension surfaces when explicitly enabled.

Gateway and CLI packages must be documented, versioned, and released by their
own repository. Release notes for this SDK must not imply that gateway/runtime
or CLI targets are exported by SDK-only vcpkg, Conan, xmake, FetchContent, or
CPM routes.

## Extension Boundary

`cxxmcp::plugin_sdk` is a stable optional SDK-adjacent package surface for
minimal plugin declarations. It depends only on `cxxmcp::protocol` and
`cxxmcp::core` implementation support, and must not grow gateway policy,
discovery, profile, or CLI concepts.

`cxxmcp::adapters` is a stable optional adapter-helper surface for connecting
plugin-style declarations to the server SDK. It is installed only when
`CXXMCP_ENABLE_ADAPTERS=ON`, depends on `cxxmcp::server` and
`cxxmcp::plugin_sdk`, remains outside the core SDK narrative, and must not pull
gateway/runtime state into canonical SDK headers.

Experimental adapter ideas that need external gateway policy, discovery,
registry, or managed hosting state belong in the gateway repository until a
design note promotes a narrow SDK extension.

## Example Boundary

In-tree examples should stay focused on SDK use: `Peer`, `Service`, client,
server, protocol, transport, auth, plugin, and adapter flows. Gateway examples
belong in the external gateway/examples repository and should not be presented
as the first-choice SDK path in this README, release notes, or package
documentation. The former `gateway_runtime.cpp` example is intentionally not
part of this SDK repository.
