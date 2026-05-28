// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Public server handler callback type aliases.

#include <functional>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/server/context.hpp"

namespace mcp::server {

/// @brief Application callback that executes a tool.
/// @return A ToolResult, or a core::Error propagated to the caller.
using ToolHandler =
    std::function<core::Result<protocol::ToolResult>(const ToolContext&)>;

/// @brief Application callback that renders a prompt.
/// @return A prompt result, or a core::Error propagated to the caller.
using PromptHandler = std::function<core::Result<protocol::PromptsGetResult>(
    const PromptContext&)>;

/// @brief Application callback that reads a resource.
/// @return Resource contents, or a core::Error propagated to the caller.
using ResourceReadHandler =
    std::function<core::Result<protocol::ResourcesReadResult>(
        const ResourceContext&)>;

}  // namespace mcp::server
