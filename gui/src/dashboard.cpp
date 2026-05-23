#include "mcp/gui/dashboard.hpp"

#include "mcp/app/policy.hpp"
#include "mcp/gui/model.hpp"

#include <wx/artprov.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/webview.h>
#include <wx/wx.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mcp::gui {
namespace {

using mcp::protocol::Json;

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
    for (const auto& tool : snapshot.tools) {
        Json permissions = Json::array();
        for (const auto& permission : tool.permissions) {
            permissions.push_back(permission_key(permission));
        }
        root["tools"].push_back({
            {"id", tool.id},
            {"name", tool.name},
            {"description", tool.description},
            {"profile_id", tool.profile_id},
            {"source_kind", source_kind_label(tool.source_kind)},
            {"enabled", tool.enabled},
            {"approval", approval_key(tool.approval)},
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
            {"source_kind", source_kind_label(resource.source_kind)},
            {"source_location", resource.source_location},
        });
    }

    root["prompts"] = Json::array();
    for (const auto& prompt : snapshot.prompts) {
        root["prompts"].push_back({
            {"id", prompt.id},
            {"name", prompt.name},
            {"description", prompt.description},
            {"template_text", prompt.template_text},
            {"source_kind", source_kind_label(prompt.source_kind)},
            {"source_location", prompt.source_location},
        });
    }

    return root;
}

const char kDashboardHtml[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>MCP Server Console</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #0b0f14;
      --sidebar: #0d121a;
      --panel: #121824;
      --panel-2: #0f1520;
      --line: #232d3b;
      --line-soft: #1b2230;
      --text: #e7ebf2;
      --muted: #8c97aa;
      --accent: #7f8cff;
      --accent-2: #52d6ad;
      --danger: #ff7272;
      --warning: #f0b84b;
      --shadow: 0 18px 40px rgba(0, 0, 0, 0.28);
    }
    * { box-sizing: border-box; }
    html, body { margin: 0; width: 100%; height: 100%; overflow: hidden; background: var(--bg); color: var(--text); font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif; }
    button, select, textarea { font: inherit; }
    .app { display: grid; grid-template-columns: 270px minmax(0, 1fr); width: 100%; height: 100%; }
    .sidebar {
      background: linear-gradient(180deg, #0d1118 0%, #0b0f14 100%);
      border-right: 1px solid var(--line);
      padding: 18px 14px;
      display: flex;
      flex-direction: column;
      gap: 16px;
    }
    .brand { padding: 6px 8px 12px; border-bottom: 1px solid var(--line-soft); }
    .brand .eyebrow { font-size: 11px; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); margin-bottom: 6px; }
    .brand .title { font-size: 18px; font-weight: 700; line-height: 1.2; }
    .brand .subtitle { color: var(--muted); font-size: 12px; margin-top: 4px; }
    .nav { display: flex; flex-direction: column; gap: 8px; padding-top: 2px; }
    .nav button {
      appearance: none;
      border: 1px solid transparent;
      background: transparent;
      color: var(--muted);
      border-radius: 12px;
      padding: 11px 12px;
      display: flex;
      align-items: center;
      gap: 10px;
      text-align: left;
      cursor: pointer;
      transition: .15s ease;
    }
    .nav button:hover { background: rgba(255,255,255,.04); color: var(--text); }
    .nav button.active { background: rgba(127,140,255,.14); color: var(--text); border-color: rgba(127,140,255,.28); }
    .nav .meta { display: block; font-size: 11px; color: var(--muted); margin-top: 2px; }
    .nav .icon { width: 16px; height: 16px; stroke: currentColor; fill: none; stroke-width: 2; stroke-linecap: round; stroke-linejoin: round; flex: 0 0 auto; }
    .sidebar-footer { margin-top: auto; color: var(--muted); font-size: 12px; line-height: 1.4; border-top: 1px solid var(--line-soft); padding-top: 12px; white-space: pre-line; }
    .content { min-width: 0; display: flex; flex-direction: column; gap: 14px; padding: 18px; }
    .topbar { display: flex; align-items: flex-start; justify-content: space-between; gap: 16px; }
    .topbar h1 { margin: 0; font-size: 24px; line-height: 1.2; }
    .topbar .meta { margin-top: 6px; color: var(--muted); font-size: 13px; }
    .actions { display: flex; flex-wrap: wrap; gap: 8px; justify-content: flex-end; }
    .action {
      appearance: none;
      border: 1px solid var(--line);
      background: linear-gradient(180deg, #161c28, #111722);
      color: var(--text);
      border-radius: 12px;
      padding: 9px 12px;
      display: inline-flex;
      align-items: center;
      gap: 8px;
      cursor: pointer;
      transition: .15s ease;
    }
    .action:hover { border-color: #41506a; }
    .action:disabled { opacity: .45; cursor: default; }
    .action .icon { width: 16px; height: 16px; stroke: currentColor; fill: none; stroke-width: 2; stroke-linecap: round; stroke-linejoin: round; }
    .workspace { flex: 1; min-height: 0; display: grid; grid-template-columns: minmax(340px, 1.05fr) minmax(360px, .95fr); gap: 14px; }
    .panel {
      min-width: 0;
      min-height: 0;
      display: flex;
      flex-direction: column;
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 16px;
      box-shadow: var(--shadow);
      overflow: hidden;
    }
    .panel-header {
      padding: 14px 16px;
      border-bottom: 1px solid var(--line-soft);
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 10px;
      background: rgba(255,255,255,.02);
    }
    .panel-header .title { font-size: 14px; font-weight: 700; }
    .panel-header .count { color: var(--muted); font-size: 12px; }
    .list { min-height: 0; overflow: auto; padding: 10px; display: flex; flex-direction: column; gap: 8px; }
    .row {
      width: 100%;
      appearance: none;
      border: 1px solid transparent;
      background: var(--panel-2);
      color: var(--text);
      border-radius: 14px;
      padding: 12px;
      cursor: pointer;
      text-align: left;
      display: flex;
      flex-direction: column;
      gap: 7px;
      transition: .15s ease;
    }
    .row:hover { background: #141b28; border-color: #2f394b; }
    .row.active { border-color: rgba(127,140,255,.38); background: rgba(127,140,255,.12); }
    .row .primary { display: flex; align-items: center; justify-content: space-between; gap: 8px; font-size: 13px; font-weight: 650; }
    .row .secondary { color: var(--muted); font-size: 12px; line-height: 1.45; white-space: pre-line; }
    .badge {
      display: inline-flex;
      align-items: center;
      border-radius: 999px;
      padding: 3px 8px;
      border: 1px solid #30394b;
      background: #1a2030;
      color: #cbd5e1;
      font-size: 11px;
      white-space: nowrap;
    }
    .badge.good { color: #8cf0cd; border-color: rgba(82,214,173,.24); background: rgba(82,214,173,.12); }
    .badge.warn { color: #ffd793; border-color: rgba(240,184,75,.24); background: rgba(240,184,75,.12); }
    .badge.bad { color: #ffb0b0; border-color: rgba(255,114,114,.24); background: rgba(255,114,114,.12); }
    .detail { min-height: 0; overflow: auto; padding: 16px; display: flex; flex-direction: column; gap: 14px; }
    .hero { display: flex; flex-direction: column; gap: 6px; padding-bottom: 2px; border-bottom: 1px solid var(--line-soft); }
    .hero .name { font-size: 20px; font-weight: 750; line-height: 1.2; }
    .hero .id { font-size: 12px; color: var(--muted); }
    .hero .desc { font-size: 13px; line-height: 1.55; color: #d8ddea; white-space: pre-line; }
    .grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 10px; }
    .stat { background: var(--panel-2); border: 1px solid var(--line-soft); border-radius: 14px; padding: 12px; }
    .stat .label { color: var(--muted); font-size: 11px; text-transform: uppercase; letter-spacing: .04em; margin-bottom: 6px; }
    .stat .value { font-size: 14px; font-weight: 600; line-height: 1.45; white-space: pre-line; }
    .section { display: flex; flex-direction: column; gap: 8px; }
    .section h2 { margin: 0; font-size: 13px; color: var(--muted); text-transform: uppercase; letter-spacing: .05em; }
    .field { display: flex; flex-direction: column; gap: 6px; }
    .field label { color: var(--muted); font-size: 12px; }
    .field input, .field select, .field textarea {
      background: #0d121b;
      color: var(--text);
      border: 1px solid #2b3546;
      border-radius: 10px;
      padding: 10px 12px;
      outline: none;
    }
    .field textarea { min-height: 132px; resize: vertical; line-height: 1.5; }
    .checks { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px 12px; }
    .check { display: flex; align-items: center; gap: 8px; color: var(--text); font-size: 13px; }
    .empty { padding: 26px 14px; border: 1px dashed #2e3748; border-radius: 14px; color: var(--muted); text-align: center; background: rgba(255,255,255,.02); }
    .status { font-size: 12px; color: var(--muted); min-height: 18px; padding-left: 2px; }
    @media (max-width: 1120px) {
      .app { grid-template-columns: 220px minmax(0, 1fr); }
      .workspace { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="app">
    <aside class="sidebar">
      <div class="brand">
        <div class="eyebrow">MCP runtime</div>
        <div class="title">Server Console</div>
        <div class="subtitle" id="sidebarMeta">Local workspace</div>
      </div>
      <nav class="nav" id="nav"></nav>
      <div class="sidebar-footer" id="sidebarFooter">Ready</div>
    </aside>
    <main class="content">
      <div class="topbar">
        <div>
          <h1 id="pageTitle">Tools</h1>
          <div class="meta" id="pageMeta">Tool catalog and policy controls.</div>
        </div>
        <div class="actions" id="actions"></div>
      </div>
      <div class="workspace">
        <section class="panel">
          <div class="panel-header">
            <div class="title" id="listTitle">Tool catalog</div>
            <div class="count" id="listCount">0 items</div>
          </div>
          <div class="list" id="list"></div>
        </section>
        <section class="panel">
          <div class="panel-header">
            <div class="title" id="detailTitle">Selection</div>
            <div class="count" id="detailCount"></div>
          </div>
          <div class="detail" id="detail"></div>
        </section>
      </div>
      <div class="status" id="statusBar"></div>
    </main>
  </div>
  <svg aria-hidden="true" style="display:none">
    <symbol id="i-refresh" viewBox="0 0 24 24"><path d="M20 12a8 8 0 0 1-13.66 5.66"/><path d="M4 12a8 8 0 0 1 13.66-5.66"/><path d="M20 4v6h-6"/><path d="M4 20v-6h6"/></symbol>
    <symbol id="i-upload" viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m7 10 5-5 5 5"/><path d="M12 5v10"/></symbol>
    <symbol id="i-download" viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m7 14 5 5 5-5"/><path d="M12 19V9"/></symbol>
    <symbol id="i-play" viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></symbol>
    <symbol id="i-pause" viewBox="0 0 24 24"><path d="M6 5h4v14H6z"/><path d="M14 5h4v14h-4z"/></symbol>
    <symbol id="i-check" viewBox="0 0 24 24"><path d="m20 6-11 11-5-5"/></symbol>
    <symbol id="i-x" viewBox="0 0 24 24"><path d="M18 6 6 18"/><path d="M6 6l12 12"/></symbol>
  </svg>
  <script>
    const state = {
      profiles: [],
      tools: [],
      resources: [],
      prompts: [],
      selected_profile_id: "",
      selected_tool_id: "",
      selected_resource_id: "",
      selected_prompt_id: "",
      status: "",
      activeSection: "tools"
    };

    const sections = {
      profiles: { title: "Profiles", meta: "Workspace endpoints and tool bindings." },
      tools: { title: "Tools", meta: "Tool catalog and policy controls." },
      resources: { title: "Resources", meta: "Published resources and sources." },
      prompts: { title: "Prompts", meta: "Prompt templates and generators." }
    };

    const selectAction = {
      profiles: "select_profile",
      tools: "select_tool",
      resources: "select_resource",
      prompts: "select_prompt"
    };

    function send(action, payload = {}) {
      if (!window.mcp) {
        return;
      }
      window.mcp.postMessage(JSON.stringify({ action, ...payload }));
    }

    function icon(id) {
      return `<svg class="icon" aria-hidden="true"><use href="#${id}"></use></svg>`;
    }

    function esc(value) {
      return String(value ?? "").replace(/[&<>"']/g, (ch) => ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#39;"
      }[ch]));
    }

    function selectedId(section) {
      return {
        profiles: state.selected_profile_id,
        tools: state.selected_tool_id,
        resources: state.selected_resource_id,
        prompts: state.selected_prompt_id,
      }[section] || "";
    }

    function currentItems() {
      return state[state.activeSection] || [];
    }

    function currentItem() {
      const id = selectedId(state.activeSection);
      return currentItems().find((item) => item.id === id) || currentItems()[0] || null;
    }

    function toolBadge(item) {
      if (state.activeSection !== "tools") {
        return "";
      }
      const approvalClass = item.approval === "approved" ? "good" : item.approval === "denied" ? "bad" : "warn";
      const enabledText = item.enabled ? "Enabled" : "Disabled";
      const enabledClass = item.enabled ? "good" : "warn";
      return `<span class="badge ${enabledClass}">${enabledText}</span><span class="badge ${approvalClass}">${esc(item.approval)}</span>`;
    }

    function renderNav() {
      const nav = document.getElementById("nav");
      nav.innerHTML = Object.entries(sections).map(([key, info]) => {
        const active = key === state.activeSection ? "active" : "";
        return `
          <button class="${active}" onclick="window.__mcpSetSection('${key}')">
            <svg class="icon" aria-hidden="true"><use href="#${key === 'tools' ? 'i-check' : key === 'profiles' ? 'i-refresh' : key === 'resources' ? 'i-download' : 'i-upload'}"></use></svg>
            <span>
              ${info.title}
              <small class="meta">${info.meta}</small>
            </span>
          </button>`;
      }).join("");
    }

    function renderActions() {
      const actions = [];
      actions.push({ id: "refresh", label: "Refresh", icon: "i-refresh", action: "refresh" });
      if (state.activeSection === "tools") {
        actions.push({ id: "import", label: "Import", icon: "i-upload", action: "import_bundle" });
        actions.push({ id: "export", label: "Export", icon: "i-download", action: "export_bundle" });
        const tool = currentItem();
        actions.push({ id: "enable", label: "Enable", icon: "i-play", action: "enable_tool", disabled: !tool || tool.enabled });
        actions.push({ id: "disable", label: "Disable", icon: "i-pause", action: "disable_tool", disabled: !tool || !tool.enabled });
      }
      const container = document.getElementById("actions");
      container.innerHTML = actions.map((button) => `
        <button class="action" ${button.disabled ? "disabled" : ""} onclick="window.__mcpAction('${button.action}')">
          ${icon(button.icon)}
          <span>${button.label}</span>
        </button>`).join("");
    }

    function listTitleFor(section) {
      return {
        profiles: "Profiles",
        tools: "Tool catalog",
        resources: "Resources",
        prompts: "Prompts"
      }[section];
    }

    function detailTitleFor(section) {
      return {
        profiles: "Profile details",
        tools: "Policy editor",
        resources: "Resource details",
        prompts: "Prompt details"
      }[section];
    }

    function renderList() {
      const items = currentItems();
      document.getElementById("listTitle").textContent = listTitleFor(state.activeSection);
      document.getElementById("listCount").textContent = `${items.length} item${items.length === 1 ? "" : "s"}`;
      const list = document.getElementById("list");
      if (!items.length) {
        list.innerHTML = `<div class="empty">No ${state.activeSection} configured.</div>`;
        return;
      }
      list.innerHTML = items.map((item) => {
        const active = item.id === selectedId(state.activeSection) ? "active" : "";
        if (state.activeSection === "profiles") {
          return `
            <button class="row ${active}" onclick='window.__mcpSelect("${state.activeSection}", ${JSON.stringify(item.id)})'>
              <div class="primary"><span>${esc(item.name)}</span><span class="badge">${item.enabled_tool_count} tools</span></div>
              <div class="secondary">ID: ${esc(item.id)}\nEndpoints: ${item.endpoint_count}</div>
            </button>`;
        }
        if (state.activeSection === "tools") {
          return `
            <button class="row ${active}" onclick='window.__mcpSelect("${state.activeSection}", ${JSON.stringify(item.id)})'>
              <div class="primary"><span>${esc(item.name)}</span><span>${toolBadge(item)}</span></div>
              <div class="secondary">${esc(item.profile_id)} · ${esc(item.source_kind)}\n${esc(item.description)}</div>
            </button>`;
        }
        if (state.activeSection === "resources") {
          return `
            <button class="row ${active}" onclick='window.__mcpSelect("${state.activeSection}", ${JSON.stringify(item.id)})'>
              <div class="primary"><span>${esc(item.name)}</span><span class="badge">${esc(item.source_kind)}</span></div>
              <div class="secondary">${esc(item.uri)}\n${esc(item.source_location)}</div>
            </button>`;
        }
        return `
          <button class="row ${active}" onclick='window.__mcpSelect("${state.activeSection}", ${JSON.stringify(item.id)})'>
            <div class="primary"><span>${esc(item.name)}</span><span class="badge">${esc(item.source_kind)}</span></div>
            <div class="secondary">${esc(item.description)}\n${esc(item.template_text.slice(0, 120))}</div>
          </button>`;
      }).join("");
    }

    function renderProfileDetail(item) {
      return `
        <div class="hero">
          <div class="name">${esc(item.name)}</div>
          <div class="id">${esc(item.id)}</div>
          <div class="desc">Workspace profile</div>
        </div>
        <div class="grid">
          <div class="stat"><div class="label">Endpoints</div><div class="value">${item.endpoint_count}</div></div>
          <div class="stat"><div class="label">Enabled tools</div><div class="value">${item.enabled_tool_count}</div></div>
        </div>`;
    }

    function renderResourceDetail(item) {
      return `
        <div class="hero">
          <div class="name">${esc(item.name)}</div>
          <div class="id">${esc(item.id)}</div>
          <div class="desc">${esc(item.description)}</div>
        </div>
        <div class="grid">
          <div class="stat"><div class="label">URI</div><div class="value">${esc(item.uri)}</div></div>
          <div class="stat"><div class="label">Source</div><div class="value">${esc(item.source_kind)}\n${esc(item.source_location)}</div></div>
        </div>`;
    }

    function renderPromptDetail(item) {
      return `
        <div class="hero">
          <div class="name">${esc(item.name)}</div>
          <div class="id">${esc(item.id)}</div>
          <div class="desc">${esc(item.description)}</div>
        </div>
        <div class="grid">
          <div class="stat"><div class="label">Source</div><div class="value">${esc(item.source_kind)}\n${esc(item.source_location)}</div></div>
          <div class="stat"><div class="label">Template length</div><div class="value">${item.template_text.length} chars</div></div>
        </div>
        <div class="section">
          <h2>Template</h2>
          <div class="field"><textarea readonly>${esc(item.template_text)}</textarea></div>
        </div>`;
    }

    function renderToolDetail(item) {
      const permissions = [
        ["network_access", "Network access"],
        ["filesystem_read", "Filesystem read"],
        ["filesystem_write", "Filesystem write"],
        ["command_execution", "Command execution"],
      ];
      const checkboxes = permissions.map(([key, label]) => {
        const checked = item.permissions.includes(key) ? "checked" : "";
        return `<label class="check"><input type="checkbox" data-perm="${key}" ${checked} /> ${label}</label>`;
      }).join("");
      return `
        <div class="hero">
          <div class="name">${esc(item.name)}</div>
          <div class="id">${esc(item.id)}</div>
          <div class="desc">${esc(item.description)}</div>
        </div>
        <div class="grid">
          <div class="stat"><div class="label">Profile</div><div class="value">${esc(item.profile_id)}</div></div>
          <div class="stat"><div class="label">Source</div><div class="value">${esc(item.source_kind)}\n${item.enabled ? "Enabled" : "Disabled"}</div></div>
          <div class="stat"><div class="label">Approval</div><div class="value">${esc(item.approval)}</div></div>
          <div class="stat"><div class="label">Permissions</div><div class="value">${esc(item.permissions.join("\n") || "None")}</div></div>
        </div>
        <div class="section">
          <h2>Policy</h2>
          <div class="field">
            <label for="approval">Approval state</label>
            <select id="approval">
              <option value="pending" ${item.approval === "pending" ? "selected" : ""}>Pending</option>
              <option value="approved" ${item.approval === "approved" ? "selected" : ""}>Approved</option>
              <option value="denied" ${item.approval === "denied" ? "selected" : ""}>Denied</option>
            </select>
          </div>
          <div class="checks">${checkboxes}</div>
          <div class="actions">
            <button class="action" onclick='window.__mcpApplyPolicy(${JSON.stringify(item.id)})'>${icon("i-check")}<span>Apply policy</span></button>
          </div>
        </div>`;
    }

    function renderDetail() {
      const item = currentItem();
      document.getElementById("detailTitle").textContent = detailTitleFor(state.activeSection);
      document.getElementById("detailCount").textContent = item ? item.id : "";
      const detail = document.getElementById("detail");
      if (!item) {
        detail.innerHTML = `<div class="empty">No selection.</div>`;
        return;
      }
      if (state.activeSection === "profiles") {
        detail.innerHTML = renderProfileDetail(item);
      } else if (state.activeSection === "tools") {
        detail.innerHTML = renderToolDetail(item);
      } else if (state.activeSection === "resources") {
        detail.innerHTML = renderResourceDetail(item);
      } else {
        detail.innerHTML = renderPromptDetail(item);
      }
    }

    function renderShell() {
      document.getElementById("pageTitle").textContent = sections[state.activeSection].title;
      document.getElementById("pageMeta").textContent = sections[state.activeSection].meta;
      document.getElementById("sidebarMeta").textContent =
        `${state.profiles.length} profiles · ${state.tools.length} tools · ${state.resources.length} resources · ${state.prompts.length} prompts`;
      document.getElementById("sidebarFooter").textContent = state.status || "Ready";
      document.getElementById("statusBar").textContent = state.status || "";
      renderNav();
      renderActions();
      renderList();
      renderDetail();
    }

    window.__mcpUpdateState = function(next) {
      Object.assign(state, next);
      renderShell();
    };

    window.__mcpSetSection = function(section) {
      state.activeSection = section;
      renderShell();
    };

    window.__mcpSelect = function(section, id) {
      send(selectAction[section], { id });
    };

    window.__mcpAction = function(action) {
      send(action);
    };

    window.__mcpApplyPolicy = function(id) {
      const approval = document.getElementById("approval").value;
      const permissions = Array.from(document.querySelectorAll("[data-perm]:checked")).map((box) => box.dataset.perm);
      send("update_policy", { id, approval, permissions });
    };
  </script>
</body>
</html>
)HTML";

std::string to_utf8(const wxString& value) {
    return value.ToStdString();
}

wxString from_utf8(std::string_view value) {
    return wxString::FromUTF8(value.data(), value.size());
}

} // namespace

class DashboardPanel::Impl final {
public:
    Impl(DashboardPanel* owner, GuiController& controller)
        : owner_(owner),
          controller_(controller) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto backend = wxWebViewBackendDefault;
        if (wxWebView::IsBackendAvailable(wxWebViewBackendEdge)) {
            backend = wxWebViewBackendEdge;
        }
        webview_ = wxWebView::New(owner_, wxID_ANY, wxWebViewDefaultURLStr, wxDefaultPosition, wxDefaultSize, backend, 0);
        if (!webview_) {
            root->Add(new wxStaticText(owner_, wxID_ANY, "Web UI is unavailable."), 1, wxEXPAND | wxALL, 12);
            owner_->SetSizer(root);
            owner_->Layout();
            return;
        }

        webview_->AddScriptMessageHandler("mcp");
        webview_->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, [this](wxWebViewEvent& event) {
            handle_message(event);
        });
        webview_->Bind(wxEVT_WEBVIEW_LOADED, [this](wxWebViewEvent&) {
            loaded_ = true;
            render();
        });

        root->Add(webview_, 1, wxEXPAND);
        owner_->SetSizer(root);
        webview_->SetPage(from_utf8(kDashboardHtml), "about:blank");
        refresh_from_controller();
    }

    void refresh() {
        refresh_from_controller();
    }

private:
    void refresh_from_controller() {
        const auto snapshot = controller_.snapshot();
        if (!snapshot) {
            show_error(snapshot.error());
            return;
        }
        snapshot_ = *snapshot;
        render();
    }

    void apply_snapshot(const GuiSnapshot& snapshot, bool render_now = true) {
        snapshot_ = snapshot;
        if (render_now) {
            render();
        }
    }

    void render() {
        if (!webview_ || !loaded_) {
            return;
        }
        const auto script = "window.__mcpUpdateState(" + snapshot_json(snapshot_).dump() + ");";
        webview_->RunScriptAsync(from_utf8(script));
    }

    void handle_message(wxWebViewEvent& event) {
        Json payload;
        try {
            payload = Json::parse(to_utf8(event.GetString()));
        } catch (const std::exception& ex) {
            show_error(core::Error{1, "invalid web ui message", ex.what()});
            return;
        }

        const auto action = payload.value("action", "");
        if (action == "refresh") {
            refresh_from_controller();
            return;
        }
        if (action == "select_profile") {
            run_action([&] { return controller_.select_profile(payload.value("id", "")); });
            return;
        }
        if (action == "select_tool") {
            run_action([&] { return controller_.select_tool(payload.value("id", "")); });
            return;
        }
        if (action == "select_resource") {
            run_action([&] { return controller_.select_resource(payload.value("id", "")); });
            return;
        }
        if (action == "select_prompt") {
            run_action([&] { return controller_.select_prompt(payload.value("id", "")); });
            return;
        }
        if (action == "enable_tool") {
            run_action([&] { return controller_.enable_tool(snapshot_.selected_tool_id); });
            return;
        }
        if (action == "disable_tool") {
            run_action([&] { return controller_.disable_tool(snapshot_.selected_tool_id); });
            return;
        }
        if (action == "update_policy") {
            app::Policy policy;
            const auto approval = approval_from_key(payload.value("approval", "pending"));
            if (!approval) {
                show_error(core::Error{1, "invalid approval state"});
                return;
            }
            policy.approval = *approval;
            for (const auto& item : payload.value("permissions", Json::array())) {
                const auto permission = permission_from_key(item.get<std::string>());
                if (!permission) {
                    show_error(core::Error{1, "invalid permission", item.dump()});
                    return;
                }
                policy.permissions.insert(*permission);
            }
            run_action([&] { return controller_.update_tool_policy(payload.value("id", ""), std::move(policy)); });
            return;
        }
        if (action == "import_bundle") {
            wxFileDialog dialog(owner_, "Import bundle", wxEmptyString, wxEmptyString,
                                "JSON files (*.json)|*.json|All files (*.*)|*.*",
                                wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dialog.ShowModal() != wxID_OK) {
                return;
            }
            run_action([&] {
                return controller_.import_bundle(std::filesystem::path(to_utf8(dialog.GetPath())));
            });
            return;
        }
        if (action == "export_bundle") {
            wxFileDialog dialog(owner_, "Export bundle", wxEmptyString, "mcp-bundle.json",
                                "JSON files (*.json)|*.json|All files (*.*)|*.*",
                                wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dialog.ShowModal() != wxID_OK) {
                return;
            }
            run_action([&] {
                return controller_.export_bundle(std::filesystem::path(to_utf8(dialog.GetPath())));
            });
            return;
        }

        show_error(core::Error{1, "unknown ui action", action});
    }

    template <typename Fn>
    void run_action(Fn&& fn) {
        const auto result = std::invoke(std::forward<Fn>(fn));
        if (!result) {
            show_error(result.error());
            return;
        }
        apply_snapshot(*result);
    }

    void show_error(const core::Error& error) {
        const auto message = error.detail.empty() ? error.message : (error.message + "\n\n" + error.detail);
        wxMessageBox(from_utf8(message), "MCP GUI", wxOK | wxICON_ERROR, owner_);
    }

    DashboardPanel* owner_ = nullptr;
    GuiController& controller_;
    wxWebView* webview_ = nullptr;
    GuiSnapshot snapshot_;
    bool loaded_ = false;
};

DashboardPanel::DashboardPanel(wxWindow* parent, GuiController& controller)
    : wxPanel(parent, wxID_ANY),
      impl_(std::make_unique<Impl>(this, controller)) {}

DashboardPanel::~DashboardPanel() = default;

void DashboardPanel::refresh() {
    if (impl_) {
        impl_->refresh();
    }
}

} // namespace mcp::gui
