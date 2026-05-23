#include "mcp/app/gateway_runtime.hpp"

#include "mcp/app/upstream_client.hpp"
#include "mcp/client/session.hpp"
#include "mcp/server/http_transport.hpp"
#include "mcp/server/stdio_transport.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <unordered_map>
#include <vector>

namespace mcp::app {
namespace {

core::Error make_runtime_error(std::string message, std::string detail = {}) {
    return core::Error{static_cast<int>(protocol::ErrorCode::InternalError), std::move(message), std::move(detail)};
}

core::Result<McpServerDefinition> find_enabled_server(const McpServerStore& servers,
                                                      std::string_view server_id) {
    const auto configured_servers = servers.list_servers();
    const auto it = std::find_if(configured_servers.begin(), configured_servers.end(), [&](const auto& server) {
        return server.id == server_id;
    });
    if (it == configured_servers.end()) {
        return std::unexpected(make_runtime_error("mcp server not found", std::string(server_id)));
    }
    if (!it->enabled) {
        return std::unexpected(make_runtime_error("cxxmcp server is disabled", std::string(server_id)));
    }
    if (it->trust == McpServerTrustState::untrusted) {
        return std::unexpected(make_runtime_error("cxxmcp server is untrusted", std::string(server_id)));
    }
    if (it->trust == McpServerTrustState::blocked) {
        return std::unexpected(make_runtime_error("cxxmcp server is blocked", std::string(server_id)));
    }
    return *it;
}

core::Result<client::McpClientSession> initialized_session(const McpServerDefinition& server) {
    auto transport = make_client_transport_for_server(server);
    if (!transport) {
        return std::unexpected(transport.error());
    }

    auto session = client::McpClientSession(std::move(*transport));

    const auto initialized = session.initialize();
    if (!initialized) {
        return std::unexpected(initialized.error());
    }
    const auto marked_initialized = session.mark_initialized();
    if (!marked_initialized) {
        return std::unexpected(marked_initialized.error());
    }
    return session;
}

} // namespace

GatewayToolCaller make_upstream_gateway_tool_caller(const McpServerStore& servers) {
    return [&servers](std::string_view server_id,
                      const protocol::ToolCall& call) -> core::Result<protocol::ToolResult> {
        const auto server = find_enabled_server(servers, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }

        auto session = initialized_session(*server);
        if (!session) {
            return std::unexpected(session.error());
        }
        return session->call_tool(call);
    };
}

GatewayPromptGetter make_upstream_gateway_prompt_getter(const McpServerStore& servers) {
    return [&servers](std::string_view server_id,
                      const protocol::PromptsGetParams& params) -> core::Result<protocol::PromptsGetResult> {
        const auto server = find_enabled_server(servers, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }

        auto session = initialized_session(*server);
        if (!session) {
            return std::unexpected(session.error());
        }
        return session->get_prompt(params);
    };
}

GatewayResourceReader make_upstream_gateway_resource_reader(const McpServerStore& servers) {
    return [&servers](std::string_view server_id,
                      const protocol::ResourcesReadParams& params) -> core::Result<protocol::ResourcesReadResult> {
        const auto server = find_enabled_server(servers, server_id);
        if (!server) {
            return std::unexpected(server.error());
        }

        auto session = initialized_session(*server);
        if (!session) {
            return std::unexpected(session.error());
        }
        return session->read_resource(params);
    };
}

GatewayToolCaller make_process_gateway_tool_caller(const McpServerStore& servers) {
    return make_upstream_gateway_tool_caller(servers);
}

GatewayPromptGetter make_process_gateway_prompt_getter(const McpServerStore& servers) {
    return make_upstream_gateway_prompt_getter(servers);
}

GatewayResourceReader make_process_gateway_resource_reader(const McpServerStore& servers) {
    return make_upstream_gateway_resource_reader(servers);
}

core::Result<core::Unit> run_stdio_gateway(const GatewayRoutingService& routing,
                                           std::string_view profile_id,
                                           std::istream& input,
                                           std::ostream& output) {
    GatewayRequestHandler handler(routing, std::string(profile_id));
    server::StdioTransport transport(input, output);
    return transport.start([&handler](const protocol::JsonRpcRequest& request,
                                      const server::SessionContext&) {
        return handler.handle(request);
    });
}

core::Result<core::Unit> run_http_gateway(const GatewayRoutingService& routing,
                                          std::string_view profile_id,
                                          const HostedEndpoint& endpoint) {
    if (endpoint.transport != McpServerTransportKind::streamable_http) {
        return std::unexpected(make_runtime_error("gateway endpoint transport must be streamable_http",
                                                  std::string(profile_id)));
    }

    GatewayRequestHandler handler(routing, std::string(profile_id));
    server::HttpTransport transport(server::HttpTransportOptions{
        .listen_host = endpoint.listen_host,
        .listen_port = endpoint.listen_port,
        .path = endpoint.path,
    });
    return transport.start([&handler](const protocol::JsonRpcRequest& request,
                                      const server::SessionContext&) {
        return handler.handle(request);
    });
}

namespace {

std::string format_transport_error(const core::Error& error) {
    if (error.detail.empty()) {
        return error.message;
    }
    return error.message + ": " + error.detail;
}

const ExposureProfile* find_profile(const std::vector<ExposureProfile>& profiles, std::string_view profile_id) {
    const auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return profile.id == profile_id;
    });
    return it == profiles.end() ? nullptr : &*it;
}

} // namespace

struct GatewayRuntimeManager::Impl final {
    Impl(const ExposureProfileStore& profiles,
         const CapabilityCatalog& capabilities,
         GatewayToolCaller call_tool,
         GatewayPromptGetter get_prompt,
         GatewayResourceReader read_resource)
        : profiles(profiles),
          routing(profiles, capabilities, std::move(call_tool), std::move(get_prompt), std::move(read_resource)) {}

    ~Impl() {
        stop_all_http_gateways();
    }

    core::Result<core::Unit> start_http_gateway(std::string_view profile_id) {
        const auto profiles_snapshot = profiles.list_exposure_profiles();
        const auto* profile = find_profile(profiles_snapshot, profile_id);
        if (!profile) {
            return std::unexpected(make_runtime_error("exposure profile not found", std::string(profile_id)));
        }
        if (profile->endpoint.transport != McpServerTransportKind::streamable_http) {
            return std::unexpected(make_runtime_error("gateway endpoint transport must be streamable_http",
                                                      std::string(profile_id)));
        }
        if (profile->endpoint.listen_port == 0) {
            return std::unexpected(make_runtime_error("gateway endpoint port must be configured",
                                                      std::string(profile_id)));
        }

        auto runtime = std::make_shared<RunningHttpGateway>();
        runtime->profile_id = profile->id;
        runtime->profile_name = profile->name;
        runtime->endpoint = profile->endpoint;
        runtime->running = true;
        runtime->last_error.clear();
        runtime->transport = std::make_shared<server::HttpTransport>(server::HttpTransportOptions{
            .listen_host = profile->endpoint.listen_host,
            .listen_port = profile->endpoint.listen_port,
            .path = profile->endpoint.path,
        });

        {
            std::lock_guard lock(mutex);
            auto& slot = http_gateways[runtime->profile_id];
            if (slot && slot->thread.joinable()) {
                if (slot->running) {
                    return std::unexpected(make_runtime_error("gateway is already running", runtime->profile_id));
                }
                return std::unexpected(make_runtime_error("gateway is still shutting down", runtime->profile_id));
            }
            if (slot && slot->running) {
                return std::unexpected(make_runtime_error("gateway is already running", runtime->profile_id));
            }
            slot = runtime;
        }

        auto* impl = this;
        runtime->thread = std::thread([impl,
                                       runtime,
                                       profile_id = runtime->profile_id,
                                       transport = runtime->transport]() {
            GatewayRequestHandler handler(impl->routing, profile_id);
            const auto served = transport->start([&handler](const protocol::JsonRpcRequest& request,
                                                            const server::SessionContext&) {
                return handler.handle(request);
            });

            std::lock_guard lock(impl->mutex);
            const auto it = impl->http_gateways.find(profile_id);
            if (it == impl->http_gateways.end() || !it->second) {
                return;
            }

            it->second->running = false;
            if (!served) {
                it->second->last_error = format_transport_error(served.error());
            } else {
                it->second->last_error.clear();
            }
            it->second->transport.reset();
        });

        return core::Unit{};
    }

    core::Result<core::Unit> stop_http_gateway(std::string_view profile_id) {
        std::shared_ptr<RunningHttpGateway> runtime;
        {
            std::lock_guard lock(mutex);
            const auto it = http_gateways.find(std::string(profile_id));
            if (it == http_gateways.end() || !it->second) {
                return std::unexpected(make_runtime_error("gateway is not running", std::string(profile_id)));
            }
            runtime = it->second;
        }

        if (runtime->transport) {
            runtime->transport->stop();
        }
        if (runtime->thread.joinable()) {
            runtime->thread.join();
        }

        {
            std::lock_guard lock(mutex);
            const auto it = http_gateways.find(std::string(profile_id));
            if (it != http_gateways.end() && it->second) {
                it->second->running = false;
                it->second->transport.reset();
            }
        }

        return core::Unit{};
    }

    std::vector<GatewayEndpointRuntime> list_http_gateways() const {
        std::vector<GatewayEndpointRuntime> result;
        const auto profiles_snapshot = profiles.list_exposure_profiles();
        std::lock_guard lock(mutex);
        result.reserve(profiles_snapshot.size());
        for (const auto& profile : profiles_snapshot) {
            GatewayEndpointRuntime runtime;
            runtime.profile_id = profile.id;
            runtime.profile_name = profile.name;
            runtime.endpoint = profile.endpoint;
            const auto it = http_gateways.find(profile.id);
            if (it != http_gateways.end() && it->second) {
                runtime.running = it->second->running;
                runtime.last_error = it->second->last_error;
            }
            result.push_back(std::move(runtime));
        }

        std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.profile_id < rhs.profile_id;
        });
        return result;
    }

private:
    struct RunningHttpGateway final {
        std::string profile_id;
        std::string profile_name;
        HostedEndpoint endpoint;
        std::shared_ptr<server::HttpTransport> transport;
        std::thread thread;
        bool running = false;
        std::string last_error;
    };

    void stop_all_http_gateways() noexcept {
        std::vector<std::shared_ptr<RunningHttpGateway>> runtimes;
        {
            std::lock_guard lock(mutex);
            for (auto& [_, runtime] : http_gateways) {
                if (runtime && runtime->thread.joinable()) {
                    runtimes.push_back(runtime);
                }
            }
        }

        for (auto& runtime : runtimes) {
            if (runtime->transport) {
                runtime->transport->stop();
            }
        }
        for (auto& runtime : runtimes) {
            if (runtime->thread.joinable()) {
                runtime->thread.join();
            }
        }
    }

    const ExposureProfileStore& profiles;
    GatewayRoutingService routing;
    mutable std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<RunningHttpGateway>> http_gateways;
};

GatewayRuntimeManager::GatewayRuntimeManager(const ExposureProfileStore& profiles,
                                             const CapabilityCatalog& capabilities,
                                             GatewayToolCaller call_tool,
                                             GatewayPromptGetter get_prompt,
                                             GatewayResourceReader read_resource)
    : impl_(std::make_unique<Impl>(profiles,
                                   capabilities,
                                   std::move(call_tool),
                                   std::move(get_prompt),
                                   std::move(read_resource))) {}

GatewayRuntimeManager::~GatewayRuntimeManager() = default;

core::Result<core::Unit> GatewayRuntimeManager::start_http_gateway(std::string_view profile_id) {
    return impl_->start_http_gateway(profile_id);
}

core::Result<core::Unit> GatewayRuntimeManager::stop_http_gateway(std::string_view profile_id) {
    return impl_->stop_http_gateway(profile_id);
}

std::vector<GatewayEndpointRuntime> GatewayRuntimeManager::list_http_gateways() const {
    return impl_->list_http_gateways();
}

} // namespace mcp::app
