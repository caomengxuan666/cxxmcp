# Ecosystem Maturity Evidence

This ledger tracks the evidence required before cxxmcp resubmits to the vcpkg
curated registry or claims fact-standard readiness. It is intentionally stricter
than local green tests: maturity requires repeated public evidence, downstream
usage, and release discipline over time.

## Current Status

Do not resubmit to the vcpkg curated registry yet. The project has strong local
SDK evidence and package-manager smoke coverage, but still needs public release
history and sustained green release-gate evidence before the curated-registry
review can be answered without policy exceptions.

The previous curated-registry attempt was
`microsoft/vcpkg#51972`. It passed the technical vcpkg checks, but the PR was
closed by a maintainer on 2026-05-27 because cxxmcp did not yet meet vcpkg's
minimum project maturity requirement. Resubmission must therefore lead with
maturity evidence, not only a corrected portfile.

## Evidence Ledger

| Area | Required Evidence | Current Evidence | Status |
|---|---|---|---|
| Stable release history | Multiple tagged SDK source releases with source archives, checksums, and compatibility notes. | `release-sdk` publishes SDK source archive, `SHA256SUMS.txt`, and `RELEASE_NOTES.md` on `v*` tags. | In progress |
| Green release gates over time | Repeated successful `release-gates` runs for the exact release commits being advertised. | The workflow declares release-blocking matrix legs and uploads CTest/JUnit/log artifacts. | In progress |
| Scheduled compiler compatibility | Non-release-blocking compiler checks must be clearly separated from release-supported target claims. | `compiler-compat` runs MinGW UCRT64 GCC and MinGW CLANG64 Clang as provisional best-effort compatibility evidence through `windows-mingw-ucrt64-gcc` and `windows-mingw-clang64-clang`; because the `mingw-sdk` job remains `continue-on-error`, MinGW is not release-supported. | Provisional |
| Installed package evidence | Package smoke from installed output across supported compiler/generator/runtime matrix entries. | `package_smoke` is release-blocking; `release-gates` now declares real vcpkg overlay default/http/websocket/OpenSSL/auth combinations plus Conan and xmake default/http/websocket/auth smoke jobs. Local vcpkg default/auth overlay smoke has passed on `x64-windows-static`, local Conan default/auth package creation passed on Windows/MSVC Release, and local xmake default-package consumption passed on Windows/MSVC. Local xmake auth against an older published archive failed because that archive did not expose the current auth header surface, so release-gates now rewrites a temporary xmake repository to consume a generated source archive from the exact workflow commit. These local runs are useful triage evidence, not substitutes for exact-commit release artifacts. | In progress |
| Downstream examples | External consumer or example repository using normal package consumption. | `templates/external_consumer` is package-smoke checked; `../cxxmcp-examples` is the external downstream example suite and minimum green scenario list. The examples CI now has adjacent-source and installed-package modes. A local installed-package build against a Release SDK install passed all 21 CTest examples on 2026-05-27. | In progress |
| Changelog discipline | Every release has a matching `CHANGELOG.md` section and compatibility notes. | `check_release_evidence.py` verifies the current project version appears in `CHANGELOG.md`; `release-sdk` emits compatibility notes. | In progress |
| Public user adoption | Reproducible downstream users, issues, or integration reports that can be cited in a registry PR. | `docs/adoption_ledger.md` now records the adoption evidence rules and explicitly distinguishes project-owned examples from independent public downstream use. No independent public downstream adoption is recorded yet. | Missing |
| Curated dependency shape | Registry build uses vcpkg dependencies where available and does not expose private implementation dependencies as cxxmcp public targets. | The overlay port uses `tl-expected`, `nlohmann-json`, and optional `cpp-httplib` from vcpkg; the cross-cutting `openssl` feature now covers HTTPS, WSS, and auth crypto without creating transport-specific OpenSSL feature names. | Prepared |
| Curated port shape | Future port uses `vcpkg_from_github()`, a release tag, SHA512 source hash, SDK-only options, and no forced `BUILD_SHARED_LIBS`. | `packaging/vcpkg/curated-portfile.future.cmake` records the intended shape. | Prepared |

## Resubmission Rule

The vcpkg curated-registry PR should be reopened only when all of these are
true:

- at least one release tag has public SDK source, checksum, and release notes
  artifacts attached;
- the release-gates matrix is green for the commit used by that release tag;
- package-smoke evidence exists for every compiler/generator/runtime mode the
  release claims;
- the curated port uses a release source archive and SHA512 hash, not the local
  overlay `SOURCE_PATH`;
- at least one downstream example or consumer can be cited;
- `docs/adoption_ledger.md` contains at least one independent public downstream
  adoption entry, not only project-owned templates or examples;
- `CHANGELOG.md`, `RELEASE_NOTES.md`, package docs, and compatibility policy
  describe the same SDK contract.

Until then, the overlay port remains the supported vcpkg path.

## Downstream Examples Evidence

The external `../cxxmcp-examples` repository is evidence for ecosystem maturity
only when it is treated as a downstream consumer, not as extra in-tree tests.
Do not modify that repository from this ledger. For a release candidate, record
the exact commit tested, the cxxmcp commit or release tag consumed, the build
mode, and whether it used an adjacent source checkout or an installed package.

The installed-package mode is the most relevant vcpkg signal:

```powershell
cmake -S ../cxxmcp-examples -B ../cxxmcp-examples/build-installed -DCXXMCP_EXAMPLES_USE_ADJACENT_SDK=OFF -DCMAKE_PREFIX_PATH=<install-prefix>
cmake --build ../cxxmcp-examples/build-installed --config Release
ctest --test-dir ../cxxmcp-examples/build-installed -C Release --output-on-failure
```

The external examples CI must keep both modes green:

- adjacent-source mode proves the examples stay current with the SDK source
  tree during development;
- installed-package mode first installs cxxmcp into a prefix, then configures
  the examples with `CXXMCP_EXAMPLES_USE_ADJACENT_SDK=OFF` and
  `CMAKE_PREFIX_PATH=<install-prefix>` so `find_package(cxxmcp CONFIG
  REQUIRED)` is the only SDK discovery path.

Adjacent-source mode is still useful before a package is installed, especially
for detecting public-header and target regressions while the SDK branch is in
motion. It should not be the only evidence cited in a vcpkg curated-registry
resubmission.

Minimum green downstream scenarios:

- `cxxmcp_sdk_smoke` for the broad SDK loopback surface: tools, prompts,
  resources, templates, completion, sampling, logging, raw requests,
  notifications, and task-backed tools.
- `cxxmcp_minimal_stdio_server` and `cxxmcp_process_stdio_client_probe` for
  stdio authoring, child-process launch, `ClientPeer::connect_stdio`, and
  `mcp::serve`.
- `cxxmcp_streamable_http_client`,
  `cxxmcp_direct_http_legacy_sse_matrix`, and
  `cxxmcp_http_auth_lite_matrix` for Streamable HTTP, legacy SSE compatibility,
  and bearer-token/auth identity propagation.
- `cxxmcp_async_request_matrix`,
  `cxxmcp_timeout_cancellation_client`,
  `cxxmcp_client_inbound_cancellation_matrix`,
  `cxxmcp_pagination_cursor_matrix`,
  `cxxmcp_client_subscription_helper_matrix`, and
  `cxxmcp_task_cancel_matrix` for request handles, timeout/cancel behavior,
  inbound cancellation, pagination, subscriptions, and task cancellation.
- `cxxmcp_typed_tool_server`, `cxxmcp_handler_interface_matrix`,
  `cxxmcp_server_to_client_context_matrix`,
  `cxxmcp_native_server_transport_matrix`,
  `cxxmcp_rich_content_cancellation_matrix`, and
  `cxxmcp_transport_adapter_matrix` for authoring ergonomics, handler
  contracts, server-to-client callbacks, custom role-generic transports, rich
  content, and adapter compatibility.
