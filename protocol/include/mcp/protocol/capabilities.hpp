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

struct SamplingCapabilities {
    bool enabled = false;
};

struct CompletionCapabilities {
    bool enabled = false;
};

struct RootCapabilities {
    bool list_changed = false;
};

struct ElicitationCapabilities {
    bool enabled = false;
};

struct ClientCapabilities {
    RootCapabilities roots;
    SamplingCapabilities sampling;
    ElicitationCapabilities elicitation;
};

struct ServerCapabilities {
    ToolCapabilities tools;
    ResourceCapabilities resources;
    PromptCapabilities prompts;
    LoggingCapabilities logging;
    CompletionCapabilities completions;
};

} // namespace mcp::protocol
