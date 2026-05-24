#pragma once

#include "mcp/server/server.hpp"

#include <utility>

namespace mcp::server {

struct ServerHandler {
    using JsonHandler = Server::JsonHandler;
    using LoggingHandler = Server::LoggingHandler;
    using RawRequestHandler = Server::RawRequestHandler;
    using RawNotificationHandler = Server::RawNotificationHandler;
    using RootsListChangedHandler = Server::RootsListChangedHandler;
    using ProgressHandler = Server::ProgressHandler;
    using ListChangedHandler = Server::ListChangedHandler;
    using ResourceUpdatedHandler = Server::ResourceUpdatedHandler;

    JsonHandler on_completion;
    JsonHandler on_sampling;
    LoggingHandler on_logging;
    RawRequestHandler on_raw_request;
    RawNotificationHandler on_raw_notification;
    RawRequestHandler on_custom_request;
    RawNotificationHandler on_custom_notification;
    ProgressHandler on_progress;
    RootsListChangedHandler on_roots_list_changed;
    ListChangedHandler on_tool_list_changed;
    ListChangedHandler on_prompt_list_changed;
    ListChangedHandler on_resource_list_changed;
    ResourceUpdatedHandler on_resource_updated;

    void apply_to(Server& server) const {
        if (on_completion) {
            server.set_completion_handler(on_completion);
        }
        if (on_sampling) {
            server.set_sampling_handler(on_sampling);
        }
        if (on_logging) {
            server.set_logging_handler(on_logging);
        }
        if (on_raw_request) {
            server.set_raw_request_handler(on_raw_request);
        }
        if (on_raw_notification) {
            server.set_raw_notification_handler(on_raw_notification);
        }
        if (on_custom_request) {
            server.set_custom_request_handler(on_custom_request);
        }
        if (on_custom_notification) {
            server.set_custom_notification_handler(on_custom_notification);
        }
        if (on_progress) {
            server.set_progress_handler(on_progress);
        }
        if (on_roots_list_changed) {
            server.set_roots_list_changed_handler(on_roots_list_changed);
        }
        if (on_tool_list_changed) {
            server.set_tool_list_changed_handler(on_tool_list_changed);
        }
        if (on_prompt_list_changed) {
            server.set_prompt_list_changed_handler(on_prompt_list_changed);
        }
        if (on_resource_list_changed) {
            server.set_resource_list_changed_handler(on_resource_list_changed);
        }
        if (on_resource_updated) {
            server.set_resource_updated_handler(on_resource_updated);
        }
    }
};

inline Server& Server::set_handler(const ServerHandler& handler) {
    handler.apply_to(*this);
    return *this;
}

} // namespace mcp::server
