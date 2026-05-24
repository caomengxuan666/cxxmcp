# RMCP Reference Notes

Upstream source:
- https://github.com/modelcontextprotocol/rust-sdk

Local reference path:
- `reference/rmcp`

What stands out versus the current C++ surface:

- RMCP centers the public API on async `Peer<RoleClient>` and `Peer<RoleServer>` objects, not separate concrete client/server classes.
- Client methods are mostly typed request helpers plus `list_all_*` pagination wrappers; the C++ client adds a larger callback-setter surface and synchronous blocking calls.
- Server-side RMCP lives around `Service<RoleServer>`, `serve_server`, `serve_client`, and handler traits; the C++ server is more registry-centric and exposes transport/auth/rate-limit ownership directly.
- Streamable HTTP is much richer in RMCP:
  - client config carries `uri`, retry policy, channel buffer size, stateless mode, auth header, custom headers, and session reinit behavior
  - server config carries SSE keep-alive/retry, stateful mode, JSON response mode, cancellation token, host/origin allowlists, and optional session store
  - the C++ HTTP transports are narrower `host/port/path/headers/timeout` wrappers
- RMCP has explicit generic transport abstractions (`Transport<R>`, `IntoTransport<R, E, A>`) and transport workers; the C++ code uses concrete transport classes with direct `start/send/stop` methods.
- RMCP exposes async request options and cancellable requests via `PeerRequestOptions`; the C++ surface does not expose that level of per-request control.

Key local references:
- `client/include/mcp/client/client.hpp`
- `server/include/mcp/server/server.hpp`
- `client/include/mcp/client/http_transport.hpp`
- `server/include/mcp/server/http_transport.hpp`
