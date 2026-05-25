// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief High-level server facade, builder, and convenience app API.
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
#include "cxxmcp/server/rate_limit.hpp"
#include "cxxmcp/server/registry.hpp"
#include "cxxmcp/server/task_manager.hpp"
#include "cxxmcp/server/transport.hpp"

namespace mcp::server {

namespace detail {

template <class T>
struct is_result : std::false_type {};

template <class T>
struct is_result<core::Result<T>> : std::true_type {};

template <class>
inline constexpr bool always_false_v = false;

template <class T, class = void>
struct callable_argument_types {
  using type = void;
};

template <class Class, class Return, class... Args>
struct callable_argument_types<Return (Class::*)(Args...) const, void> {
  using type = std::tuple<std::decay_t<Args>...>;
};

template <class Class, class Return, class... Args>
struct callable_argument_types<Return (Class::*)(Args...), void> {
  using type = std::tuple<std::decay_t<Args>...>;
};

template <class Return, class... Args>
struct callable_argument_types<Return (*)(Args...), void> {
  using type = std::tuple<std::decay_t<Args>...>;
};

template <class T>
struct callable_argument_types<T, std::void_t<decltype(&T::operator())>>
    : callable_argument_types<decltype(&T::operator())> {};

template <class Handler, class... Args>
inline constexpr bool callable_arguments_match_v = std::is_same_v<
    typename callable_argument_types<std::decay_t<Handler>>::type,
    std::tuple<std::decay_t<Args>...>>;

template <class T>
inline protocol::Json value_to_json(T&& value) {
  using Value = std::decay_t<T>;
  if constexpr (std::is_same_v<Value, protocol::Json>) {
    return std::forward<T>(value);
  } else {
    return protocol::Json(std::forward<T>(value));
  }
}

template <class Handler>
inline bool callable_is_empty(const Handler&) noexcept {
  return false;
}

template <class Return, class... Args>
inline bool callable_is_empty(
    const std::function<Return(Args...)>& handler) noexcept {
  return !handler;
}

template <class Return, class... Args>
inline bool callable_is_empty(Return (*handler)(Args...)) noexcept {
  return handler == nullptr;
}

template <class Handler>
inline void require_callable(const Handler& handler, std::string_view label) {
  if (callable_is_empty(handler)) {
    throw std::invalid_argument(std::string(label) +
                                " handler must not be empty");
  }
}

inline protocol::ToolResult value_to_tool_result(protocol::ToolResult result) {
  return result;
}

inline protocol::ToolResult value_to_tool_result(std::string text) {
  protocol::ToolResult result;
  protocol::ContentBlock block;
  block.type = "text";
  block.text = std::move(text);
  result.content.push_back(std::move(block));
  return result;
}

inline protocol::ToolResult value_to_tool_result(const char* text) {
  return value_to_tool_result(std::string(text == nullptr ? "" : text));
}

template <class T>
inline protocol::ToolResult value_to_tool_result(T&& value) {
  protocol::ToolResult result;
  result.structured_content = value_to_json(std::forward<T>(value));
  protocol::ContentBlock block;
  block.type = "text";
  block.text = result.structured_content->dump();
  result.content.push_back(std::move(block));
  return result;
}

inline protocol::PromptsGetResult value_to_prompt_result(
    protocol::PromptsGetResult result) {
  return result;
}

inline protocol::PromptsGetResult value_to_prompt_result(std::string text) {
  protocol::PromptsGetResult result;
  protocol::ContentBlock block;
  block.type = "text";
  block.text = std::move(text);
  protocol::PromptMessage message;
  message.role = "assistant";
  message.content = std::move(block);
  result.messages.push_back(std::move(message));
  return result;
}

inline protocol::ResourcesReadResult value_to_resource_read_result(
    protocol::ResourcesReadResult result, std::string_view) {
  return result;
}

inline protocol::ResourcesReadResult value_to_resource_read_result(
    protocol::ResourceContents contents, std::string_view) {
  protocol::ResourcesReadResult result;
  result.contents.push_back(std::move(contents));
  return result;
}

inline protocol::ResourcesReadResult value_to_resource_read_result(
    std::string text, std::string_view uri) {
  protocol::ResourcesReadResult result;
  protocol::ResourceContents contents;
  contents.uri = std::string(uri);
  contents.mime_type = "text/plain";
  contents.text = std::move(text);
  result.contents.push_back(std::move(contents));
  return result;
}

template <class Arg>
inline Arg argument_from_json(const protocol::Json& arguments,
                              std::string_view fallback_name = {}) {
  if constexpr (std::is_same_v<std::decay_t<Arg>, protocol::Json>) {
    return arguments;
  } else {
    if (!fallback_name.empty() && arguments.is_object() &&
        arguments.contains(std::string(fallback_name))) {
      return arguments.at(std::string(fallback_name)).get<Arg>();
    }
    if (arguments.is_object() && arguments.size() == 1) {
      return arguments.begin().value().get<Arg>();
    }
    return arguments.get<Arg>();
  }
}

template <class Result>
inline void apply_default_output_schema(protocol::ToolDefinition& definition) {
  if constexpr (!std::is_same_v<std::decay_t<Result>, protocol::ToolResult> &&
                !std::is_same_v<std::decay_t<Result>, std::string> &&
                !std::is_same_v<std::decay_t<Result>, const char*> &&
                !std::is_same_v<std::decay_t<Result>, char*>) {
    if (definition.output_schema.empty()) {
      definition.output_schema = protocol::schema_for<Result>();
    }
  }
}

template <class Handler, class Args>
decltype(auto) invoke_tool_handler(Handler& handler, Args&& args,
                                   const ToolContext& context) {
  using Arg = std::decay_t<Args>;
  if constexpr (std::is_invocable_v<Handler&, Arg, const ToolContext&>) {
    return handler(std::forward<Args>(args), context);
  } else if constexpr (std::is_invocable_v<Handler&, const ToolContext&, Arg>) {
    return handler(context, std::forward<Args>(args));
  } else if constexpr (std::is_invocable_v<Handler&, Arg, CancellationToken>) {
    return handler(std::forward<Args>(args), context.cancellation);
  } else if constexpr (std::is_invocable_v<Handler&, CancellationToken, Arg>) {
    return handler(context.cancellation, std::forward<Args>(args));
  } else if constexpr (std::is_invocable_v<Handler&, Arg>) {
    return handler(std::forward<Args>(args));
  } else if constexpr (std::is_invocable_v<Handler&, const ToolContext&>) {
    return handler(context);
  } else if constexpr (std::is_invocable_v<Handler&>) {
    return handler();
  } else {
    static_assert(always_false_v<Handler>,
                  "tool handler must accept Args, Args+ToolContext, "
                  "ToolContext+Args, Args+CancellationToken, "
                  "CancellationToken+Args, ToolContext, or no arguments");
  }
}

template <class Handler>
decltype(auto) invoke_prompt_handler(Handler& handler,
                                     const PromptContext& context) {
  using Json = protocol::Json;
  if constexpr (callable_arguments_match_v<Handler, Json, PromptContext>) {
    return handler(context.arguments, context);
  } else if constexpr (callable_arguments_match_v<Handler, PromptContext,
                                                  Json>) {
    return handler(context, context.arguments);
  } else if constexpr (callable_arguments_match_v<Handler, std::string,
                                                  PromptContext>) {
    auto text = argument_from_json<std::string>(context.arguments,
                                                std::string_view("text"));
    return handler(std::move(text), context);
  } else if constexpr (callable_arguments_match_v<Handler, PromptContext,
                                                  std::string>) {
    auto text = argument_from_json<std::string>(context.arguments,
                                                std::string_view("text"));
    return handler(context, std::move(text));
  } else if constexpr (callable_arguments_match_v<Handler, Json>) {
    return handler(context.arguments);
  } else if constexpr (callable_arguments_match_v<Handler, std::string>) {
    auto text = argument_from_json<std::string>(context.arguments,
                                                std::string_view("text"));
    return handler(std::move(text));
  } else if constexpr (callable_arguments_match_v<Handler, PromptContext>) {
    return handler(context);
  } else if constexpr (std::is_invocable_v<Handler&, const Json&,
                                           const PromptContext&>) {
    return handler(context.arguments, context);
  } else if constexpr (std::is_invocable_v<Handler&, const PromptContext&,
                                           const Json&>) {
    return handler(context, context.arguments);
  } else if constexpr (std::is_invocable_v<Handler&, std::string,
                                           const PromptContext&>) {
    auto text = argument_from_json<std::string>(context.arguments,
                                                std::string_view("text"));
    return handler(std::move(text), context);
  } else if constexpr (std::is_invocable_v<Handler&, const PromptContext&,
                                           std::string>) {
    auto text = argument_from_json<std::string>(context.arguments,
                                                std::string_view("text"));
    return handler(context, std::move(text));
  } else if constexpr (std::is_invocable_v<Handler&, const Json&>) {
    return handler(context.arguments);
  } else if constexpr (std::is_invocable_v<Handler&, std::string>) {
    auto text = argument_from_json<std::string>(context.arguments,
                                                std::string_view("text"));
    return handler(std::move(text));
  } else if constexpr (std::is_invocable_v<Handler&, const PromptContext&>) {
    return handler(context);
  } else if constexpr (std::is_invocable_v<Handler&>) {
    return handler();
  } else {
    static_assert(always_false_v<Handler>,
                  "prompt handler must accept Json, Json+PromptContext, "
                  "PromptContext+Json, string, string+PromptContext, "
                  "PromptContext+string, PromptContext, or no arguments");
  }
}

template <class Handler>
decltype(auto) invoke_resource_handler(Handler& handler,
                                       const ResourceContext& context) {
  using Json = protocol::Json;
  if constexpr (callable_arguments_match_v<Handler, Json, ResourceContext>) {
    return handler(context.params, context);
  } else if constexpr (callable_arguments_match_v<Handler, ResourceContext,
                                                  Json>) {
    return handler(context, context.params);
  } else if constexpr (callable_arguments_match_v<Handler, std::string,
                                                  ResourceContext>) {
    return handler(context.uri, context);
  } else if constexpr (callable_arguments_match_v<Handler, ResourceContext,
                                                  std::string>) {
    return handler(context, context.uri);
  } else if constexpr (callable_arguments_match_v<Handler, Json>) {
    return handler(context.params);
  } else if constexpr (callable_arguments_match_v<Handler, std::string>) {
    return handler(context.uri);
  } else if constexpr (callable_arguments_match_v<Handler, ResourceContext>) {
    return handler(context);
  } else if constexpr (std::is_invocable_v<Handler&, const Json&,
                                           const ResourceContext&>) {
    return handler(context.params, context);
  } else if constexpr (std::is_invocable_v<Handler&, const ResourceContext&,
                                           const Json&>) {
    return handler(context, context.params);
  } else if constexpr (std::is_invocable_v<Handler&, std::string,
                                           const ResourceContext&>) {
    return handler(context.uri, context);
  } else if constexpr (std::is_invocable_v<Handler&, const ResourceContext&,
                                           std::string>) {
    return handler(context, context.uri);
  } else if constexpr (std::is_invocable_v<Handler&, const Json&>) {
    return handler(context.params);
  } else if constexpr (std::is_invocable_v<Handler&, std::string>) {
    return handler(context.uri);
  } else if constexpr (std::is_invocable_v<Handler&, const ResourceContext&>) {
    return handler(context);
  } else if constexpr (std::is_invocable_v<Handler&>) {
    return handler();
  } else {
    static_assert(always_false_v<Handler>,
                  "resource handler must accept Json, Json+ResourceContext, "
                  "ResourceContext+Json, string, string+ResourceContext, "
                  "ResourceContext+string, ResourceContext, or no arguments");
  }
}

}  // namespace detail

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

/// @brief High-level MCP server facade.
///
/// Server owns the configured transports and registries. Register tools,
/// prompts, resources, and resource templates before start(), then use
/// transports to serve JSON-RPC requests. Direct list/get/call/read methods are
/// also available for tests, embedded use, and custom routing.
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

  /// @brief Gets a registered prompt result.
  /// @param name Registered prompt name.
  /// @param arguments JSON arguments supplied to the prompt handler.
  /// @param session_id Optional session identifier passed into the prompt
  /// context.
  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view name,
      protocol::Json arguments = protocol::Json::object(),
      const std::string& session_id = {}) const;

  /// @brief Returns the mutable resource registry.
  ResourceRegistry& resources() noexcept;

  /// @brief Returns the resource registry.
  const ResourceRegistry& resources() const noexcept;

  /// @brief Lists registered resources.
  std::vector<protocol::Resource> list_resources() const;

  /// @brief Reads a registered resource.
  /// @param uri Registered resource URI.
  /// @param params Optional JSON parameters supplied to the resource handler.
  /// @param session_id Optional session identifier passed into the resource
  /// context.
  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri, protocol::Json params = protocol::Json::object(),
      const std::string& session_id = {}) const;

  /// @brief Returns the mutable resource-template registry.
  ResourceTemplateRegistry& resource_templates() noexcept;

  /// @brief Returns the resource-template registry.
  const ResourceTemplateRegistry& resource_templates() const noexcept;

  /// @brief Lists registered resource templates.
  std::vector<protocol::ResourceTemplate> list_resource_templates() const;

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

  /// @brief Adds a transport owned by the server.
  /// @param transport Transport to start when start() is called. It must not be
  /// null.
  core::Result<core::Unit> add_transport(std::unique_ptr<Transport> transport);

  /// @brief Installs an authentication provider used by supported transports.
  void set_auth_provider(std::unique_ptr<AuthProvider> auth_provider);

  /// @brief Installs a rate limiter used by supported transports.
  void set_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter);

  /// @brief Starts every registered transport.
  core::Result<core::Unit> start();

  /// @brief Stops all transports.
  void stop() noexcept;

  /// @brief Handles JSON-shaped extension requests such as completion or
  /// sampling.
  using JsonHandler =
      std::function<core::Result<protocol::Json>(const protocol::Json&)>;

  /// @brief Handles logging messages from clients.
  using LoggingHandler =
      std::function<void(std::string_view, std::string_view)>;

  /// @brief Optionally handles raw or custom requests before built-in fallback.
  /// @return A response when handled, or std::nullopt to let normal dispatch
  /// continue.
  using RawRequestHandler =
      std::function<std::optional<protocol::JsonRpcResponse>(
          const protocol::JsonRpcRequest&, const SessionContext&)>;

  /// @brief Handles raw or custom notifications.
  using RawNotificationHandler = std::function<core::Result<core::Unit>(
      const protocol::JsonRpcNotification&, const SessionContext&)>;

  /// @brief Handles task list requests.
  using TaskListHandler = std::function<core::Result<protocol::TaskListResult>(
      const protocol::TaskListParams&, const SessionContext&)>;

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

  /// @brief Registers the sampling request handler.
  void set_sampling_handler(JsonHandler handler);

  /// @brief Registers the logging notification handler.
  void set_logging_handler(LoggingHandler handler);

  /// @brief Registers a raw request hook.
  void set_raw_request_handler(RawRequestHandler handler);

  /// @brief Registers a raw notification hook.
  void set_raw_notification_handler(RawNotificationHandler handler);

  /// @brief Registers a custom request handler.
  void set_custom_request_handler(RawRequestHandler handler);

  /// @brief Registers a custom notification handler.
  void set_custom_notification_handler(RawNotificationHandler handler);

  /// @brief Registers a task list request handler.
  void set_task_list_handler(TaskListHandler handler);

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

  /// @brief Installs callbacks from a ServerHandlerInterface contract.
  /// @param handler Handler contract. Unimplemented methods fall back to
  /// the server's built-in dispatch or a method-not-found response.
  Server& set_handler(const ServerHandlerInterface& handler);

 private:
  ServerOptions options_;
  ToolRegistry tools_;
  PromptRegistry prompts_;
  ResourceRegistry resources_;
  ResourceTemplateRegistry resource_templates_;
  std::shared_ptr<TaskOperationProcessor> task_processor_;
  std::unique_ptr<AuthProvider> auth_provider_;
  std::unique_ptr<RateLimiter> rate_limiter_;
  std::vector<std::unique_ptr<Transport>> transports_;
  JsonHandler completion_handler_;
  JsonHandler sampling_handler_;
  LoggingHandler logging_handler_;
  RawRequestHandler raw_request_handler_;
  RawNotificationHandler raw_notification_handler_;
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
  std::unordered_map<const Transport*, std::unordered_set<std::string>>
      resource_subscriptions_;
  std::shared_ptr<std::mutex> subscriptions_mutex_ =
      std::make_shared<std::mutex>();

  core::Result<core::Unit> broadcast_notification(
      const protocol::JsonRpcNotification& notification);
  core::Result<core::Unit> notify_resource_subscribers(
      std::string_view uri, const protocol::JsonRpcNotification& notification);
  core::Result<core::Unit> set_resource_subscription(
      const SessionContext& context, std::string_view uri, bool subscribed);
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

  /// @brief Sets the completion request handler.
  ServerBuilder& on_completion(Server::JsonHandler handler);

  /// @brief Sets the sampling request handler.
  ServerBuilder& on_sampling(Server::JsonHandler handler);

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

  /// @brief Builds a configured server.
  /// @return Owned server on success, or the first registration/configuration
  /// error.
  core::Result<std::unique_ptr<Server>> build();

 private:
  using ServerRegistration = std::function<core::Result<core::Unit>(Server&)>;

  ServerOptions options_;
  std::unique_ptr<AuthProvider> auth_provider_;
  std::unique_ptr<RateLimiter> rate_limiter_;
  std::shared_ptr<TaskOperationProcessor> task_processor_;
  std::vector<std::unique_ptr<Transport>> transports_;
  std::vector<ServerRegistration> registrations_;
  Server::JsonHandler completion_handler_;
  Server::JsonHandler sampling_handler_;
  Server::LoggingHandler logging_handler_;
  Server::RawRequestHandler raw_request_handler_;
  Server::RawNotificationHandler raw_notification_handler_;
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

/// @brief Typed tool registration produced by mcp::server::tool().
template <class Args, class Result, class Handler>
struct TypedToolRegistration {
  protocol::ToolDefinition definition;
  Handler handler;
};

/// @brief Fluent typed tool builder for low-boilerplate server authoring.
template <class Args, class Result>
class TypedToolBuilder {
 public:
  explicit TypedToolBuilder(std::string name) {
    definition_ =
        protocol::tool_definition(std::move(name)).input<Args>().build();
    detail::apply_default_output_schema<Result>(definition_);
  }

  TypedToolBuilder& title(std::string value) {
    definition_.title = std::move(value);
    return *this;
  }

  TypedToolBuilder& description(std::string value) {
    definition_.description = std::move(value);
    return *this;
  }

  TypedToolBuilder& input_schema(protocol::Json schema) {
    definition_.input_schema = std::move(schema);
    return *this;
  }

  TypedToolBuilder& output_schema(protocol::Json schema) {
    definition_.output_schema = std::move(schema);
    return *this;
  }

  TypedToolBuilder& streaming(bool value = true) {
    definition_.streaming = value;
    return *this;
  }

  TypedToolBuilder& icon(protocol::Icon value) {
    definition_.icons.push_back(std::move(value));
    return *this;
  }

  TypedToolBuilder& task_support(protocol::TaskSupport value) {
    if (!definition_.execution.has_value()) {
      definition_.execution = protocol::ToolExecution{};
    }
    definition_.execution->task_support = value;
    return *this;
  }

  TypedToolBuilder& annotations(protocol::Json value) {
    definition_.annotations = std::move(value);
    return *this;
  }

  TypedToolBuilder& meta(protocol::Json value) {
    definition_.meta = std::move(value);
    return *this;
  }

  template <class Handler>
  TypedToolRegistration<Args, Result, Handler> handler(Handler value) {
    detail::require_callable(value, "tool");
    return TypedToolRegistration<Args, Result, Handler>{std::move(definition_),
                                                        std::move(value)};
  }

 private:
  protocol::ToolDefinition definition_;
};

/// @brief Starts a typed tool registration builder.
template <class Args, class Result>
inline TypedToolBuilder<Args, Result> tool(std::string name) {
  return TypedToolBuilder<Args, Result>(std::move(name));
}

/// @brief Convenience entry point for compact server applications.
///
/// App::builder() exposes a higher-level builder that can create common
/// transports and adapt simple C++ callables into MCP handlers.
class App {
 public:
  /// @brief Higher-level server builder with callable adapters.
  class Builder {
   public:
    /// @brief Sets the advertised server name.
    Builder& name(std::string value);

    /// @brief Sets the advertised server version.
    Builder& version(std::string value);

    /// @brief Sets the advertised server instructions.
    Builder& instructions(std::string value);

    /// @brief Adds a stdio server transport.
    Builder& stdio();

    /// @brief Adds a streamable HTTP server transport.
    /// @param host Host/interface to bind.
    /// @param port TCP port to bind.
    /// @param path HTTP path for MCP requests.
    Builder& streamable_http(std::string host, std::uint16_t port,
                             std::string path = "/mcp");

    /// @brief Adds a legacy SSE server transport.
    /// @param host Host/interface to bind.
    /// @param port TCP port to bind.
    /// @param path HTTP path for MCP requests.
    Builder& legacy_sse(std::string host, std::uint16_t port,
                        std::string path = "/mcp");

    /// @brief Adds a caller-supplied transport.
    /// @param value Transport owned by the built server.
    Builder& transport(std::unique_ptr<Transport> value);

    /// @brief Enables server-side task processing for task-aware tools.
    Builder& tasks(TaskOperationProcessorOptions options = {});

    /// @brief Registers a tool using a typed argument adapter.
    /// @tparam Args Type decoded from the JSON arguments object.
    /// @tparam Result Expected handler result type.
    /// @tparam Handler Callable invoked with Args.
    /// @param name Tool name advertised to clients.
    /// @param handler Callable returning Result or core::Result<Result>.
    /// @return Reference to this builder for chaining.
    /// @note std::exception-derived errors thrown by argument decoding are
    /// caught and converted to InvalidParams results when the tool is invoked.
    template <class Args, class Result, class Handler>
    Builder& tool(std::string name, Handler handler);

    /// @brief Registers a typed callable using an explicit tool definition.
    template <class Args, class Result, class Handler>
    Builder& tool(protocol::ToolDefinition definition, Handler handler);

    /// @brief Registers a typed tool registration built by mcp::server::tool().
    template <class Args, class Result, class Handler>
    Builder& tool(TypedToolRegistration<Args, Result, Handler> registration);

    /// @brief Registers a fully described tool and low-level handler.
    Builder& tool(protocol::ToolDefinition definition, ToolHandler handler);

    /// @brief Registers a prompt using a callable adapter.
    /// @param name Prompt name advertised to clients.
    /// @param handler Callable accepting Json, string, PromptContext, one of
    /// the Json/string plus PromptContext combinations, or no argument. Returns
    /// prompt text/result or core::Result of either.
    template <class Handler>
    Builder& prompt(std::string name, Handler handler);

    /// @brief Registers a fully described prompt and low-level handler.
    Builder& prompt(protocol::Prompt prompt, PromptHandler handler);

    /// @brief Registers a resource using a callable adapter.
    /// @param name Resource URI and default display name.
    /// @param handler Callable accepting Json params, requested URI string,
    /// ResourceContext, one of the Json/string plus ResourceContext
    /// combinations, or no argument. Returns resource text/contents/result,
    /// protocol::Resource metadata, or core::Result of these.
    template <class Handler>
    Builder& resource(std::string name, Handler handler);

    /// @brief Registers a fully described resource and low-level read handler.
    Builder& resource(protocol::Resource resource, ResourceReadHandler handler);

    /// @brief Registers a resource template using a callable adapter.
    /// @param name Default resource-template name and URI template when the
    /// handler does not fill those fields.
    /// @param handler Callable returning a ResourceTemplate or
    /// core::Result<ResourceTemplate>.
    /// @throws std::runtime_error if a Result-returning handler fails during
    /// registration.
    template <class Handler>
    Builder& resource_template(std::string name, Handler handler);

    /// @brief Registers a fully described resource template.
    Builder& resource_template(protocol::ResourceTemplate resource_template);

    /// @brief Registers a completion request handler adapter.
    template <class Handler>
    Builder& completion(Handler handler);

    /// @brief Registers a sampling request handler adapter.
    template <class Handler>
    Builder& sampling(Handler handler);

    /// @brief Registers a logging notification handler adapter.
    template <class Handler>
    Builder& logging(Handler handler);

    /// @brief Registers a raw request hook adapter.
    /// @note A handler returning std::optional<JsonRpcResponse> or
    /// JsonRpcResponse controls dispatch; a void-returning handler only
    /// observes the request.
    template <class Handler>
    Builder& raw_request(Handler handler);

    /// @brief Builds the configured server.
    core::Result<std::unique_ptr<Server>> build();

    /// @brief Builds, starts, and runs the configured server application.
    int run();

   private:
    ServerBuilder builder_;
  };

  /// @brief Creates a new convenience server builder.
  static Builder builder();
};

template <class Args, class Result, class Handler>
App::Builder& App::Builder::tool(std::string name, Handler handler) {
  detail::require_callable(handler, "tool");
  auto definition =
      protocol::tool_definition(std::move(name)).input<Args>().build();
  detail::apply_default_output_schema<Result>(definition);
  return tool<Args, Result>(std::move(definition), std::move(handler));
}

template <class Args, class Result, class Handler>
App::Builder& App::Builder::tool(protocol::ToolDefinition definition,
                                 Handler handler) {
  detail::require_callable(handler, "tool");
  if (definition.input_schema.empty()) {
    definition.input_schema = protocol::schema_for<Args>();
  }
  detail::apply_default_output_schema<Result>(definition);
  return tool(
      std::move(definition),
      [handler = std::move(handler)](
          const ToolContext& context) -> core::Result<protocol::ToolResult> {
        try {
          auto args = detail::argument_from_json<Args>(context.arguments);
          auto handled =
              detail::invoke_tool_handler(handler, std::move(args), context);
          if constexpr (detail::is_result<decltype(handled)>::value) {
            if (!handled) {
              return std::unexpected(handled.error());
            }
            return detail::value_to_tool_result(*handled);
          } else {
            return detail::value_to_tool_result(std::move(handled));
          }
        } catch (const std::exception& exception) {
          return std::unexpected(core::Error{
              static_cast<int>(protocol::ErrorCode::InvalidParams),
              "failed to decode tool arguments",
              exception.what(),
          });
        }
      });
}

template <class Args, class Result, class Handler>
App::Builder& App::Builder::tool(
    TypedToolRegistration<Args, Result, Handler> registration) {
  return tool<Args, Result>(std::move(registration.definition),
                            std::move(registration.handler));
}

template <class Handler>
App::Builder& App::Builder::prompt(std::string name, Handler handler) {
  detail::require_callable(handler, "prompt");
  protocol::Prompt prompt;
  prompt.name = std::move(name);
  return this->prompt(
      std::move(prompt),
      [handler = std::move(handler)](const PromptContext& context)
          -> core::Result<protocol::PromptsGetResult> {
        try {
          auto handled = detail::invoke_prompt_handler(handler, context);
          if constexpr (detail::is_result<decltype(handled)>::value) {
            if (!handled) {
              return std::unexpected(handled.error());
            }
            return detail::value_to_prompt_result(*handled);
          } else {
            return detail::value_to_prompt_result(std::move(handled));
          }
        } catch (const std::exception& exception) {
          return std::unexpected(core::Error{
              static_cast<int>(protocol::ErrorCode::InvalidParams),
              "failed to run prompt handler",
              exception.what(),
          });
        }
      });
}

template <class Handler>
App::Builder& App::Builder::resource(std::string name, Handler handler) {
  detail::require_callable(handler, "resource");
  protocol::Resource resource;
  resource.uri = std::move(name);
  resource.name = resource.uri;
  if constexpr (std::is_invocable_v<Handler>) {
    using Handled = decltype(handler());
    if constexpr (std::is_same_v<std::decay_t<Handled>, protocol::Resource>) {
      resource = handler();
    }
  }
  return this->resource(
      std::move(resource),
      [handler = std::move(handler)](const ResourceContext& context)
          -> core::Result<protocol::ResourcesReadResult> {
        try {
          auto handled = detail::invoke_resource_handler(handler, context);
          if constexpr (std::is_same_v<std::decay_t<decltype(handled)>,
                                       protocol::Resource>) {
            return protocol::ResourcesReadResult{};
          } else if constexpr (detail::is_result<decltype(handled)>::value) {
            if (!handled) {
              return std::unexpected(handled.error());
            }
            return detail::value_to_resource_read_result(*handled, context.uri);
          } else {
            return detail::value_to_resource_read_result(std::move(handled),
                                                         context.uri);
          }
        } catch (const std::exception& exception) {
          return std::unexpected(core::Error{
              static_cast<int>(protocol::ErrorCode::InvalidParams),
              "failed to run resource handler",
              exception.what(),
          });
        }
      });
}

template <class Handler>
App::Builder& App::Builder::resource_template(std::string name,
                                              Handler handler) {
  detail::require_callable(handler, "resource_template");
  protocol::ResourceTemplate resource_template;
  if constexpr (std::is_invocable_v<Handler>) {
    auto handled = handler();
    if constexpr (detail::is_result<decltype(handled)>::value) {
      if (!handled) {
        throw std::runtime_error(handled.error().message);
      }
      resource_template = *handled;
    } else {
      resource_template = std::move(handled);
    }
  } else if constexpr (std::is_invocable_v<Handler, std::string>) {
    resource_template = handler({});
  } else {
    static_assert(
        std::is_invocable_v<Handler>,
        "resource_template handler must accept no arguments or string");
  }
  if (resource_template.name.empty()) {
    resource_template.name = name;
  }
  if (resource_template.uri_template.empty()) {
    resource_template.uri_template = std::move(name);
  }
  return this->resource_template(std::move(resource_template));
}

template <class Handler>
App::Builder& App::Builder::completion(Handler handler) {
  detail::require_callable(handler, "completion");
  builder_.on_completion(
      [handler = std::move(handler)](
          const protocol::Json& request) -> core::Result<protocol::Json> {
        if constexpr (detail::is_result<decltype(handler(request))>::value) {
          return handler(request);
        } else {
          return detail::value_to_json(handler(request));
        }
      });
  return *this;
}

template <class Handler>
App::Builder& App::Builder::sampling(Handler handler) {
  detail::require_callable(handler, "sampling");
  builder_.on_sampling(
      [handler = std::move(handler)](
          const protocol::Json& request) -> core::Result<protocol::Json> {
        if constexpr (detail::is_result<decltype(handler(request))>::value) {
          return handler(request);
        } else {
          return detail::value_to_json(handler(request));
        }
      });
  return *this;
}

template <class Handler>
App::Builder& App::Builder::logging(Handler handler) {
  detail::require_callable(handler, "logging");
  builder_.on_logging([handler = std::move(handler)](std::string_view level,
                                                     std::string_view message) {
    handler(level, message);
  });
  return *this;
}

template <class Handler>
App::Builder& App::Builder::raw_request(Handler handler) {
  detail::require_callable(handler, "raw_request");
  builder_.on_raw_request([handler = std::move(handler)](
                              const protocol::JsonRpcRequest& request,
                              const SessionContext& context)
                              -> std::optional<protocol::JsonRpcResponse> {
    (void)context;
    if constexpr (std::is_same_v<std::decay_t<decltype(handler(request))>,
                                 std::optional<protocol::JsonRpcResponse>>) {
      return handler(request);
    } else if constexpr (std::is_same_v<
                             std::decay_t<decltype(handler(request))>,
                             protocol::JsonRpcResponse>) {
      return handler(request);
    } else {
      handler(request);
      return std::nullopt;
    }
  });
  return *this;
}

}  // namespace mcp::server

#include "cxxmcp/server/handler.hpp"
