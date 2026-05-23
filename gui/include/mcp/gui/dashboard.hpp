#pragma once

#include "mcp/gui/controller.hpp"

#include <memory>
#include <wx/panel.h>

namespace mcp::gui {

class DashboardPanel final : public wxPanel {
public:
    explicit DashboardPanel(wxWindow* parent, GuiController& controller);
    ~DashboardPanel() override;

    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mcp::gui
