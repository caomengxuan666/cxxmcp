#pragma once

#include "mcp/core/result.hpp"

#include <string>
#include <vector>

namespace mcp::app {

enum class ResourceSourceKind {
    local_manifest,
    local_plugin,
    remote_mcp_server,
    generated_adapter,
};

struct ResourceSource {
    ResourceSourceKind kind = ResourceSourceKind::local_manifest;
    std::string location;
};

struct ResourceDescriptor {
    std::string id;
    std::string name;
    std::string description;
    std::string uri;
    ResourceSource source;
};

class ResourceCatalog {
public:
    virtual ~ResourceCatalog() = default;
    virtual std::vector<ResourceDescriptor> list() const = 0;
    virtual core::Result<core::Unit> add(ResourceDescriptor resource) = 0;
};

} // namespace mcp::app
