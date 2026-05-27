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

## Evidence Ledger

| Area | Required Evidence | Current Evidence | Status |
|---|---|---|---|
| Stable release history | Multiple tagged SDK source releases with source archives, checksums, and compatibility notes. | `release-sdk` publishes SDK source archive, `SHA256SUMS.txt`, and `RELEASE_NOTES.md` on `v*` tags. | In progress |
| Green release gates over time | Repeated successful `release-gates` runs for the exact release commits being advertised. | The workflow declares release-blocking matrix legs and uploads CTest/JUnit/log artifacts. | In progress |
| Installed package evidence | Package smoke from installed output across supported compiler/generator/runtime matrix entries. | `package_smoke` is release-blocking; local vcpkg default/auth overlay smoke has passed on `x64-windows-static`. | In progress |
| Downstream examples | External consumer or example repository using normal package consumption. | `templates/external_consumer` is package-smoke checked; `cxxmcp-examples` is documented as downstream validation. | In progress |
| Changelog discipline | Every release has a matching `CHANGELOG.md` section and compatibility notes. | `check_release_evidence.py` verifies the current project version appears in `CHANGELOG.md`; `release-sdk` emits compatibility notes. | In progress |
| Public user adoption | Reproducible downstream users, issues, or integration reports that can be cited in a registry PR. | Not yet recorded in this repository. | Missing |
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
- `CHANGELOG.md`, `RELEASE_NOTES.md`, package docs, and compatibility policy
  describe the same SDK contract.

Until then, the overlay port remains the supported vcpkg path.
