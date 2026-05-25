// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Role-aware peer facades for MCP client and server SDK users.
///
/// Peer<RoleClient> and Peer<RoleServer> are thin SDK-facing facades over the
/// existing client and server implementations. They make the public surface
/// read like a peer-oriented MCP SDK while keeping the current concrete classes
/// as the implementation layer.

#include <memory>
#include <string_view>
#include <utility>

#include "cxxmcp/client/client.hpp"
#include "cxxmcp/client/session.hpp"
#include "cxxmcp/handler.hpp"
#include "cxxmcp/roles.hpp"
#include "cxxmcp/server/peer.hpp"
#include "cxxmcp/server/server.hpp"

namespace mcp {

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

  core::Result<core::Unit> raw_notification(
      const protocol::JsonRpcNotification& notification) {
    return client_.raw_notification(notification);
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

  core::Result<core::Unit> start() { return server_->start(); }

  void stop() noexcept { server_->stop(); }

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

 private:
  std::unique_ptr<server::Server> server_;
};

using ClientPeer = Peer<RoleClient>;
using ServerPeer = Peer<RoleServer>;

}  // namespace mcp
