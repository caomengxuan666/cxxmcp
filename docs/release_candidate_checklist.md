# Release Candidate Checklist

Use this checklist for every public release candidate before making stable,
curated-registry, or fact-standard claims about cxxmcp.

## Identity

- Release tag:
- Commit SHA:
- MCP protocol snapshot:
- RMCP reference commit:
- TypeScript SDK reference version:
- Python SDK reference version:

## Required Evidence Artifacts

Attach or link all artifacts from `.github/workflows/release-gates.yml`:

- `cxxmcp-release-gates-linux-gcc-ninja`
- `cxxmcp-release-gates-linux-clang-ninja`
- `cxxmcp-release-gates-macos-appleclang-ninja`
- `cxxmcp-release-gates-windows-msvc-ninja-static-runtime`
- `cxxmcp-release-gates-windows-clangcl-ninja-static-runtime`
- `cxxmcp-release-gates-windows-msvc-vs-dynamic-runtime`
- `cxxmcp-performance-evidence-linux-gcc-ninja`
- `cxxmcp-public-header-compile-evidence-linux-gcc-ninja`
- `cxxmcp-auth-release-gate-linux-gcc-ninja`
- `cxxmcp-auth-release-gate-windows-msvc-ninja`
- `cxxmcp-auth-openssl-release-gate-linux-gcc-ninja`
- `cxxmcp-package-vcpkg-default`
- `cxxmcp-package-vcpkg-http`
- `cxxmcp-package-vcpkg-websocket`
- `cxxmcp-package-vcpkg-http-openssl`
- `cxxmcp-package-vcpkg-websocket-openssl`
- `cxxmcp-package-vcpkg-http-auth`
- `cxxmcp-package-vcpkg-websocket-auth`
- `cxxmcp-package-vcpkg-http-auth-openssl`
- `cxxmcp-package-conan-default`
- `cxxmcp-package-conan-http`
- `cxxmcp-package-conan-websocket`
- `cxxmcp-package-conan-http-auth`
- `cxxmcp-package-conan-websocket-auth`
- `cxxmcp-package-xmake-default`
- `cxxmcp-package-xmake-http`
- `cxxmcp-package-xmake-websocket`
- `cxxmcp-package-xmake-http-auth`
- `cxxmcp-package-xmake-websocket-auth`
- `cxxmcp-doxygen-html`
- `cxxmcp-source`
- `cxxmcp-release-evidence`

Release-blocking, auth, OpenSSL auth, and performance CTest artifacts must
contain `CMakeCache.txt`, the relevant CTest JUnit XML, and CTest logs. The
public-header compile evidence artifact must contain `CMakeCache.txt` and
`public-header-compile-evidence.json`. Package-manager artifacts must contain
the downstream configure/build evidence and command logs for their package
manager: vcpkg install plus CMake configure/build logs, Conan create/install
plus CMake configure/build logs, and xmake repo/build logs. These command logs
must be non-empty. The Doxygen artifact must contain the generated HTML index,
and the source artifact must contain `SHA256SUMS.txt`.

The release evidence artifact must contain the README, README_zh, changelog,
contribution guide, security policy, code of conduct, adoption ledger, auth
guides, compatibility policy, public API stability policy, dependency policy,
protocol model audit, performance debt ledger, public API surface manifest,
RMCP source mapping, release process, Peer/Service migration guide, release
gates, release candidate checklist, release notes template, request lifecycle
notes, technical audit, TODO, the external consumer template, release evidence
verifier scripts, and example source files used for the canonical SDK path
review.

Review `docs/technical_audit.md` as a status ledger, not as a standalone
release claim. Its current "fixed" issue count means the audited code defects
are closed in the source tree; package-manager maturity, optional auth/OpenSSL
package proof, public API stability, generated docs, and
performance/compile-time claims still require the exact-commit artifacts below.
Accepted limitations, transport-specific boundaries, and tracked debt must be
reflected in release notes, `docs/performance_debt.md`, or this checklist
before any broader readiness claim is made.

After downloading the workflow artifacts, run the artifact verifier with
`--review-output release-artifact-review.md` and attach the generated review
record to the release notes or release evidence bundle. Fill the tag, commit,
workflow run URL, and release URL fields before publishing.

## Gate Review

- [ ] `source-style` passed formatting, cpplint, protocol coverage, package
      metadata, unresolved source-marker, TODO status, and release artifact
      verifier checks, including the release artifact verifier self-test.
- [ ] Protocol model coverage check passed for `*_from_json` / `*_to_json`
      pairing.
- [ ] RMCP source-drift check passed for the pinned reference checkout,
      `docs/rmcp_source_mapping.json`, and the protocol model audit table.
- [ ] `build-config-smoke` built the SDK Debug and Release configurations.
- [ ] `clang-tidy-public-headers` passed on public SDK header fixtures.
- [ ] All release-blocking CTest labels passed on every advertised matrix leg.
- [ ] `package_smoke` passed from installed output on every advertised matrix
      leg, using the same generator and compiler family as that matrix leg.
- [ ] vcpkg overlay default/auth, Conan default/auth, and xmake default/auth
      package-consumption smoke jobs passed.
- [ ] `CXXMCP_AUTH_CRYPTO=OpenSSL` Linux GCC release gate passed
      `auth_openssl`, all `public_header_auth*` fixtures, and OpenSSL package
      smoke.
- [ ] `protocol_serialization_benchmark` Linux GCC Release evidence is
      attached before making serialization hot-path claims.
- [ ] The benchmark output in
      `cxxmcp-performance-evidence-linux-gcc-ninja` was reviewed and any
      serialization performance wording in release notes is scoped to that
      evidence.
- [ ] Public-header compile-time evidence is attached before deciding whether
      `json_fwd` or `extern template` work is warranted.
- [ ] `docs/technical_audit.md` was reviewed for the exact release commit:
      open code defects remain zero, accepted limitations are disclosed, and
      every entry in `Audit Evidence Map` still maps to tests, release
      artifacts, `docs/performance_debt.md`, or TODO entries.
- [ ] Release notes disclose the stdio blocking-read limitation: stdio
      `std::getline` cannot guarantee that `stop()` interrupts a partial-line
      read; process-stdio and Streamable HTTP are the recommended production
      interop paths pending exact release evidence.
- [ ] Release notes disclose the reconnect boundary: Streamable HTTP/SSE has
      scoped retry/reconnect behavior, while generic cross-transport reconnect
      remains application-owned.
- [ ] Release notes disclose the cancellation-token wait boundary: timeout-only
      waits use exact deadlines, while waits that include atomic
      `CancellationToken` remain cooperative polling until a future public
      cancellation primitive changes that contract.
- [ ] Public-header compile-time evidence review produced a recorded decision
      in `docs/performance_debt.md`: either no-op with rationale, or concrete
      follow-up work for `json_fwd` / `extern template`.
- [ ] Public header compile tests passed on every advertised matrix leg.
- [ ] `public-api-surface.json` was generated from the same commit and reviewed
      against the previous release evidence before claiming public SDK surface
      stability.
- [ ] `scripts/compare_public_api_surface.py --previous <previous>
      --current <current>` passed, or every reported removal/scalar change is
      classified in the public API diff review and release notes.
- [ ] RMCP, TypeScript SDK, and Python SDK interoperability gates passed where
      those runtimes are advertised for the release.
- [ ] `docs/conformance_evidence.md` was refreshed from current
      `modelcontextprotocol/conformance --suite all` server and client runs;
      any sub-suite results are labeled as supporting evidence, not headline
      comparisons.
- [ ] Doxygen HTML was generated from the same commit as the source artifact.
- [ ] Source archive checksum was recorded in release notes.
- [ ] Source archive content verification passed: SDK sources, verifier
      scripts, docs, package-smoke tests, and external consumer templates are
      present; generated Doxygen and external gateway/CLI sources are absent.
- [ ] Dependency review followed `docs/dependency_policy.md`: time-sensitive
      upstream-status claims such as vendored `tl::expected` and hidden
      `cpp-httplib` usage were rechecked for this exact release commit and
      scoped in release notes.
- [ ] Final release tarball content verification passed for release-gates,
      Doxygen HTML, release-gate source, and release evidence archives.
- [ ] `release-artifact-review.md` was generated by
      `scripts/check_release_artifacts.py --review-output`, filled with the
      exact tag, commit, release-gates run URL, and release URL, and attached
      to the release notes or evidence bundle.
- [ ] Re-running `scripts/check_release_artifacts.py` against the assembled
      release artifact directory without `--review-output` still passes, which
      proves `release-artifact-review.md` is present in the published artifact
      set.
- [ ] `scripts/check_release_evidence.py` passed for the source tree and the
      uploaded `cxxmcp-release-evidence` artifact.

## Public Surface Review

- [ ] Protocol model audit matches the pinned MCP/RMCP reference versions.
- [ ] Public headers under `sdk/**/include/cxxmcp` were reviewed for accidental
      runtime, gateway, policy, discovery, profile, or transport-backend leaks.
- [ ] Public API diff review followed
      [Public API Stability](public_api_stability.md) and classified every
      public change as stable additive, experimental, deprecated, behavior
      clarification, bug/security/protocol fix, or breaking.
- [ ] New public API has an explicit stable or experimental status in docs or
      release notes.
- [ ] Experimental APIs are not required by stable entry points and are not
      presented as the first-choice SDK path.
- [ ] Public target list still matches README and CMake package exports.
- [ ] README, compatibility policy, and public API stability docs still state
      that `cxxmcp` is the package/target prefix while `mcp` is the stable C++
      namespace.
- [ ] Deprecated APIs have migration text and use `CXXMCP_DEPRECATED`.
- [ ] Public renames keep old aliases until the next major release.
- [ ] ABI stability is not claimed for static-library releases.

## Canonical SDK Path Review

- [ ] README and README_zh present `Peer` / `Service` before concrete
      `Client` / `Server` APIs.
- [ ] Examples listed as first-choice SDK examples use `Peer` / `Service`.
- [ ] Examples listed as compatibility or low-level examples are labeled that
      way in their source comments or surrounding docs.
- [ ] Changelog entries describe the same canonical Peer/Service path.
- [ ] Compatibility policy, release gates, release evidence artifact, and
      package targets agree on the supported compiler/generator/runtime matrix.
- [ ] Generated API docs and release evidence present `Peer` / `Service` as the
      canonical SDK path.
- [ ] External gateway/runtime/CLI/app/plugin tooling remains outside this SDK
      repository and package contract.

## Ecosystem And Registry Review

- [ ] `docs/ecosystem_maturity_evidence.md` still distinguishes configured
      release infrastructure from repeated green release evidence.
- [ ] `docs/adoption_ledger.md` has been reviewed for independent public
      downstream adoption; project-owned examples are not counted as adoption.
- [ ] vcpkg curated-registry claims are not made unless stable release history,
      repeated green release gates, package-manager evidence, changelog
      discipline, and public adoption evidence are all present.
- [ ] Any package-registry submission text links the exact release tag,
      release-gates run id, checksums, package-consumption logs, and adoption
      ledger entries it relies on.

## Release Notes

- [ ] Release stage matches the rules in [Release process](release_process.md).
- [ ] Include the supported compiler/generator/runtime matrix.
- [ ] Include protocol snapshot support and unsupported-version behavior.
- [ ] Include source compatibility notes and API diff classifications for
      public API changes.
- [ ] Include dependency/reference versions used by conformance tests.
- [ ] Include all-suite conformance results for both server and client, plus
      accepted exceptions, if conformance support is advertised.
- [ ] Include checksums for published source artifacts.
- [ ] Include package metadata or package recipe references for advertised
      package-manager routes.
- [ ] Link generated API documentation.
- [ ] Include accepted limitations, transport-specific boundaries, and tracked
      compile-time/performance debt that remain visible in
      `docs/technical_audit.md` or `docs/performance_debt.md`.
- [ ] Use [Release notes template](release_notes_template.md) so release notes,
      artifacts, and compatibility policy stay aligned.

Do not publish a stable release or claim fact-standard status while any required
evidence artifact is missing or any release-blocking matrix leg is red.
