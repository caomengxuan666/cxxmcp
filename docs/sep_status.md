# MCP SEP Implementation Status

This document tracks all accepted MCP Specification Enhancement Proposals (SEPs)
and their implementation status in cxxmcp.

SEPs are accepted when their GitHub issue is closed on the
[modelcontextprotocol](https://github.com/modelcontextprotocol/modelcontextprotocol)
repository with the `SEP` label. Open issues are proposals, not accepted spec
changes.

Last updated: 2026-05-28 (SEP-1699)

## Legend

- **Done** -- Fully implemented and tested in cxxmcp
- **Partial** -- Partially implemented or missing test coverage
- **N/A** -- Not applicable to the SDK layer (meta/governance/process)
- **Open** -- Accepted into spec but not yet implemented in cxxmcp

---

## Protocol Features

| SEP | Title | Status | Notes |
|-----|-------|--------|-------|
| [SEP-973](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/973) | Icons and websiteUrl for implementations, resources, tools, prompts | **Done** | `Icon.sizes` as `vector<string>` across tools, prompts, resources, templates. `website_url` on `ImplementationInfo`. |
| [SEP-986](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/986) | Tool name format validation | **Done** | Character whitelist `[A-Za-z0-9._-]`, max 128 bytes, advisory warnings for leading/trailing `-` or `.`. |
| [SEP-992](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/992) | Notification configuration for tool call result | **Open** | Allow clients to configure whether tool call results trigger notifications. |
| [SEP-1006](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1006) | Bidirectional tool calls | **Open** | Server can call tools on the client during request handling. |
| [SEP-1303](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1303) | Input validation errors as tool execution errors | **Open** | Distinguish input validation failures from tool execution failures. |
| [SEP-1319](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1319) | Decouple request payload from RPC methods | **Done** | `ToolCall.task`, `CreateMessageParams.task`, task-aware dispatch in `ServerPeer`. |
| [SEP-1371](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1371) | Multiline format for StringSchema | **Open** | Add `multiline` format hint to elicitation string schemas. |
| [SEP-1385](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1385) | Tool execution requirements metadata (`requires` field) | **Open** | Declare what a tool needs to execute (e.g., permissions, resources). |
| [SEP-1391](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1391) | Long-running operations | **Open** | Standardized model for operations that take extended time. |
| [SEP-1440](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1440) | Enhanced completion values with rich metadata | **Open** | Attach metadata to completion suggestions. |
| [SEP-1456](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1456) | Custom input with predefined options in elicitInput | **Open** | Extend elicitation with custom input + predefined choices. |
| [SEP-1533](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1533) | Metadata in resource read response | **Open** | Attach metadata to resource read results. |
| [SEP-1560](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1560) | `secretHint` tool annotation | **Open** | Mark tools that handle secrets/sensitive data. |
| [SEP-1573](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1573) | Allow non-file URI schemes for roots | **Open** | Expand roots beyond `file://` URIs. |
| [SEP-1577](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1577) | Sampling with tools | **Done** | `ToolChoice` enum, tool_use/tool_result role validation, balance constraints. |
| [SEP-1685](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1685) | State meta field on ServerRequest/ServerResponse | **Open** | Attach state metadata to server request/response objects. |
| [SEP-1686](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1686) | Task lifecycle | **Done** | Full task lifecycle: create, list, get, cancel, result, status notifications. |
| [SEP-1700](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1700) | Multi-turn SSE (using `204 No Content`) | **Open** | Use HTTP 204 to signal SSE stream pause without closing. |
| [SEP-1724](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1724) | Extensions field for capabilities | **Done** | `extensions` JSON object on `ClientCapabilities` and `ServerCapabilities`. |
| [SEP-1789](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1789) | Clarify `_meta` property reserved keys | **N/A** | Spec clarification, no SDK code change needed. |
| [SEP-1792](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1792) | `agencyHint` tool annotation | **Open** | Indicate the level of autonomous agency a tool has. |

## Elicitation

| SEP | Title | Status | Notes |
|-----|-------|--------|-------|
| [SEP-1034](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1034) | Default values for primitive types in elicitation schemas | **Done** | `default_value` on all primitive schema types. `default_values` for multi-select. |
| [SEP-1036](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1036) | URL mode elicitation | **Done** | `ElicitationMode::Url`, `ElicitationCapabilities.url`, completion notifications. |
| [SEP-1306](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1306) | Binary mode elicitation for file uploads | **Open** | Elicitation mode for binary/file upload interactions. |
| [SEP-1330](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1330) | Elicitation enum schema improvements | **Done** | `enumNames`, `value_titles`, titled `oneOf`, multi-select `anyOf`/`oneOf`. |

## Auth and Security

| SEP | Title | Status | Notes |
|-----|-------|--------|-------|
| [SEP-835](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/835) | Scope upgrade on 403 insufficient_scope | **Done** | `select_authorization_scopes` priority chain. Scope-upgrade URL from `WWW-Authenticate`. |
| [SEP-985](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/985) | Align OAuth Protected Resource Metadata with RFC 9728 | **Done** | `ProtectedResourceMetadata` struct follows RFC 9728. |
| [SEP-988](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/988) | High security profile | **Open** | Stricter security requirements for high-security deployments. |
| [SEP-990](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/990) | Enterprise IdP policy controls during MCP OAuth flows | **Open** | Allow enterprise IdPs to enforce policies during OAuth. |
| [SEP-991](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/991) | Client ID Metadata Documents (CIMD) | **Done** | `ClientIdMetadataDocument`, `is_valid_client_id_metadata_url()`. |
| [SEP-1024](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1024) | Client security requirements for local server installation | **Open** | Security requirements when installing local MCP servers. |
| [SEP-1046](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1046) | OAuth client credentials flow | **Partial** | Client-side `client_credentials` flow, metadata validation, credential storage, and HTTP token exchange for `client_secret_post`. Private-key JWT and broader policy handling remain open. |
| [SEP-1075](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1075) | Security annotations for tool definitions | **Open** | Annotate tools with security-relevant metadata. |
| [SEP-1289](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1289) | Client identity verification via JWT and public keys | **Open** | Verify client identity using JWT/PK. |
| [SEP-1299](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1299) | Server-side auth management with client session binding | **Open** | Bind auth state to client sessions. |
| [SEP-1313](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1313) | Explicit authorization check method | **Open** | New method for checking authorization before tool execution. |
| [SEP-1380](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1380) | Client credential manager | **Open** | Standardized credential management for clients. |
| [SEP-1386](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1386) | Fine-grained authorization behaviour | **Open** | More granular authorization controls. |
| [SEP-1415](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1415) | HTTP message signing for client authentication | **Open** | Sign HTTP requests for client auth. |
| [SEP-2207](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/2207) | OIDC offline_access auto-append | **Done** | `add_offline_access_if_supported()` in `lifecycle.hpp`. |

## Transport

| SEP | Title | Status | Notes |
|-----|-------|--------|-------|
| [SEP-975](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/975) | Transport-agnostic resumable requests | **Open** | Resume interrupted requests across any transport. |
| [SEP-1335](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1335) | Address Streamable HTTP transport issues | **Done** | Session, resume, stale-session, backpressure all addressed. |
| [SEP-1352](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1352) | gRPC transport | **Open** | Add gRPC as an MCP transport option. |
| [SEP-1359](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1359) | Protocol-level sessions | **Open** | Session management at the protocol layer. |
| [SEP-1612](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1612) | Pure HTTP transport (fully compliant, backward-compatible) | **Open** | Simplified HTTP transport without SSE. |
| [SEP-1699](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1699) | SSE polling via server-side disconnect | **Done** | `enable_sse_polling`, `sse_disconnect_retry`, `disconnect_session_sse()`. Priming event with `id` + `data: {}`. |

## Schema and Validation

| SEP | Title | Status | Notes |
|-----|-------|--------|-------|
| [SEP-834](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/834) | Support full JSON Schema 2020-12 | **Open** | Upgrade from JSON Schema draft-07 to 2020-12. |
| [SEP-1300](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1300) | Tool filtering with groups and tags | **Open** | Categorize and filter tools by group/tag. |
| [SEP-1487](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1487) | `trustedHint` tool annotation | **Open** | Indicate whether a tool is from a trusted source. |
| [SEP-1488](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1488) | `securitySchemes` in tool metadata for mixed-auth servers | **Open** | Declare auth requirements per tool. |
| [SEP-1575](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1575) | Tool semantic versioning | **Open** | SemVer for tool definitions. |
| [SEP-1613](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1613) | JSON Schema 2020-12 as default dialect | **Open** | Make 2020-12 the default schema dialect. |

## SDK and Ecosystem

| SEP | Title | Status | Notes |
|-----|-------|--------|-------|
| [SEP-958](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/958) | Zero-knowledge MCP nesting/chaining via session calls | **Open** | Enable nested MCP sessions without client knowledge. |
| [SEP-979](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/979) | Client advertisement for supported server capabilities | **Open** | Clients declare which server capabilities they support. |
| [SEP-1004](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1004) | Server profiles | **Open** | Standardized server capability profiles. |
| [SEP-1076](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1076) | Dependency annotations | **Open** | Declare tool dependencies. |
| [SEP-1364](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1364) | Elevating MCP sessions | **Open** | Promote sessions to higher privilege levels. |
| [SEP-1381](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1381) | Utilized capability declarations | **Open** | Declare which capabilities a client actually uses. |
| [SEP-1382](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1382) | Documentation best practices for MCP tools | **N/A** | Guidelines, no SDK code change. |
| [SEP-1442](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1442) | Make MCP stateless by default | **Open** | Default to stateless sessions. |
| [SEP-1461](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1461) | Attested client registration and proof-of-possession | **Open** | Cryptographic attestation for client registration. |
| [SEP-1502](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1502) | MCP extension specification and directory structure | **Open** | Standardized extension discovery. |
| [SEP-1596](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1596) | Configuration schema for MCP servers | **Open** | Standardized server configuration format. |
| [SEP-1607](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1607) | Context middleware | **Open** | Middleware layer for context injection. |
| [SEP-1649](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1649) | MCP Server Cards via .well-known | **Open** | HTTP server discovery mechanism. |
| [SEP-1655](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1655) | Client-side state management | **Open** | Standardized client-side state handling. |
| [SEP-1708](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1708) | Client-brokered filesystem access | **Open** | Client mediates server filesystem access. |
| [SEP-1730](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1730) | SDK tiering system | **N/A** | RMCP roadmap marker, not an SDK feature. |
| [SEP-1763](https://github.com/modelcontextprotocol/modelcontextprotocol/issues/1763) | Interceptors for MCP | **Open** | Request/response interception layer. |

## Governance and Process (N/A for SDK)

| SEP | Title | Notes |
|-----|-------|-------|
| SEP-001 (932) | MCP Governance | Process document |
| SEP-965 | Unified specification structure | Spec reorganization |
| SEP-993 | Namespaces | Naming conventions |
| SEP-994 | Shared communication practices | Guidelines |
| SEP-1286 | Improve SEP workflow | Process improvement |
| SEP-1302 | Formalize working groups | Governance |
| SEP-1309 | Specification version management | Process |
| SEP-1400 | Semantic versioning for MCP spec | Process |
| SEP-1413 | Connector symbol for qualified names | Naming |
| SEP-1444 | SDK tiering system (duplicate of 1730) | Process |
| SEP-1597 | HTTP REST transport proposal | Superseded |
| SEP-1627 | Conformance testing | Process |
| SEP-1626 | LLM Friendly Identifiers (LFID) | Naming |
| SEP-1789 | Clarify `_meta` reserved keys | Clarification |

---

## Summary

| Category | Done | Partial | Open | N/A |
|----------|------|---------|------|-----|
| Protocol Features | 5 | 0 | 10 | 1 |
| Elicitation | 3 | 0 | 1 | 0 |
| Auth and Security | 5 | 1 | 9 | 0 |
| Transport | 2 | 0 | 4 | 0 |
| Schema and Validation | 0 | 0 | 6 | 0 |
| SDK and Ecosystem | 0 | 0 | 11 | 2 |
| Governance and Process | -- | -- | -- | 14 |
| **Total** | **15** | **1** | **41** | **17** |

## Open SEPs by Priority

### High (core protocol gaps)

- **SEP-834 / SEP-1613**: JSON Schema 2020-12 support
- **SEP-1303**: Input validation errors as tool execution errors
- **SEP-1391**: Long-running operations
- **SEP-1575**: Tool semantic versioning

### Medium (auth and transport)

- **SEP-1046**: OAuth client credentials flow hardening
- **SEP-988**: High security profile
- **SEP-1700**: Multi-turn SSE

### Low (nice-to-have)

- **SEP-1006**: Bidirectional tool calls
- **SEP-1300**: Tool filtering with groups/tags
- **SEP-1442**: Stateless by default
- **SEP-1763**: Interceptors
