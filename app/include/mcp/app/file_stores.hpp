#pragma once

#include "mcp/app/mcp_server.hpp"

#include <filesystem>

namespace mcp::app {

class JsonMcpServerStore final : public McpServerStore {
public:
    explicit JsonMcpServerStore(std::filesystem::path path);

    std::vector<McpServerDefinition> list_servers() const override;
    core::Result<core::Unit> save(McpServerDefinition server) override;
    core::Result<core::Unit> remove(std::string_view server_id) override;

private:
    std::filesystem::path path_;
};

class JsonCapabilityCatalog final : public CapabilityCatalog {
public:
    explicit JsonCapabilityCatalog(std::filesystem::path path);

    std::vector<DiscoveredCapability> list_capabilities() const override;
    core::Result<core::Unit> replace_for_server(std::string server_id,
                                                std::vector<DiscoveredCapability> capabilities) override;

private:
    std::filesystem::path path_;
};

class JsonExposureProfileStore final : public ExposureProfileStore {
public:
    explicit JsonExposureProfileStore(std::filesystem::path path);

    std::vector<ExposureProfile> list_exposure_profiles() const override;
    core::Result<core::Unit> save(ExposureProfile profile) override;
    core::Result<core::Unit> remove(std::string_view profile_id) override;

private:
    std::filesystem::path path_;
};

} // namespace mcp::app
