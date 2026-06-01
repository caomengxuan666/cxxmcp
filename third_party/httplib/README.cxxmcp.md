# cpp-httplib Vendor Notes

This directory vendors `cpp-httplib` for cxxmcp's default bundled source build.
Package-manager builds should use `CXXMCP_USE_SYSTEM_DEPS=ON` and consume the
package-manager provided `httplib::httplib` target instead.

- Upstream: https://github.com/yhirose/cpp-httplib
- Version: v0.46.0
- Commit: 008e107d0fcddee7cb96dc5ad3c3189fd090e40a

Do not edit these files casually. Update the vendored copy as an explicit
dependency update with package-smoke and release-gate evidence.
