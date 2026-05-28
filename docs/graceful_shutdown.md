# Graceful Shutdown

cxxmcp does not install process-wide signal handlers. Applications own process
signals, event loops, and service managers, while the SDK provides idempotent
`stop()` / `close()` paths on peers, services, clients, servers, and transports.

## Recommended Pattern

For POSIX services, install a minimal `SIGINT` / `SIGTERM` handler that only
sets an atomic flag. Do not call SDK APIs directly from the signal handler.
Perform shutdown from normal application code:

```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include <cxxmcp/server.hpp>

std::atomic_bool stop_requested{false};

extern "C" void request_stop(int) {
  stop_requested.store(true, std::memory_order_relaxed);
}

int main() {
  std::signal(SIGINT, request_stop);
  std::signal(SIGTERM, request_stop);

  auto peer = mcp::server::ServerPeer::builder()
                  .name("example")
                  .version("1.0.0")
                  .build();
  auto running = mcp::serve(std::move(peer));
  if (!running) {
    return 1;
  }

  while (!stop_requested.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return running->stop().has_value() ? 0 : 1;
}
```

For Windows console applications, use `SetConsoleCtrlHandler()` with the same
rule: set an atomic flag in the handler and call SDK shutdown APIs from normal
application control flow.

## SDK Shutdown Semantics

- `RunningService::stop()` and `close()` are idempotent.
- `ServerPeer::stop()` stops all attached transports.
- `ClientPeer::stop()` stops its owned transport, if any.
- `Server::stop()` is best-effort and calls `stop()` on owned transports.
- Destructors perform best-effort cleanup, but production applications should
  still call `stop()` / `close()` explicitly so shutdown latency and errors are
  visible.

## Transport Notes

Streamable HTTP transports can usually unblock through their platform HTTP
server stop path. Stdio transports over ordinary C++ streams cannot portably
interrupt a thread already blocked in `std::getline()`. When bounded shutdown
latency matters, prefer platform-owned process/pipe or HTTP transports whose
stop path can close the underlying handle.
