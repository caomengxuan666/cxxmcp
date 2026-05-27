#!/usr/bin/env python3
"""Ensure canonical SDK headers do not expose runtime/tooling layers."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


CANONICAL_INCLUDE_ROOTS = [
    Path("sdk/include/cxxmcp"),
    Path("sdk/auth/include/cxxmcp"),
    Path("sdk/core/include/cxxmcp"),
    Path("sdk/protocol/include/cxxmcp"),
    Path("sdk/transport/include/cxxmcp"),
    Path("sdk/client/include/cxxmcp"),
    Path("sdk/server/include/cxxmcp"),
]

FORBIDDEN_INCLUDE_PREFIXES = (
    "cxxmcp/app",
    "cxxmcp/cli",
    "cxxmcp/gateway",
    "cxxmcp/observability",
    "cxxmcp/profile",
    "cxxmcp/runtime",
    "cxxmcp/tools",
)

FORBIDDEN_PATH_PARTS = (
    "/app/",
    "/gateway/",
    "/observability/",
    "/runtime/",
    "/tools/cli/",
)

INCLUDE_PATTERN = re.compile(r"^\s*#\s*include\s+[<\"]([^>\"]+)[>\"]")
BLOCK_COMMENT_PATTERN = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_PATTERN = re.compile(r"//.*")
STRING_LITERAL_PATTERN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')

FORBIDDEN_NAMESPACE_TOKENS = (
    "mcp::app",
    "mcp::cli",
    "mcp::gateway",
    "mcp::observability",
    "namespace mcp::app",
    "namespace mcp::cli",
    "namespace mcp::gateway",
    "namespace mcp::observability",
)

# Runtime/gateway/CLI type names that must stay out of canonical public SDK
# headers.  The list is intentionally concrete so protocol terms such as
# "discovery", "registry", or "policy" can still appear in legitimate SDK APIs.
FORBIDDEN_API_IDENTIFIERS = (
    "ApprovalState",
    "CapabilityBinding",
    "CapabilityCatalog",
    "ClientDiscoverySession",
    "CommandApp",
    "CommandServices",
    "DiscoveredCapability",
    "DiscoveryResult",
    "Endpoint",
    "ExportBundle",
    "ExposureManagementService",
    "ExposureProfile",
    "ExposureProfileStore",
    "GatewayBatchInitResult",
    "GatewayClientConfigService",
    "GatewayConfigImportResult",
    "GatewayConfigImportService",
    "GatewayEndpointRuntime",
    "GatewayProfileInitReport",
    "GatewayProfileStatus",
    "GatewayReadinessIssue",
    "GatewayReadinessReport",
    "GatewayReadinessService",
    "GatewayRequestHandler",
    "GatewayRoutingService",
    "GatewayRuntimeManager",
    "GatewayServerHealth",
    "GatewayStatusReport",
    "GatewayStatusService",
    "HostedEndpoint",
    "ImportExportService",
    "JsonCapabilityCatalog",
    "JsonExposureProfileStore",
    "JsonImportExportService",
    "JsonMcpServerStore",
    "McpDiscoverySession",
    "McpDiscoverySessionFactory",
    "McpServerDefinition",
    "McpServerRuntime",
    "McpServerRuntimeState",
    "McpServerStore",
    "McpServerTransportKind",
    "McpServerTrustState",
    "MemoryCapabilityCatalog",
    "MemoryExposureProfileStore",
    "MemoryMcpServerStore",
    "MemoryProfileStore",
    "MemoryPromptCatalog",
    "MemoryResourceCatalog",
    "MemoryToolCatalog",
    "NamespaceStrategy",
    "Permission",
    "Policy",
    "Profile",
    "ProfileStore",
    "PromptCatalog",
    "PromptDescriptor",
    "PromptSource",
    "PromptSourceKind",
    "ResourceCatalog",
    "ResourceDescriptor",
    "ResourceSource",
    "ResourceSourceKind",
    "RuntimeOptions",
    "ServerDiscoveryReport",
    "ServerHealthReport",
    "ServerManagementService",
    "StdioLaunchConfig",
    "TaskManagementOptions",
    "TaskManagementService",
    "ToolCatalog",
    "ToolDescriptor",
    "ToolManagementService",
    "ToolSource",
    "ToolSourceKind",
)

FORBIDDEN_FUNCTION_IDENTIFIERS = (
    "default_state_directory",
    "parse_runtime_options",
)


def fail(message: str) -> None:
    raise SystemExit(f"sdk header boundary check failed: {message}")


def iter_headers(source: Path):
    for include_root in CANONICAL_INCLUDE_ROOTS:
        root = source / include_root
        if not root.is_dir():
            fail(f"missing canonical include root: {include_root}")
        yield from root.rglob("*.hpp")


def check_include(header: Path, include: str) -> None:
    normalized = include.replace("\\", "/")
    for prefix in FORBIDDEN_INCLUDE_PREFIXES:
        if normalized == prefix or normalized.startswith(f"{prefix}/"):
            fail(f"{header} includes forbidden tooling/runtime header {include!r}")
    for part in FORBIDDEN_PATH_PARTS:
        if part in normalized:
            fail(f"{header} includes forbidden tooling/runtime path {include!r}")


def scrub_cpp_text(text: str) -> str:
    text = BLOCK_COMMENT_PATTERN.sub(" ", text)
    lines = []
    for line in text.splitlines():
        line = LINE_COMMENT_PATTERN.sub(" ", line)
        line = STRING_LITERAL_PATTERN.sub(" ", line)
        lines.append(line)
    return "\n".join(lines)


def check_forbidden_api_tokens(header: Path, text: str) -> None:
    scrubbed = scrub_cpp_text(text)
    for token in FORBIDDEN_NAMESPACE_TOKENS:
        if re.search(rf"\b{re.escape(token)}\b", scrubbed):
            fail(f"{header} exposes forbidden tooling/runtime namespace {token!r}")
    for identifier in FORBIDDEN_API_IDENTIFIERS:
        if re.search(rf"\b{re.escape(identifier)}\b", scrubbed):
            fail(f"{header} exposes forbidden tooling/runtime type {identifier!r}")
    for identifier in FORBIDDEN_FUNCTION_IDENTIFIERS:
        if re.search(rf"\b{re.escape(identifier)}\b", scrubbed):
            fail(
                f"{header} exposes forbidden tooling/runtime function "
                f"{identifier!r}"
            )


def check_source_tree(source: Path) -> None:
    checked = 0
    for header in iter_headers(source):
        checked += 1
        text = header.read_text(encoding="utf-8")
        for line in text.splitlines():
            match = INCLUDE_PATTERN.match(line)
            if match:
                check_include(header.relative_to(source), match.group(1))
        check_forbidden_api_tokens(header.relative_to(source), text)
    if checked == 0:
        fail("no SDK headers were checked")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=".", help="repository source root")
    args = parser.parse_args()
    check_source_tree(Path(args.source).resolve())


if __name__ == "__main__":
    main()
