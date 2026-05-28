# Transport Adapter Ergonomics

The canonical transport boundary remains `mcp::transport::Transport<Role>`.
Applications that already have their own source/sink, queue, or worker loop do
not need to hand-write a full subclass for common cases.

## Function Transport

`mcp::transport::FunctionTransport<Role>` adapts application-owned send,
receive, close, and diagnostics callables to the role-generic transport
contract.

Use it when an existing runtime already owns message parsing and queueing, and
cxxmcp only needs a `Transport<Role>` object.

## JSON Line Transport

`mcp::transport::JsonLineTransport<Role>` adapts newline-delimited JSON-RPC
sources and sinks. The application supplies string read/write callbacks; the
adapter handles `JsonRpcMessage` serialization and parsing through the protocol
layer.

Use it for custom pipes, sockets, embedded host bridges, or test harnesses that
already exchange one JSON-RPC document per line.

## Queue Transport

`mcp::transport::QueueTransport<Role>` is a thread-safe in-memory queue
transport. `send()` captures outbound messages, `push_inbound()` feeds messages
to `receive()`, and `close()` unblocks waiting receivers.

Use it for worker/queue integration tests, embedded loopbacks, or application
runtimes that want to connect their own event loop to `Peer` / `Service`
without exposing private queues as SDK concepts.

These adapters stay in the transport layer. They do not add gateway policy,
retry orchestration, session discovery, or protocol extensions.

## Built-In Stdio Boundaries

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
