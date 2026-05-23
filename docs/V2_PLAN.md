# cxxmcp v2 Plan

## Goal
Build a small, spec-first MCP runtime / gateway for local tools, internal services, and upstream MCP servers.

## Product Positioning
This project should be a lightweight MCP Runtime / Gateway.

It should help users add, run, supervise, and expose MCP servers through standard transports and a stable SDK surface.

The central product object is an MCP server registry plus exposure profiles. Tools, prompts, and resources are discovered
capabilities of registered MCP servers, not the root product model.

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

5. MCP server hosting and gateway
   - A user adds existing MCP servers from Trae, VS Code, Claude Desktop, Cursor, or manual JSON config.
   - The runtime starts stdio servers, connects to HTTP servers, performs MCP initialize, and discovers tools/prompts/resources.
   - The user creates profiles that expose selected capabilities on different local ports, paths, and instructions.
   - Downstream MCP clients connect to this runtime instead of connecting to every upstream server directly.

## Non-Goals
- No plugin marketplace
- No all-in-one platform
- No custom protocol fork
- No complex hot reload as a core feature
- No feature-heavy sample tool bundle in the runtime
- No GUI-only business logic
- No MCP config format fork unless required for import/export compatibility

## Capability Matrix
The public surface should cover the feature set used by current official MCP SDKs, not just the
old tool-only path.

### Must-have
- tools
- prompts
- resources
- roots
- logging
- completions
- sampling
- elicitation
- subscriptions / change notifications
- raw JSON-RPC escape hatches

### Transport and compatibility
- stdio
- streamable HTTP
- legacy SSE compatibility behind the same facade
- JSON-only request/response mode for constrained hosts

### Runtime and gateway
- discovery from upstream MCP servers
- profile binding
- policy checks
- auth hooks
- request routing
- hosted downstream endpoints
- tasks / long-running job tracking

### App-only workflows
- import / export
- server catalog
- exposure catalog
- approval / trust workflows
- one-click add / enable / disable / remove
- CLI and GUI reuse the same app services

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
- MCP server registry
- exposure profile storage
- discovered capability catalog
- tool/prompt/resource aggregation
- user-facing policy model
- import / export
- commands shared by CLI and GUI

## Dependency Inventory

We should keep the dependency set explicit and boring. If a library already solves the problem
cleanly, use it instead of hand-rolling another subsystem.

### Core libraries
- `nlohmann/json` for JSON value handling
- `jsonrpcpp` for JSON-RPC 2.0 parsing and serialization
- `cpp-httplib` for HTTP/HTTPS server, client, and SSE/streaming transport
- `OpenSSL` for TLS
- `spdlog` for logging and diagnostics
- `tl::expected` or an equivalent C++17-compatible expected implementation for the facade layer

### CLI libraries
- `CLI11` for command line parsing

### GUI libraries
GUI work is paused and has no active CMake target in the current rewrite.
When it returns, choose the desktop shell separately and keep all product logic in `app`.

### Test libraries
- `GoogleTest` for unit and integration tests

### Optional libraries
- `miniz` only if archive packaging or compressed exports become necessary
- `pybind11` only if we keep a Python plugin bridge
- `fmt` only as a transitive dependency of the logging layer if needed

## C++ Standard Usage

Baseline: C++23 for public API drafts.

C++20-compatible fallbacks may be added later only if packaging or compiler support requires them.

The planned high-level facade should remain C++17-friendly in syntax and ownership style.
That means:
- no required use of concepts in the public surface
- no required use of coroutines in the public surface
- no required use of `std::expected` in the facade headers
- fluent builders and callback-based handlers are preferred over heavy template metaprogramming

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
- `jsonrpcpp`
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
- standard library only

### gui
Deferred. There is no active GUI layer in the current CMake graph.

### cli
- `CLI11`
- `app`
- `client`
- `server`
- `plugin-sdk`
- `spdlog`

## Dependency Management
- use a CMake manifest-based workflow
- prefer package manager packages for lightweight third-party libraries
- use git submodules for heavyweight compiled dependencies that we build in-tree
- keep static builds as the default for the shipped binaries
- use FetchContent only for small or header-only dependencies when needed
- vendor only when a dependency is unavailable or packaging needs it
- keep the dependency list in one place
- Boost is not an allowed dependency
- do not reimplement protocol, HTTP, CLI, or logging stacks unless a concrete gap exists

## HTTP Stack Rule
- use `cpp-httplib` as the only HTTP server/client stack
- keep HTTP transport behind a small interface
- do not leak `cpp-httplib` types into public protocol or app APIs
- run long-lived or blocking handlers through bounded runtime control
- support stdio transport without HTTP dependencies
- do not add standalone Asio unless a non-HTTP async transport proves it is necessary

## Deferred GUI Notes
GUI is intentionally out of the active build for now. The next GUI decision should be made after the CLI and gateway runtime are stable.

## GUI Shape

The GUI should be an operational console, not a marketing shell.

Main views:
- MCP servers
- exposure profiles
- discovered tools
- discovered resources
- discovered prompts
- gateway endpoints
- permissions and policy
- logs and diagnostics

Main actions:
- add MCP servers from command/args/env
- add MCP servers from HTTP URL/headers
- import Trae / VS Code / Claude / Cursor config
- start / stop / restart upstream server instances
- discover tools, prompts, and resources
- bind capabilities into exposure profiles
- run different profiles on different ports and prompts
- import / export configuration
- inspect request and response payloads

## Gateway Core Model

### McpServerDefinition
The configured upstream MCP server.
- stable id
- name and display name
- transport: `stdio`, `streamable_http`, or `legacy_sse`
- stdio command, args, cwd, env
- HTTP URL and headers
- enabled / auto-start flags
- trust state: untrusted, trusted, blocked
- tags

### McpServerRuntime
The live state of a configured MCP server.
- server id
- state: stopped, starting, initializing, running, degraded, failed
- process id for stdio servers
- session id for HTTP transports
- negotiated protocol version
- negotiated capabilities
- last error and log tail

### DiscoveredCapability
The normalized result of `tools/list`, `prompts/list`, and `resources/list`.
- kind: tool, prompt, resource
- upstream server id
- upstream name
- exposed name
- title and description
- input/output schema where available
- URI or prompt template where applicable
- capability hash for change detection

### ExposureProfile
The downstream surface exposed by this runtime.
- profile id and name
- instructions / system prompt fragment
- hosted endpoint: host, port, path, transport
- capability bindings
- environment overrides

### CapabilityBinding
The explicit connection between an upstream capability and a downstream profile.
- upstream server id
- capability kind
- upstream name
- exposed name
- namespace strategy: none, server prefix, custom
- enabled state
- policy

## Tool Data Model

Legacy note: tool catalog remains as a normalized capability view, but server registry is the primary model.

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
- `roots`
- `list_prompts`
- `list_resources`
- `get_prompt`
- `read_resource`
- `complete`
- `on_sample`
- `on_elicit`
- `on_log`
- `notify`

### server
- `ServerBuilder`
- `Server`
- `Transport`
- `ToolRegistry`
- `ResourceRegistry`
- `PromptRegistry`
- `RootsProvider`
- `LoggingSink`
- `SamplingHandler`
- `ElicitationHandler`

### plugin-sdk
- `Tool`
- `ToolContext`
- `ToolResult`
- `StreamEmitter`

### app
- `ProfileStore`
- `McpServerStore`
- `CapabilityCatalog`
- `ExposureProfileStore`
- `ToolCatalog`
- `PolicyManager`
- `ImportExportService`
- `McpServerDefinition`
- `DiscoveredCapability`
- `ExposureProfile`
- `CapabilityBinding`
- `ToolSource`
- `ToolDescriptor`
- `Profile`
- `Policy`
- `GatewayRuntime`
- `GatewayRoutingService`
- `GatewayStatusService`

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
- define MCP server registry
- define discovered capability catalog
- define exposure profile store
- define shared tool catalog services
- define import / export
- define one-click tool management workflows

### Phase 6: gateway runtime
- implement stdio upstream client session
- implement Streamable HTTP upstream client session
- implement MCP discovery from upstream servers
- implement gateway routing from downstream requests to upstream servers
- implement hosted endpoints per exposure profile

### Phase 7: CLI
- add CLI commands
- wire CLI to the shared app layer

### Phase 8: packaging and tests
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
- keep CLI thin
- no duplicated business logic in CLI

### CLI
- test command parsing
- test command output with stable snapshots
- test non-zero exits

### GUI
Deferred until the gateway runtime and CLI are stable.

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
