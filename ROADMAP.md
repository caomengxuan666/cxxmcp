# Roadmap

This document outlines the current development direction for cxxmcp.

## Current Focus

- Conformance parity with the latest MCP specification draft (2026)
- Stabilizing the public C++ SDK API surface
- Improving cross-platform build support (Windows, Linux, macOS)

## Near Term

- Automated dependency update tooling (Dependabot or Renovate)
- Publish stable release with full conformance evidence
- Improve test coverage for edge cases in transport layer

## Longer Term

- Tier 2 application with MCP conformance program
- Language bindings for Python and Rust via C FFI

## Completed

- Full server and client implementation (Streamable HTTP, SSE, stdio, WebSocket)
- WebSocket transport with built-in auto-reconnect (cpp-httplib based)
- OAuth 2.1 authorization support
- SEP-2243 `Mcp-Method` / `Mcp-Name` header compliance
- 108/109 server conformance, 428/436 client conformance against latest spec
