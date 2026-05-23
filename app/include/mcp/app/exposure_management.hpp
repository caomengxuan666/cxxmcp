#pragma once

#include "mcp/app/mcp_server.hpp"
#include "mcp/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mcp::app {

class ExposureManagementService final {
public:
    ExposureManagementService(ExposureProfileStore& profiles, CapabilityCatalog& capabilities);

    core::Result<ExposureProfile> get_profile(std::string_view profile_id) const;
    core::Result<core::Unit> create_profile(std::string_view profile_id, std::string_view name);
    core::Result<core::Unit> remove_profile(std::string_view profile_id);
    core::Result<core::Unit> configure_endpoint(std::string_view profile_id,
                                                std::string_view host,
                                                std::uint16_t port,
                                                std::string_view path = "/mcp");
    core::Result<core::Unit> set_instructions(std::string_view profile_id,
                                              std::string_view instructions);
    core::Result<core::Unit> bind_capability(std::string_view profile_id,
                                             std::string_view capability_id,
                                             std::string_view exposed_name = {});
    core::Result<std::size_t> bind_server_capabilities(std::string_view profile_id,
                                                       std::string_view server_id);
    core::Result<core::Unit> set_binding_enabled(std::string_view profile_id,
                                                 std::string_view capability_id,
                                                 bool enabled);
    core::Result<core::Unit> unbind_capability(std::string_view profile_id,
                                               std::string_view capability_id);
    core::Result<std::size_t> prune_stale_bindings(std::string_view profile_id);
    core::Result<core::Unit> remove_bindings_for_server(std::string_view server_id);

private:
    ExposureProfileStore& profiles_;
    CapabilityCatalog& capabilities_;
};

} // namespace mcp::app
