// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/app/services.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <utility>

#include "cxxmcp/app/serialization.hpp"

namespace mcp::app {

namespace {

core::Error make_app_error(std::string message, std::string detail = {}) {
  return core::Error{1, std::move(message), std::move(detail)};
}

}  // namespace

MemoryProfileStore::MemoryProfileStore(std::vector<Profile> profiles)
    : profiles_(std::move(profiles)) {}

std::vector<Profile> MemoryProfileStore::list_profiles() const {
  return profiles_;
}

core::Result<core::Unit> MemoryProfileStore::save(Profile profile) {
  if (profile.id.empty()) {
    return std::unexpected(make_app_error("profile id must not be empty"));
  }

  const auto it = std::find_if(
      profiles_.begin(), profiles_.end(),
      [&](const auto& existing) { return existing.id == profile.id; });
  if (it == profiles_.end()) {
    profiles_.push_back(std::move(profile));
  } else {
    *it = std::move(profile);
  }

  return core::Unit{};
}

MemoryMcpServerStore::MemoryMcpServerStore(
    std::vector<McpServerDefinition> servers)
    : servers_(std::move(servers)) {}

std::vector<McpServerDefinition> MemoryMcpServerStore::list_servers() const {
  return servers_;
}

core::Result<core::Unit> MemoryMcpServerStore::save(
    McpServerDefinition server) {
  if (server.id.empty()) {
    return std::unexpected(make_app_error("mcp server id must not be empty"));
  }
  if (server.name.empty()) {
    return std::unexpected(make_app_error("mcp server name must not be empty"));
  }
  if (server.transport == McpServerTransportKind::stdio &&
      server.stdio.command.empty()) {
    return std::unexpected(
        make_app_error("stdio mcp server command must not be empty"));
  }
  if ((server.transport == McpServerTransportKind::streamable_http ||
       server.transport == McpServerTransportKind::legacy_sse) &&
      server.http.url.empty()) {
    return std::unexpected(
        make_app_error("http mcp server url must not be empty"));
  }

  const auto it = std::find_if(
      servers_.begin(), servers_.end(),
      [&](const auto& existing) { return existing.id == server.id; });
  if (it == servers_.end()) {
    servers_.push_back(std::move(server));
  } else {
    *it = std::move(server);
  }

  return core::Unit{};
}

core::Result<core::Unit> MemoryMcpServerStore::remove(
    std::string_view server_id) {
  const auto original_size = servers_.size();
  servers_.erase(std::remove_if(servers_.begin(), servers_.end(),
                                [&](const auto& existing) {
                                  return existing.id == server_id;
                                }),
                 servers_.end());
  if (servers_.size() == original_size) {
    return std::unexpected(
        make_app_error("mcp server not found", std::string(server_id)));
  }
  return core::Unit{};
}

MemoryCapabilityCatalog::MemoryCapabilityCatalog(
    std::vector<DiscoveredCapability> capabilities)
    : capabilities_(std::move(capabilities)) {}

std::vector<DiscoveredCapability> MemoryCapabilityCatalog::list_capabilities()
    const {
  return capabilities_;
}

core::Result<core::Unit> MemoryCapabilityCatalog::replace_for_server(
    std::string server_id, std::vector<DiscoveredCapability> capabilities) {
  if (server_id.empty()) {
    return std::unexpected(
        make_app_error("capability server id must not be empty"));
  }

  capabilities_.erase(std::remove_if(capabilities_.begin(), capabilities_.end(),
                                     [&](const auto& existing) {
                                       return existing.server_id == server_id;
                                     }),
                      capabilities_.end());

  for (auto& capability : capabilities) {
    if (capability.id.empty()) {
      return std::unexpected(make_app_error("capability id must not be empty"));
    }
    capability.server_id = server_id;
    capabilities_.push_back(std::move(capability));
  }

  return core::Unit{};
}

MemoryExposureProfileStore::MemoryExposureProfileStore(
    std::vector<ExposureProfile> profiles)
    : profiles_(std::move(profiles)) {}

std::vector<ExposureProfile>
MemoryExposureProfileStore::list_exposure_profiles() const {
  return profiles_;
}

core::Result<core::Unit> MemoryExposureProfileStore::save(
    ExposureProfile profile) {
  if (profile.id.empty()) {
    return std::unexpected(
        make_app_error("exposure profile id must not be empty"));
  }
  if (profile.name.empty()) {
    return std::unexpected(
        make_app_error("exposure profile name must not be empty"));
  }

  const auto it = std::find_if(
      profiles_.begin(), profiles_.end(),
      [&](const auto& existing) { return existing.id == profile.id; });
  if (it == profiles_.end()) {
    profiles_.push_back(std::move(profile));
  } else {
    *it = std::move(profile);
  }

  return core::Unit{};
}

core::Result<core::Unit> MemoryExposureProfileStore::remove(
    std::string_view profile_id) {
  const auto original_size = profiles_.size();
  profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(),
                                 [&](const auto& profile) {
                                   return profile.id == profile_id;
                                 }),
                  profiles_.end());
  if (profiles_.size() == original_size) {
    return std::unexpected(
        make_app_error("exposure profile not found", std::string(profile_id)));
  }

  return core::Unit{};
}

MemoryToolCatalog::MemoryToolCatalog(std::vector<ToolDescriptor> tools)
    : tools_(std::move(tools)) {}

std::vector<ToolDescriptor> MemoryToolCatalog::list() const { return tools_; }

core::Result<core::Unit> MemoryToolCatalog::add(ToolDescriptor tool) {
  if (tool.id.empty()) {
    return std::unexpected(make_app_error("tool id must not be empty"));
  }
  if (tool.definition.name.empty()) {
    return std::unexpected(
        make_app_error("tool definition name must not be empty"));
  }

  const auto it = std::find_if(
      tools_.begin(), tools_.end(),
      [&](const auto& existing) { return existing.id == tool.id; });
  if (it != tools_.end()) {
    return std::unexpected(make_app_error("tool already exists", tool.id));
  }

  tools_.push_back(std::move(tool));
  return core::Unit{};
}

core::Result<core::Unit> MemoryToolCatalog::update_policy(std::string tool_id,
                                                          Policy policy) {
  const auto it = std::find_if(
      tools_.begin(), tools_.end(),
      [&](const auto& existing) { return existing.id == tool_id; });
  if (it == tools_.end()) {
    return std::unexpected(
        make_app_error("tool not found", std::move(tool_id)));
  }

  it->policy = std::move(policy);
  return core::Unit{};
}

MemoryResourceCatalog::MemoryResourceCatalog(
    std::vector<ResourceDescriptor> resources)
    : resources_(std::move(resources)) {}

std::vector<ResourceDescriptor> MemoryResourceCatalog::list() const {
  return resources_;
}

core::Result<core::Unit> MemoryResourceCatalog::add(
    ResourceDescriptor resource) {
  if (resource.id.empty()) {
    return std::unexpected(make_app_error("resource id must not be empty"));
  }
  if (resource.name.empty()) {
    return std::unexpected(make_app_error("resource name must not be empty"));
  }
  if (resource.uri.empty()) {
    return std::unexpected(make_app_error("resource uri must not be empty"));
  }

  const auto it = std::find_if(
      resources_.begin(), resources_.end(),
      [&](const auto& existing) { return existing.id == resource.id; });
  if (it != resources_.end()) {
    return std::unexpected(
        make_app_error("resource already exists", resource.id));
  }

  resources_.push_back(std::move(resource));
  return core::Unit{};
}

MemoryPromptCatalog::MemoryPromptCatalog(std::vector<PromptDescriptor> prompts)
    : prompts_(std::move(prompts)) {}

std::vector<PromptDescriptor> MemoryPromptCatalog::list() const {
  return prompts_;
}

core::Result<core::Unit> MemoryPromptCatalog::add(PromptDescriptor prompt) {
  if (prompt.id.empty()) {
    return std::unexpected(make_app_error("prompt id must not be empty"));
  }
  if (prompt.name.empty()) {
    return std::unexpected(make_app_error("prompt name must not be empty"));
  }
  if (prompt.template_text.empty()) {
    return std::unexpected(make_app_error("prompt template must not be empty"));
  }

  const auto it = std::find_if(
      prompts_.begin(), prompts_.end(),
      [&](const auto& existing) { return existing.id == prompt.id; });
  if (it != prompts_.end()) {
    return std::unexpected(make_app_error("prompt already exists", prompt.id));
  }

  prompts_.push_back(std::move(prompt));
  return core::Unit{};
}

core::Result<ExportBundle> JsonImportExportService::import_bundle(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return std::unexpected(
        make_app_error("failed to open bundle", path.string()));
  }

  Json json;
  try {
    input >> json;
  } catch (const std::exception& exception) {
    return std::unexpected(
        make_app_error("failed to parse bundle", exception.what()));
  }
  const auto bundle = export_bundle_from_json(json);
  if (!bundle) {
    return std::unexpected(bundle.error());
  }

  return *bundle;
}

core::Result<core::Unit> JsonImportExportService::export_bundle(
    const ExportBundle& bundle, const std::filesystem::path& path) {
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      return std::unexpected(
          make_app_error("failed to create export directory", ec.message()));
    }
  }

  std::ofstream output(path);
  if (!output.is_open()) {
    return std::unexpected(
        make_app_error("failed to open bundle for writing", path.string()));
  }

  output << to_json(bundle).dump(2);
  if (!output.good()) {
    return std::unexpected(
        make_app_error("failed to write bundle", path.string()));
  }

  return core::Unit{};
}

}  // namespace mcp::app
