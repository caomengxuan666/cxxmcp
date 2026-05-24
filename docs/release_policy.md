# Release Policy

`cxxmcp` uses semantic versioning for public releases.

## Versioning

- patch: bug fixes, documentation fixes, and behavior-preserving maintenance
- minor: additive SDK surface, additive transports, and compatibility-preserving growth
- major: breaking public API, breaking behavior, or package layout changes

## Release stages

- alpha: public shape may still move
- beta: API mostly settled, breaking changes should be rare
- rc: only bug fixes and documentation changes
- stable: public contract is frozen for the minor line

## Release artifacts

Every release should include:

- versioned source and package metadata
- installable CMake targets
- generated documentation
- changelog entries
- a compatibility note when the public surface changes

## Deprecation

Public renames and removals should follow a staged migration path:

1. add the new name
2. keep the old name as an alias
3. document the migration
4. remove the old name only in the next major version
