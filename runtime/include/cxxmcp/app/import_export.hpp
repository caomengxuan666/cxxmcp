#pragma once

#include "cxxmcp/app/profile.hpp"
#include "cxxmcp/app/tool_catalog.hpp"
#include "cxxmcp/core/result.hpp"

#include <filesystem>
#include <vector>

namespace mcp::app {

    struct ExportBundle {
        Profile profile;
        std::vector<ToolDescriptor> tools;
    };

    class ImportExportService {
    public:
        virtual ~ImportExportService() = default;
        virtual core::Result<ExportBundle> import_bundle(const std::filesystem::path &path) = 0;
        virtual core::Result<core::Unit> export_bundle(const ExportBundle &bundle, const std::filesystem::path &path) = 0;
    };

}// namespace mcp::app
