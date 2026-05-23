#include "mcp/gui/app.hpp"

#include "mcp/app/services.hpp"
#include "mcp/gui/controller.hpp"
#include "mcp/gui/dashboard.hpp"

#include <wx/artprov.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/listbook.h>
#include <wx/splitter.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/toolbar.h>
#include <wx/wx.h>

#include <algorithm>
#include <initializer_list>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mcp::gui {
namespace {

using mcp::protocol::Json;

constexpr int kToolbarRefresh = wxID_HIGHEST + 1;
constexpr int kToolbarImport = wxID_HIGHEST + 2;
constexpr int kToolbarExport = wxID_HIGHEST + 3;
constexpr int kToolbarEnable = wxID_HIGHEST + 4;
constexpr int kToolbarDisable = wxID_HIGHEST + 5;

wxString to_wx(const std::string& value) {
    return wxString::FromUTF8(value);
}

std::string from_wx(const wxString& value) {
    return value.ToStdString();
}

std::string source_kind_label(app::ToolSourceKind kind) {
    switch (kind) {
    case app::ToolSourceKind::local_manifest:
        return "Local manifest";
    case app::ToolSourceKind::local_plugin:
        return "Local plugin";
    case app::ToolSourceKind::remote_mcp_server:
        return "Remote MCP server";
    case app::ToolSourceKind::generated_adapter:
        return "Generated adapter";
    }
    return "Unknown";
}

std::string source_kind_label(app::ResourceSourceKind kind) {
    switch (kind) {
    case app::ResourceSourceKind::local_manifest:
        return "Local manifest";
    case app::ResourceSourceKind::local_plugin:
        return "Local plugin";
    case app::ResourceSourceKind::remote_mcp_server:
        return "Remote MCP server";
    case app::ResourceSourceKind::generated_adapter:
        return "Generated adapter";
    }
    return "Unknown";
}

std::string source_kind_label(app::PromptSourceKind kind) {
    switch (kind) {
    case app::PromptSourceKind::local_manifest:
        return "Local manifest";
    case app::PromptSourceKind::local_plugin:
        return "Local plugin";
    case app::PromptSourceKind::remote_mcp_server:
        return "Remote MCP server";
    case app::PromptSourceKind::generated_adapter:
        return "Generated adapter";
    }
    return "Unknown";
}

std::string approval_label(app::ApprovalState state) {
    switch (state) {
    case app::ApprovalState::pending:
        return "Pending";
    case app::ApprovalState::approved:
        return "Approved";
    case app::ApprovalState::denied:
        return "Denied";
    }
    return "Pending";
}

int approval_index(app::ApprovalState state) {
    switch (state) {
    case app::ApprovalState::pending:
        return 0;
    case app::ApprovalState::approved:
        return 1;
    case app::ApprovalState::denied:
        return 2;
    }
    return 0;
}

app::ApprovalState approval_from_index(int index) {
    switch (index) {
    case 1:
        return app::ApprovalState::approved;
    case 2:
        return app::ApprovalState::denied;
    default:
        return app::ApprovalState::pending;
    }
}

std::string permission_label(app::Permission permission) {
    switch (permission) {
    case app::Permission::network_access:
        return "Network access";
    case app::Permission::filesystem_read:
        return "Filesystem read";
    case app::Permission::filesystem_write:
        return "Filesystem write";
    case app::Permission::command_execution:
        return "Command execution";
    }
    return "Unknown";
}

std::string permission_description(app::Permission permission) {
    switch (permission) {
    case app::Permission::network_access:
        return "Can reach remote endpoints.";
    case app::Permission::filesystem_read:
        return "Can inspect local files.";
    case app::Permission::filesystem_write:
        return "Can modify local files.";
    case app::Permission::command_execution:
        return "Can launch local commands.";
    }
    return {};
}

std::string source_kind_key(app::ToolSourceKind kind) {
    switch (kind) {
    case app::ToolSourceKind::local_manifest:
        return "local_manifest";
    case app::ToolSourceKind::local_plugin:
        return "local_plugin";
    case app::ToolSourceKind::remote_mcp_server:
        return "remote_mcp_server";
    case app::ToolSourceKind::generated_adapter:
        return "generated_adapter";
    }
    return "unknown";
}

std::string source_kind_key(app::ResourceSourceKind kind) {
    switch (kind) {
    case app::ResourceSourceKind::local_manifest:
        return "local_manifest";
    case app::ResourceSourceKind::local_plugin:
        return "local_plugin";
    case app::ResourceSourceKind::remote_mcp_server:
        return "remote_mcp_server";
    case app::ResourceSourceKind::generated_adapter:
        return "generated_adapter";
    }
    return "unknown";
}

std::string source_kind_key(app::PromptSourceKind kind) {
    switch (kind) {
    case app::PromptSourceKind::local_manifest:
        return "local_manifest";
    case app::PromptSourceKind::local_plugin:
        return "local_plugin";
    case app::PromptSourceKind::remote_mcp_server:
        return "remote_mcp_server";
    case app::PromptSourceKind::generated_adapter:
        return "generated_adapter";
    }
    return "unknown";
}

std::string approval_key(app::ApprovalState state) {
    switch (state) {
    case app::ApprovalState::pending:
        return "pending";
    case app::ApprovalState::approved:
        return "approved";
    case app::ApprovalState::denied:
        return "denied";
    }
    return "pending";
}

std::optional<app::ApprovalState> approval_from_key(std::string_view key) {
    if (key == "approved") {
        return app::ApprovalState::approved;
    }
    if (key == "denied") {
        return app::ApprovalState::denied;
    }
    if (key == "pending") {
        return app::ApprovalState::pending;
    }
    return std::nullopt;
}

std::string permission_key(app::Permission permission) {
    switch (permission) {
    case app::Permission::network_access:
        return "network_access";
    case app::Permission::filesystem_read:
        return "filesystem_read";
    case app::Permission::filesystem_write:
        return "filesystem_write";
    case app::Permission::command_execution:
        return "command_execution";
    }
    return "unknown";
}

std::optional<app::Permission> permission_from_key(std::string_view key) {
    if (key == "network_access") {
        return app::Permission::network_access;
    }
    if (key == "filesystem_read") {
        return app::Permission::filesystem_read;
    }
    if (key == "filesystem_write") {
        return app::Permission::filesystem_write;
    }
    if (key == "command_execution") {
        return app::Permission::command_execution;
    }
    return std::nullopt;
}

Json snapshot_json(const GuiSnapshot& snapshot) {
    Json root = Json::object();
    root["status"] = snapshot.status;
    root["selected_profile_id"] = snapshot.selected_profile_id;
    root["selected_tool_id"] = snapshot.selected_tool_id;
    root["selected_resource_id"] = snapshot.selected_resource_id;
    root["selected_prompt_id"] = snapshot.selected_prompt_id;

    root["profiles"] = Json::array();
    for (const auto& profile : snapshot.profiles) {
        root["profiles"].push_back({
            {"id", profile.id},
            {"name", profile.name},
            {"endpoint_count", profile.endpoint_count},
            {"enabled_tool_count", profile.enabled_tool_count},
        });
    }

    root["tools"] = Json::array();
    for (const auto& tool_row : snapshot.tools) {
        Json permissions = Json::array();
        for (const auto& permission : tool_row.permissions) {
            permissions.push_back(permission_key(permission));
        }
        root["tools"].push_back({
            {"id", tool_row.id},
            {"name", tool_row.name},
            {"description", tool_row.description},
            {"profile_id", tool_row.profile_id},
            {"source_kind", source_kind_key(tool_row.source_kind)},
            {"enabled", tool_row.enabled},
            {"approval", approval_key(tool_row.approval)},
            {"permissions", permissions},
        });
    }

    root["resources"] = Json::array();
    for (const auto& resource : snapshot.resources) {
        root["resources"].push_back({
            {"id", resource.id},
            {"name", resource.name},
            {"description", resource.description},
            {"uri", resource.uri},
            {"source_kind", source_kind_key(resource.source_kind)},
            {"source_location", resource.source_location},
        });
    }

    root["prompts"] = Json::array();
    for (const auto& prompt_row : snapshot.prompts) {
        root["prompts"].push_back({
            {"id", prompt_row.id},
            {"name", prompt_row.name},
            {"description", prompt_row.description},
            {"template_text", prompt_row.template_text},
            {"source_kind", source_kind_key(prompt_row.source_kind)},
            {"source_location", prompt_row.source_location},
        });
    }

    return root;
}

std::string ellipsize(const std::string& text, std::size_t max_length) {
    if (text.size() <= max_length) {
        return text;
    }
    if (max_length <= 3) {
        return text.substr(0, max_length);
    }
    return text.substr(0, max_length - 3) + "...";
}

bool has_permission(const std::vector<app::Permission>& permissions, app::Permission permission) {
    return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
}

app::Policy policy(bool enabled, app::ApprovalState approval, std::initializer_list<app::Permission> permissions) {
    app::Policy result;
    result.enabled = enabled;
    result.approval = approval;
    result.permissions.insert(permissions.begin(), permissions.end());
    return result;
}

app::ToolDescriptor tool(std::string id,
                         std::string name,
                         std::string description,
                         std::string profile_id,
                         app::ToolSourceKind source_kind,
                         std::string source_location,
                         bool enabled,
                         app::ApprovalState approval,
                         std::initializer_list<app::Permission> permissions) {
    return app::ToolDescriptor{
        .id = std::move(id),
        .definition = protocol::ToolDefinition{
            .name = std::move(name),
            .description = std::move(description),
            .input_schema = Json::object(),
            .streaming = false,
        },
        .source = app::ToolSource{
            .kind = source_kind,
            .location = std::move(source_location),
        },
        .policy = policy(enabled, approval, permissions),
        .profile_id = std::move(profile_id),
    };
}

app::ResourceDescriptor resource(std::string id,
                                 std::string name,
                                 std::string description,
                                 std::string uri,
                                 app::ResourceSourceKind source_kind,
                                 std::string source_location) {
    return app::ResourceDescriptor{
        .id = std::move(id),
        .name = std::move(name),
        .description = std::move(description),
        .uri = std::move(uri),
        .source = app::ResourceSource{
            .kind = source_kind,
            .location = std::move(source_location),
        },
    };
}

app::PromptDescriptor prompt(std::string id,
                             std::string name,
                             std::string description,
                             std::string template_text,
                             app::PromptSourceKind source_kind,
                             std::string source_location) {
    return app::PromptDescriptor{
        .id = std::move(id),
        .name = std::move(name),
        .description = std::move(description),
        .template_text = std::move(template_text),
        .source = app::PromptSource{
            .kind = source_kind,
            .location = std::move(source_location),
        },
    };
}

app::Profile default_profile() {
    return app::Profile{
        .id = "default",
        .name = "Default Workspace",
        .endpoints = {
            app::Endpoint{.name = "stdio", .url = "stdio://local"},
        },
        .enabled_tool_ids = {"tool.echo"},
        .environment = {
            {"MCP_RUNTIME", "local"},
        },
    };
}

struct GuiRuntime {
    GuiRuntime()
        : tools({
              tool("tool.echo", "Echo", "Echo arguments back to the caller.", "default",
                   app::ToolSourceKind::local_manifest, "tools/echo.json", true, app::ApprovalState::approved,
                   {app::Permission::filesystem_read}),
              tool("tool.search", "Search", "Query an internal search endpoint.", "default",
                   app::ToolSourceKind::remote_mcp_server, "https://mcp.internal/search", false,
                   app::ApprovalState::pending, {app::Permission::network_access}),
              tool("tool.export", "Export Bundle", "Export the current profile bundle.", "default",
                   app::ToolSourceKind::generated_adapter, "adapter://export-bundle", false,
                   app::ApprovalState::approved,
                   {app::Permission::filesystem_read, app::Permission::filesystem_write}),
          }),
          resources({
              resource("resource.docs", "Documentation", "Workspace and runtime documentation.",
                       "mcp://resources/docs", app::ResourceSourceKind::local_manifest,
                       "resources/docs.json"),
              resource("resource.status", "Status Snapshot", "Live runtime status snapshot.",
                       "mcp://resources/status", app::ResourceSourceKind::generated_adapter,
                       "adapter://runtime-status"),
          }),
          prompts({
              prompt("prompt.summary", "Workspace Summary", "Summarize the current workspace state.",
                     "Summarize the current workspace and active tool selection.", app::PromptSourceKind::local_manifest,
                     "prompts/summary.txt"),
              prompt("prompt.report", "Status Report", "Draft a concise status report.",
                     "Draft a concise report for the current MCP workspace.", app::PromptSourceKind::generated_adapter,
                     "adapter://status-report"),
          }),
          profiles({default_profile()}),
          management(tools, profiles),
          controller(mcp::gui::GuiServices{
              .management = management,
              .tools = tools,
              .resources = resources,
              .prompts = prompts,
              .profiles = profiles,
              .bundles = bundles,
          }) {}

    app::MemoryToolCatalog tools;
    app::MemoryResourceCatalog resources;
    app::MemoryPromptCatalog prompts;
    app::MemoryProfileStore profiles;
    app::ToolManagementService management;
    app::JsonImportExportService bundles;
    GuiController controller;
};

struct LaunchContext {
    GuiOptions options;
    GuiRuntime runtime;
};

LaunchContext* g_launch_context = nullptr;

class ProfilePanel final : public wxPanel {
public:
    explicit ProfilePanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(this, wxID_ANY, "Profiles");
        auto font = title->GetFont();
        font.MakeBold();
        title->SetFont(font);

        auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                              wxSP_LIVE_UPDATE | wxBORDER_NONE);
        splitter->SetMinimumPaneSize(220);

        auto* list_panel = new wxPanel(splitter, wxID_ANY);
        auto* list_sizer = new wxBoxSizer(wxVERTICAL);

        list_ = new wxListCtrl(list_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxLC_REPORT | wxLC_SINGLE_SEL);
        list_->InsertColumn(0, "Name");
        list_->InsertColumn(1, "ID");
        list_->InsertColumn(2, "Endpoints");
        list_->InsertColumn(3, "Enabled tools");
        list_->SetColumnWidth(0, 180);
        list_->SetColumnWidth(1, 140);
        list_->SetColumnWidth(2, 90);
        list_->SetColumnWidth(3, 110);

        list_sizer->Add(list_, 1, wxEXPAND);
        list_panel->SetSizer(list_sizer);

        auto* detail_panel = new wxPanel(splitter, wxID_ANY);
        auto* detail_sizer = new wxBoxSizer(wxVERTICAL);

        detail_title_ = new wxStaticText(detail_panel, wxID_ANY, "No profile selected.");
        auto detail_font = detail_title_->GetFont();
        detail_font.MakeBold();
        detail_title_->SetFont(detail_font);

        detail_id_ = new wxStaticText(detail_panel, wxID_ANY, "");
        detail_summary_ = new wxStaticText(detail_panel, wxID_ANY, "");
        detail_summary_->Wrap(360);

        detail_sizer->Add(detail_title_, 0, wxBOTTOM, 4);
        detail_sizer->Add(detail_id_, 0, wxBOTTOM, 8);
        detail_sizer->Add(detail_summary_, 0, wxEXPAND);
        detail_panel->SetSizer(detail_sizer);

        splitter->SplitVertically(list_panel, detail_panel, 460);

        root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 12);
        root->Add(splitter, 1, wxEXPAND | wxALL, 12);
        SetSizer(root);

        list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
            if (updating_ || !on_selected_) {
                return;
            }
            const auto row = static_cast<std::size_t>(event.GetIndex());
            if (row >= profile_ids_.size()) {
                return;
            }
            on_selected_(profile_ids_[row]);
        });

        list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
            if (updating_ || !on_selected_) {
                return;
            }
            const auto row = static_cast<std::size_t>(event.GetIndex());
            if (row >= profile_ids_.size()) {
                return;
            }
            on_selected_(profile_ids_[row]);
        });
    }

    void set_on_selected(std::function<void(std::string)> on_selected) {
        on_selected_ = std::move(on_selected);
    }

    void set_snapshot(const GuiSnapshot& snapshot) {
        updating_ = true;
        list_->DeleteAllItems();
        profile_ids_.clear();

        for (const auto& profile : snapshot.profiles) {
            const auto row = list_->InsertItem(list_->GetItemCount(), to_wx(profile.name));
            list_->SetItem(row, 1, to_wx(profile.id));
            list_->SetItem(row, 2, to_wx(std::to_string(profile.endpoint_count)));
            list_->SetItem(row, 3, to_wx(std::to_string(profile.enabled_tool_count)));
            profile_ids_.push_back(profile.id);
        }

        if (profile_ids_.empty()) {
            list_->Disable();
            detail_title_->SetLabel("No profile selected.");
            detail_id_->SetLabel("");
            detail_summary_->SetLabel("No profiles configured.");
        } else {
            list_->Enable();
            auto selected_index = 0;
            const auto it = std::find(profile_ids_.begin(), profile_ids_.end(), snapshot.selected_profile_id);
            if (it != profile_ids_.end()) {
                selected_index = static_cast<int>(std::distance(profile_ids_.begin(), it));
            }
            list_->SetItemState(selected_index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            list_->EnsureVisible(selected_index);
            update_detail(snapshot.profiles[static_cast<std::size_t>(selected_index)]);
        }

        Layout();
        updating_ = false;
    }

private:
    void update_detail(const ProfileRow& row) {
        detail_title_->SetLabel(to_wx(row.name));
        detail_id_->SetLabel(to_wx("ID: " + row.id));
        detail_summary_->SetLabel(to_wx("Endpoints: " + std::to_string(row.endpoint_count) +
                                         "\nEnabled tools: " + std::to_string(row.enabled_tool_count)));
        detail_summary_->Wrap(360);
    }

    wxListCtrl* list_ = nullptr;
    wxStaticText* detail_title_ = nullptr;
    wxStaticText* detail_id_ = nullptr;
    wxStaticText* detail_summary_ = nullptr;
    std::vector<std::string> profile_ids_;
    std::function<void(std::string)> on_selected_;
    bool updating_ = false;
};

class ToolCatalogPanel final : public wxPanel {
public:
    explicit ToolCatalogPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(this, wxID_ANY, "Tools");
        auto font = title->GetFont();
        font.MakeBold();
        title->SetFont(font);

        list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(620, 420), wxLC_REPORT | wxLC_SINGLE_SEL);
        list_->InsertColumn(0, "Name");
        list_->InsertColumn(1, "Source");
        list_->InsertColumn(2, "Profile");
        list_->InsertColumn(3, "Approval");
        list_->InsertColumn(4, "Enabled");
        list_->SetColumnWidth(0, 180);
        list_->SetColumnWidth(1, 160);
        list_->SetColumnWidth(2, 120);
        list_->SetColumnWidth(3, 90);
        list_->SetColumnWidth(4, 80);

        summary_ = new wxStaticText(this, wxID_ANY, "No tools loaded.");
        summary_->Wrap(620);

        root->Add(title, 0, wxBOTTOM, 6);
        root->Add(list_, 1, wxEXPAND | wxBOTTOM, 6);
        root->Add(summary_, 0, wxEXPAND);
        SetSizer(root);

        list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
            if (updating_ || !on_selected_) {
                return;
            }
            const auto row = static_cast<std::size_t>(event.GetIndex());
            if (row >= tool_ids_.size()) {
                return;
            }
            on_selected_(tool_ids_[row]);
        });

        list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
            if (updating_ || !on_selected_) {
                return;
            }
            const auto row = static_cast<std::size_t>(event.GetIndex());
            if (row >= tool_ids_.size()) {
                return;
            }
            on_selected_(tool_ids_[row]);
        });
    }

    void set_on_selected(std::function<void(std::string)> on_selected) {
        on_selected_ = std::move(on_selected);
    }

    void set_snapshot(const GuiSnapshot& snapshot) {
        updating_ = true;
        list_->DeleteAllItems();
        tool_ids_.clear();

        for (const auto& tool : snapshot.tools) {
            const auto row = list_->InsertItem(list_->GetItemCount(), to_wx(tool.name));
            list_->SetItem(row, 1, to_wx(source_kind_label(tool.source_kind)));
            list_->SetItem(row, 2, to_wx(tool.profile_id));
            list_->SetItem(row, 3, to_wx(approval_label(tool.approval)));
            list_->SetItem(row, 4, tool.enabled ? "Yes" : "No");
            tool_ids_.push_back(tool.id);
        }

        if (tool_ids_.empty()) {
            summary_->SetLabel("No tools configured.");
            list_->Disable();
        } else {
            list_->Enable();
            auto selected_index = 0;
            const auto it = std::find(tool_ids_.begin(), tool_ids_.end(), snapshot.selected_tool_id);
            if (it != tool_ids_.end()) {
                selected_index = static_cast<int>(std::distance(tool_ids_.begin(), it));
            }
            list_->SetItemState(selected_index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            list_->EnsureVisible(selected_index);

            const auto& row = snapshot.tools[static_cast<std::size_t>(selected_index)];
            summary_->SetLabel(to_wx("Selected: " + row.name + "  Approval: " + approval_label(row.approval) +
                                     "  Permissions: " + std::to_string(row.permissions.size())));
        }

        summary_->Wrap(420);
        Layout();
        updating_ = false;
    }

    std::string selected_tool_id() const {
        const auto row = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (row < 0) {
            return {};
        }
        const auto index = static_cast<std::size_t>(row);
        if (index >= tool_ids_.size()) {
            return {};
        }
        return tool_ids_[index];
    }

private:
    wxListCtrl* list_ = nullptr;
    wxStaticText* summary_ = nullptr;
    std::vector<std::string> tool_ids_;
    std::function<void(std::string)> on_selected_;
    bool updating_ = false;
};

class PolicyPanel final : public wxPanel {
public:
    explicit PolicyPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(this, wxID_ANY, "Policy");
        auto font = title->GetFont();
        font.MakeBold();
        title->SetFont(font);

        tool_name_ = new wxStaticText(this, wxID_ANY, "No tool selected.");
        tool_summary_ = new wxStaticText(this, wxID_ANY, "Select a tool to inspect its policy.");
        tool_summary_->Wrap(300);

        approval_ = new wxChoice(this, wxID_ANY);
        approval_->Append("Pending");
        approval_->Append("Approved");
        approval_->Append("Denied");

        network_ = new wxCheckBox(this, wxID_ANY, to_wx(permission_label(app::Permission::network_access)));
        filesystem_read_ = new wxCheckBox(this, wxID_ANY, to_wx(permission_label(app::Permission::filesystem_read)));
        filesystem_write_ = new wxCheckBox(this, wxID_ANY, to_wx(permission_label(app::Permission::filesystem_write)));
        command_execution_ =
            new wxCheckBox(this, wxID_ANY, to_wx(permission_label(app::Permission::command_execution)));

        apply_ = new wxButton(this, wxID_ANY, "Apply Policy");

        root->Add(title, 0, wxBOTTOM, 6);
        root->Add(tool_name_, 0, wxBOTTOM, 4);
        root->Add(tool_summary_, 0, wxBOTTOM, 10);
        root->Add(new wxStaticText(this, wxID_ANY, "Approval"), 0, wxBOTTOM, 2);
        root->Add(approval_, 0, wxEXPAND | wxBOTTOM, 10);
        root->Add(new wxStaticText(this, wxID_ANY, "Permissions"), 0, wxBOTTOM, 4);
        root->Add(network_, 0, wxBOTTOM, 2);
        root->Add(filesystem_read_, 0, wxBOTTOM, 2);
        root->Add(filesystem_write_, 0, wxBOTTOM, 2);
        root->Add(command_execution_, 0, wxBOTTOM, 10);
        root->Add(apply_, 0, wxALIGN_LEFT);
        SetSizer(root);

        apply_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (updating_ || !current_tool_ || !on_apply_) {
                return;
            }
            on_apply_(current_tool_->id, build_policy());
        });
    }

    void set_on_apply(std::function<void(std::string, app::Policy)> on_apply) {
        on_apply_ = std::move(on_apply);
    }

    void set_snapshot(const GuiSnapshot& snapshot) {
        updating_ = true;
        current_tool_.reset();

        const auto it = std::find_if(snapshot.tools.begin(), snapshot.tools.end(), [&](const auto& tool) {
            return tool.id == snapshot.selected_tool_id;
        });
        if (it == snapshot.tools.end()) {
            tool_name_->SetLabel("No tool selected.");
            tool_summary_->SetLabel("Select a tool to inspect its policy.");
            approval_->SetSelection(0);
            network_->SetValue(false);
            filesystem_read_->SetValue(false);
            filesystem_write_->SetValue(false);
            command_execution_->SetValue(false);
            enable_controls(false);
        } else {
            current_tool_ = *it;
            tool_name_->SetLabel(to_wx(it->name + " [" + it->id + "]"));
            tool_summary_->SetLabel(to_wx(it->description + "\nSource: " + source_kind_label(it->source_kind) +
                                           "\nProfile: " + it->profile_id +
                                           "\nEnabled: " + (it->enabled ? "yes" : "no")));
            approval_->SetSelection(approval_index(it->approval));
            network_->SetValue(has_permission(it->permissions, app::Permission::network_access));
            filesystem_read_->SetValue(has_permission(it->permissions, app::Permission::filesystem_read));
            filesystem_write_->SetValue(has_permission(it->permissions, app::Permission::filesystem_write));
            command_execution_->SetValue(has_permission(it->permissions, app::Permission::command_execution));
            enable_controls(true);
        }

        tool_summary_->Wrap(300);
        Layout();
        updating_ = false;
    }

private:
    app::Policy build_policy() const {
        app::Policy policy;
        policy.approval = approval_from_index(approval_->GetSelection());
        if (network_->GetValue()) {
            policy.permissions.insert(app::Permission::network_access);
        }
        if (filesystem_read_->GetValue()) {
            policy.permissions.insert(app::Permission::filesystem_read);
        }
        if (filesystem_write_->GetValue()) {
            policy.permissions.insert(app::Permission::filesystem_write);
        }
        if (command_execution_->GetValue()) {
            policy.permissions.insert(app::Permission::command_execution);
        }
        if (current_tool_) {
            policy.enabled = current_tool_->enabled;
        }
        return policy;
    }

    void enable_controls(bool enabled) {
        approval_->Enable(enabled);
        network_->Enable(enabled);
        filesystem_read_->Enable(enabled);
        filesystem_write_->Enable(enabled);
        command_execution_->Enable(enabled);
        apply_->Enable(enabled);
    }

    wxStaticText* tool_name_ = nullptr;
    wxStaticText* tool_summary_ = nullptr;
    wxChoice* approval_ = nullptr;
    wxCheckBox* network_ = nullptr;
    wxCheckBox* filesystem_read_ = nullptr;
    wxCheckBox* filesystem_write_ = nullptr;
    wxCheckBox* command_execution_ = nullptr;
    wxButton* apply_ = nullptr;
    std::optional<ToolRow> current_tool_;
    std::function<void(std::string, app::Policy)> on_apply_;
    bool updating_ = false;
};

class ToolWorkspacePanel final : public wxPanel {
public:
    explicit ToolWorkspacePanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                              wxSP_LIVE_UPDATE | wxBORDER_NONE);
        splitter->SetMinimumPaneSize(260);

        tool_panel_ = new ToolCatalogPanel(splitter);
        policy_panel_ = new PolicyPanel(splitter);
        splitter->SplitVertically(tool_panel_, policy_panel_, 520);

        root->Add(splitter, 1, wxEXPAND | wxALL, 12);
        SetSizer(root);
    }

    void set_on_selected(std::function<void(std::string)> on_selected) {
        tool_panel_->set_on_selected(std::move(on_selected));
    }

    void set_on_apply(std::function<void(std::string, app::Policy)> on_apply) {
        policy_panel_->set_on_apply(std::move(on_apply));
    }

    void set_snapshot(const GuiSnapshot& snapshot) {
        tool_panel_->set_snapshot(snapshot);
        policy_panel_->set_snapshot(snapshot);
    }

    std::string selected_tool_id() const {
        return tool_panel_->selected_tool_id();
    }

private:
    ToolCatalogPanel* tool_panel_ = nullptr;
    PolicyPanel* policy_panel_ = nullptr;
};

class ResourcePanel final : public wxPanel {
public:
    explicit ResourcePanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(this, wxID_ANY, "Resources");
        auto font = title->GetFont();
        font.MakeBold();
        title->SetFont(font);

        auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                              wxSP_LIVE_UPDATE | wxBORDER_NONE);
        splitter->SetMinimumPaneSize(220);

        auto* list_panel = new wxPanel(splitter, wxID_ANY);
        auto* list_sizer = new wxBoxSizer(wxVERTICAL);
        list_ = new wxListCtrl(list_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxLC_REPORT | wxLC_SINGLE_SEL);
        list_->InsertColumn(0, "Name");
        list_->InsertColumn(1, "URI");
        list_->InsertColumn(2, "Source");
        list_->SetColumnWidth(0, 180);
        list_->SetColumnWidth(1, 220);
        list_->SetColumnWidth(2, 160);

        list_sizer->Add(list_, 1, wxEXPAND);
        list_panel->SetSizer(list_sizer);

        auto* detail_panel = new wxPanel(splitter, wxID_ANY);
        auto* detail_sizer = new wxBoxSizer(wxVERTICAL);
        detail_title_ = new wxStaticText(detail_panel, wxID_ANY, "No resource selected.");
        auto detail_font = detail_title_->GetFont();
        detail_font.MakeBold();
        detail_title_->SetFont(detail_font);
        detail_meta_ = new wxStaticText(detail_panel, wxID_ANY, "");
        detail_body_ = new wxStaticText(detail_panel, wxID_ANY, "No resources configured.");
        detail_body_->Wrap(360);

        detail_sizer->Add(detail_title_, 0, wxBOTTOM, 4);
        detail_sizer->Add(detail_meta_, 0, wxBOTTOM, 8);
        detail_sizer->Add(detail_body_, 0, wxEXPAND);
        detail_panel->SetSizer(detail_sizer);

        splitter->SplitVertically(list_panel, detail_panel, 460);

        root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 12);
        root->Add(splitter, 1, wxEXPAND | wxALL, 12);
        SetSizer(root);

        list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
            if (updating_ || !on_selected_) {
                return;
            }
            const auto row = static_cast<std::size_t>(event.GetIndex());
            if (row >= resource_ids_.size()) {
                return;
            }
            on_selected_(resource_ids_[row]);
        });

        list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
            if (updating_ || !on_selected_) {
                return;
            }
            const auto row = static_cast<std::size_t>(event.GetIndex());
            if (row >= resource_ids_.size()) {
                return;
            }
            on_selected_(resource_ids_[row]);
        });
    }

    void set_on_selected(std::function<void(std::string)> on_selected) {
        on_selected_ = std::move(on_selected);
    }

    void set_snapshot(const GuiSnapshot& snapshot) {
        updating_ = true;
        list_->DeleteAllItems();
        resource_ids_.clear();

        for (const auto& resource : snapshot.resources) {
            const auto row = list_->InsertItem(list_->GetItemCount(), to_wx(resource.name));
            list_->SetItem(row, 1, to_wx(resource.uri));
            list_->SetItem(row, 2, to_wx(source_kind_label(resource.source_kind)));
            resource_ids_.push_back(resource.id);
        }

        if (resource_ids_.empty()) {
            list_->Disable();
            detail_title_->SetLabel("No resource selected.");
            detail_meta_->SetLabel("");
            detail_body_->SetLabel("No resources configured.");
        } else {
            list_->Enable();
            auto selected_index = 0;
            const auto it = std::find(resource_ids_.begin(), resource_ids_.end(), snapshot.selected_resource_id);
            if (it != resource_ids_.end()) {
                selected_index = static_cast<int>(std::distance(resource_ids_.begin(), it));
            }
            list_->SetItemState(selected_index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            list_->EnsureVisible(selected_index);
            update_detail(snapshot.resources[static_cast<std::size_t>(selected_index)]);
        }

        Layout();
        updating_ = false;
    }

    std::string selected_resource_id() const {
        const auto row = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (row < 0) {
            return {};
        }
        const auto index = static_cast<std::size_t>(row);
        if (index >= resource_ids_.size()) {
            return {};
        }
        return resource_ids_[index];
    }

private:
    void update_detail(const ResourceRow& row) {
        detail_title_->SetLabel(to_wx(row.name));
        detail_meta_->SetLabel(to_wx("ID: " + row.id + "    Source: " + source_kind_label(row.source_kind)));
        detail_body_->SetLabel(to_wx(row.description + "\nURI: " + row.uri + "\nLocation: " + row.source_location));
        detail_body_->Wrap(360);
    }

    wxListCtrl* list_ = nullptr;
    wxStaticText* detail_title_ = nullptr;
    wxStaticText* detail_meta_ = nullptr;
    wxStaticText* detail_body_ = nullptr;
    std::vector<std::string> resource_ids_;
    std::function<void(std::string)> on_selected_;
    bool updating_ = false;
};

class PromptPanel final : public wxPanel {
public:
    explicit PromptPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(this, wxID_ANY, "Prompts");
        auto font = title->GetFont();
        font.MakeBold();
        title->SetFont(font);

        auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                              wxSP_LIVE_UPDATE | wxBORDER_NONE);
        splitter->SetMinimumPaneSize(220);

        auto* list_panel = new wxPanel(splitter, wxID_ANY);
        auto* list_sizer = new wxBoxSizer(wxVERTICAL);
        list_ = new wxListCtrl(list_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxLC_REPORT | wxLC_SINGLE_SEL);
        list_->InsertColumn(0, "Name");
        list_->InsertColumn(1, "Source");
        list_->InsertColumn(2, "Template");
        list_->SetColumnWidth(0, 180);
        list_->SetColumnWidth(1, 160);
        list_->SetColumnWidth(2, 250);

        list_sizer->Add(list_, 1, wxEXPAND);
        list_panel->SetSizer(list_sizer);

        auto* detail_panel = new wxPanel(splitter, wxID_ANY);
        auto* detail_sizer = new wxBoxSizer(wxVERTICAL);
        detail_title_ = new wxStaticText(detail_panel, wxID_ANY, "No prompt selected.");
        auto detail_font = detail_title_->GetFont();
        detail_font.MakeBold();
        detail_title_->SetFont(detail_font);
        detail_meta_ = new wxStaticText(detail_panel, wxID_ANY, "");
        detail_body_ = new wxStaticText(detail_panel, wxID_ANY, "No prompts configured.");
        detail_body_->Wrap(360);

        detail_sizer->Add(detail_title_, 0, wxBOTTOM, 4);
        detail_sizer->Add(detail_meta_, 0, wxBOTTOM, 8);
        detail_sizer->Add(detail_body_, 0, wxEXPAND);
        detail_panel->SetSizer(detail_sizer);

        splitter->SplitVertically(list_panel, detail_panel, 460);

        root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 12);
        root->Add(splitter, 1, wxEXPAND | wxALL, 12);
        SetSizer(root);

        list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
            if (updating_ || !on_selected_) {
                return;
            }
            const auto row = static_cast<std::size_t>(event.GetIndex());
            if (row >= prompt_ids_.size()) {
                return;
            }
            on_selected_(prompt_ids_[row]);
        });

        list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
            if (updating_ || !on_selected_) {
                return;
            }
            const auto row = static_cast<std::size_t>(event.GetIndex());
            if (row >= prompt_ids_.size()) {
                return;
            }
            on_selected_(prompt_ids_[row]);
        });
    }

    void set_on_selected(std::function<void(std::string)> on_selected) {
        on_selected_ = std::move(on_selected);
    }

    void set_snapshot(const GuiSnapshot& snapshot) {
        updating_ = true;
        list_->DeleteAllItems();
        prompt_ids_.clear();

        for (const auto& prompt : snapshot.prompts) {
            const auto row = list_->InsertItem(list_->GetItemCount(), to_wx(prompt.name));
            list_->SetItem(row, 1, to_wx(source_kind_label(prompt.source_kind)));
            list_->SetItem(row, 2, to_wx(ellipsize(prompt.template_text, 48)));
            prompt_ids_.push_back(prompt.id);
        }

        if (prompt_ids_.empty()) {
            list_->Disable();
            detail_title_->SetLabel("No prompt selected.");
            detail_meta_->SetLabel("");
            detail_body_->SetLabel("No prompts configured.");
        } else {
            list_->Enable();
            auto selected_index = 0;
            const auto it = std::find(prompt_ids_.begin(), prompt_ids_.end(), snapshot.selected_prompt_id);
            if (it != prompt_ids_.end()) {
                selected_index = static_cast<int>(std::distance(prompt_ids_.begin(), it));
            }
            list_->SetItemState(selected_index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            list_->EnsureVisible(selected_index);
            update_detail(snapshot.prompts[static_cast<std::size_t>(selected_index)]);
        }

        Layout();
        updating_ = false;
    }

    std::string selected_prompt_id() const {
        const auto row = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (row < 0) {
            return {};
        }
        const auto index = static_cast<std::size_t>(row);
        if (index >= prompt_ids_.size()) {
            return {};
        }
        return prompt_ids_[index];
    }

private:
    void update_detail(const PromptRow& row) {
        detail_title_->SetLabel(to_wx(row.name));
        detail_meta_->SetLabel(to_wx("ID: " + row.id + "    Source: " + source_kind_label(row.source_kind)));
        detail_body_->SetLabel(to_wx(row.description + "\n\nTemplate:\n" + ellipsize(row.template_text, 240) +
                                     "\n\nLocation: " + row.source_location));
        detail_body_->Wrap(360);
    }

    wxListCtrl* list_ = nullptr;
    wxStaticText* detail_title_ = nullptr;
    wxStaticText* detail_meta_ = nullptr;
    wxStaticText* detail_body_ = nullptr;
    std::vector<std::string> prompt_ids_;
    std::function<void(std::string)> on_selected_;
    bool updating_ = false;
};

class MainFrame final : public wxFrame {
public:
    explicit MainFrame(LaunchContext& context)
        : wxFrame(nullptr, wxID_ANY, "MCP Server Console", wxDefaultPosition, wxSize(1280, 820)),
          context_(context),
          dashboard_(new DashboardPanel(this, context_.runtime.controller)) {
        auto* root_sizer = new wxBoxSizer(wxVERTICAL);
        root_sizer->Add(dashboard_, 1, wxEXPAND);
        SetSizer(root_sizer);
        SetMinSize(wxSize(1024, 680));
    }

private:
    LaunchContext& context_;
    DashboardPanel* dashboard_ = nullptr;
};

class WxGuiApp final : public wxApp {
public:
    bool OnInit() override {
        if (g_launch_context == nullptr) {
            return false;
        }

        auto* frame = new MainFrame(*g_launch_context);
        SetTopWindow(frame);
        frame->Show(true);
        if (g_launch_context->options.start_minimized) {
            frame->Iconize(true);
        }
        return true;
    }
};

wxIMPLEMENT_APP_NO_MAIN(WxGuiApp);

} // namespace

core::Result<int> run_gui(const GuiOptions& options) {
    LaunchContext context{
        .options = options,
        .runtime = GuiRuntime{},
    };

    g_launch_context = &context;

    int argc = 1;
    char arg0[] = "mcp-gui";
    char* argv[] = {arg0, nullptr};
    if (!wxEntryStart(argc, argv)) {
        g_launch_context = nullptr;
        return std::unexpected(core::Error{
            1,
            "failed to initialize wxWidgets",
            "wxEntryStart returned false.",
        });
    }

    struct WxCleanup {
        ~WxCleanup() {
            wxEntryCleanup();
            g_launch_context = nullptr;
        }
    } cleanup;

    if (!wxTheApp->CallOnInit()) {
        return std::unexpected(core::Error{
            1,
            "failed to initialize GUI",
            "wxApp::OnInit returned false.",
        });
    }

    const auto exit_code = wxTheApp->OnRun();
    wxTheApp->OnExit();
    return exit_code;
}

} // namespace mcp::gui
