# Security Policy

## Supported Versions

Security fixes target the latest `master` branch and any active release
candidate branch. Stable release-line support will be listed in release notes
once cxxmcp declares a stable minor line.

## Reporting A Vulnerability

Use GitHub private vulnerability reporting if it is enabled for the repository.
If private reporting is not available, contact the maintainer through the
GitHub profile and request a private channel. Do not open a public issue with
exploit details, credentials, tokens, private URLs, or proof-of-concept payloads.

Please include:

- affected commit, tag, or package version;
- platform and compiler;
- affected transport or SDK surface;
- minimal reproduction steps;
- expected impact;
- whether the issue is already public.

The project will acknowledge valid reports, coordinate a fix, and publish
release notes or advisories when a public release is affected.

## Scope

Security reports are appropriate for parser crashes, request smuggling,
transport/session isolation failures, unsafe process execution, credential
leaks, auth bypasses, denial-of-service vectors, and dependency vulnerabilities
that affect the SDK package.
