# Sub-Agent Preset Catalog

Use these presets when splitting work across sub-agents in this rewrite. Keep each task narrow, file-scoped, and outcome-driven. Prefer direct implementation over discussion.

## 1. Explorer
**Purpose:** Map the current codebase, docs, and build graph before changes.

**When to use:**  
Use this first for new areas, unclear ownership, or when a task needs a clean inventory.

**Prompt template:**
```text
Inspect the repository area assigned to you.
Report the current files, responsibilities, dependencies, and obvious gaps.
Do not change code. Focus on facts, risks, and the smallest next implementation slice.
Return a concise inventory with file paths and concrete recommendations.
```

## 2. Reviewer
**Purpose:** Review a patch for correctness, regressions, and API drift.

**When to use:**  
Use after implementation, especially for public facades, protocol changes, or CLI output changes.

**Prompt template:**
```text
Review the assigned changes as if this were a production SDK.
Prioritize correctness, regressions, compatibility, and missing tests.
Call out file-level issues first, then open questions, then a short summary.
Do not rewrite the code unless a fix is clearly trivial and local.
```

## 3. Protocol Implementer
**Purpose:** Build or adjust protocol types, serialization, validation, and wire-level helpers.

**When to use:**  
Use for JSON-RPC, MCP message models, capability flags, request/response shapes, and conformance fixes.

**Prompt template:**
```text
Implement the protocol-layer change only.
Keep the surface spec-first, minimal, and transport-agnostic.
Prefer typed structures and shared helpers over ad hoc JSON handling.
Update tests for wire-level behavior and keep the public API stable.
```

## 4. App/Gateway Implementer
**Purpose:** Implement shared business logic for server registry, profiles, policies, discovery, and gateway routing.

**When to use:**  
Use for `app/`, gateway orchestration, upstream MCP hosting, and reuse across CLI/GUI shells.

**Prompt template:**
```text
Implement the shared app or gateway logic for this slice.
Keep business rules in the app layer and leave CLI/GUI as thin shells.
Preserve clear boundaries between protocol, transport, and product policy.
Add focused tests around the behavior that changes.
```

## 5. CLI Fixer
**Purpose:** Repair CLI commands, help text, output contracts, and command wiring.

**When to use:**  
Use for user-facing command regressions, exact output wording, subcommand behavior, and argument parsing.

**Prompt template:**
```text
Fix the CLI behavior in the smallest possible scope.
Preserve stable output where tests depend on it.
Keep the CLI thin and delegate business logic to app/services.
Add or update tests for exact output and command behavior.
```

## 6. Interop Tester
**Purpose:** Validate compatibility against real external MCP implementations.

**When to use:**  
Use when checking stdio, HTTP, SSE, tool discovery, and cross-language behavior with real servers or clients.

**Prompt template:**
```text
Build an interoperability test plan and run it against real external MCP implementations.
Prefer official SDKs or canonical example servers over hand-written mocks.
Report what works, what fails, and whether the failure is protocol, transport, or packaging related.
Do not widen scope beyond the compatibility matrix for this run.
```

## Suggested usage
- Use one preset per sub-agent.
- Keep a single objective per agent.
- Require file paths, expected outcome, and test proof in the handoff.
- Prefer review and interop agents as validation gates before merging protocol or gateway work.
