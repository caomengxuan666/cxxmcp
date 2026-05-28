# Protocol Model Audit

This audit records the current cxxmcp protocol model surface against the pinned
RMCP reference used by release gates.

- MCP protocol snapshot: `2025-11-25`
- RMCP reference commit: `c330fede90e4729c234f8e87fdbc5ea27a1dd10c`
- RMCP model source root: `reference/rmcp/crates/rmcp/src/model`
- cxxmcp model source root: `sdk/protocol/include/cxxmcp/protocol`

The C++ SDK does not mirror every RMCP Rust type name one-to-one. The audit is
wire-shape based: a cxxmcp model is considered covered when the public protocol
headers can serialize, parse, preserve extension data, and round-trip the same
MCP payload family.

## RMCP Model File Coverage

| RMCP source | cxxmcp protocol coverage | Notes |
| --- | --- | --- |
| `model.rs` | `types.hpp`, `initialize.hpp`, `resource.hpp`, `prompt.hpp`, `logging.hpp`, `sampling.hpp`, `completion.hpp`, `roots.hpp`, `elicitation.hpp`, `task.hpp`, `tool.hpp` | JSON-RPC envelopes, initialize payloads, pagination, progress/cancellation, roots, completion, logging, sampling, elicitation, tasks, prompts, resources, and tool request/result payloads are represented in typed models. Custom request/notification/result escape hatches use raw `Json` and JSON-RPC helpers. |
| `annotated.rs` | `tool.hpp`, `prompt.hpp`, `resource.hpp` | Annotations are preserved as raw JSON objects on content/tool/prompt/resource surfaces where the protocol permits them. |
| `capabilities.rs` | `capabilities.hpp` | Client/server capability families use object-presence semantics, optional bool presence tracking, raw per-capability object preservation, `experimental`, and `extensions`. |
| `content.rs` | `tool.hpp`, `resource.hpp`, `sampling.hpp` | Text, image, audio, embedded resource, resource link, sampling tool-use, and sampling tool-result content variants are covered by typed content models. |
| `elicitation_schema.rs` | `elicitation.hpp` | Primitive, enum, single-select, multi-select, URL, form, validation, and result actions are covered by typed schema/result models. |
| `extension.rs` | all protocol family headers | Unknown JSON members are preserved through `extensions` bags on typed models that need forward-compatible round trips. |
| `meta.rs` | all protocol family headers | `_meta` is represented where MCP payloads permit request/result metadata. Progress token helpers keep request metadata placement separate from top-level extension data. |
| `prompt.rs` | `prompt.hpp`, `tool.hpp` | Prompt definitions, arguments, messages, and prompt content reuse the common content block model. |
| `resource.rs` | `resource.hpp`, `tool.hpp` | Resources, resource templates, contents, read/list/subscribe/unsubscribe, update notifications, embedded resource content, and resource links are covered. |
| `task.rs` | `task.hpp` | Task status, task object, create/list/get/cancel/result payloads, retention fields, TTL, and flattened operation results are covered. |
| `tool.rs` | `tool.hpp`, `task.hpp` | Tool definitions, task support/execution hints, annotations, call params, list results, structured output, content results, and `isError` optional-bool semantics are covered. |

## cxxmcp Protocol Header Coverage

| Header | Primary public models | RMCP alignment status |
| --- | --- | --- |
| `types.hpp` | JSON-RPC envelopes, ids, icons, progress/cancellation notifications | Covered. JSON-RPC error/message validation is release-blocking through `protocol` tests. |
| `capabilities.hpp` | client/server capabilities, task capabilities | Covered. Object-presence and optional bool semantics are tested. |
| `initialize.hpp` | implementation info, initialize params/result | Covered. Supported version policy and implementation metadata are tested. |
| `tool.hpp` | tool definition/call/result, content blocks, task support | Covered. Tagged content unions, schema presence, structured content, extension data, and optional `isError` are tested. |
| `prompt.hpp` | prompt definition/list/get/message | Covered. Argument optional bool presence and content/message round trips are tested. |
| `resource.hpp` | resources, templates, contents, read/list/subscribe/update | Covered. Size constraints, templates, resource links, and contents are tested. |
| `roots.hpp` | roots list result | Covered. |
| `completion.hpp` | references, arguments, completion params/result | Covered. Tagged-union reference constraints and optional `hasMore` are tested. |
| `logging.hpp` | setLevel and message notification params | Covered. Level enums and notification payload requirements are tested. |
| `sampling.hpp` | sampling messages, content variants, model preferences, tool choice, createMessage params/result | Covered. RMCP roles, includeContext enum, and tool-use/tool-result sequencing constraints are tested. |
| `elicitation.hpp` | primitive schemas, elicitation request/result/completion | Covered. Form, URL, validation, enum schema shapes, and completion notifications are tested. |
| `task.hpp` | task object, request params, operation results, list/create/get/cancel/result | Covered. Status, TTL, cancellation/timeout integration, and flattened result shapes are tested. |
| `schema.hpp` | JSON Schema builder and `SchemaTraits<T>` customization | Covered as SDK authoring support, not a distinct RMCP wire family. |

## Gates That Protect This Audit

- `scripts/check_protocol_model_coverage.py` checks `*_from_json` and
  `*_to_json` helper pairing in public protocol headers.
- `scripts/check_rmcp_source_drift.py` verifies that the pinned
  `reference/rmcp` checkout is at commit
  `c330fede90e4729c234f8e87fdbc5ea27a1dd10c`, that every mapped RMCP model
  source file exists, that no new unmapped RMCP model source file has appeared
  under `crates/rmcp/src/model`, and that this document matches the generated
  source mapping in `docs/rmcp_source_mapping.json`.
- `protocol` release-blocking tests cover protocol family round trips, required
  fields, type constraints, extension bags, `_meta`, optional bool presence,
  content variants, capabilities, tasks, elicitation, and version policy.
- `rmcp_conformance` and cross-SDK interop gates exercise RMCP, TypeScript SDK,
  and Python SDK behavior against cxxmcp transports and peers.

Regenerate the checked RMCP source mapping after a deliberate RMCP reference
refresh with:

```sh
python scripts/check_rmcp_source_drift.py --write-mapping
```

Verify the mapping without modifying files with:

```sh
python scripts/check_rmcp_source_drift.py
```

Keep this document in sync when adding, renaming, or removing any public
protocol model, parser, serializer, or pinned RMCP reference.
