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
`cxxmcp::auth` targets are SDK-adjacent auth surfaces when explicitly enabled.

Gateway and CLI packages must be documented, versioned, and released by their
own repository. Release notes for this SDK must not imply that gateway/runtime
or CLI targets are exported by SDK-only vcpkg, Conan, xmake, FetchContent, or
CPM routes.

## Extension Boundary

Plugin declarations, adapter layers, external gateway policy, discovery,
registry, and managed hosting state belong outside this SDK repository until a
design note promotes a narrow SDK extension with release evidence.

## Example Boundary

In-tree examples should stay focused on SDK use: `Peer`, `Service`, client,
server, protocol, transport, auth, and transport-adapter flows. Gateway and
plugin examples belong in external repositories and should not be presented as
the first-choice SDK path in this README, release notes, or package
documentation. The former in-tree gateway runtime example is intentionally not
part of this SDK repository.

## Transport Adapter Ergonomics

The canonical transport boundary remains `mcp::transport::Transport<Role>`.
Applications that already have their own source/sink, queue, or worker loop do
not need to hand-write a full subclass for common cases.

### Function Transport

`mcp::transport::FunctionTransport<Role>` adapts application-owned send,
receive, close, and diagnostics callables to the role-generic transport
contract.

Use it when an existing runtime already owns message parsing and queueing, and
cxxmcp only needs a `Transport<Role>` object.

### JSON Line Transport

`mcp::transport::JsonLineTransport<Role>` adapts newline-delimited JSON-RPC
sources and sinks. The application supplies string read/write callbacks; the
adapter handles `JsonRpcMessage` serialization and parsing through the protocol
layer.

Use it for custom pipes, sockets, embedded host bridges, or test harnesses that
already exchange one JSON-RPC document per line.

### Queue Transport

`mcp::transport::QueueTransport<Role>` is a thread-safe in-memory queue
transport. `send()` captures outbound messages, `push_inbound()` feeds messages
to `receive()`, and `close()` unblocks waiting receivers.

Use it for worker/queue integration tests, embedded loopbacks, or application
runtimes that want to connect their own event loop to `Peer` / `Service`
without exposing private queues as SDK concepts.

These transport adapters stay in the transport layer. They do not add gateway
policy, retry orchestration, session discovery, or protocol extensions.

### Built-In Stdio Boundaries

The canonical SDK stdio boundary is the role-generic
`mcp::transport::ServerStdioTransport` / `ClientStdioTransport` used through
`ServerPeer`, `ClientPeer`, and `Service`. That path owns message-level
transport semantics and is the place where close/receive-loop behavior,
initialized-state gates, and request lifecycle handling are tested.

The older concrete `mcp::server::StdioTransport` remains a compatibility
convenience for simple local servers. It reads from caller-owned
`std::istream` objects with `std::getline()`. Its `stop()` method prevents
future loop iterations, but it cannot portably interrupt a thread already
blocked in a stream read. Use the role-generic stdio transport or a
platform-owned process/pipe transport when shutdown latency matters.
