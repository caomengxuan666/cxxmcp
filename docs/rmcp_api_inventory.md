# rmcp API Inventory

This is the distilled reference for the official Rust SDK surface we are matching.
Sources:
- https://docs.rs/rmcp/latest/rmcp/
- https://github.com/modelcontextprotocol/rust-sdk

Current upstream reference point:
- docs.rs crate version: `rmcp 1.7.0`
- GitHub release: `rmcp-v1.7.0`

## Client-facing surface
- `list_tools`, `list_all_tools`, `call_tool`
- `list_prompts`, `list_all_prompts`, `get_prompt`
- `list_resources`, `list_all_resources`, `read_resource`
- `list_resource_templates`, `list_all_resource_templates`
- `subscribe`, `unsubscribe`
- `complete`
- `set_level`
- `notify_initialized`, `notify_cancelled`, `notify_progress`, `notify_roots_list_changed`
- client handler hooks for logging, custom requests / notifications, resource / tool / prompt change events, and roots

## Server-facing surface
- `initialize`, `ping`, `get_info`
- `list_tools`, `call_tool`, `get_tool`
- `list_resources`, `read_resource`
- `list_prompts`, `get_prompt`
- `complete`
- logging level handling
- subscriptions and resource change notifications
- custom request / notification hooks
- roots list change hooks

## Sampling
- server-to-client `createMessage`
- typed sampling message and content models
- model preferences and tool/result content support

## Elicitation
- feature-gated
- typed elicitation request / result models
- typed and URL-based elicitation flows

## Transport
- stdio server transport
- child-process client transport
- streamable HTTP client and server transports
- transport conversion traits / adapters

## Data model families
- tool, prompt, resource, resource template
- root
- completion
- logging
- sampling
- elicitation
- JSON-RPC request / response / notification / error

## Note
We should mirror the public shape of these families in C++, but keep our own naming and ownership style consistent with the rest of the codebase.
