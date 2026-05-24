#pragma once

/// @file
/// @brief Aggregate callback configuration for mcp::client::Client.

#include "cxxmcp/client/client.hpp"

#include <utility>

namespace mcp::client {

    /// @brief Optional callback bundle for configuring a Client in one call.
    ///
    /// Each member mirrors a Client::on_* registration function. apply_to() and
    /// Client::set_handler() install only non-empty members; empty members leave any
    /// existing callback on the target Client unchanged.
    struct ClientHandler {
        using InitializedHandler = Client::InitializedHandler;
        using CancelledHandler = Client::CancelledHandler;
        using LoggingMessageHandler = Client::LoggingMessageHandler;
        using ChangedHandler = Client::ListChangedHandler;
        using ResourceUpdatedHandler = Client::ResourceUpdatedHandler;
        using ProgressHandler = Client::ProgressHandler;
        using ElicitationCompleteHandler = Client::ElicitationCompleteHandler;
        using TaskStatusHandler = Client::TaskStatusHandler;
        using RootsListRequestHandler = Client::RootsListRequestHandler;
        using SamplingRequestHandler = Client::SamplingRequestHandler;
        using ElicitationRequestHandler = Client::ElicitationRequestHandler;
        using CustomRequestHandler = Client::CustomRequestHandler;
        using RawNotificationHandler = Client::RawNotificationHandler;

        /// Called when the server sends an initialized notification.
        InitializedHandler on_initialized;
        /// Called when the server cancels a request.
        CancelledHandler on_cancelled;
        /// Called for server logging message notifications.
        LoggingMessageHandler on_logging_message;
        /// Called when the server's tool list changes.
        ChangedHandler on_tool_list_changed;
        /// Called when the server's prompt list changes.
        ChangedHandler on_prompt_list_changed;
        /// Called when the server's resource list changes.
        ChangedHandler on_resource_list_changed;
        /// Called when a subscribed resource URI is updated.
        ResourceUpdatedHandler on_resource_updated;
        /// Called for progress notifications.
        ProgressHandler on_progress;
        /// Called when an elicitation flow completes.
        ElicitationCompleteHandler on_elicitation_complete;
        /// Called when a task status notification is received.
        TaskStatusHandler on_task_status;
        /// Called when the client's roots list changes.
        ChangedHandler on_roots_list_changed;
        /// Handles server list-roots requests.
        RootsListRequestHandler on_list_roots_request;
        /// Handles server sampling createMessage requests.
        SamplingRequestHandler on_create_message_request;
        /// Handles server elicitation requests.
        ElicitationRequestHandler on_create_elicitation_request;
        /// Handles custom server requests.
        CustomRequestHandler on_custom_request;
        /// Compatibility alias for on_list_roots_request.
        RootsListRequestHandler on_roots_list_request;
        /// Compatibility alias for on_create_message_request.
        SamplingRequestHandler on_sampling_request;
        /// Compatibility alias for on_create_elicitation_request.
        ElicitationRequestHandler on_elicitation_request;
        /// Observes raw inbound notifications after built-in dispatch.
        RawNotificationHandler on_raw_notification;
        /// Compatibility alias for on_raw_notification.
        RawNotificationHandler on_custom_notification;

        /// @brief Applies all non-empty callbacks to a client.
        /// @param client Client to configure.
        void apply_to(Client &client) const {
            if (on_initialized) {
                client.on_initialized(on_initialized);
            }
            if (on_cancelled) {
                client.on_cancelled(on_cancelled);
            }
            if (on_logging_message) {
                client.on_logging_message(on_logging_message);
            }
            if (on_tool_list_changed) {
                client.on_tool_list_changed(on_tool_list_changed);
            }
            if (on_prompt_list_changed) {
                client.on_prompt_list_changed(on_prompt_list_changed);
            }
            if (on_resource_list_changed) {
                client.on_resource_list_changed(on_resource_list_changed);
            }
            if (on_resource_updated) {
                client.on_resource_updated(on_resource_updated);
            }
            if (on_progress) {
                client.on_progress(on_progress);
            }
            if (on_elicitation_complete) {
                client.on_elicitation_complete(on_elicitation_complete);
            }
            if (on_task_status) {
                client.on_task_status(on_task_status);
            }
            if (on_roots_list_changed) {
                client.on_roots_list_changed(on_roots_list_changed);
            }
            if (on_list_roots_request) {
                client.on_list_roots_request(on_list_roots_request);
            }
            if (on_create_message_request) {
                client.on_create_message_request(on_create_message_request);
            }
            if (on_create_elicitation_request) {
                client.on_create_elicitation_request(on_create_elicitation_request);
            }
            if (on_custom_request) {
                client.on_custom_request(on_custom_request);
            }
            if (on_roots_list_request) {
                client.on_roots_list_request(on_roots_list_request);
            }
            if (on_sampling_request) {
                client.on_sampling_request(on_sampling_request);
            }
            if (on_elicitation_request) {
                client.on_elicitation_request(on_elicitation_request);
            }
            if (on_raw_notification) {
                client.on_raw_notification(on_raw_notification);
            }
            if (on_custom_notification) {
                client.on_custom_notification(on_custom_notification);
            }
        }
    };

    /// @brief Installs all non-empty callbacks from a ClientHandler.
    /// @param handler Callback aggregate to apply.
    /// @return Reference to this client for chaining.
    inline Client &Client::set_handler(const ClientHandler &handler) {
        handler.apply_to(*this);
        return *this;
    }

}// namespace mcp::client
