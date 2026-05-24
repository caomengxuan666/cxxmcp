// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string_view>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/server/registry.hpp"

namespace mcp::adapters {

class Adapter {
 public:
  virtual ~Adapter() = default;
  virtual std::string_view name() const noexcept = 0;
  virtual core::Result<core::Unit> register_tools(
      server::ToolRegistry& registry) = 0;
};

}  // namespace mcp::adapters
