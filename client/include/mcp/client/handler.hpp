#pragma once

#include "mcp/client/client.hpp"

#include <utility>

namespace mcp::client {

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

    InitializedHandler on_initialized;
    CancelledHandler on_cancelled;
    LoggingMessageHandler on_logging_message;
    ChangedHandler on_tool_list_changed;
    ChangedHandler on_prompt_list_changed;
    ChangedHandler on_resource_list_changed;
    ResourceUpdatedHandler on_resource_updated;
    ProgressHandler on_progress;
    ElicitationCompleteHandler on_elicitation_complete;
    TaskStatusHandler on_task_status;
    ChangedHandler on_roots_list_changed;
    RootsListRequestHandler on_list_roots_request;
    SamplingRequestHandler on_create_message_request;
    ElicitationRequestHandler on_create_elicitation_request;
    CustomRequestHandler on_custom_request;
    RootsListRequestHandler on_roots_list_request;
    SamplingRequestHandler on_sampling_request;
    ElicitationRequestHandler on_elicitation_request;
    RawNotificationHandler on_raw_notification;
    RawNotificationHandler on_custom_notification;

    void apply_to(Client& client) const {
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

inline Client& Client::set_handler(const ClientHandler& handler) {
    handler.apply_to(*this);
    return *this;
}

} // namespace mcp::client
