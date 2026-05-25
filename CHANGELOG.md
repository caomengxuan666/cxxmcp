# Changelog

## 2.0.0

- Reframed the public SDK around `peer` / `service` facades.
- Kept `client` / `server` as compatibility and convenience wrappers.
- Added HTTP URI support and auth header support for client transport setup.
- Added `ClientPeer` and `ServerPeer` examples.
- Aligned `process_stdio_client` with the peer/service path.
- Added a dedicated SDK umbrella test for `cxxmcp/sdk.hpp`.
