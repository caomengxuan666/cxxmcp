#pragma once

/// @file
/// @brief Aggregate callback configuration for mcp::server::Server.

#include "cxxmcp/server/server.hpp"

#include <utility>

namespace mcp::server {

    /// @brief Optional callback bundle for configuring a Server in one call.
    ///
    /// Each member mirrors a Server::set_*_handler() function. apply_to() and
    /// Server::set_handler() install only non-empty members; empty members leave any
    /// existing callback on the target Server unchanged.
    struct ServerHandler {
        using JsonHandler = Server::JsonHandler;
        using LoggingHandler = Server::LoggingHandler;
        using RawRequestHandler = Server::RawRequestHandler;
        using RawNotificationHandler = Server::RawNotificationHandler;
        using TaskListHandler = Server::TaskListHandler;
        using TaskGetHandler = Server::TaskGetHandler;
        using TaskCancelHandler = Server::TaskCancelHandler;
        using TaskResultHandler = Server::TaskResultHandler;
        using RootsListChangedHandler = Server::RootsListChangedHandler;
        using ProgressHandler = Server::ProgressHandler;
        using ListChangedHandler = Server::ListChangedHandler;
        using ResourceUpdatedHandler = Server::ResourceUpdatedHandler;

        /// Handles completion requests.
        JsonHandler on_completion;
        /// Handles sampling requests.
        JsonHandler on_sampling;
        /// Handles logging notifications.
        LoggingHandler on_logging;
        /// Optionally handles raw requests before built-in dispatch.
        RawRequestHandler on_raw_request;
        /// Handles raw notifications.
        RawNotificationHandler on_raw_notification;
        /// Optionally handles custom requests.
        RawRequestHandler on_custom_request;
        /// Handles custom notifications.
        RawNotificationHandler on_custom_notification;
        /// Handles task list requests.
        TaskListHandler on_task_list;
        /// Handles task get requests.
        TaskGetHandler on_task_get;
        /// Handles task cancel requests.
        TaskCancelHandler on_task_cancel;
        /// Handles task result requests.
        TaskResultHandler on_task_result;
        /// Handles progress notifications from clients.
        ProgressHandler on_progress;
        /// Handles roots-list-changed notifications from clients.
        RootsListChangedHandler on_roots_list_changed;
        /// Handles tool-list-changed notifications from clients.
        ListChangedHandler on_tool_list_changed;
        /// Handles prompt-list-changed notifications from clients.
        ListChangedHandler on_prompt_list_changed;
        /// Handles resource-list-changed notifications from clients.
        ListChangedHandler on_resource_list_changed;
        /// Handles resource-updated notifications from clients.
        ResourceUpdatedHandler on_resource_updated;

        /// @brief Applies all non-empty callbacks to a server.
        /// @param server Server to configure.
        void apply_to(Server &server) const {
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
            if (on_task_list) {
                server.set_task_list_handler(on_task_list);
            }
            if (on_task_get) {
                server.set_task_get_handler(on_task_get);
            }
            if (on_task_cancel) {
                server.set_task_cancel_handler(on_task_cancel);
            }
            if (on_task_result) {
                server.set_task_result_handler(on_task_result);
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

    /// @brief Installs all non-empty callbacks from a ServerHandler.
    /// @param handler Callback aggregate to apply.
    /// @return Reference to this server for chaining.
    inline Server &Server::set_handler(const ServerHandler &handler) {
        handler.apply_to(*this);
        return *this;
    }

}// namespace mcp::server
