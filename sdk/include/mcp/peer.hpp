#pragma once

#include "mcp/handler.hpp"
#include "mcp/client/client.hpp"
#include "mcp/client/session.hpp"
#include "mcp/server/peer.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcp {

struct RoleClient {};
struct RoleServer {};

template <class Role>
class Peer;

template <>
class Peer<RoleClient> {
public:
    explicit Peer(client::Client& client) noexcept
        : client_(&client) {}

    client::Client& client() noexcept {
        return *client_;
    }

    const client::Client& client() const noexcept {
        return *client_;
    }

    core::Result<protocol::Json> initialize(std::string client_name = "cxxmcp",
                                            std::string client_version = "0") {
        return client().initialize(std::move(client_name), std::move(client_version));
    }

    core::Result<core::Unit> notify_initialized() {
        return client().notify_initialized();
    }

    core::Result<core::Unit> notify_cancelled(protocol::RequestId request_id,
                                              std::string reason = {}) {
        return client().notify_cancelled(request_id, std::move(reason));
    }

    core::Result<core::Unit> notify_progress(protocol::ProgressToken progress_token,
                                             double progress,
                                             std::optional<double> total = std::nullopt,
                                             std::string message = {}) {
        return client().notify_progress(progress_token, progress, total, std::move(message));
    }

    core::Result<core::Unit> notify_roots_list_changed() {
        return client().notify_roots_list_changed();
    }

    core::Result<core::Unit> ping() {
        return client().ping();
    }

    core::Result<std::vector<protocol::Prompt>> list_prompts() {
        return client().list_prompts();
    }

    core::Result<std::vector<protocol::Prompt>> list_all_prompts() {
        return client().list_all_prompts();
    }

    core::Result<protocol::PromptsGetResult> get_prompt(const protocol::PromptsGetParams& params) {
        return client().get_prompt(params);
    }

    core::Result<protocol::PromptsGetResult> get_prompt(std::string_view name,
                                                        const protocol::Json& arguments = protocol::Json::object()) {
        return client().get_prompt(name, arguments);
    }

    core::Result<std::vector<protocol::Resource>> list_resources() {
        return client().list_resources();
    }

    core::Result<std::vector<protocol::Resource>> list_all_resources() {
        return client().list_all_resources();
    }

    core::Result<protocol::ResourcesReadResult> read_resource(const protocol::ResourcesReadParams& params) {
        return client().read_resource(params);
    }

    core::Result<protocol::ResourcesReadResult> read_resource(std::string_view uri) {
        return client().read_resource(uri);
    }

    core::Result<std::vector<protocol::ResourceTemplate>> list_resource_templates() {
        return client().list_resource_templates();
    }

    core::Result<std::vector<protocol::ResourceTemplate>> list_all_resource_templates() {
        return client().list_all_resource_templates();
    }

    core::Result<std::vector<protocol::ToolDefinition>> list_tools() {
        return client().list_tools();
    }

    core::Result<std::vector<protocol::ToolDefinition>> list_all_tools() {
        return client().list_all_tools();
    }

    core::Result<protocol::ToolResult> call_tool(const protocol::ToolCall& call) {
        return client().call_tool(call);
    }

    core::Result<protocol::ToolResult> call_raw(std::string_view name,
                                                const protocol::Json& arguments = protocol::Json::object()) {
        return client().call_raw(name, arguments);
    }

    core::Result<protocol::CompleteResult> complete(const protocol::CompleteParams& request) {
        return client().complete(request);
    }

    core::Result<protocol::Json> complete(const protocol::Json& request) {
        return client().complete(request);
    }

    core::Result<protocol::CreateMessageResult> create_message(const protocol::CreateMessageParams& request) {
        return client().create_message(request);
    }

    core::Result<protocol::Json> create_message(const protocol::Json& request) {
        return client().create_message(request);
    }

    core::Result<protocol::CreateElicitationResult> create_elicitation(
            const protocol::CreateElicitationRequestParam& request) {
        return client().create_elicitation(request);
    }

    core::Result<protocol::Json> create_elicitation(const protocol::Json& request) {
        return client().create_elicitation(request);
    }

    core::Result<std::vector<protocol::Task>> list_tasks() {
        return client().list_tasks();
    }

    core::Result<std::vector<protocol::Task>> list_all_tasks() {
        return client().list_all_tasks();
    }

    core::Result<protocol::Task> get_task(const protocol::TaskGetParams& request) {
        return client().get_task(request);
    }

    core::Result<protocol::Task> get_task(std::string_view task_id) {
        return client().get_task(task_id);
    }

    core::Result<protocol::Task> cancel_task(const protocol::TaskCancelParams& request) {
        return client().cancel_task(request);
    }

    core::Result<protocol::Task> cancel_task(std::string_view task_id) {
        return client().cancel_task(task_id);
    }

    core::Result<protocol::Json> task_result(const protocol::TaskResultParams& request) {
        return client().task_result(request);
    }

    core::Result<protocol::Json> task_result(std::string_view task_id) {
        return client().task_result(task_id);
    }

    core::Result<core::Unit> set_level(const protocol::LoggingSetLevelParams& params) {
        return client().set_level(params);
    }

    core::Result<core::Unit> set_level(std::string_view level) {
        return client().set_level(level);
    }

    core::Result<core::Unit> subscribe(std::string_view uri) {
        return client().subscribe(uri);
    }

    core::Result<core::Unit> unsubscribe(std::string_view uri) {
        return client().unsubscribe(uri);
    }

    core::Result<protocol::Json> request(const protocol::JsonRpcRequest& request) {
        return client().request(request);
    }

    core::Result<core::Unit> notify(const protocol::JsonRpcNotification& notification) {
        return client().notify(notification);
    }

    core::Result<protocol::Json> raw_request(const protocol::JsonRpcRequest& request) {
        return client().raw_request(request);
    }

    core::Result<core::Unit> raw_notification(const protocol::JsonRpcNotification& notification) {
        return client().raw_notification(notification);
    }

private:
    client::Client* client_;
};

template <>
class Peer<RoleServer> {
public:
    explicit Peer(server::ClientPeer peer) noexcept
        : peer_(std::move(peer)) {}

    explicit Peer(const server::SessionContext& context) noexcept
        : peer_(context.client()) {}

    server::ClientPeer& client() noexcept {
        return peer_;
    }

    const server::ClientPeer& client() const noexcept {
        return peer_;
    }

    bool available() const noexcept {
        return peer_.available();
    }

    bool supports_roots() const noexcept {
        return peer_.supports_roots();
    }

    bool supports_sampling_tools() const noexcept {
        return peer_.supports_sampling_tools();
    }

    bool supports_elicitation_form() const noexcept {
        return peer_.supports_elicitation_form();
    }

    bool supports_elicitation_url() const noexcept {
        return peer_.supports_elicitation_url();
    }

    bool supports_elicitation() const noexcept {
        return peer_.supports_elicitation();
    }

    core::Result<protocol::Json> request(std::string method,
                                         protocol::Json params = protocol::Json::object()) const {
        return peer_.request(std::move(method), std::move(params));
    }

    core::Result<protocol::RootsListResult> list_roots() const {
        return peer_.list_roots();
    }

    core::Result<protocol::CreateMessageResult> create_message(
            const protocol::CreateMessageParams& params) const {
        return peer_.create_message(params);
    }

    core::Result<protocol::CreateElicitationResult> create_elicitation(
            const protocol::CreateElicitationRequestParam& params) const {
        return peer_.create_elicitation(params);
    }

    core::Result<std::vector<protocol::Task>> list_tasks() const {
        return peer_.list_tasks();
    }

    core::Result<std::vector<protocol::Task>> list_all_tasks() const {
        return peer_.list_all_tasks();
    }

    core::Result<protocol::Task> get_task(std::string_view task_id) const {
        return peer_.get_task(task_id);
    }

    core::Result<protocol::Task> cancel_task(std::string_view task_id) const {
        return peer_.cancel_task(task_id);
    }

    core::Result<protocol::Json> task_result(std::string_view task_id) const {
        return peer_.task_result(task_id);
    }

    core::Result<core::Unit> notify_elicitation_complete(std::string elicitation_id) const {
        return peer_.notify_elicitation_complete(std::move(elicitation_id));
    }

private:
    server::ClientPeer peer_;
};

using ClientPeer = Peer<RoleClient>;
using ServerPeer = Peer<RoleServer>;

inline ClientPeer make_peer(client::Client& client) noexcept {
    return ClientPeer(client);
}

inline ServerPeer make_peer(server::ClientPeer peer) noexcept {
    return ServerPeer(std::move(peer));
}

inline ServerPeer make_peer(const server::SessionContext& context) noexcept {
    return ServerPeer(context);
}

} // namespace mcp
