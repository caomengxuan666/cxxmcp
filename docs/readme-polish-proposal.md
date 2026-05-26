# README Polish Proposal

This is a small-scope draft for improving the README without changing its
technical claims or project positioning. The goal is to make the top of the
README easier to scan while keeping the tone practical and SDK-oriented.

## Recommended Additions

- A short positioning paragraph that says what `cxxmcp` is, what C++ standard
  the SDK targets, and which runtime pieces are optional.
- A compact "current focus" or "project status" note that describes the project
  as an SDK candidate or SDK-oriented implementation without implying official
  status.
- A quick-start path for two common users:
  - application authors who want to link the SDK through CMake package targets;
  - contributors who want to build and run the local smoke tests.
- A small capability table or checklist near the top, limited to areas that are
  already represented in the repository: protocol models, client/server SDK
  layers, transports, packaging, and optional runtime tools.
- Links from the top section to compatibility policy, release gates, and the SDK
  candidate process so readers can verify quality expectations from project
  documents instead of from broad README claims.
- A brief note that the public SDK surface is intentionally narrow, naming the
  main layers and pointing deeper examples to later README sections.

## Not Recommended

- Do not describe the project as an official MCP SDK unless that status is
  granted and documented by the relevant upstream project.
- Do not claim to be the standard, reference, canonical, complete, or most
  compatible C++ MCP implementation.
- Do not add broad marketing phrases such as "production ready for every use
  case", "enterprise grade", or "best-in-class".
- Do not place large architecture diagrams, long protocol explanations, or
  exhaustive API listings at the very top of the README.
- Do not lead with optional runtime, gateway, or CLI features in a way that makes
  the embeddable SDK surface feel secondary.
- Do not add badges for unverified claims, unpublished package channels, or
  quality gates that are not actually enforced in CI or release workflow.

## Suggested README Top Draft

````markdown
# cxxmcp

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C.svg)](https://cmake.org/)
[![MCP](https://img.shields.io/badge/protocol-Model%20Context%20Protocol-111827.svg)](https://modelcontextprotocol.io/)
[![SDK](https://img.shields.io/badge/package-C%2B%2B%20SDK-0F766E.svg)](#using-as-a-library)

`cxxmcp` is a C++17 SDK for building Model Context Protocol clients and
servers, with optional C++20 runtime, gateway, CLI, and example targets layered
above the embeddable SDK.

The project is organized around a narrow public SDK surface:
`protocol`, `transport`, `handler`, `peer`, `service`, `client`, and `server`.
These layers are intended to be usable from normal CMake package targets while
leaving runtime tooling optional.

Current focus:

- SDK-first client and server APIs for embedded C++ applications
- typed protocol models with JSON-RPC escape hatches
- stdio, process stdio, Streamable HTTP, and SSE-compatible transport paths
- package install smoke coverage and documented release gates
- candidate-level SDK quality work tracked in project docs

Read this in [Chinese](README_zh.md).

## Quick Start

```cmake
find_package(cxxmcp CONFIG REQUIRED)

add_executable(my_server server.cpp)
target_link_libraries(my_server PRIVATE cxxmcp::server)

add_executable(my_client main.cpp)
target_link_libraries(my_client PRIVATE cxxmcp::client)
```

Build from source:

```powershell
cmake -S . -B build
cmake --build build
```

For compatibility expectations, release checks, and SDK candidate process notes,
see:

- [Compatibility Policy](docs/compatibility_policy.md)
- [Release Gates](docs/release_gates.md)
- [SDK Candidate Process](docs/official_sdk_candidate_process.md)
````
