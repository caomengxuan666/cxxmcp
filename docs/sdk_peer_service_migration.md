# Peer/Service Migration Guide

`Peer` and `Service` are the canonical SDK entry points. Concrete
`client::Client`, `server::Server`, and `server::App` remain available for
compatibility, tests, and low-level integrations, but new application code and
docs should start with the role-aware path.

## Canonical Shape

Use this layering for new code:

1. Build or connect a role-aware peer.
2. Attach a transport before serving server peers.
3. Start the peer through `mcp::serve(...)`.
4. Keep the returned `RunningService<Role>` as the lifecycle owner.
5. Call `stop()`, `close()`, or let destruction perform best-effort shutdown.

Server side:

```cpp
#include <iostream>
#include <memory>
#include <utility>

#include <cxxmcp/peer.hpp>
#include <cxxmcp/server.hpp>
#include <cxxmcp/service.hpp>
#include <cxxmcp/transport/stdio_transport.hpp>

int main() {
  mcp::server::ServerBuilder builder;
  builder.name("demo-server").version("1.0.0");

  auto server = builder.build();
  if (!server) {
    return 1;
  }

  mcp::ServerPeer peer(std::move(*server));
  peer.add_transport(
      std::make_unique<mcp::transport::ServerStdioTransport>(std::cin,
                                                             std::cout));

  auto running = mcp::serve(std::move(peer));
  if (!running) {
    return 1;
  }

  return running->wait().has_value() ? 0 : 1;
}
```

Client side:

```cpp
#include <utility>

#include <cxxmcp/peer.hpp>
#include <cxxmcp/service.hpp>

int main() {
  auto peer = mcp::ClientPeer::connect_streamable_http({
      .host = "127.0.0.1",
      .port = 3000,
      .path = "/mcp",
  });

  auto running = mcp::serve(std::move(peer));
  if (!running) {
    return 1;
  }

  running->peer().initialize();
  running->peer().list_all_tools();
  return running->stop().has_value() ? 0 : 1;
}
```

## Migrating Server Code

Old direct server lifecycle:

```cpp
mcp::server::ServerBuilder builder;
auto server = builder.build();
server->add_transport(std::make_unique<mcp::server::StdioTransport>());
server->start();
```

New service lifecycle:

```cpp
mcp::server::ServerBuilder builder;
auto server = builder.build();

mcp::ServerPeer peer(std::move(*server));
peer.add_transport(std::make_unique<mcp::transport::ServerStdioTransport>(
    std::cin, std::cout));

auto running = mcp::serve(std::move(peer));
running->wait();
```

`ServerBuilder` remains the supported way to assemble server capabilities,
registries, and handlers. The migration target is the lifecycle and public
control surface: application code should own `ServerPeer` and
`RunningService<RoleServer>`, not a raw `server::Server` loop.

## Migrating Client Code

Old direct client lifecycle:

```cpp
auto client = mcp::client::Client::connect_streamable_http(endpoint);
client.initialize();
client.list_all_tools();
client.stop();
```

New service lifecycle:

```cpp
auto peer = mcp::ClientPeer::connect_streamable_http(endpoint);
auto running = mcp::serve(std::move(peer));
running->peer().initialize();
running->peer().list_all_tools();
running->stop();
```

The typed request helpers remain the same shape through `running->peer()`.
Use raw JSON-RPC helpers from the peer when a future MCP method or vendor
extension is not covered by typed helpers yet.

## Compatibility Boundaries

- `client::Client` and `server::Server` are compatibility wrappers and
  implementation anchors.
- `server::App::builder()` is a convenience facade for compact demos and
  legacy-style stdio tools.
- New docs should present `Peer` and `Service` first.
- Low-level examples may still use concrete classes when they demonstrate
  adapters, loopback tests, or implementation internals.
- Runtime, gateway, policy, discovery, and CLI concepts stay outside the core
  SDK contract.
