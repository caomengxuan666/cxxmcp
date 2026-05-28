# Elicitation Lifecycle

This document describes the elicitation behavior that is part of the SDK
contract today. Elicitation remains an optional MCP capability family: normal
clients and servers do not need to implement it unless they advertise or depend
on it.

## Layering

Elicitation wire models live in `cxxmcp/protocol/elicitation.hpp` and belong to
the `cxxmcp::protocol` target. Client/server helper APIs live above those
models and send normal `elicitation/create` requests or
`notifications/elicitation/complete` notifications.

Runtime, gateway, policy, approval UI, profile, and CLI behavior is outside the
SDK elicitation contract.

## Modes

The SDK models the two current elicitation modes explicitly:

- Form mode sends a user-facing `message` and a constrained
  `requestedSchema`. The schema builder supports string, number, integer,
  boolean, email, and string enum fields.
- URL mode sends a `message`, `elicitationId`, and `url`. Completion is
  correlated later with `notifications/elicitation/complete`.

Unknown JSON members and `_meta` are preserved for forward-compatible
round-trips.

## Capability Gating

Server-side `ClientPeer::create_elicitation()` checks the connected client's
negotiated capabilities before sending:

- form requests require `elicitation.form`;
- URL requests require `elicitation.url`;
- URL requests rejected by capability gating use the protocol-level
  URL-elicitation-required error code.

For common SDK authoring, `ClientPeer::elicit<T>()` and
`ClientPeer::elicit_async<T>()` build form requests from `SchemaTraits<T>`.
Primitive schemas are wrapped as a required `value` field; object schemas with
`properties` are sent directly and decoded from the accepted content object.
The explicit-schema overload lets applications supply an
`ElicitationSchema` while still receiving typed content. `decline` and `cancel`
results are mapped to stable `elicitation` errors. `ClientPeer::elicit_url()`
and `elicit_url_async()` provide the same capability gating, timeout, and
cancellation behavior for URL-mode requests.

Raw JSON-RPC remains available for conformance probes, vendor extensions, and
future protocol behavior, but raw calls bypass typed helper gating.

## Client Request Handling

Clients may install an `on_create_elicitation_request` handler. If no handler is
installed, the SDK validates the incoming request parameters and returns a typed
`decline` result. Invalid request parameters still return a JSON-RPC error
instead of being silently declined.

Handlers can return `accept`, `decline`, or `cancel`. Accepted form results may
include a content object. URL-mode requests normally return immediately and use
the completion notification to signal that the external flow has finished.

## Schema Validation

The protocol layer validates the shape of elicitation schemas and primitive
property schemas when parsing.

Applications can call `validate_elicitation_content(schema, content)` to check
returned form content against the SDK's constrained schema model. The validator
checks required fields, primitive JSON types, numeric bounds, and enum values.
`validate_elicitation_result_content(schema, result)` applies the same rules to
accepted elicitation results and treats `decline` / `cancel` results as not
requiring content.

This is intentionally narrower than a complete JSON Schema implementation.
Unknown content members are allowed because the SDK schema model does not expose
`additionalProperties`.

## Stability

The typed models, serializers, parsers, client request handler hook,
server-side `create_elicitation()` / `elicit<T>()` / `elicit_url()` helpers,
and completion notification are stable SDK API. Content validation and richer
application-level approval UX are optional follow-up work.
