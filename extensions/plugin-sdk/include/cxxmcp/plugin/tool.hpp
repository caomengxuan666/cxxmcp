// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/tool.hpp"

namespace mcp::plugin {

struct ToolExecutionContext {
  std::string tool_name;
  protocol::Json arguments = protocol::Json::object();
};

class ToolPlugin {
 public:
  virtual ~ToolPlugin() = default;
  virtual std::vector<protocol::ToolDefinition> tools() const = 0;
  virtual core::Result<protocol::ToolResult> call(
      const ToolExecutionContext& context) = 0;
};

}  // namespace mcp::plugin
