#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/elicitation.hpp"
#include "mcp/protocol/task.hpp"
#include "mcp/protocol/roots.hpp"
#include "mcp/protocol/sampling.hpp"
#include "mcp/protocol/serialization.hpp"
#include "mcp/server/transport.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcp::server {

class ClientPeer {
public:
    explicit ClientPeer(Transport* transport = nullptr) noexcept
        : transport_(transport) {}

    bool available() const noexcept {
        return transport_ != nullptr;
    }

    bool supports_roots() const noexcept {
        const auto capabilities = transport_ ? transport_->client_capabilities() : std::optional<protocol::ClientCapabilities>{};
        return capabilities.has_value() && capabilities->roots.list_changed;
    }

    bool supports_sampling_tools() const noexcept {
        const auto capabilities = transport_ ? transport_->client_capabilities() : std::optional<protocol::ClientCapabilities>{};
        return capabilities.has_value() && capabilities->sampling.enabled;
    }

    bool supports_elicitation_form() const noexcept {
        const auto capabilities = transport_ ? transport_->client_capabilities() : std::optional<protocol::ClientCapabilities>{};
        return capabilities.has_value() && capabilities->elicitation.form;
    }

    bool supports_elicitation_url() const noexcept {
        const auto capabilities = transport_ ? transport_->client_capabilities() : std::optional<protocol::ClientCapabilities>{};
        return capabilities.has_value() && capabilities->elicitation.url;
    }

    bool supports_elicitation() const noexcept {
        return supports_elicitation_form() || supports_elicitation_url();
    }

    core::Result<protocol::Json> request(std::string method,
                                         protocol::Json params = protocol::Json::object()) const {
        if (!transport_) {
            return std::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::InternalError),
                "client peer is not available",
                {},
            });
        }

        auto response = transport_->send_request(protocol::JsonRpcRequest{
            .method = std::move(method),
            .params = std::move(params),
            .id = next_request_id(),
        });
        if (!response) {
            return std::unexpected(response.error());
        }
        if (response->error.has_value()) {
            return std::unexpected(core::Error{
                response->error->code,
                response->error->message,
                response->error->data.has_value() ? response->error->data->dump() : std::string{},
            });
        }
        if (!response->result.has_value()) {
            return std::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::InvalidRequest),
                "client peer response did not contain a result",
                {},
            });
        }
        return *response->result;
    }

    core::Result<protocol::RootsListResult> list_roots() const {
        if (!supports_roots()) {
            return std::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::MethodNotFound),
                "client does not support roots",
                {},
            });
        }
        auto payload = request(std::string(protocol::RootsListMethod), protocol::Json::object());
        if (!payload) {
            return std::unexpected(payload.error());
        }
        auto result = protocol::roots_list_result_from_json(*payload);
        if (!result) {
            return std::unexpected(result.error());
        }
        return *result;
    }

    core::Result<protocol::CreateMessageResult> create_message(
            const protocol::CreateMessageParams& params) const {
        if (!supports_sampling_tools()) {
            return std::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::MethodNotFound),
                "client does not support sampling",
                {},
            });
        }
        auto payload = request(std::string(protocol::SamplingCreateMessageMethod),
                               protocol::create_message_params_to_json(params));
        if (!payload) {
            return std::unexpected(payload.error());
        }
        auto result = protocol::create_message_result_from_json(*payload);
        if (!result) {
            return std::unexpected(result.error());
        }
        return *result;
    }

    core::Result<protocol::CreateElicitationResult> create_elicitation(
            const protocol::CreateElicitationRequestParam& params) const {
        if (params.mode == protocol::ElicitationMode::Url && !supports_elicitation_url()) {
            return std::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::UrlElicitationRequired),
                "client does not support url elicitation",
                {},
            });
        }
        if (params.mode == protocol::ElicitationMode::Form && !supports_elicitation_form()) {
            return std::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::MethodNotFound),
                "client does not support elicitation",
                {},
            });
        }
        auto payload = request(std::string(protocol::ElicitationCreateMethod),
                               protocol::create_elicitation_request_param_to_json(params));
        if (!payload) {
            return std::unexpected(payload.error());
        }
        auto result = protocol::create_elicitation_result_from_json(*payload);
        if (!result) {
            return std::unexpected(result.error());
        }
        return *result;
    }

    core::Result<std::vector<protocol::Task>> list_tasks() const {
        auto payload = request(std::string(protocol::TasksListMethod), protocol::Json::object());
        if (!payload) {
            return std::unexpected(payload.error());
        }
        const auto tasks = protocol::task_list_result_from_json(*payload);
        if (!tasks) {
            return std::unexpected(tasks.error());
        }
        return tasks->tasks;
    }

    core::Result<std::vector<protocol::Task>> list_all_tasks() const {
        std::vector<protocol::Task> all;
        std::optional<std::string> cursor;
        do {
            auto payload = request(std::string(protocol::TasksListMethod),
                                   cursor.has_value() ? protocol::Json{{"cursor", *cursor}} : protocol::Json::object());
            if (!payload) {
                return std::unexpected(payload.error());
            }
            const auto page = protocol::task_list_result_from_json(*payload);
            if (!page) {
                return std::unexpected(page.error());
            }
            all.insert(all.end(), page->tasks.begin(), page->tasks.end());
            cursor = page->next_cursor;
        } while (cursor.has_value() && !cursor->empty());
        return all;
    }

    core::Result<protocol::Task> get_task(std::string_view task_id) const {
        auto payload = request(std::string(protocol::TasksGetMethod),
                               protocol::task_get_params_to_json(protocol::TaskGetParams{
                                   .task_id = std::string(task_id),
                               }));
        if (!payload) {
            return std::unexpected(payload.error());
        }
        const auto task = protocol::task_from_json(*payload);
        if (!task) {
            return std::unexpected(task.error());
        }
        return *task;
    }

    core::Result<protocol::Task> cancel_task(std::string_view task_id) const {
        auto payload = request(std::string(protocol::TasksCancelMethod),
                               protocol::task_cancel_params_to_json(protocol::TaskCancelParams{
                                   .task_id = std::string(task_id),
                               }));
        if (!payload) {
            return std::unexpected(payload.error());
        }
        const auto task = protocol::task_from_json(*payload);
        if (!task) {
            return std::unexpected(task.error());
        }
        return *task;
    }

    core::Result<protocol::Json> task_result(std::string_view task_id) const {
        return request(std::string(protocol::TasksResultMethod),
                       protocol::task_get_params_to_json(protocol::TaskResultParams{
                           .task_id = std::string(task_id),
                       }));
    }

    core::Result<core::Unit> notify_elicitation_complete(std::string elicitation_id) const {
        if (!transport_) {
            return std::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::InternalError),
                "client peer is not available",
                {},
            });
        }
        return transport_->send_notification(protocol::JsonRpcNotification{
            .method = std::string(protocol::ElicitationCompleteNotificationMethod),
            .params = protocol::elicitation_complete_notification_params_to_json(
                protocol::ElicitationCompleteNotificationParams{
                    .elicitation_id = std::move(elicitation_id),
                }),
        });
    }

private:
    static protocol::RequestId next_request_id() {
        static std::atomic<std::int64_t> next{1};
        return next.fetch_add(1);
    }

    Transport* transport_;
};

inline ClientPeer client_peer(const SessionContext& context) noexcept {
    return ClientPeer(context.transport);
}

inline ClientPeer SessionContext::client() const noexcept {
    return ClientPeer(transport);
}

} // namespace mcp::server
