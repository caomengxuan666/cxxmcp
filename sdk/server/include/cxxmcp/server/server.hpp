// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief High-level server compatibility API, builder, and convenience app
/// API.
///
/// Server owns registries for MCP tools, prompts, resources, and resource
/// templates. Request handlers return core::Result<T> to surface protocol
/// errors without changing the ABI. Notifications and server-to-client requests
/// are dispatched through the installed transports.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/capabilities.hpp"
#include "cxxmcp/protocol/logging.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/server/auth.hpp"
#include "cxxmcp/server/context.hpp"
#include "cxxmcp/server/rate_limit.hpp"
#include "cxxmcp/server/registry.hpp"
#include "cxxmcp/server/schema_validator.hpp"
#include "cxxmcp/server/task_manager.hpp"
#include "cxxmcp/server/transport.hpp"

namespace mcp::server {

/// @brief Configuration used to construct a Server.
struct ServerOptions {
  /// Capabilities advertised in the initialize response.
  protocol::ServerCapabilities capabilities;

  /// Server name advertised to clients.
  std::string server_name = "cxxmcp";

  /// Server version advertised to clients.
  std::string server_version = "2.0.0";

  /// Optional human-readable instructions advertised to clients.
  std::string instructions;
};

/// @brief Public server metadata returned by Server::get_info().
struct ServerInfo {
  /// Advertised server name.
  std::string name;

  /// Advertised server version.
  std::string version;

  /// Advertised server instructions.
  std::string instructions;
};

/// @brief Aggregate callback set installable on a Server with
/// Server::set_handler().
struct ServerHandler;
struct ServerHandlerInterface;

/// @brief High-level MCP server compatibility API.
///
/// Server owns the configured transports and registries. Register tools,
/// prompts, resources, and resource templates before start(), then use
/// transports to serve JSON-RPC requests. Direct list/get/call/read methods are
/// also available for tests, embedded use, and custom routing. New
/// applications should prefer ServerPeer and Service as their entry points;
/// Server remains a stable embeddable layer and compatibility surface.
class Server {
 public:
  /// @brief Constructs a server from explicit options.
  /// @param options Server capabilities, name, version, and instructions.
  explicit Server(ServerOptions options);

  /// @brief Stops transports and task workers before owned registries vanish.
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) noexcept;
  Server& operator=(Server&&) noexcept;

  /// @brief Returns server metadata advertised during initialization.
  ServerInfo get_info() const;

  /// @brief Returns the capabilities advertised to clients.
  const protocol::ServerCapabilities& capabilities() const noexcept;

  /// @brief Returns the mutable tool registry.
  ToolRegistry& tools() noexcept;

  /// @brief Returns the tool registry.
  const ToolRegistry& tools() const noexcept;

  /// @brief Lists registered tool definitions.
  std::vector<protocol::ToolDefinition> list_tools() const;

  /// @brief Handles a paginated tools/list request.
  core::Result<protocol::ToolsListResult> list_tools(
      const protocol::PaginatedRequestParams& params,
      const SessionContext& context) const;

  /// @brief Gets a registered tool definition by name.
  core::Result<protocol::ToolDefinition> get_tool(std::string_view name) const;

  /// @brief Invokes a registered tool.
  /// @param name Registered tool name.
  /// @param arguments JSON arguments supplied to the tool handler.
  /// @param session_id Optional session identifier passed into the tool
  /// context.
  /// @return Tool result, or an MCP error if the tool is missing or fails.
  core::Result<protocol::ToolResult> call_tool(
      std::string_view name,
      protocol::Json arguments = protocol::Json::object(),
      const std::string& session_id = {}) const;
  core::Result<protocol::ToolResult> call_tool(
      std::string_view name, protocol::Json arguments,
      const SessionContext& context,
      CancellationToken cancellation = CancellationToken::none()) const;

  /// @brief Enables the built-in SDK task processor.
  Server& use_task_manager(TaskOperationProcessorOptions options = {});

  /// @brief Installs a caller-owned task processor shared with this server.
  Server& use_task_manager(std::shared_ptr<TaskOperationProcessor> processor);

  /// @brief Returns the installed task processor, if any.
  std::shared_ptr<TaskOperationProcessor> task_manager() const noexcept;

  /// @brief Returns the mutable prompt registry.
  PromptRegistry& prompts() noexcept;

  /// @brief Returns the prompt registry.
  const PromptRegistry& prompts() const noexcept;

  /// @brief Lists registered prompts.
  std::vector<protocol::Prompt> list_prompts() const;

  /// @brief Handles a paginated prompts/list request.
  core::Result<protocol::PromptsListResult> list_prompts(
      const protocol::PaginatedRequestParams& params,
      const SessionContext& context) const;

  /// @brief Gets a registered prompt result.
  /// @param name Registered prompt name.
  /// @param arguments JSON arguments supplied to the prompt handler.
  /// @param session_id Optional session identifier passed into the prompt
  /// context.
  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view name,
      protocol::Json arguments = protocol::Json::object(),
      const std::string& session_id = {}) const;
  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view name, protocol::Json arguments,
      const SessionContext& context,
      CancellationToken cancellation = CancellationToken::none()) const;

  /// @brief Returns the mutable resource registry.
  ResourceRegistry& resources() noexcept;

  /// @brief Returns the resource registry.
  const ResourceRegistry& resources() const noexcept;

  /// @brief Lists registered resources.
  std::vector<protocol::Resource> list_resources() const;

  /// @brief Handles a paginated resources/list request.
  core::Result<protocol::ResourcesListResult> list_resources(
      const protocol::PaginatedRequestParams& params,
      const SessionContext& context) const;

  /// @brief Reads a registered resource.
  /// @param uri Registered resource URI.
  /// @param params Optional JSON parameters supplied to the resource handler.
  /// @param session_id Optional session identifier passed into the resource
  /// context.
  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri, protocol::Json params = protocol::Json::object(),
      const std::string& session_id = {}) const;
  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri, protocol::Json params,
      const SessionContext& context,
      CancellationToken cancellation = CancellationToken::none()) const;

  /// @brief Returns the mutable resource-template registry.
  ResourceTemplateRegistry& resource_templates() noexcept;

  /// @brief Returns the resource-template registry.
  const ResourceTemplateRegistry& resource_templates() const noexcept;

  /// @brief Lists registered resource templates.
  std::vector<protocol::ResourceTemplate> list_resource_templates() const;

  /// @brief Handles a paginated resources/templates/list request.
  core::Result<protocol::ResourceTemplatesListResult> list_resource_templates(
      const protocol::PaginatedRequestParams& params,
      const SessionContext& context) const;

  /// @brief Builds the JSON result for an initialize request.
  core::Result<protocol::Json> initialize();

  /// @brief Handles a ping request.
  /// @param context Session context for the requester.
  core::Result<protocol::Json> ping(const SessionContext& context = {});

  /// @brief Dispatches an inbound JSON-RPC request from a transport.
  /// @param request Incoming request.
  /// @param context Session and transport context associated with the request.
  /// @return JSON-RPC response, or an error before response construction.
  core::Result<protocol::JsonRpcResponse> handle_request(
      const protocol::JsonRpcRequest& request, const SessionContext& context);

  /// @brief Applies the configured AuthProvider to a session context.
  ///
  /// Returns the input context unchanged when no provider is configured or the
  /// context already contains an authenticated identity. ServerPeer uses this
  /// hook before native dispatch so peer-boundary handlers cannot bypass
  /// server auth policy.
  core::Result<SessionContext> authenticate_context(
      const SessionContext& context);

  /// @brief Dispatches an inbound JSON-RPC notification from a transport.
  core::Result<core::Unit> handle_notification(
      const protocol::JsonRpcNotification& notification,
      const SessionContext& context);

  /// @brief Notifies connected clients that roots changed.
  core::Result<core::Unit> notify_roots_list_changed();

  /// @brief Notifies connected clients that the tool list changed.
  core::Result<core::Unit> notify_tool_list_changed();

  /// @brief Notifies connected clients that the prompt list changed.
  core::Result<core::Unit> notify_prompt_list_changed();

  /// @brief Notifies connected clients that the resource list changed.
  core::Result<core::Unit> notify_resource_list_changed();

  /// @brief Notifies subscribed clients that a resource URI was updated.
  /// @param uri Resource URI that changed.
  core::Result<core::Unit> notify_resource_updated(std::string_view uri);

  /// @brief Updates resource subscription routing for a transport session.
  core::Result<core::Unit> set_resource_subscription(
      const SessionContext& context, std::string_view uri, bool subscribed);

  /// @brief Sends a progress notification to connected clients.
  core::Result<core::Unit> notify_progress(
      const protocol::ProgressNotificationParams& params);

  /// @brief Sends a logging message notification to connected clients.
  core::Result<core::Unit> notify_logging_message(
      const protocol::LoggingMessageNotificationParams& params);

  /// @brief Sends an elicitation-complete notification.
  /// @param elicitation_id Identifier of the completed elicitation request.
  core::Result<core::Unit> notify_elicitation_complete(
      std::string elicitation_id);

  /// @brief Sends a task status notification.
  core::Result<core::Unit> notify_task_status(const protocol::Task& task);

  /// @brief Sends a server-to-client request to create a new task.
  ///
  /// This lets the server proactively create a task on the client, for example
  /// when a long-running background job is initiated server-side.  The returned
  /// CreateTaskResult carries the client-accepted task snapshot.
  core::Result<protocol::CreateTaskResult> enqueue_task(
      const protocol::Task& task);

  /// @brief Adds a transport owned by the server.
  /// @param transport Transport to start when start() is called. It must not be
  /// null.
  core::Result<core::Unit> add_transport(std::unique_ptr<Transport> transport);

  /// @brief Registers a borrowed session transport for outbound server events.
  ///
  /// The server does not own, start, or stop this transport. Peer/Service
  /// uses this low-level hook to keep server-initiated notifications and
  /// resource subscription routing working when the receive loop is driven by a
  /// role-generic transport.
  core::Result<core::Unit> add_session_transport(Transport& transport);

  /// @brief Installs an authentication provider used by supported transports.
  void set_auth_provider(std::unique_ptr<AuthProvider> auth_provider);

  /// @brief Installs a rate limiter used by supported transports.
  void set_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter);

  /// @brief Installs an optional JSON Schema validator.
  void set_schema_validator(
      std::shared_ptr<const JsonSchemaValidator> validator);

  /// @brief Returns the configured JSON Schema validator, if any.
  std::shared_ptr<const JsonSchemaValidator> schema_validator() const noexcept {
    return schema_validator_;
  }

  /// @brief Starts every registered transport.
  core::Result<core::Unit> start();

  /// @brief Stops all transports.
  void stop() noexcept;

  /// @brief Handles JSON-shaped extension requests such as completion or
  /// sampling.
  using JsonHandler =
      std::function<core::Result<protocol::Json>(const protocol::Json&)>;
  using JsonContextHandler = std::function<core::Result<protocol::Json>(
      const protocol::Json&, const SessionContext&)>;
  using JsonRequestContextHandler = std::function<core::Result<protocol::Json>(
      const protocol::Json&, const SessionContext&, CancellationToken)>;

  /// @brief Handles logging messages from clients.
  using LoggingHandler =
      std::function<void(std::string_view, std::string_view)>;

  /// @brief Optionally handles raw or custom requests before built-in fallback.
  /// @return A response when handled, or std::nullopt to let normal dispatch
  /// continue.
  using RawRequestHandler =
      std::function<std::optional<protocol::JsonRpcResponse>(
          const protocol::JsonRpcRequest&, const SessionContext&)>;
  using RawRequestContextHandler =
      std::function<std::optional<protocol::JsonRpcResponse>(
          const protocol::JsonRpcRequest&, const SessionContext&,
          CancellationToken)>;

  /// @brief Handles raw or custom notifications.
  using RawNotificationHandler = std::function<core::Result<core::Unit>(
      const protocol::JsonRpcNotification&, const SessionContext&)>;

  /// @brief Handles task list requests.
  using TaskListHandler = std::function<core::Result<protocol::TaskListResult>(
      const protocol::TaskListParams&, const SessionContext&)>;

  /// @brief Handles tools/list requests.
  using ToolsListHandler =
      std::function<core::Result<protocol::ToolsListResult>(
          const protocol::PaginatedRequestParams&, const SessionContext&)>;

  /// @brief Handles prompts/list requests.
  using PromptsListHandler =
      std::function<core::Result<protocol::PromptsListResult>(
          const protocol::PaginatedRequestParams&, const SessionContext&)>;

  /// @brief Handles resources/list requests.
  using ResourcesListHandler =
      std::function<core::Result<protocol::ResourcesListResult>(
          const protocol::PaginatedRequestParams&, const SessionContext&)>;

  /// @brief Handles resources/templates/list requests.
  using ResourceTemplatesListHandler =
      std::function<core::Result<protocol::ResourceTemplatesListResult>(
          const protocol::PaginatedRequestParams&, const SessionContext&)>;

  /// @brief Handles task get requests.
  using TaskGetHandler = std::function<core::Result<protocol::Task>(
      const protocol::TaskGetParams&, const SessionContext&)>;

  /// @brief Handles task cancel requests.
  using TaskCancelHandler = std::function<core::Result<protocol::Task>(
      const protocol::TaskCancelParams&, const SessionContext&)>;

  /// @brief Handles task result requests.
  using TaskResultHandler = std::function<core::Result<protocol::Json>(
      const protocol::TaskResultParams&, const SessionContext&)>;

  /// @brief Handles client roots-list-changed notifications.
  using RootsListChangedHandler =
      std::function<core::Result<core::Unit>(const SessionContext&)>;

  /// @brief Handles client progress notifications.
  using ProgressHandler = std::function<core::Result<core::Unit>(
      const protocol::ProgressNotificationParams&, const SessionContext&)>;

  /// @brief Handles client list-changed notifications.
  using ListChangedHandler =
      std::function<core::Result<core::Unit>(const SessionContext&)>;

  /// @brief Handles client resource-updated notifications.
  using ResourceUpdatedHandler = std::function<core::Result<core::Unit>(
      const std::string& uri, const SessionContext&)>;

  /// @brief Registers the completion request handler.
  void set_completion_handler(JsonHandler handler);
  void set_completion_handler(JsonContextHandler handler);
  void set_completion_handler(JsonRequestContextHandler handler);

  /// @brief Registers the sampling request handler.
  void set_sampling_handler(JsonHandler handler);
  void set_sampling_handler(JsonContextHandler handler);
  void set_sampling_handler(JsonRequestContextHandler handler);

  /// @brief Registers the logging notification handler.
  void set_logging_handler(LoggingHandler handler);

  /// @brief Registers a raw request hook.
  void set_raw_request_handler(RawRequestHandler handler);
  void set_raw_request_handler(RawRequestContextHandler handler);

  /// @brief Returns the registered raw request handler, if any.
  const RawRequestHandler& raw_request_handler() const noexcept {
    return raw_request_handler_;
  }

  /// @brief Returns the registered raw request context handler, if any.
  const RawRequestContextHandler& raw_request_context_handler() const noexcept {
    return raw_request_context_handler_;
  }

  /// @brief Registers a raw notification hook.
  void set_raw_notification_handler(RawNotificationHandler handler);

  /// @brief Registers a custom request handler.
  void set_custom_request_handler(RawRequestHandler handler);
  void set_custom_request_handler(RawRequestContextHandler handler);

  /// @brief Registers a custom notification handler.
  void set_custom_notification_handler(RawNotificationHandler handler);

  /// @brief Registers a task list request handler.
  void set_task_list_handler(TaskListHandler handler);

  /// @brief Registers a tools/list request handler.
  void set_tools_list_handler(ToolsListHandler handler);

  /// @brief Registers a prompts/list request handler.
  void set_prompts_list_handler(PromptsListHandler handler);

  /// @brief Registers a resources/list request handler.
  void set_resources_list_handler(ResourcesListHandler handler);

  /// @brief Registers a resources/templates/list request handler.
  void set_resource_templates_list_handler(
      ResourceTemplatesListHandler handler);

  /// @brief Registers a task get request handler.
  void set_task_get_handler(TaskGetHandler handler);

  /// @brief Registers a task cancel request handler.
  void set_task_cancel_handler(TaskCancelHandler handler);

  /// @brief Registers a task result request handler.
  void set_task_result_handler(TaskResultHandler handler);

  /// @brief Registers a progress notification handler.
  void set_progress_handler(ProgressHandler handler);

  /// @brief Registers a roots-list-changed notification handler.
  void set_roots_list_changed_handler(RootsListChangedHandler handler);

  /// @brief Registers a tool-list-changed notification handler.
  void set_tool_list_changed_handler(ListChangedHandler handler);

  /// @brief Registers a prompt-list-changed notification handler.
  void set_prompt_list_changed_handler(ListChangedHandler handler);

  /// @brief Registers a resource-list-changed notification handler.
  void set_resource_list_changed_handler(ListChangedHandler handler);

  /// @brief Registers a resource-updated notification handler.
  void set_resource_updated_handler(ResourceUpdatedHandler handler);

  /// @brief Installs every non-empty callback from a ServerHandler aggregate.
  /// @param handler Callback aggregate. Empty members leave existing callbacks
  /// unchanged.
  /// @return Reference to this server for chaining.
  Server& set_handler(const ServerHandler& handler);

  /// @brief Installs callbacks from a borrowed ServerHandlerInterface contract.
  /// @param handler Handler contract. The caller must keep it alive until all
  /// installed callbacks are replaced or the Server is destroyed.
  /// Unimplemented methods fall back to the server's built-in dispatch or a
  /// method-not-found response.
  Server& set_handler(const ServerHandlerInterface& handler);

  /// @brief Installs callbacks from an owned ServerHandlerInterface contract.
  /// @param handler Shared handler contract kept alive by installed callbacks.
  /// Unimplemented methods fall back to the server's built-in dispatch or a
  /// method-not-found response.
  /// @throws std::invalid_argument if handler is null.
  Server& set_handler(std::shared_ptr<const ServerHandlerInterface> handler);

 private:
  template <typename Handler>
  Handler copy_handler(Handler Server::* slot) const {
    std::lock_guard<std::mutex> lock(*handlers_mutex_);
    return this->*slot;
  }

  template <typename Handler>
  void store_handler(Handler Server::* slot, Handler handler) {
    std::lock_guard<std::mutex> lock(*handlers_mutex_);
    this->*slot = std::move(handler);
  }

  ServerOptions options_;
  ToolRegistry tools_;
  PromptRegistry prompts_;
  ResourceRegistry resources_;
  ResourceTemplateRegistry resource_templates_;
  std::shared_ptr<TaskOperationProcessor> task_processor_;
  std::unique_ptr<AuthProvider> auth_provider_;
  std::unique_ptr<RateLimiter> rate_limiter_;
  std::shared_ptr<const JsonSchemaValidator> schema_validator_;
  std::vector<std::unique_ptr<Transport>> transports_;
  JsonRequestContextHandler completion_handler_;
  JsonRequestContextHandler sampling_handler_;
  LoggingHandler logging_handler_;
  RawRequestHandler raw_request_handler_;
  RawRequestContextHandler raw_request_context_handler_;
  RawNotificationHandler raw_notification_handler_;
  ToolsListHandler tools_list_handler_;
  PromptsListHandler prompts_list_handler_;
  ResourcesListHandler resources_list_handler_;
  ResourceTemplatesListHandler resource_templates_list_handler_;
  TaskListHandler task_list_handler_;
  TaskGetHandler task_get_handler_;
  TaskCancelHandler task_cancel_handler_;
  TaskResultHandler task_result_handler_;
  ProgressHandler progress_handler_;
  RootsListChangedHandler roots_list_changed_handler_;
  ListChangedHandler tool_list_changed_handler_;
  ListChangedHandler prompt_list_changed_handler_;
  ListChangedHandler resource_list_changed_handler_;
  ResourceUpdatedHandler resource_updated_handler_;
  mutable std::shared_ptr<std::mutex> handlers_mutex_ =
      std::make_shared<std::mutex>();
  std::unordered_map<Transport*, std::unordered_set<std::string>>
      resource_subscriptions_;
  std::vector<Transport*> session_transports_;
  std::unordered_set<Transport*> session_transport_set_;
  std::shared_ptr<std::mutex> subscriptions_mutex_ =
      std::make_shared<std::mutex>();

  core::Result<core::Unit> broadcast_notification(
      const protocol::JsonRpcNotification& notification);
  core::Result<core::Unit> notify_resource_subscribers(
      std::string_view uri, const protocol::JsonRpcNotification& notification);
  CancellationToken begin_request_cancellation(
      const protocol::RequestId& request_id);
  void end_request_cancellation(const protocol::RequestId& request_id) noexcept;
  void cancel_request(const protocol::RequestId& request_id) noexcept;

  std::shared_ptr<std::mutex> active_request_cancellations_mutex_ =
      std::make_shared<std::mutex>();
  std::unordered_map<std::string, CancellationSource>
      active_request_cancellations_;
};

/// @brief Fluent builder for constructing a configured Server.
///
/// Registrations are accumulated until build(). build() creates a Server,
/// transfers owned transports/providers, registers tools, prompts, resources,
/// and templates, then applies configured handler overrides.
class ServerBuilder {
 public:
  /// @brief Sets the advertised server name.
  ServerBuilder& name(std::string value);

  /// @brief Sets the advertised server version.
  ServerBuilder& version(std::string value);

  /// @brief Sets the advertised server instructions.
  ServerBuilder& instructions(std::string value);

  /// @brief Replaces the capabilities advertised during initialization.
  ServerBuilder& with_capabilities(protocol::ServerCapabilities capabilities);

  /// @brief Adds a transport to be owned by the built server.
  ServerBuilder& with_transport(std::unique_ptr<Transport> transport);

  /// @brief Sets the authentication provider owned by the built server.
  ServerBuilder& with_auth_provider(
      std::unique_ptr<AuthProvider> auth_provider);

  /// @brief Sets the rate limiter owned by the built server.
  ServerBuilder& with_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter);

  /// @brief Sets the optional JSON Schema validator used by the built server.
  ServerBuilder& with_schema_validator(
      std::shared_ptr<const JsonSchemaValidator> validator);

  /// @brief Enables the built-in SDK task processor on the built server.
  ServerBuilder& with_task_manager(TaskOperationProcessorOptions options = {});

  /// @brief Uses an explicit task processor on the built server.
  ServerBuilder& with_task_manager(
      std::shared_ptr<TaskOperationProcessor> processor);

  /// @brief Registers a tool definition and handler.
  /// @param definition Tool metadata advertised by list_tools().
  /// @param handler Handler invoked when the tool is called.
  /// @return Reference to this builder for chaining.
  /// @note The registration is applied during build(); duplicate or invalid
  /// names are reported in the build() result.
  ServerBuilder& add_tool(protocol::ToolDefinition definition,
                          ToolHandler handler);

  /// @brief Registers a prompt and handler.
  /// @param prompt Prompt metadata advertised by list_prompts().
  /// @param handler Handler invoked when the prompt is requested.
  ServerBuilder& add_prompt(protocol::Prompt prompt, PromptHandler handler);

  /// @brief Registers a resource and read handler.
  /// @param resource Resource metadata advertised by list_resources().
  /// @param handler Handler invoked when the resource URI is read.
  ServerBuilder& add_resource(protocol::Resource resource,
                              ResourceReadHandler handler);

  /// @brief Registers a resource template.
  /// @param resource_template Template metadata advertised by
  /// list_resource_templates().
  ServerBuilder& add_resource_template(
      protocol::ResourceTemplate resource_template);

  /// @brief Applies a composable router object to this builder.
  template <class Router>
  ServerBuilder& with_router(const Router& router) {
    return router.apply_to(*this);
  }

  /// @brief Sets the completion request handler.
  ServerBuilder& on_completion(Server::JsonHandler handler);
  ServerBuilder& on_completion(Server::JsonContextHandler handler);
  ServerBuilder& on_completion(Server::JsonRequestContextHandler handler);

  /// @brief Sets the sampling request handler.
  ServerBuilder& on_sampling(Server::JsonHandler handler);
  ServerBuilder& on_sampling(Server::JsonContextHandler handler);
  ServerBuilder& on_sampling(Server::JsonRequestContextHandler handler);

  /// @brief Sets the logging notification handler.
  ServerBuilder& on_logging(Server::LoggingHandler handler);

  /// @brief Sets the raw request hook.
  ServerBuilder& on_raw_request(Server::RawRequestHandler handler);

  /// @brief Sets the raw notification hook.
  ServerBuilder& on_raw_notification(Server::RawNotificationHandler handler);

  /// @brief Sets the custom request handler.
  ServerBuilder& on_custom_request(Server::RawRequestHandler handler);

  /// @brief Sets the custom notification handler.
  ServerBuilder& on_custom_notification(Server::RawNotificationHandler handler);

  /// @brief Sets the task list handler.
  ServerBuilder& on_task_list(Server::TaskListHandler handler);

  /// @brief Sets the tools/list handler.
  ServerBuilder& on_tools_list(Server::ToolsListHandler handler);

  /// @brief Sets the prompts/list handler.
  ServerBuilder& on_prompts_list(Server::PromptsListHandler handler);

  /// @brief Sets the resources/list handler.
  ServerBuilder& on_resources_list(Server::ResourcesListHandler handler);

  /// @brief Sets the resources/templates/list handler.
  ServerBuilder& on_resource_templates_list(
      Server::ResourceTemplatesListHandler handler);

  /// @brief Sets the task get handler.
  ServerBuilder& on_task_get(Server::TaskGetHandler handler);

  /// @brief Sets the task cancel handler.
  ServerBuilder& on_task_cancel(Server::TaskCancelHandler handler);

  /// @brief Sets the task result handler.
  ServerBuilder& on_task_result(Server::TaskResultHandler handler);

  /// @brief Sets the progress notification handler.
  ServerBuilder& on_progress(Server::ProgressHandler handler);

  /// @brief Sets the roots-list-changed notification handler.
  ServerBuilder& on_roots_list_changed(Server::RootsListChangedHandler handler);

  /// @brief Sets the tool-list-changed notification handler.
  ServerBuilder& on_tool_list_changed(Server::ListChangedHandler handler);

  /// @brief Sets the prompt-list-changed notification handler.
  ServerBuilder& on_prompt_list_changed(Server::ListChangedHandler handler);

  /// @brief Sets the resource-list-changed notification handler.
  ServerBuilder& on_resource_list_changed(Server::ListChangedHandler handler);

  /// @brief Sets the resource-updated notification handler.
  ServerBuilder& on_resource_updated(Server::ResourceUpdatedHandler handler);

  /// @brief Installs every non-empty callback from a handler aggregate.
  ServerBuilder& with_handler(ServerHandler handler);

  /// @brief Installs callbacks from a borrowed contract-style handler.
  ///
  /// The referenced handler must outlive the built server because installed
  /// callbacks delegate to it through a non-owning compatibility wrapper.
  ServerBuilder& with_handler(const ServerHandlerInterface& handler);

  /// @brief Installs callbacks from an owned contract-style handler.
  ///
  /// The shared handler is retained by the built server callbacks.
  ServerBuilder& with_handler(
      std::shared_ptr<const ServerHandlerInterface> handler);

  /// @brief Builds a configured server.
  /// @return Owned server on success, or the first registration/configuration
  /// error.
  core::Result<std::unique_ptr<Server>> build();

 private:
  using ServerRegistration = std::function<core::Result<core::Unit>(Server&)>;

  ServerOptions options_;
  std::unique_ptr<AuthProvider> auth_provider_;
  std::unique_ptr<RateLimiter> rate_limiter_;
  std::shared_ptr<const JsonSchemaValidator> schema_validator_;
  std::shared_ptr<TaskOperationProcessor> task_processor_;
  std::vector<std::unique_ptr<Transport>> transports_;
  std::vector<ServerRegistration> registrations_;
  Server::JsonRequestContextHandler completion_handler_;
  Server::JsonRequestContextHandler sampling_handler_;
  Server::LoggingHandler logging_handler_;
  Server::RawRequestHandler raw_request_handler_;
  Server::RawNotificationHandler raw_notification_handler_;
  Server::ToolsListHandler tools_list_handler_;
  Server::PromptsListHandler prompts_list_handler_;
  Server::ResourcesListHandler resources_list_handler_;
  Server::ResourceTemplatesListHandler resource_templates_list_handler_;
  Server::TaskListHandler task_list_handler_;
  Server::TaskGetHandler task_get_handler_;
  Server::TaskCancelHandler task_cancel_handler_;
  Server::TaskResultHandler task_result_handler_;
  Server::ProgressHandler progress_handler_;
  Server::RootsListChangedHandler roots_list_changed_handler_;
  Server::ListChangedHandler tool_list_changed_handler_;
  Server::ListChangedHandler prompt_list_changed_handler_;
  Server::ListChangedHandler resource_list_changed_handler_;
  Server::ResourceUpdatedHandler resource_updated_handler_;
};

}  // namespace mcp::server
