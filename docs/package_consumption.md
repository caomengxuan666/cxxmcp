# Package Consumption

This document covers lightweight consumption paths that are useful before or
outside central package registries. They are SDK-only paths: gateway and other
tooling live outside this package contract.

## Dependency Policy

The SDK has two supported dependency modes:

- Default source/archive builds use bundled header-only SDK dependencies so
  FetchContent, CPM.cmake, and direct source installs work without a package
  manager. The install tree includes `tl/expected.hpp` and
  `nlohmann/json.hpp`.
- Registry builds should set `CXXMCP_USE_SYSTEM_DEPS=ON` and use package
  manager dependencies for `tl-expected`, `nlohmann-json`, and `cpp-httplib`.
  In this mode the install tree must not vendor `tl` or `nlohmann` headers.

`cpp-httplib` is a transport implementation dependency. It is intentionally not
installed as a public SDK header. Downstream code should use
`cxxmcp/transport/http_transport.hpp`, `cxxmcp/client/http_transport.hpp`, or
`cxxmcp/server/http_transport.hpp` instead of including `httplib.h`.

The optional WebSocket transport is also backed by `cpp-httplib`. Package
managers should expose it as a separate opt-in feature/config that maps to
`CXXMCP_ENABLE_WEBSOCKET=ON` and keeps the default SDK package path off.

Tooling dependencies such as spdlog and CLI11 are outside the SDK package
contract. vcpkg/Conan package submissions for the SDK should keep examples,
tests, and docs disabled by default.

## vcpkg Overlay Port

`cxxmcp-sdk` is not in the vcpkg curated registry. The repository-hosted port at
`packaging/vcpkg/ports/cxxmcp-sdk` is the supported vcpkg path for now, and should
be consumed as an overlay port from a checkout of this repository.

For a one-off install:

```powershell
vcpkg install cxxmcp-sdk --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
```

For manifest mode, keep your application manifest narrow:

```json
{
  "dependencies": [
    "cxxmcp-sdk"
  ]
}
```

Then install with the overlay path:

```powershell
vcpkg install --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
```

An example `vcpkg-configuration.json` shape is available at:

```text
packaging/vcpkg/vcpkg-configuration.overlay-example.json
```

Copy it next to your downstream `vcpkg.json` and replace the
`builtin-baseline` placeholder with the vcpkg commit your project pins. The
`overlay-ports` path is relative to the directory containing
`vcpkg-configuration.json`; adjust it if your checkout lives elsewhere.

The overlay port builds only the C++17 SDK package targets. It sets
`CXXMCP_USE_SYSTEM_DEPS=ON`, disables examples, tests, and docs, and depends on
vcpkg packages for `tl-expected`, `nlohmann-json`, and
`cpp-httplib`. It does not make spdlog, CLI11, or external gateway tooling part
of SDK package consumption.

The optional auth scaffold is exposed as an opt-in feature and is not part of
the default vcpkg package path:

```powershell
vcpkg install "cxxmcp-sdk[auth]" --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
```

The `auth` feature maps to `CXXMCP_ENABLE_AUTH=ON`. It enables the
transport-neutral OAuth/DPoP contracts without pulling OpenSSL into the default
package path.

The `openssl` feature is a single cross-cutting opt-in feature. Combine it with
`http` for HTTPS transport support, with `websocket` for WSS support, and with
`auth` for the `cxxmcp::auth_openssl` JOSE/JWT/DPoP helpers:

```powershell
vcpkg install "cxxmcp-sdk[http,openssl]" --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
vcpkg install "cxxmcp-sdk[websocket,openssl]" --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
vcpkg install "cxxmcp-sdk[auth,openssl]" --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
```

The `websocket` feature maps to `CXXMCP_ENABLE_WEBSOCKET=ON` and implicitly
enables HTTP transport support because both transports share `cpp-httplib`:

```powershell
vcpkg install "cxxmcp-sdk[websocket]" --overlay-ports=C:\path\to\cxxmcp\packaging\vcpkg\ports
```

## Future vcpkg Registry Paths

If users need vcpkg versioning before the curated registry accepts the port,
the next step is a standalone or repository-hosted custom Git registry for the
same SDK-only port. A future configuration shape is sketched at:

```text
packaging/vcpkg/vcpkg-configuration.git-registry-future-example.json
```

That file is intentionally an example, not an active registry promise. Replace
the repository URL and both baseline placeholders with real registry commits
before using it.

Curated-registry resubmission is gated by
`docs/ecosystem_maturity_evidence.md`, not by the presence of the overlay port
alone.

A future vcpkg curated-registry pull request should differ from the local
overlay port in these ways:

- fetch source with `vcpkg_from_github()` from a release tag and SHA512 source
  archive hash, instead of using the local checkout as `SOURCE_PATH`; use
  `packaging/vcpkg/curated-portfile.future.cmake` as the current review sketch;
- keep `vcpkg_check_linkage(ONLY_STATIC_LIBRARY)` while the SDK libraries are
  explicitly built as static libraries and shared-library ABI support is not
  claimed; do not force `-DBUILD_SHARED_LIBS=OFF` in the portfile;
- keep SDK-only build options enabled and examples, tests, and docs disabled;
- keep default `cpp-httplib` consumption as loopback HTTP without TLS, and use
  one cross-cutting `openssl` feature for HTTPS, WSS, and auth crypto rather
  than adding transport-specific TLS feature names;
- keep OpenSSL out of the default SDK package path; `auth`, `http`, and
  `websocket` only gain OpenSSL behavior when the user also selects `openssl`;
- keep package smoke evidence for default, HTTP, WebSocket, auth, and
  OpenSSL-enabled combinations; default installs must not expose `cxxmcp::auth`
  or private implementation targets such as `cxxmcp::cpp_httplib`.

## FetchContent

Prefer the SDK source release archive over GitHub's generated source archive.
The SDK archive includes the header-only SDK dependencies needed by the default
bundled build, including the vendored `cpp-httplib` fallback used by HTTP and
WebSocket transports.

The concrete `v1.1.4` URL below is the latest published SDK source archive
known to these docs. It is valid for consumers that want the published default
SDK surface. Do not use it as evidence for the current worktree's optional auth
header surface; current-source or release-candidate validation must use the
exact source archive and checksum produced by that release-gates run.

```cmake
include(FetchContent)

FetchContent_Declare(
    cxxmcp
    URL https://github.com/caomengxuan666/cxxmcp/releases/download/v1.1.4/cxxmcp-sdk-source-v1.1.4.tar.gz
    URL_HASH SHA256=c222d7e0752bff2d16457a427441acc654249b3102851163a6d6d4c411b8d9fb
)

set(CXXMCP_BUILD_SDK ON CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cxxmcp)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE cxxmcp::server)
```

## CPM.cmake

`CPM.cmake` can consume the same SDK source archive. Keep the SDK options
explicit so downstream builds do not accidentally enable examples, tests, or
docs.

Use the URL and hash from the release you intentionally pin. For release
candidate validation, use the exact source artifact produced by that candidate
run rather than copying a previously published release example unchanged.

cxxmcp does not install or export a `CPM.cmake` helper. The consuming project
must provide it, for example by vendoring `cmake/CPM.cmake` in its own source
tree or by bootstrapping it before the `include()` call. The path below is a
consumer-owned file path, not a file supplied by cxxmcp.

```cmake
include(cmake/CPM.cmake)

set(CXXMCP_BUILD_SDK ON CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CXXMCP_BUILD_DOCS OFF CACHE BOOL "" FORCE)

CPMAddPackage(
    NAME cxxmcp
    URL https://github.com/caomengxuan666/cxxmcp/releases/download/v1.1.4/cxxmcp-sdk-source-v1.1.4.tar.gz
    URL_HASH SHA256=c222d7e0752bff2d16457a427441acc654249b3102851163a6d6d4c411b8d9fb
)

add_executable(my_client main.cpp)
target_link_libraries(my_client PRIVATE cxxmcp::client)
```

## Conan

The root `conanfile.py` keeps auth disabled by default. The default package is
the SDK-only route:

```powershell
conan create . -o cxxmcp/*:with_auth=False -o cxxmcp/*:with_examples=False -o cxxmcp/*:with_tests=False -s build_type=Release
```

Consumers that need the optional auth scaffold must opt in explicitly:

```powershell
conan create . -o cxxmcp/*:with_auth=True -o cxxmcp/*:with_examples=False -o cxxmcp/*:with_tests=False -s build_type=Release
```

`with_auth=True` maps to `CXXMCP_ENABLE_AUTH=ON` and exposes the
`cxxmcp::auth` component. The default Conan package remains SDK-only and does
not export auth headers or OpenSSL requirements.

`with_websocket=True` maps to `CXXMCP_ENABLE_WEBSOCKET=ON` and implicitly
enables `CXXMCP_ENABLE_HTTP=ON`.

## Narrow SDK Targets

Choose the narrowest public target that matches the binary you are building.
This keeps transitive dependencies predictable and avoids pulling SDK layers
that the consumer does not use.

```cmake
find_package(cxxmcp CONFIG REQUIRED)

add_library(protocol_only protocol_only.cpp)
target_link_libraries(protocol_only PRIVATE cxxmcp::protocol)

add_executable(my_client client_main.cpp)
target_link_libraries(my_client PRIVATE cxxmcp::client)

add_executable(my_server server_main.cpp)
target_link_libraries(my_server PRIVATE cxxmcp::server)

add_executable(loopback loopback.cpp)
target_link_libraries(loopback PRIVATE cxxmcp::sdk)
```

- Use `cxxmcp::protocol` for protocol models, JSON-RPC envelopes, and
  serialization helpers only.
- Use `cxxmcp::client` for an embeddable MCP client. It brings the protocol
  and transport SDK layers needed by client transports.
- Use `cxxmcp::server` for an embeddable MCP server. It brings the protocol
  and transport SDK layers needed by server transports.
- Use `cxxmcp::sdk` only when one target intentionally needs protocol, client,
  and server APIs together, such as loopback tests or SDK examples.

## External Consumer Template

A minimal out-of-tree CMake project is kept in:

```text
templates/external_consumer
```

It is intentionally small: one `find_package(cxxmcp CONFIG REQUIRED)`, one
executable, and one narrow SDK target link. The package-smoke test configures
and builds this template against the installed SDK output so the template stays
valid for release candidates.

## xmake-repo

The xmake-repo recipe draft lives at:

```text
packaging/xmake/packages/c/cxxmcp/xmake.lua
```

It builds the same SDK source archive and disables examples, tests, and docs.
Submit it to xmake-repo after the package interface is stable enough for
registry review. The recipe has opt-in `auth` and `websocket` configs. The
`websocket` config maps to `CXXMCP_ENABLE_WEBSOCKET=ON` and implicitly enables
HTTP support; the defaults remain auth-off and websocket-off.
