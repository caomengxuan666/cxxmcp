# Roadmap

This roadmap is intentionally short. Release evidence, compatibility policy,
and ecosystem readiness are tracked in `todo.md` and `docs/`.

## Current Focus

- Keep `Peer` / `Service` as the first-choice SDK path.
- Keep release artifacts, package docs, examples, and compatibility policy in
  sync for each tagged release.
- Build maturity evidence before making fact-standard or curated-registry
  claims.

## Near Term

- Publish generated docs and release artifacts from exact tagged release runs.
- Keep package-consumption evidence green for vcpkg, Conan, xmake, and
  FetchContent-style consumers.
- Recheck conformance evidence before advertising release status.

## Longer Term

- Dependency-update automation.

## Completed

- Full server and client implementation across Streamable HTTP, SSE-compatible
  paths, stdio, process stdio, and WebSocket.
- WebSocket transport with built-in auto-reconnect (cpp-httplib based).
- Optional OAuth 2.1 / DPoP / JWKS auth target.
- SEP-2243 `Mcp-Method` / `Mcp-Name` handling with current exception documented
  in `docs/conformance_evidence.md`.
- Cross-SDK conformance and interop gates.
