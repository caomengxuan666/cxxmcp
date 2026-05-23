# MCPServer.cpp v2 Plan

## Goal
Build a small, spec-first MCP runtime for local tools and internal services.

## Product Positioning
This project should be a lightweight MCP Runtime / Gateway.

It should help users expose local or internal capabilities to MCP clients through standard transports and a stable SDK surface.

## Primary Scenarios
1. Local dev tool bridge
   - A developer runs one binary on a laptop.
   - MCP clients connect through stdio or localhost HTTP.
   - The server exposes tools for files, git, shell helpers, and local APIs.

2. Internal enterprise gateway
   - A team deploys the server behind auth and logging.
   - The server wraps internal REST APIs, scripts, and data services.
   - The server must be boring, auditable, and easy to operate.

3. Embedded runtime
   - A desktop app or CLI ships with a small MCP endpoint.
   - The app exposes a controlled tool surface.
   - Startup must be fast and dependencies must stay minimal.

4. Adapter layer
   - Existing services are wrapped as MCP tools or resources.
   - The project should provide adapters, not a new ecosystem.

## Non-Goals
- No plugin marketplace
- No all-in-one platform
- No custom protocol fork
- No complex hot reload as a core feature
- No feature-heavy sample tool bundle in the runtime

## Package Layout
```text
repo/
  core/
  protocol/
  client/
  server/
  plugin-sdk/
  adapters/
  observability/
  app/
  cli/
  gui/
  tests/
  docs/
```

## Package Responsibilities

### protocol
Pure MCP and JSON-RPC types.
- request / response / notification
- error codes
- capability flags
- serialization helpers
- no threads
- no transport
- no business logic

### client
Client-side helper library.
- build requests
- send requests
- parse responses
- handle streaming responses
- support tests and automation
- no server runtime code

### server
Server runtime library.
- transport startup
- routing / dispatch
- session management
- registry
- auth hooks
- rate limiting hooks
- no product samples

### plugin-sdk
Tool author surface.
- tool metadata
- argument schema
- result helpers
- optional streaming contract
- keep the API small and stable

### adapters
Thin wrappers for concrete capabilities.
- file
- http
- shell
- git
- internal APIs

### observability
Operational concerns.
- logging
- metrics
- tracing hooks
- request/session diagnostics

### cli
The deployable binary.
- config loading
- startup
- shutdown
- environment wiring

### gui
Desktop management application.
- server profile management
- tool catalog browser
- one-click add / enable / disable
- permission controls
- tool grouping and search
- config import / export
- standalone desktop entrypoint
- no Qt

### app
Shared application services.
- profile storage
- tool catalog aggregation
- user-facing policy model
- import / export
- commands shared by CLI and GUI

## Library Stack

### Core libraries
- `nlohmann/json` for JSON
- `cpp-httplib` for HTTP/HTTPS server and client
- `OpenSSL` for TLS
- `spdlog` for logging

### CLI libraries
- `CLI11` for command line parsing

### GUI libraries
- `wxWidgets` for the main desktop UI

### Optional libraries
- `miniz` only if archive packaging or compressed exports become necessary
- `pybind11` only if we keep a Python plugin bridge
- `GoogleTest` for unit and integration tests

## C++ Standard Usage

Baseline: C++23 for public API drafts.

C++20-compatible fallbacks may be added later only if packaging or compiler support requires them.

### C++20 features
- `std::jthread` and `std::stop_token` for cooperative shutdown
- `std::span` for non-owning buffer views
- `std::string_view` for input-only text
- `std::ranges` for catalog and registry processing
- `std::format` for formatting where available
- `std::filesystem` for paths and config locations
- `std::variant` and `std::optional` for protocol modeling

### C++23 features
- `std::expected` for fallible operations
- `std::byteswap` if protocol helpers need it
- `std::print` only if toolchain support is clean

### Standard library rules
- prefer `string_view` for read-only parameters
- prefer `span` for contiguous views
- prefer `expected` or a small Result type for fallible APIs
- prefer RAII over manual cleanup
- prefer `jthread` over detached threads
- prefer `filesystem::path` over string path concatenation
- avoid owning raw pointers in public APIs

## Dependency Matrix

### protocol
- `nlohmann/json`
- standard library only

### client
- `cpp-httplib`
- `nlohmann/json`
- `spdlog`

### server
- `cpp-httplib`
- `OpenSSL`
- `nlohmann/json`
- `spdlog`

### plugin-sdk
- `nlohmann/json`
- standard library only

### adapters
- `nlohmann/json`
- `cpp-httplib` if networked
- `OpenSSL` only if secure transport is needed

### app
- `nlohmann/json`
- `spdlog`
- `CLI11`
- standard library only

### gui
- `wxWidgets`
- `nlohmann/json`
- `spdlog`
- `app`

### cli
- `CLI11`
- `app`
- `client`
- `server`
- `plugin-sdk`

## Dependency Management
- use a CMake manifest-based workflow
- prefer package manager packages for lightweight third-party libraries
- use git submodules for heavyweight compiled dependencies that we build in-tree
- keep static builds as the default for the shipped binaries
- use FetchContent only for small or header-only dependencies when needed
- vendor only when a dependency is unavailable or packaging needs it
- keep the dependency list in one place
- Boost is not an allowed dependency

## HTTP Stack Rule
- use `cpp-httplib` as the only HTTP server/client stack
- keep HTTP transport behind a small interface
- do not leak `cpp-httplib` types into public protocol or app APIs
- run long-lived or blocking handlers through bounded runtime control
- support stdio transport without HTTP dependencies
- do not add standalone Asio unless a non-HTTP async transport proves it is necessary

## Why wxWidgets
- cross-platform
- native look and feel
- no Qt
- suitable for forms, trees, lists, dialogs, and admin-style tooling
- better fit for a management console than an immediate-mode debug UI

## GUI Shape

The GUI should be an operational console, not a marketing shell.

Main views:
- server profiles
- tool registry
- resource registry
- prompt registry
- plugin sources
- permissions and policy
- logs and diagnostics

Main actions:
- add tools from local files
- add tools from a MCP server endpoint
- enable / disable tools
- group tools by profile
- start / stop server instances
- import / export configuration
- inspect request and response payloads

## Tool Data Model

### ToolSource
Where a tool comes from.
- local manifest
- local plugin
- remote MCP server
- generated adapter

### ToolDescriptor
The normalized record used by CLI, GUI, and runtime.
- stable id
- display name
- description
- input schema
- output shape
- source
- enabled state
- approval state
- profile binding

### Profile
A user-managed container for runtime settings.
- server endpoints
- auth settings
- environment variables
- enabled tools
- permissions
- logging and limits

### Policy
User control over what is allowed.
- allow
- deny
- require approval
- read-only
- network access
- filesystem access
- command execution

## Public API Shape

### protocol
- `Request`
- `Response`
- `Error`
- `make_response`
- `make_error`
- `parse_request`

### client
- `Client`
- `send`
- `stream`
- `call_tool`
- `list_tools`

### server
- `ServerBuilder`
- `Server`
- `Transport`
- `ToolRegistry`
- `ResourceRegistry`
- `PromptRegistry`

### plugin-sdk
- `Tool`
- `ToolContext`
- `ToolResult`
- `StreamEmitter`

### app
- `ProfileStore`
- `ToolCatalog`
- `PolicyManager`
- `ImportExportService`
- `ToolSource`
- `ToolDescriptor`
- `Profile`
- `Policy`

### gui
- `MainFrame`
- `ProfilePanel`
- `ToolCatalogPanel`
- `PolicyPanel`

## Dependency Rules
- protocol must not depend on server
- client must not depend on plugin internals
- server may depend on protocol and client transport primitives
- plugin-sdk must depend on protocol only
- adapters may depend on protocol and plugin-sdk
- app may depend on protocol, client, server, and plugin-sdk
- gui may depend on app and client, but not on server internals
- cli may depend on app, client, server, and plugin-sdk
- gui and cli must not duplicate tool management logic
- public headers should stay thin and stable
- avoid exposing transitive implementation dependencies in public headers
- avoid vendoring libraries unless a package manager cannot satisfy the build
- prefer one HTTP stack and one JSON stack only
- do not add a second GUI toolkit
- do not add a second logging framework
- do not add both a lightweight HTTP stack and a separate competing HTTP stack unless there is a concrete reason

## Implementation Phases

### Phase 0: reset
- keep the repo empty on a rewrite branch
- freeze scope and non-goals

### Phase 1: protocol core
- define request and response types
- define error mapping
- define serialization and validation
- add tests for protocol compliance

### Phase 2: client SDK
- add request helpers
- add transport client
- add response parsing
- add streaming helper

### Phase 3: server runtime
- add registries
- add dispatch
- add session handling
- add auth and rate-limit hooks
- add stdio and HTTP transports

### Phase 4: plugin SDK and adapters
- define tool extension API
- add built-in adapters for common local tools
- keep plugin loading optional

### Phase 5: app layer
- define profile and policy storage
- define shared tool catalog services
- define import / export
- define one-click tool management workflows

### Phase 6: GUI and CLI
- add CLI commands
- add wxWidgets GUI
- wire both to the shared app layer

### Phase 7: packaging and tests
- add CLI binary
- add unit tests
- add integration tests
- add minimal example configs

## Development Mode

Use contract-first, test-first development.

This is not strict TDD for every UI detail. It is strict where correctness matters.

### Protocol
- write tests first
- use golden JSON fixtures
- verify request / response / error shapes
- no implementation without tests

### Client and server
- build vertical slices
- start with client-server round-trip tests
- test stdio and HTTP separately
- keep transport behind interfaces

### App layer
- test profile, policy, catalog, and import/export logic
- keep GUI and CLI thin
- no duplicated business logic in GUI or CLI

### CLI
- test command parsing
- test command output with stable snapshots
- test non-zero exits

### GUI
- do not TDD pixel layout
- test view-model or app-service behavior
- keep wxWidgets code as thin as possible

### Adapter and plugin SDK
- contract tests first
- each adapter must register deterministic tool descriptors
- each adapter must have tool-call tests

### Rule
No feature enters core without either a protocol test, a unit test, or an integration test.

## Test Strategy
- protocol tests first
- client round-trip tests
- server dispatch tests
- transport tests
- adapter tests
- plugin contract tests

## Migration Rule
If a feature does not help a user expose or consume MCP capabilities in a standard way, it does not belong in v2 core.

## Notes On Tool Management
- The app layer should aggregate all known tool sources.
- A tool source may be local, remote, or generated.
- Users must be able to approve, disable, group, and rename tools.
- The GUI should expose the same actions as the CLI.
- No action should be hard-coded only in the GUI.
- One-click management means one normalized catalog, one policy model, and one set of actions for all sources.
