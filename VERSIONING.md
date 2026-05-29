# Versioning

cxxmcp follows [Semantic Versioning](https://semver.org/) (SemVer).

## Version Format

```
MAJOR.MINOR.PATCH
```

- **MAJOR**: Breaking changes to the public SDK API surface.
- **MINOR**: New features, new protocol capabilities, or new transport support. Backward compatible.
- **PATCH**: Bug fixes, conformance fixes, documentation updates. Backward compatible.

## Pre-release Identifiers

Pre-release versions use identifiers such as `-alpha.1`, `-beta.1`, or `-rc.1`:

- **alpha**: Feature-incomplete, API may change without notice.
- **beta**: Feature-complete, API may change based on feedback.
- **rc**: Release candidate, API frozen unless a blocking issue is found.

## What Constitutes a Breaking Change

- Removing or renaming a public symbol in `sdk/**/include/cxxmcp`.
- Changing the behavior of a public function in a way that breaks existing callers.
- Changing the default value of a public configuration option.
- Removing support for a previously supported transport or protocol version.

Adding new public symbols, new optional parameters, or new configuration options is not a breaking change.

## Release Cadence

There is no fixed release schedule. Releases are made when meaningful changes accumulate or when a conformance fix is needed to track a new spec version.
