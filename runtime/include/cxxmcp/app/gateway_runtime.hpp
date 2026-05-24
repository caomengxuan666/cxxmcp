#pragma once

#include "cxxmcp/app/gateway.hpp"
#include "cxxmcp/app/mcp_server.hpp"
#include "cxxmcp/core/result.hpp"

#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mcp::app {

    GatewayToolCaller make_upstream_gateway_tool_caller(const McpServerStore &servers);
    GatewayPromptGetter make_upstream_gateway_prompt_getter(const McpServerStore &servers);
    GatewayResourceReader make_upstream_gateway_resource_reader(const McpServerStore &servers);

    GatewayToolCaller make_process_gateway_tool_caller(const McpServerStore &servers);
    GatewayPromptGetter make_process_gateway_prompt_getter(const McpServerStore &servers);
    GatewayResourceReader make_process_gateway_resource_reader(const McpServerStore &servers);

    core::Result<core::Unit> run_stdio_gateway(const GatewayRoutingService &routing,
                                               std::string_view profile_id,
                                               std::istream &input,
                                               std::ostream &output);

    core::Result<core::Unit> run_http_gateway(const GatewayRoutingService &routing,
                                              std::string_view profile_id,
                                              const HostedEndpoint &endpoint);

    struct GatewayEndpointRuntime {
        std::string profile_id;
        std::string profile_name;
        HostedEndpoint endpoint;
        bool running = false;
        std::string last_error;
    };

    class GatewayRuntimeManager final {
    public:
        GatewayRuntimeManager(const ExposureProfileStore &profiles,
                              const CapabilityCatalog &capabilities,
                              GatewayToolCaller call_tool,
                              GatewayPromptGetter get_prompt = {},
                              GatewayResourceReader read_resource = {});
        ~GatewayRuntimeManager();

        core::Result<core::Unit> start_http_gateway(std::string_view profile_id);
        core::Result<core::Unit> stop_http_gateway(std::string_view profile_id);
        std::vector<GatewayEndpointRuntime> list_http_gateways() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}// namespace mcp::app
