#pragma once

#include "mcp/app/import_export.hpp"
#include "mcp/app/profile.hpp"
#include "mcp/app/tool_management.hpp"
#include "mcp/app/tool_catalog.hpp"
#include "mcp/core/result.hpp"

#include <ostream>
#include <span>
#include <string_view>

namespace mcp::cli {

struct CommandServices {
    app::ToolManagementService& management;
    app::ToolCatalog& tools;
    app::ProfileStore& profiles;
    app::ImportExportService& bundles;
};

class CommandApp {
public:
    explicit CommandApp(CommandServices services);

    core::Result<int> run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err);

private:
    CommandServices services_;
};

} // namespace mcp::cli
