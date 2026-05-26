# Official SDK Candidate Process

This document describes how cxxmcp should move from a strong C++ SDK candidate
to an official MCP SDK proposal and, later, a credible fact-standard C++ MCP
SDK.

Fact-standard status is not granted by an application form. It is earned by
adoption, compatibility, release evidence, and sustained maintenance. The
practical application path is to first propose cxxmcp as an official C++ SDK
candidate, then let public releases and ecosystem adoption establish whether it
becomes the default choice for C++ MCP users.

## Target Outcomes

- Official C++ SDK candidate: the MCP maintainers and SDK working group have
  reviewed the project, its release evidence, and its maintenance plan.
- Official SDK listing: the official MCP SDK documentation links to cxxmcp or
  to a successor repository accepted by the MCP maintainers.
- Fact-standard readiness: cxxmcp has public release artifacts, repeatable
  conformance evidence, package-manager routes, and real downstream users.

Do not describe a release as fact-standard-ready until the release candidate
checklist is complete for the exact release commit and all advertised release
matrix legs are green.

## Preconditions

Before opening an official SDK proposal, prepare a release candidate that has
all of the following evidence:

- The release-blocking test set passes on every advertised matrix leg.
- Installed-package `package_smoke` passes from a clean external CMake project
  on every advertised matrix leg.
- Public header compile tests pass under the configured SDK C++ standard.
- RMCP, TypeScript SDK, and Python SDK interoperability evidence is available
  for the advertised transport paths.
- Generated Doxygen API documentation is uploaded for the same commit.
- A source archive and checksums are uploaded for the same commit.
- Release evidence includes the README, README_zh, changelog, compatibility
  policy, release gates, release candidate checklist, release notes, TODO, and
  canonical examples.
- Public examples and generated documentation present `Peer` / `Service` as
  the canonical SDK path.
- Runtime, gateway, CLI, app, adapters, and plugin SDK surfaces remain outside
  the core SDK contract.
- A maintainer list, security contact, semver policy, and compatibility policy
  are published.
- At least one package-consumption route is documented. CMake install-tree
  consumption is required; vcpkg, Conan, or a documented overlay route should
  be added before broader standardization claims.

## Proposal Package

Create or update an `OFFICIAL_SDK_PROPOSAL.md` document before contacting the
MCP maintainers. The proposal should include:

- Project name and repository URL.
- Proposed official SDK identity, such as `cpp-sdk` or `cxxmcp`.
- Supported MCP protocol snapshot or snapshots.
- Supported compiler, generator, standard-library, and platform matrix.
- Supported transports: stdio, process stdio, Streamable HTTP, and any legacy
  SSE compatibility mode.
- Supported SDK roles: client, server, Peer, Service, handler, protocol,
  request, cancellation, progress, sampling, elicitation, tasks, prompts,
  resources, and tools.
- Explicit non-goals: no custom MCP dialect, no alternate wire format, no
  runtime or gateway type in the core SDK contract.
- Links to release-gate artifacts, CTest/JUnit logs, Doxygen documentation,
  source archives, and checksums.
- Links to interop evidence against RMCP, the TypeScript SDK, and the Python
  SDK.
- API stability policy, deprecation policy, and ABI policy.
- Maintainer names, expected review latency, release cadence, and security
  handling.
- Packaging plan for CMake install-tree consumers and package-manager users.
- Migration plan if the MCP maintainers require a repository under the
  `modelcontextprotocol` organization.

## Application Path

There is no known public one-click application form for adding a new official
MCP SDK. Use the normal MCP contribution and community process:

1. Publish a release candidate with all required evidence attached.
2. Open a GitHub issue or discussion in the official MCP project proposing an
   official C++ SDK candidate.
3. Link the release candidate, proposal package, compatibility policy, release
   gates, release checklist, and interop evidence.
4. Ask for SDK working group or maintainer review before submitting a docs PR.
5. Discuss repository ownership. Be ready to keep the project external as a
   community SDK, transfer it, or mirror it under an official MCP repository if
   the maintainers require that for official status.
6. After maintainer agreement, submit a documentation PR that adds the C++ SDK
   to the official SDK page or to the location requested by the maintainers.
7. Keep release evidence and compatibility notes attached to every release
   that claims official or fact-standard readiness.

Useful official entry points:

- MCP SDK documentation: https://modelcontextprotocol.io/docs/sdk
- MCP contributing guide: https://modelcontextprotocol.io/development/contributing
- MCP SDK working group charter: https://modelcontextprotocol.io/community/sdk/charter
- MCP specification: https://modelcontextprotocol.io/specification/latest

## Fact-Standard Readiness Bar

After an official SDK proposal is opened, cxxmcp can credibly move toward
fact-standard status only when these conditions hold across multiple releases:

- The SDK-first public surface remains stable across semver releases.
- The release matrix stays green on supported platforms.
- Protocol snapshot support is updated promptly and older snapshots are kept or
  removed according to the compatibility policy.
- Interop with official and reference SDKs remains release-blocking.
- Downstream projects can consume the SDK without relying on the source tree.
- Package-manager users have a supported route.
- Documentation, examples, generated API docs, release notes, and compatibility
  policy describe the same canonical SDK path.
- Users can file issues, receive fixes, and migrate deprecated APIs through a
  documented path.

Fact-standard status should be treated as an ecosystem claim, not a release
badge. The project may call itself a candidate while evidence is still being
collected, but should avoid stronger language until adoption and release
history make the claim defensible.
