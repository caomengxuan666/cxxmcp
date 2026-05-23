#pragma once

namespace mcp::protocol {

struct ToolCapabilities {
    bool list_changed = false;
};

struct ResourceCapabilities {
    bool list_changed = false;
    bool subscribe = false;
};

struct PromptCapabilities {
    bool list_changed = false;
};

struct LoggingCapabilities {
    bool enabled = false;
};

struct ServerCapabilities {
    ToolCapabilities tools;
    ResourceCapabilities resources;
    PromptCapabilities prompts;
    LoggingCapabilities logging;
};

} // namespace mcp::protocol

