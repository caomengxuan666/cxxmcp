#pragma once

#include "mcp/app/mcp_server.hpp"
#include "mcp/app/serialization.hpp"
#include "mcp/core/result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mcp::app {

class GatewayReadinessService;

class GatewayClientConfigService final {
public:
    explicit GatewayClientConfigService(const ExposureProfileStore& profiles);

    core::Result<Json> make_http_client_config(std::string_view profile_id,
                                               std::string_view server_name = {}) const;
    core::Result<Json> make_all_http_client_configs(std::string_view server_name_prefix = {}) const;
    core::Result<Json> make_ready_http_client_configs(const GatewayReadinessService& readiness,
                                                      std::string_view server_name_prefix = {}) const;
    core::Result<Json> make_stdio_client_config(std::string_view profile_id,
                                                std::string_view command,
                                                std::vector<std::string> args,
                                                std::string_view server_name = {}) const;

private:
    const ExposureProfileStore& profiles_;
};

} // namespace mcp::app
