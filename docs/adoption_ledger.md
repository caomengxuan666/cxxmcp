# Adoption Ledger

This ledger records downstream adoption evidence that may be cited in future
fact-standard and vcpkg curated-registry claims.

Do not count project-owned examples, local smoke tests, or private experiments
as public adoption. They can prove package-consumption mechanics, but they do
not prove independent ecosystem use.

## Current Status

No independent public downstream adoption is recorded yet.

The project should not claim broad ecosystem adoption or resubmit to the vcpkg
curated registry on adoption grounds until at least one reproducible public
downstream integration is listed below.

## Project-Owned Evidence

These entries are useful release evidence, but they are not independent public
adoption:

| Project | Evidence Type | Current Use |
|---|---|---|
| `templates/external_consumer` | In-repository package smoke template | Proves clean `find_package(cxxmcp CONFIG REQUIRED)` consumption from installed output. |
| `../cxxmcp-examples` | Adjacent downstream examples repository | Proves example coverage in adjacent-source and installed-package modes when its CI or local release-candidate run is recorded. |

## Public Downstream Adoption

| Project | Owner | Evidence Link | cxxmcp Version/Commit | Notes |
|---|---|---|---|---|
| _None recorded yet_ |  |  |  |  |

## Search Audit

These searches are not adoption entries. They record public checks that did not
produce a qualifying downstream integration.

| Date | Query / Source | Result | Decision |
|---|---|---|---|
| 2026-05-28 | Web search for `"cxxmcp" "find_package"`, `"cxxmcp" "CXXMCP"`, and `"cxxmcp" "github"` | Found star/list references such as `tattwamasi/starry-eye` mentioning `caomengxuan666/cxxmcp`, but no public repository, issue, PR, release note, package recipe, or integration report consuming cxxmcp. | Not adoption. Keep public downstream adoption empty. |
| 2026-05-28 | Follow-up web search for `"cxxmcp" "CMakeLists.txt"`, `"cxxmcp" "vcpkg"`, and `"cxxmcp" "Model Context Protocol" -site:github.com/caomengxuan666` | Again found public star/list references, including [`tattwamasi/starry-eye`](https://github.com/tattwamasi/starry-eye/blob/master/README.md), but no qualifying public downstream build, package recipe, integration PR, or release note. | Not adoption. Keep public downstream adoption empty. |

## Entry Requirements

An entry may be cited only when it includes:

- a public repository, issue, PR, release note, package recipe, or integration
  report URL;
- the cxxmcp version, tag, or commit consumed;
- the package path used, such as CMake install, vcpkg overlay, Conan, xmake, or
  source subdirectory;
- the platform/compiler where the integration was verified;
- enough reproduction notes for a maintainer or package reviewer to understand
  the evidence.
