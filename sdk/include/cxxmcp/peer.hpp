// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Role-aware peer facades for MCP client and server SDK users.
///
/// Peer<RoleClient> and Peer<RoleServer> are the SDK-facing MCP execution
/// boundary. They expose role-generic message dispatch loops so
/// Transport<Role> can be the public service boundary while concrete Client and
/// Server types remain lower-level convenience APIs.

#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/client/client.hpp"
#include "cxxmcp/client/session.hpp"
#include "cxxmcp/client/transport_adapter_fwd.hpp"
#include "cxxmcp/error.hpp"
#include "cxxmcp/handler.hpp"
#include "cxxmcp/roles.hpp"
#include "cxxmcp/server/peer.hpp"
#include "cxxmcp/server/server.hpp"
#include "cxxmcp/server/transport_adapter.hpp"
#include "cxxmcp/server/transport_adapter_fwd.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp {

namespace detail {

inline core::Error peer_dispatch_error(std::string_view message) {
  return errors::make(protocol::ErrorCode::InvalidRequest,
                      std::string(message));
}

inline protocol::ErrorObject peer_error_object_from_core_error(
    const core::Error& error) {
  return errors::to_json_rpc_error(error);
}

template <class Transport, class Dispatch>
inline core::Result<core::Unit> serve_transport_loop(
    Transport& transport, CancellationToken cancellation, Dispatch dispatch) {
  while (!cancellation.cancelled()) {
    auto received = transport.receive();
    if (!received) {
      return std::unexpected(received.error());
    }
    if (!received->has_value()) {
      return core::Unit{};
    }

    auto dispatched = dispatch(received->value());
    if (!dispatched) {
      return std::unexpected(dispatched.error());
    }
    if (dispatched->has_value()) {
      auto sent = transport.send(std::move(dispatched->value()));
      if (!sent) {
        return std::unexpected(sent.error());
      }
    }
  }
  return core::Unit{};
}

inline void keep_first_service_error(std::optional<core::Error>& first_error,
                                     std::mutex& mutex,
                                     core::Error error) noexcept {
  std::lock_guard lock(mutex);
  if (!first_error.has_value()) {
    first_error = std::move(error);
  }
}

}  // namespace detail

/// @brief Role-specialized MCP peer facade.
template <class Role>
class Peer;

/// @brief Client-side peer facade for talking to an MCP server.
template <>
class Peer<RoleClient> {
 public:
  /// @brief Creates a client peer from an owned transport.
  explicit Peer(std::unique_ptr<client::Transport> transport)
      : client_(std::move(transport)) {}

  /// @brief Creates a client peer from an owned role-generic transport.
  explicit Peer(std::unique_ptr<transport::ClientTransport> transport)
      : client_(client::make_contract_transport_adapter(std::move(transport))) {
  }

  /// @brief Creates a client peer from an existing client implementation.
  explicit Peer(client::Client client) : client_(std::move(client)) {}

  static Peer connect_streamable_http(
      client::Client::StreamableHttpEndpoint endpoint) {
    return Peer(client::Client::connect_streamable_http(std::move(endpoint)));
  }

  static Peer connect_legacy_sse(
      client::Client::StreamableHttpEndpoint endpoint) {
    return Peer(client::Client::connect_legacy_sse(std::move(endpoint)));
  }

  static Peer connect_stdio(client::Client::StdioEndpoint endpoint) {
    return Peer(client::Client::connect_stdio(std::move(endpoint)));
  }

  client::Client& client() noexcept { return client_; }

  const client::Client& client() const noexcept { return client_; }

  core::Result<protocol::Json> initialize(std::string client_name = "cxxmcp",
                                          std::string client_version = "0") {
    return client_.initialize(std::move(client_name),
                              std::move(client_version));
  }

  core::Result<core::Unit> notify_initialized() {
    return client_.notify_initialized();
  }

  core::Result<core::Unit> notify_cancelled(protocol::RequestId request_id,
                                            std::string reason = {}) {
    return client_.notify_cancelled(std::move(request_id), std::move(reason));
  }

  core::Result<core::Unit> notify_progress(
      protocol::ProgressToken progress_token, double progress,
      std::optional<double> total = std::nullopt, std::string message = {}) {
    return client_.notify_progress(std::move(progress_token), progress, total,
                                   std::move(message));
  }

  core::Result<core::Unit> notify_roots_list_changed() {
    return client_.notify_roots_list_changed();
  }

  core::Result<core::Unit> ping() { return client_.ping(); }

  std::vector<protocol::Root> list_roots() const {
    return client_.list_roots();
  }

  Peer& set_roots(std::vector<protocol::Root> roots) {
    client_.set_roots(std::move(roots));
    return *this;
  }

  Peer& set_capabilities(protocol::ClientCapabilities capabilities) {
    client_.set_capabilities(std::move(capabilities));
    return *this;
  }

  core::Result<std::vector<protocol::ToolDefinition>> list_tools() {
    return client_.list_tools();
  }

  core::Result<std::vector<protocol::ToolDefinition>> list_all_tools() {
    return client_.list_all_tools();
  }

  core::Result<protocol::ToolResult> call_tool(const protocol::ToolCall& call) {
    return client_.call_tool(call);
  }

  core::Result<protocol::CreateTaskResult> call_tool_task(
      const protocol::ToolCall& call) {
    return client_.call_tool_task(call);
  }

  core::Result<protocol::ToolResult> call_tool(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object()) {
    return client_.call_raw(name, arguments);
  }

  core::Result<std::vector<protocol::Prompt>> list_prompts() {
    return client_.list_prompts();
  }

  core::Result<std::vector<protocol::Prompt>> list_all_prompts() {
    return client_.list_all_prompts();
  }

  core::Result<protocol::PromptsGetResult> get_prompt(
      const protocol::PromptsGetParams& params) {
    return client_.get_prompt(params);
  }

  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object()) {
    return client_.get_prompt(name, arguments);
  }

  core::Result<std::vector<protocol::Resource>> list_resources() {
    return client_.list_resources();
  }

  core::Result<std::vector<protocol::Resource>> list_all_resources() {
    return client_.list_all_resources();
  }

  core::Result<std::vector<protocol::ResourceTemplate>>
  list_resource_templates() {
    return client_.list_resource_templates();
  }

  core::Result<std::vector<protocol::ResourceTemplate>>
  list_all_resource_templates() {
    return client_.list_all_resource_templates();
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      const protocol::ResourcesReadParams& params) {
    return client_.read_resource(params);
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri) {
    return client_.read_resource(uri);
  }

  core::Result<protocol::CompleteResult> complete(
      const protocol::CompleteParams& request) {
    return client_.complete(request);
  }

  core::Result<protocol::Json> complete(const protocol::Json& request) {
    return client_.complete(request);
  }

  core::Result<protocol::CompletionResult> complete_prompt_argument(
      std::string_view prompt_name, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    return client_.complete_prompt_argument(prompt_name, argument_name,
                                            std::move(current_value),
                                            std::move(context));
  }

  core::Result<protocol::CompletionResult> complete_resource_argument(
      std::string_view uri_template, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    return client_.complete_resource_argument(uri_template, argument_name,
                                              std::move(current_value),
                                              std::move(context));
  }

  core::Result<std::vector<std::string>> complete_prompt_simple(
      std::string_view prompt_name, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    return client_.complete_prompt_simple(prompt_name, argument_name,
                                          std::move(current_value),
                                          std::move(context));
  }

  core::Result<std::vector<std::string>> complete_resource_simple(
      std::string_view uri_template, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    return client_.complete_resource_simple(uri_template, argument_name,
                                            std::move(current_value),
                                            std::move(context));
  }

  core::Result<protocol::CreateMessageResult> create_message(
      const protocol::CreateMessageParams& request) {
    return client_.create_message(request);
  }

  core::Result<protocol::Json> create_message(const protocol::Json& request) {
    return client_.create_message(request);
  }

  core::Result<protocol::CreateElicitationResult> create_elicitation(
      const protocol::CreateElicitationRequestParam& request) {
    return client_.create_elicitation(request);
  }

  core::Result<protocol::Json> create_elicitation(
      const protocol::Json& request) {
    return client_.create_elicitation(request);
  }

  core::Result<std::vector<protocol::Task>> list_tasks() {
    return client_.list_tasks();
  }

  core::Result<std::vector<protocol::Task>> list_all_tasks() {
    return client_.list_all_tasks();
  }

  core::Result<protocol::Task> get_task(std::string_view task_id) {
    return client_.get_task(task_id);
  }

  core::Result<protocol::Task> cancel_task(std::string_view task_id) {
    return client_.cancel_task(task_id);
  }

  core::Result<protocol::Json> task_result(std::string_view task_id) {
    return client_.task_result(task_id);
  }

  core::Result<core::Unit> set_level(std::string_view level) {
    return client_.set_level(level);
  }

  core::Result<core::Unit> subscribe(std::string_view uri) {
    return client_.subscribe(uri);
  }

  core::Result<core::Unit> unsubscribe(std::string_view uri) {
    return client_.unsubscribe(uri);
  }

  core::Result<protocol::Json> raw_request(
      const protocol::JsonRpcRequest& request) {
    return client_.raw_request(request);
  }

  RequestHandle<protocol::Json> request_async(
      std::string method, protocol::Json params = protocol::Json::object(),
      RequestOptions options = {}) {
    return client_.request_async(std::move(method), std::move(params),
                                 std::move(options));
  }

  template <class T, class Parser>
  RequestHandle<T> request_async(std::string method, protocol::Json params,
                                 Parser parser, RequestOptions options = {}) {
    return client_.request_async<T>(std::move(method), std::move(params),
                                    std::move(parser), std::move(options));
  }

  RequestHandle<std::vector<protocol::ToolDefinition>> list_tools_async(
      RequestOptions options = {}) {
    return client_.list_tools_async(std::move(options));
  }

  RequestHandle<std::vector<protocol::Prompt>> list_prompts_async(
      RequestOptions options = {}) {
    return client_.list_prompts_async(std::move(options));
  }

  RequestHandle<std::vector<protocol::Resource>> list_resources_async(
      RequestOptions options = {}) {
    return client_.list_resources_async(std::move(options));
  }

  RequestHandle<std::vector<protocol::ResourceTemplate>>
  list_resource_templates_async(RequestOptions options = {}) {
    return client_.list_resource_templates_async(std::move(options));
  }

  RequestHandle<protocol::ToolResult> call_tool_async(
      const protocol::ToolCall& call, RequestOptions options = {}) {
    return client_.call_tool_async(call, std::move(options));
  }

  RequestHandle<protocol::ToolResult> call_tool_async(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object(),
      RequestOptions options = {}) {
    return client_.call_tool_async(name, arguments, std::move(options));
  }

  RequestHandle<protocol::CreateTaskResult> call_tool_task_async(
      const protocol::ToolCall& call, RequestOptions options = {}) {
    return client_.call_tool_task_async(call, std::move(options));
  }

  RequestHandle<protocol::PromptsGetResult> get_prompt_async(
      const protocol::PromptsGetParams& params, RequestOptions options = {}) {
    return client_.get_prompt_async(params, std::move(options));
  }

  RequestHandle<protocol::PromptsGetResult> get_prompt_async(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object(),
      RequestOptions options = {}) {
    return client_.get_prompt_async(name, arguments, std::move(options));
  }

  RequestHandle<protocol::ResourcesReadResult> read_resource_async(
      const protocol::ResourcesReadParams& params,
      RequestOptions options = {}) {
    return client_.read_resource_async(params, std::move(options));
  }

  RequestHandle<protocol::ResourcesReadResult> read_resource_async(
      std::string_view uri, RequestOptions options = {}) {
    return client_.read_resource_async(uri, std::move(options));
  }

  RequestHandle<protocol::CompleteResult> complete_async(
      const protocol::CompleteParams& request, RequestOptions options = {}) {
    return client_.complete_async(request, std::move(options));
  }

  RequestHandle<protocol::Json> complete_async(const protocol::Json& request,
                                               RequestOptions options = {}) {
    return client_.complete_async(request, std::move(options));
  }

  RequestHandle<protocol::CreateMessageResult> create_message_async(
      const protocol::CreateMessageParams& request,
      RequestOptions options = {}) {
    return client_.create_message_async(request, std::move(options));
  }

  RequestHandle<protocol::Json> create_message_async(
      const protocol::Json& request, RequestOptions options = {}) {
    return client_.create_message_async(request, std::move(options));
  }

  RequestHandle<protocol::CreateElicitationResult> create_elicitation_async(
      const protocol::CreateElicitationRequestParam& request,
      RequestOptions options = {}) {
    return client_.create_elicitation_async(request, std::move(options));
  }

  RequestHandle<protocol::Json> create_elicitation_async(
      const protocol::Json& request, RequestOptions options = {}) {
    return client_.create_elicitation_async(request, std::move(options));
  }

  RequestHandle<std::vector<protocol::Task>> list_tasks_async(
      RequestOptions options = {}) {
    return client_.list_tasks_async(std::move(options));
  }

  RequestHandle<protocol::Task> get_task_async(
      const protocol::TaskGetParams& request, RequestOptions options = {}) {
    return client_.get_task_async(request, std::move(options));
  }

  RequestHandle<protocol::Task> get_task_async(std::string_view task_id,
                                               RequestOptions options = {}) {
    return client_.get_task_async(task_id, std::move(options));
  }

  RequestHandle<protocol::Task> cancel_task_async(
      const protocol::TaskCancelParams& request, RequestOptions options = {}) {
    return client_.cancel_task_async(request, std::move(options));
  }

  RequestHandle<protocol::Task> cancel_task_async(std::string_view task_id,
                                                  RequestOptions options = {}) {
    return client_.cancel_task_async(task_id, std::move(options));
  }

  RequestHandle<protocol::Json> task_result_async(
      const protocol::TaskResultParams& request, RequestOptions options = {}) {
    return client_.task_result_async(request, std::move(options));
  }

  RequestHandle<protocol::Json> task_result_async(std::string_view task_id,
                                                  RequestOptions options = {}) {
    return client_.task_result_async(task_id, std::move(options));
  }

  core::Result<core::Unit> raw_notification(
      const protocol::JsonRpcNotification& notification) {
    return client_.raw_notification(notification);
  }

  /// @brief Dispatches one inbound role-generic transport message.
  ///
  /// Requests produce a JSON-RPC response message, notifications produce no
  /// outbound message, and standalone responses remain the responsibility of
  /// request-handle correlation paths.
  core::Result<std::optional<protocol::JsonRpcMessage>> dispatch_message(
      const protocol::JsonRpcMessage& message) {
    if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      auto handled = client_.handle_request(*request);
      if (!handled) {
        return protocol::JsonRpcMessage{protocol::make_error_response(
            request->id,
            detail::peer_error_object_from_core_error(handled.error()))};
      }
      return protocol::JsonRpcMessage{std::move(*handled)};
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      const auto handled = client_.handle_notification(*notification);
      if (!handled) {
        return std::unexpected(handled.error());
      }
      return std::nullopt;
    }

    return std::unexpected(detail::peer_dispatch_error(
        "client peer cannot dispatch an uncorrelated response"));
  }

  /// @brief Runs a sequential receive loop over a role-generic client
  /// transport.
  core::Result<core::Unit> serve_transport(
      transport::ClientTransport& transport,
      CancellationToken cancellation = {}) {
    return detail::serve_transport_loop(
        transport, cancellation,
        [this](const protocol::JsonRpcMessage& message) {
          return dispatch_message(message);
        });
  }

 private:
  client::Client client_;
};

/// @brief Server-side peer facade for exposing MCP capabilities.
template <>
class Peer<RoleServer> {
 public:
  /// @brief Creates a server peer from options.
  explicit Peer(server::ServerOptions options = {})
      : server_(std::make_unique<server::Server>(std::move(options))) {}

  /// @brief Creates a server peer from an owned server implementation.
  explicit Peer(std::unique_ptr<server::Server> server)
      : server_(std::move(server)) {}

  static core::Result<Peer> build(server::ServerBuilder builder) {
    auto built = builder.build();
    if (!built) {
      return std::unexpected(built.error());
    }
    return Peer(std::move(*built));
  }

  server::Server& server() noexcept { return *server_; }

  const server::Server& server() const noexcept { return *server_; }

  server::ServerInfo get_info() const { return server_->get_info(); }

  const protocol::ServerCapabilities& capabilities() const noexcept {
    return server_->capabilities();
  }

  std::vector<protocol::ToolDefinition> list_tools() const {
    return server_->list_tools();
  }

  core::Result<protocol::ToolDefinition> get_tool(std::string_view name) const {
    return server_->get_tool(name);
  }

  core::Result<protocol::ToolResult> call_tool(
      std::string_view name,
      protocol::Json arguments = protocol::Json::object(),
      const std::string& session_id = {}) const {
    return server_->call_tool(name, std::move(arguments), session_id);
  }

  std::vector<protocol::Prompt> list_prompts() const {
    return server_->list_prompts();
  }

  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view name,
      protocol::Json arguments = protocol::Json::object(),
      const std::string& session_id = {}) const {
    return server_->get_prompt(name, std::move(arguments), session_id);
  }

  std::vector<protocol::Resource> list_resources() const {
    return server_->list_resources();
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri, protocol::Json params = protocol::Json::object(),
      const std::string& session_id = {}) const {
    return server_->read_resource(uri, std::move(params), session_id);
  }

  std::vector<protocol::ResourceTemplate> list_resource_templates() const {
    return server_->list_resource_templates();
  }

  core::Result<protocol::Json> initialize() { return server_->initialize(); }

  core::Result<protocol::Json> ping(
      const server::SessionContext& context = {}) {
    return server_->ping(context);
  }

  core::Result<protocol::JsonRpcResponse> handle_request(
      const protocol::JsonRpcRequest& request,
      const server::SessionContext& context = {}) {
    return server_->handle_request(request, context);
  }

  core::Result<core::Unit> handle_notification(
      const protocol::JsonRpcNotification& notification,
      const server::SessionContext& context = {}) {
    return server_->handle_notification(notification, context);
  }

  core::Result<core::Unit> add_transport(
      std::unique_ptr<server::Transport> transport) {
    return server_->add_transport(std::move(transport));
  }

  /// @brief Adds an owned role-generic server transport.
  core::Result<core::Unit> add_transport(
      std::unique_ptr<transport::ServerTransport> transport) {
    if (!transport) {
      return std::unexpected(detail::peer_dispatch_error(
          "server peer transport must not be null"));
    }
    native_transports_.push_back(std::move(transport));
    native_context_transports_.push_back(
        std::make_unique<server::ContractTransportAdapter>(
            *native_transports_.back()));
    const auto attached =
        server_->add_session_transport(*native_context_transports_.back());
    if (!attached) {
      native_context_transports_.pop_back();
      native_transports_.pop_back();
      return std::unexpected(attached.error());
    }
    return core::Unit{};
  }

  core::Result<core::Unit> start(CancellationToken cancellation = {}) {
    if (native_transports_.empty()) {
      return server_->start();
    }

    std::vector<std::thread> workers;
    workers.reserve(native_transports_.size());
    std::mutex error_mutex;
    std::optional<core::Error> first_error;

    for (std::size_t index = 0; index < native_transports_.size(); ++index) {
      auto* transport_ptr = native_transports_[index].get();
      auto* context_transport_ptr = native_context_transports_[index].get();
      workers.emplace_back([this, transport_ptr, context_transport_ptr,
                            cancellation, &error_mutex,
                            &first_error]() noexcept {
        server::SessionContext context;
        context.remote_address = std::string(transport_ptr->name());
        context.transport = context_transport_ptr;
        const auto served =
            serve_transport(*transport_ptr, context, cancellation);
        if (!served) {
          detail::keep_first_service_error(first_error, error_mutex,
                                           served.error());
        }
      });
    }

    for (auto& worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }

    if (first_error.has_value()) {
      return std::unexpected(*first_error);
    }
    return core::Unit{};
  }

  void stop() noexcept {
    for (auto& transport : native_transports_) {
      if (transport) {
        (void)transport->close();
      }
    }
    server_->stop();
  }

  core::Result<core::Unit> notify_roots_list_changed() {
    return server_->notify_roots_list_changed();
  }

  core::Result<core::Unit> notify_tool_list_changed() {
    return server_->notify_tool_list_changed();
  }

  core::Result<core::Unit> notify_prompt_list_changed() {
    return server_->notify_prompt_list_changed();
  }

  core::Result<core::Unit> notify_resource_list_changed() {
    return server_->notify_resource_list_changed();
  }

  core::Result<core::Unit> notify_resource_updated(std::string_view uri) {
    return server_->notify_resource_updated(uri);
  }

  core::Result<core::Unit> notify_progress(
      const protocol::ProgressNotificationParams& params) {
    return server_->notify_progress(params);
  }

  core::Result<core::Unit> notify_logging_message(
      const protocol::LoggingMessageNotificationParams& params) {
    return server_->notify_logging_message(params);
  }

  core::Result<core::Unit> notify_elicitation_complete(
      std::string elicitation_id) {
    return server_->notify_elicitation_complete(std::move(elicitation_id));
  }

  core::Result<core::Unit> notify_task_status(const protocol::Task& task) {
    return server_->notify_task_status(task);
  }

  /// @brief Dispatches one inbound role-generic transport message.
  ///
  /// Requests produce a JSON-RPC response message, notifications produce no
  /// outbound message, and standalone responses remain the responsibility of
  /// request-handle correlation paths.
  core::Result<std::optional<protocol::JsonRpcMessage>> dispatch_message(
      const protocol::JsonRpcMessage& message,
      const server::SessionContext& context = {}) {
    if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      auto handled = server_->handle_request(*request, context);
      if (!handled) {
        return protocol::JsonRpcMessage{protocol::make_error_response(
            request->id,
            detail::peer_error_object_from_core_error(handled.error()))};
      }
      return protocol::JsonRpcMessage{std::move(*handled)};
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      const auto handled = server_->handle_notification(*notification, context);
      if (!handled) {
        return std::unexpected(handled.error());
      }
      return std::nullopt;
    }

    return std::unexpected(detail::peer_dispatch_error(
        "server peer cannot dispatch an uncorrelated response"));
  }

  /// @brief Runs a sequential receive loop over a role-generic server
  /// transport.
  core::Result<core::Unit> serve_transport(
      transport::ServerTransport& transport,
      const server::SessionContext& context = {},
      CancellationToken cancellation = {}) {
    return detail::serve_transport_loop(
        transport, cancellation,
        [this, &context](const protocol::JsonRpcMessage& message) {
          return dispatch_message(message, context);
        });
  }

 private:
  std::unique_ptr<server::Server> server_;
  std::vector<std::unique_ptr<transport::ServerTransport>> native_transports_;
  std::vector<std::unique_ptr<server::ContractTransportAdapter>>
      native_context_transports_;
};

using ClientPeer = Peer<RoleClient>;
using ServerPeer = Peer<RoleServer>;

}  // namespace mcp
