#include "cxxmcp/app/file_stores.hpp"

#include "cxxmcp/app/serialization.hpp"
#include "cxxmcp/app/services.hpp"

#include <exception>
#include <fstream>
#include <utility>

namespace mcp::app {
    namespace {

        core::Error make_file_store_error(std::string message, std::string detail = {}) {
            return core::Error{1, std::move(message), std::move(detail)};
        }

        core::Result<Json> read_json_array(const std::filesystem::path &path, std::string_view label) {
            if (!std::filesystem::exists(path)) {
                return Json::array();
            }

            std::ifstream input(path);
            if (!input.is_open()) {
                return std::unexpected(make_file_store_error("failed to open " + std::string(label), path.string()));
            }

            Json json;
            try {
                input >> json;
            } catch (const std::exception &exception) {
                return std::unexpected(make_file_store_error("failed to parse " + std::string(label), exception.what()));
            }

            if (!json.is_array()) {
                return std::unexpected(make_file_store_error(std::string(label) + " must contain a JSON array"));
            }
            return json;
        }

        core::Result<core::Unit> write_json_array(const std::filesystem::path &path, const Json &json, std::string_view label) {
            const auto parent = path.parent_path();
            if (!parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    return std::unexpected(make_file_store_error("failed to create " + std::string(label) + " directory",
                                                                 ec.message()));
                }
            }

            std::ofstream output(path);
            if (!output.is_open()) {
                return std::unexpected(make_file_store_error("failed to open " + std::string(label) + " for writing",
                                                             path.string()));
            }

            output << json.dump(2);
            if (!output.good()) {
                return std::unexpected(make_file_store_error("failed to write " + std::string(label), path.string()));
            }
            return core::Unit{};
        }

        core::Result<std::vector<McpServerDefinition>> load_servers(const std::filesystem::path &path) {
            const auto json = read_json_array(path, "mcp servers store");
            if (!json) {
                return std::unexpected(json.error());
            }

            std::vector<McpServerDefinition> servers;
            for (const auto &item: *json) {
                const auto server = mcp_server_definition_from_json(item);
                if (!server) {
                    return std::unexpected(server.error());
                }
                servers.push_back(*server);
            }
            return servers;
        }

        core::Result<core::Unit> save_servers(const std::filesystem::path &path,
                                              const std::vector<McpServerDefinition> &servers) {
            Json json = Json::array();
            for (const auto &server: servers) {
                json.push_back(to_json(server));
            }
            return write_json_array(path, json, "mcp servers store");
        }

        core::Result<std::vector<DiscoveredCapability>> load_capabilities(const std::filesystem::path &path) {
            const auto json = read_json_array(path, "capability catalog");
            if (!json) {
                return std::unexpected(json.error());
            }

            std::vector<DiscoveredCapability> capabilities;
            for (const auto &item: *json) {
                const auto capability = discovered_capability_from_json(item);
                if (!capability) {
                    return std::unexpected(capability.error());
                }
                capabilities.push_back(*capability);
            }
            return capabilities;
        }

        core::Result<core::Unit> save_capabilities(const std::filesystem::path &path,
                                                   const std::vector<DiscoveredCapability> &capabilities) {
            Json json = Json::array();
            for (const auto &capability: capabilities) {
                json.push_back(to_json(capability));
            }
            return write_json_array(path, json, "capability catalog");
        }

        core::Result<std::vector<ExposureProfile>> load_exposure_profiles(const std::filesystem::path &path) {
            const auto json = read_json_array(path, "exposure profile store");
            if (!json) {
                return std::unexpected(json.error());
            }

            std::vector<ExposureProfile> profiles;
            for (const auto &item: *json) {
                const auto profile = exposure_profile_from_json(item);
                if (!profile) {
                    return std::unexpected(profile.error());
                }
                profiles.push_back(*profile);
            }
            return profiles;
        }

        core::Result<core::Unit> save_exposure_profiles(const std::filesystem::path &path,
                                                        const std::vector<ExposureProfile> &profiles) {
            Json json = Json::array();
            for (const auto &profile: profiles) {
                json.push_back(to_json(profile));
            }
            return write_json_array(path, json, "exposure profile store");
        }

    }// namespace

    JsonMcpServerStore::JsonMcpServerStore(std::filesystem::path path)
        : path_(std::move(path)) {}

    std::vector<McpServerDefinition> JsonMcpServerStore::list_servers() const {
        const auto servers = load_servers(path_);
        if (!servers) {
            return {};
        }
        return *servers;
    }

    core::Result<core::Unit> JsonMcpServerStore::save(McpServerDefinition server) {
        const auto servers = load_servers(path_);
        if (!servers) {
            return std::unexpected(servers.error());
        }

        MemoryMcpServerStore memory(*servers);
        const auto saved = memory.save(std::move(server));
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return save_servers(path_, memory.list_servers());
    }

    core::Result<core::Unit> JsonMcpServerStore::remove(std::string_view server_id) {
        const auto servers = load_servers(path_);
        if (!servers) {
            return std::unexpected(servers.error());
        }

        MemoryMcpServerStore memory(*servers);
        const auto removed = memory.remove(server_id);
        if (!removed) {
            return std::unexpected(removed.error());
        }
        return save_servers(path_, memory.list_servers());
    }

    JsonCapabilityCatalog::JsonCapabilityCatalog(std::filesystem::path path)
        : path_(std::move(path)) {}

    std::vector<DiscoveredCapability> JsonCapabilityCatalog::list_capabilities() const {
        const auto capabilities = load_capabilities(path_);
        if (!capabilities) {
            return {};
        }
        return *capabilities;
    }

    core::Result<core::Unit> JsonCapabilityCatalog::replace_for_server(
            std::string server_id,
            std::vector<DiscoveredCapability> capabilities) {
        const auto existing = load_capabilities(path_);
        if (!existing) {
            return std::unexpected(existing.error());
        }

        MemoryCapabilityCatalog memory(*existing);
        const auto replaced = memory.replace_for_server(std::move(server_id), std::move(capabilities));
        if (!replaced) {
            return std::unexpected(replaced.error());
        }
        return save_capabilities(path_, memory.list_capabilities());
    }

    JsonExposureProfileStore::JsonExposureProfileStore(std::filesystem::path path)
        : path_(std::move(path)) {}

    std::vector<ExposureProfile> JsonExposureProfileStore::list_exposure_profiles() const {
        const auto profiles = load_exposure_profiles(path_);
        if (!profiles) {
            return {};
        }
        return *profiles;
    }

    core::Result<core::Unit> JsonExposureProfileStore::save(ExposureProfile profile) {
        const auto profiles = load_exposure_profiles(path_);
        if (!profiles) {
            return std::unexpected(profiles.error());
        }

        MemoryExposureProfileStore memory(*profiles);
        const auto saved = memory.save(std::move(profile));
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return save_exposure_profiles(path_, memory.list_exposure_profiles());
    }

    core::Result<core::Unit> JsonExposureProfileStore::remove(std::string_view profile_id) {
        const auto profiles = load_exposure_profiles(path_);
        if (!profiles) {
            return std::unexpected(profiles.error());
        }

        MemoryExposureProfileStore memory(*profiles);
        const auto removed = memory.remove(profile_id);
        if (!removed) {
            return std::unexpected(removed.error());
        }
        return save_exposure_profiles(path_, memory.list_exposure_profiles());
    }

}// namespace mcp::app
