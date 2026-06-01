# SDK Compile-Time And Hot-Path Debt

This ledger tracks compile-time and hot-path allocation work that affects SDK
consumers. It is intentionally separate from feature roadmaps: entries here are
not user-facing capability gaps unless they change the public package contract.

## Current Gates

- `protocol_serialization_benchmark` records JSON-RPC serialization output
  bytes, elapsed time, allocation count, and allocated bytes for request,
  response, notification, and error-response hot paths.
- `public_header_*` tests compile public SDK headers independently.
- `cxxmcp-public-header-compile-evidence-linux-gcc-ninja` records
  target-level elapsed time for the public-header compile fixtures before
  `json_fwd` or `extern template` decisions are made.
- `package_smoke` configures and builds an installed C++17 consumer project.
- `scripts/check_release_evidence.py --source .` verifies that this ledger and
  the related release evidence files are present.

## Local Evidence Review

- Ran local Windows/MSVC Debug `protocol_serialization_benchmark` from
  `build-auth-openssl` on 2026-05-28. All benchmark cases completed. The
  largest current allocation counts are the `_meta` large request and
  notification paths, both around `743k` to `754k` allocations per 1000
  iterations in this local Debug run. This is diagnostic evidence only; release
  notes must cite the Linux GCC Release artifact before making measured
  hot-path claims.
- Reviewed local Windows/MSVC Debug evidence from
  `build-auth-openssl/public-header-compile-evidence.json`, generated
  `2026-05-28T00:05:05Z` from `build-auth-openssl` with serial builds after
  the handler-header split and example include fix. The public-header targets
  all completed successfully in roughly `1.09s` to `6.91s`.
- This is local Debug evidence only. It does not replace the release-gates
  `cxxmcp-public-header-compile-evidence-linux-gcc-ninja` artifact, and it is
  not enough by itself to justify `json_fwd` or `extern template` work.

## Ledger

| Area | Status | Evidence | Next action |
| --- | --- | --- | --- |
| JSON-RPC envelope serialization | Improved | `sdk/protocol/src/serialization.cpp`, `tests/protocol/serialization_benchmark.cpp` | Keep benchmark output attached to release-candidate evidence before claiming performance wins. |
| Request cancellation key allocation | Fixed | Client/server request-id keys use typed `i:` / `s:` prefixes instead of temporary JSON dumps. | Revisit only if request-id correlation maps become measurable hot paths. |
| `RequestHandle` wait strategy | Fixed | `RequestHandle::await_response()` uses `AsyncResult<T>` with CV-based blocking. Cancellation token callback registration wakes request handles without occupying request executor workers. `TaskHandle::wait()` uses CV-based push notification with RPC poll fallback. All polling and `sleep_for` waits eliminated. | No follow-up needed. |
| Registry `list()` sorting | Fixed | Tool, prompt, resource, and resource-template registries cache sorted list snapshots and invalidate on mutation. | Add larger registry benchmarks before making throughput claims. |
| Session transport duplicate lookup | Fixed | Server tracks session transport pointers in a side set while preserving vector fan-out order. | No immediate follow-up unless transport ownership changes. |
| `server.hpp` authoring template weight | Improved | Stable `Server` / `ServerBuilder` declarations are split from authoring helpers. Public-header compile evidence artifact is wired for Linux GCC Release. | Review release artifact timings before adding `extern template` declarations. |
| `nlohmann::json` public include cost | Open | Protocol headers still expose `protocol::Json = nlohmann::json`; public-header compile evidence artifact is wired for Linux GCC Release. | Evaluate `json_fwd` only if package smoke and public header compile-time evidence show consumer pain. |
| `Transport<Role>` template instantiation | Open | Role-generic transport remains header-only; public-header compile evidence artifact is wired for Linux GCC Release. | Evaluate explicit instantiation only after compiler matrix evidence confirms the ABI/package impact is acceptable. |

## Release Evidence Procedure

Before a release candidate that claims compile-time or hot-path improvement:

1. Run `ctest --test-dir <build> -R "^protocol_serialization_benchmark$" --output-on-failure`.
2. Run the public header compile tests for the advertised compiler matrix.
3. Attach `cxxmcp-public-header-compile-evidence-linux-gcc-ninja` when making
   compile-time debt decisions.
4. Run installed-package `package_smoke` from the same commit.
5. Attach benchmark output and package-smoke logs to release evidence.
6. Update this ledger when an item moves between Open, Improved, and Fixed.
