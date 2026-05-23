#pragma once

#include "mcp/core/result.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace mcp::app {

struct Endpoint {
    std::string name;
    std::string url;
};

struct Profile {
    std::string id;
    std::string name;
    std::vector<Endpoint> endpoints;
    std::vector<std::string> enabled_tool_ids;
    std::unordered_map<std::string, std::string> environment;
};

class ProfileStore {
public:
    virtual ~ProfileStore() = default;
    virtual std::vector<Profile> list_profiles() const = 0;
    virtual core::Result<core::Unit> save(Profile profile) = 0;
};

} // namespace mcp::app
