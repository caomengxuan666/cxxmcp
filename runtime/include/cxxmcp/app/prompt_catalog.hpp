// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string>
#include <vector>

#include "cxxmcp/core/result.hpp"

namespace mcp::app {

enum class PromptSourceKind {
  local_manifest,
  local_plugin,
  remote_mcp_server,
  generated_adapter,
};

struct PromptSource {
  PromptSourceKind kind = PromptSourceKind::local_manifest;
  std::string location;
};

struct PromptDescriptor {
  std::string id;
  std::string name;
  std::string description;
  std::string template_text;
  PromptSource source;
};

class PromptCatalog {
 public:
  virtual ~PromptCatalog() = default;
  virtual std::vector<PromptDescriptor> list() const = 0;
  virtual core::Result<core::Unit> add(PromptDescriptor prompt) = 0;
};

}  // namespace mcp::app
