#include "mcp/gateway/runtime.hpp"

#include "mcp/app/client_discovery.hpp"
#include "mcp/app/exposure_management.hpp"
#include "mcp/app/gateway.hpp"
#include "mcp/app/gateway_runtime.hpp"
#include "mcp/app/mcp_server.hpp"
#include "mcp/app/server_management.hpp"
#include "mcp/app/services.hpp"
#include "mcp/app/upstream_client.hpp"
#include "mcp/protocol/types.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mcp::gateway {
namespace {

core::Error make_gateway_error(std::string message, std::string detail = {}) {
    return core::Error{static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest), std::move(message), std::move(detail)};
}

template <class T>
T* find_by_id(std::vector<T>& items, std::string_view id) {
    const auto it = std::find_if(items.begin(), items.end(), [&](const auto& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &*it;
}

template <class T>
const T* find_by_id(const std::vector<T>& items, std::string_view id) {
    const auto it = std::find_if(items.begin(), items.end(), [&](const auto& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &*it;
}

void apply_env_overrides(mcp::app::McpServerDefinition& server,
                         const std::unordered_map<std::string, std::string>& overrides) {
    for (const auto& [name, value] : overrides) {
        server.stdio.env[name] = value;
    }
}

void apply_header_overrides(mcp::app::McpServerDefinition& server,
                            const std::unordered_map<std::string, std::string>& overrides) {
    for (const auto& [name, value] : overrides) {
        server.http.headers[name] = value;
    }
}

} // namespace

struct Runtime::Builder::Impl {
    std::string profile_id = "gateway";
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    std::string path = "/mcp";
    std::string instruction;
    bool trust = true;
    bool discover = true;
    std::string bind_server_id;
    std::vector<mcp::app::McpServerDefinition> servers;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> env_overrides;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> header_overrides;
};

struct Runtime::Impl {
    explicit Impl(std::vector<mcp::app::McpServerDefinition> server_defs, std::string profile)
        : servers(std::move(server_defs)),
          capabilities(),
          profiles(),
          server_management(servers,
                            capabilities,
                            profiles,
                            mcp::app::make_client_discovery_session_for_server),
          exposure_management(profiles, capabilities),
          routing(profiles,
                  capabilities,
                  mcp::app::make_upstream_gateway_tool_caller(servers),
                  mcp::app::make_upstream_gateway_prompt_getter(servers),
                  mcp::app::make_upstream_gateway_resource_reader(servers)),
          runtime_manager(profiles,
                          capabilities,
                          mcp::app::make_upstream_gateway_tool_caller(servers),
                          mcp::app::make_upstream_gateway_prompt_getter(servers),
                          mcp::app::make_upstream_gateway_resource_reader(servers)),
          profile_id(std::move(profile)) {}

    mcp::app::MemoryMcpServerStore servers;
    mcp::app::MemoryCapabilityCatalog capabilities;
    mcp::app::MemoryExposureProfileStore profiles;
    mcp::app::ServerManagementService server_management;
    mcp::app::ExposureManagementService exposure_management;
    mcp::app::GatewayRoutingService routing;
    mcp::app::GatewayRuntimeManager runtime_manager;
    std::string profile_id;
};

Runtime::Builder::Builder()
    : impl_(std::make_unique<Impl>()) {}

Runtime::Builder::Builder(Builder&&) noexcept = default;
Runtime::Builder& Runtime::Builder::operator=(Builder&&) noexcept = default;
Runtime::Builder::~Builder() = default;

Runtime::Builder& Runtime::Builder::profile(std::string id) {
    impl_->profile_id = std::move(id);
    return *this;
}

Runtime::Builder& Runtime::Builder::host(std::string host) {
    impl_->host = std::move(host);
    return *this;
}

Runtime::Builder& Runtime::Builder::port(std::uint16_t port) {
    impl_->port = port;
    return *this;
}

Runtime::Builder& Runtime::Builder::path(std::string path) {
    impl_->path = std::move(path);
    return *this;
}

Runtime::Builder& Runtime::Builder::instruction(std::string value) {
    impl_->instruction = std::move(value);
    return *this;
}

Runtime::Builder& Runtime::Builder::trust(bool enabled) {
    impl_->trust = enabled;
    return *this;
}

Runtime::Builder& Runtime::Builder::discover(bool enabled) {
    impl_->discover = enabled;
    return *this;
}

Runtime::Builder& Runtime::Builder::bind_server(std::string server_id) {
    impl_->bind_server_id = std::move(server_id);
    return *this;
}

Runtime::Builder& Runtime::Builder::add_stdio_server(std::string id,
                                                     std::string command,
                                                     std::vector<std::string> args) {
    auto* server = find_by_id(impl_->servers, id);
    if (!server) {
        impl_->servers.push_back(mcp::app::McpServerDefinition{});
        server = &impl_->servers.back();
    }

    server->id = std::move(id);
    server->name = server->id;
    server->display_name = server->id;
    server->transport = mcp::app::McpServerTransportKind::stdio;
    server->stdio.command = std::move(command);
    server->stdio.args = std::move(args);
    server->stdio.cwd.clear();
    server->stdio.env.clear();
    server->http = {};
    server->enabled = true;
    server->auto_start = true;
    server->trust = mcp::app::McpServerTrustState::untrusted;
    return *this;
}

Runtime::Builder& Runtime::Builder::add_http_server(std::string id, std::string url) {
    auto* server = find_by_id(impl_->servers, id);
    if (!server) {
        impl_->servers.push_back(mcp::app::McpServerDefinition{});
        server = &impl_->servers.back();
    }

    server->id = std::move(id);
    server->name = server->id;
    server->display_name = server->id;
    server->transport = mcp::app::McpServerTransportKind::streamable_http;
    server->http.url = std::move(url);
    server->http.headers.clear();
    server->stdio = {};
    server->enabled = true;
    server->auto_start = true;
    server->trust = mcp::app::McpServerTrustState::untrusted;
    return *this;
}

Runtime::Builder& Runtime::Builder::add_env(std::string server_id,
                                            std::string name,
                                            std::string value) {
    impl_->env_overrides[std::move(server_id)][std::move(name)] = std::move(value);
    return *this;
}

Runtime::Builder& Runtime::Builder::add_header(std::string server_id,
                                               std::string name,
                                               std::string value) {
    impl_->header_overrides[std::move(server_id)][std::move(name)] = std::move(value);
    return *this;
}

core::Result<Runtime> Runtime::Builder::build() const {
    if (impl_->profile_id.empty()) {
        return std::unexpected(make_gateway_error("gateway profile id must not be empty"));
    }
    if (impl_->host.empty()) {
        return std::unexpected(make_gateway_error("gateway host must not be empty"));
    }
    if (impl_->port == 0) {
        return std::unexpected(make_gateway_error("gateway port must not be zero"));
    }
    if (impl_->servers.empty()) {
        return std::unexpected(make_gateway_error("at least one upstream server must be configured"));
    }

    auto servers = impl_->servers;
    for (auto& server : servers) {
        if (impl_->trust) {
            server.trust = mcp::app::McpServerTrustState::trusted;
        }
        if (const auto env_it = impl_->env_overrides.find(server.id); env_it != impl_->env_overrides.end()) {
            apply_env_overrides(server, env_it->second);
        }
        if (const auto header_it = impl_->header_overrides.find(server.id);
            header_it != impl_->header_overrides.end()) {
            apply_header_overrides(server, header_it->second);
        }
    }

    Runtime runtime(std::make_unique<Runtime::Impl>(std::move(servers), impl_->profile_id));
    auto& impl = *runtime.impl_;

    for (const auto& server : impl.servers.list_servers()) {
        const auto saved = impl.server_management.save_server(server);
        if (!saved) {
            return std::unexpected(saved.error());
        }
    }

    if (impl_->discover) {
        const auto discovered = impl.server_management.discover_all_servers();
        for (const auto& report : discovered) {
            if (!report.error_message.empty()) {
                return std::unexpected(make_gateway_error(report.error_message, report.error_detail));
            }
        }
    } else if (!impl_->bind_server_id.empty()) {
        const auto discovered = impl.server_management.discover_server(impl_->bind_server_id);
        if (!discovered) {
            return std::unexpected(discovered.error());
        }
    }

    const auto created = impl.exposure_management.create_profile(impl.profile_id, impl.profile_id);
    if (!created) {
        return std::unexpected(created.error());
    }

    const auto configured = impl.exposure_management.configure_endpoint(impl.profile_id,
                                                                        impl_->host,
                                                                        impl_->port,
                                                                        impl_->path);
    if (!configured) {
        return std::unexpected(configured.error());
    }

    if (!impl_->instruction.empty()) {
        const auto instructed = impl.exposure_management.set_instructions(impl.profile_id, impl_->instruction);
        if (!instructed) {
            return std::unexpected(instructed.error());
        }
    }

    std::string server_to_bind = impl_->bind_server_id;
    if (server_to_bind.empty() && impl.servers.list_servers().size() == 1) {
        server_to_bind = impl.servers.list_servers().front().id;
    }
    if (!server_to_bind.empty()) {
        const auto bound = impl.exposure_management.bind_server_capabilities(impl.profile_id, server_to_bind);
        if (!bound) {
            return std::unexpected(bound.error());
        }
    }

    return runtime;
}

Runtime::Runtime(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

Runtime::Runtime(Runtime&&) noexcept = default;
Runtime& Runtime::operator=(Runtime&&) noexcept = default;
Runtime::~Runtime() = default;

Runtime::Builder Runtime::builder() {
    return Builder{};
}

core::Result<core::Unit> Runtime::start() {
    if (!impl_) {
        return std::unexpected(make_gateway_error("gateway runtime is not configured"));
    }
    const auto started = impl_->runtime_manager.start_http_gateway(impl_->profile_id);
    if (!started) {
        return std::unexpected(started.error());
    }
    return core::Unit{};
}

core::Result<core::Unit> Runtime::stop() {
    if (!impl_) {
        return std::unexpected(make_gateway_error("gateway runtime is not configured"));
    }
    const auto stopped = impl_->runtime_manager.stop_http_gateway(impl_->profile_id);
    if (!stopped) {
        return std::unexpected(stopped.error());
    }
    return core::Unit{};
}

int Runtime::run() {
    if (!impl_) {
        return 1;
    }

    const auto profiles = impl_->profiles.list_exposure_profiles();
    const auto* profile = find_by_id(profiles, impl_->profile_id);
    if (!profile) {
        return 1;
    }

    const auto served = mcp::app::run_http_gateway(impl_->routing, impl_->profile_id, profile->endpoint);
    return served ? 0 : 1;
}

} // namespace mcp::gateway
