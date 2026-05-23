#pragma once

#include "mcp/app/import_export.hpp"
#include "mcp/app/prompt_catalog.hpp"
#include "mcp/app/profile.hpp"
#include "mcp/app/resource_catalog.hpp"
#include "mcp/app/tool_catalog.hpp"

#include <filesystem>

namespace mcp::app {

class MemoryProfileStore final : public ProfileStore {
public:
    MemoryProfileStore() = default;
    explicit MemoryProfileStore(std::vector<Profile> profiles);

    std::vector<Profile> list_profiles() const override;
    core::Result<core::Unit> save(Profile profile) override;

private:
    std::vector<Profile> profiles_;
};

class MemoryToolCatalog final : public ToolCatalog {
public:
    MemoryToolCatalog() = default;
    explicit MemoryToolCatalog(std::vector<ToolDescriptor> tools);

    std::vector<ToolDescriptor> list() const override;
    core::Result<core::Unit> add(ToolDescriptor tool) override;
    core::Result<core::Unit> update_policy(std::string tool_id, Policy policy) override;

private:
    std::vector<ToolDescriptor> tools_;
};

class MemoryResourceCatalog final : public ResourceCatalog {
public:
    MemoryResourceCatalog() = default;
    explicit MemoryResourceCatalog(std::vector<ResourceDescriptor> resources);

    std::vector<ResourceDescriptor> list() const override;
    core::Result<core::Unit> add(ResourceDescriptor resource) override;

private:
    std::vector<ResourceDescriptor> resources_;
};

class MemoryPromptCatalog final : public PromptCatalog {
public:
    MemoryPromptCatalog() = default;
    explicit MemoryPromptCatalog(std::vector<PromptDescriptor> prompts);

    std::vector<PromptDescriptor> list() const override;
    core::Result<core::Unit> add(PromptDescriptor prompt) override;

private:
    std::vector<PromptDescriptor> prompts_;
};

class JsonImportExportService final : public ImportExportService {
public:
    core::Result<ExportBundle> import_bundle(const std::filesystem::path& path) override;
    core::Result<core::Unit> export_bundle(const ExportBundle& bundle, const std::filesystem::path& path) override;
};

} // namespace mcp::app
