# Changelog

## 2.0.0

- Reframed the public SDK around `peer` / `service` facades.
- Kept `client` / `server` as compatibility and convenience wrappers.
- Added HTTP URI support and auth header support for client transport setup.
- Added `ClientPeer` and `ServerPeer` examples.
- Aligned `process_stdio_client` with the peer/service path.
- Routed native `ClientPeer` initialize, synchronous helpers, paginated helpers,
  outbound notifications, and service shutdown through Peer-owned transport
  paths.
- Added `ClientPeer` handler registration helpers so normal client callbacks no
  longer require accessing the underlying `client::Client`.
- Marked `ClientPeer::client()` and `ServerPeer::server()` as deprecated
  compatibility escape hatches.
- Tightened process stdio response-id validation with stable transport errors.
- Added a dedicated SDK umbrella test for `cxxmcp/sdk.hpp`.
