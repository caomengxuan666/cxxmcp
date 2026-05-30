# Dependency Policy

cxxmcp keeps the public SDK dependency surface intentionally small and
separates SDK package dependencies from examples, tests, documentation, and
external gateway tooling.

## Supported Modes

- Source/archive builds use bundled header-only SDK dependencies so direct
  CMake, FetchContent, CPM.cmake, and source installs work without a package
  manager. The install tree includes `tl/expected.hpp` and
  `nlohmann/json.hpp`.
- Registry builds should use `CXXMCP_USE_SYSTEM_DEPS=ON` and package-manager
  dependencies for `tl-expected`, `nlohmann-json`, and `cpp-httplib`. In this
  mode the install tree must not vendor `tl` or `nlohmann` headers.

`cpp-httplib` is used by the HTTP transport implementation. Downstream code
should include the cxxmcp HTTP transport headers, not `httplib.h` directly.

## Update Cadence

- GitHub Actions and the `third_party/httplib` submodule are monitored by
  Dependabot.
- Bundled header-only dependencies are updated deliberately through normal pull
  requests with package-smoke and release-gate evidence.
- Security updates that affect public SDK builds are treated as priority fixes
  under the project security and release policies.

## Package Registry Guidance

Package recipes should keep the default SDK package narrow:

- build SDK libraries and public headers only;
- disable examples, tests, and docs by default;
- avoid pulling OpenSSL or other optional auth dependencies into default
  installs;
- keep gateway/runtime tooling outside the SDK package contract.

The detailed package-consumption policy is maintained in
[`docs/package_consumption.md`](docs/package_consumption.md#dependency-policy).
Release-time dependency and reference review rules are maintained in
[`docs/dependency_policy.md`](docs/dependency_policy.md).
