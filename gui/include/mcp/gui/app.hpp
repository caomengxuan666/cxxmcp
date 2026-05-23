#pragma once

#include "mcp/core/result.hpp"

namespace mcp::gui {

struct GuiOptions {
    bool start_minimized = false;
};

core::Result<int> run_gui(const GuiOptions& options);

} // namespace mcp::gui

