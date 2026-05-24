#pragma once

/// @file
/// @brief Core client facade and transport interface for MCP clients.
///
/// The client API is synchronous at the call site: methods return core::Result<T>
/// with either a protocol value or an error. Inbound requests and notifications are
/// delivered through registered callbacks, which may be invoked by the transport
/// thread that received the message.

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/capabilities.hpp"
#include "cxxmcp/protocol/completion.hpp"
#include "cxxmcp/protocol/elicitation.hpp"
#include "cxxmcp/protocol/logging.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/roots.hpp"
#include "cxxmcp/protocol/sampling.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/protocol/types.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcp::client {

    /// @brief Basic options for endpoint-oriented client construction.
    struct ClientOptions {
        /// Remote endpoint URI or implementation-defined endpoint string.
        std::string endpoint;

        /// Per-operation timeout used by transports that support request deadlines.
        std::chrono::milliseconds timeout{30000};
    };

    /// @brief Aggregate callback set installable on a Client with Client::set_handler().
    struct ClientHandler;

    /// @brief Handles JSON-RPC requests sent by the server to this client.
    /// @param request Incoming JSON-RPC request.
    /// @return A JSON-RPC response or an error converted to a JSON-RPC error response.
    using TransportRequestHandler = std::function<core::Result<protocol::JsonRpcResponse>(
            const protocol::JsonRpcRequest &)>;

    /// @brief Handles JSON-RPC notifications sent by the server to this client.
    /// @param notification Incoming JSON-RPC notification.
    /// @return Unit on success, or an error describing why the notification could not be handled.
    using TransportNotificationHandler = std::function<core::Result<core::Unit>(
            const protocol::JsonRpcNotification &)>;

    /// @brief Abstract client transport used by Client to exchange JSON-RPC messages.
    ///
    /// Implementations own the wire protocol details. Client calls send() for outbound
    /// requests and may call start() lazily before the first operation so transports can
    /// register inbound request and notification callbacks.
    class Transport {
    public:
        virtual ~Transport() = default;

        /// @brief Sends a JSON-RPC request and waits for the corresponding response.
        /// @param request Fully formed JSON-RPC request.
        /// @return Response returned by the peer, or a transport/protocol error.
        virtual core::Result<protocol::JsonRpcResponse> send(const protocol::JsonRpcRequest &request) = 0;

        /// @brief Sends a JSON-RPC notification without waiting for a response.
        /// @param notification Fully formed JSON-RPC notification.
        /// @return Unit on success, or a transport/protocol error.
        virtual core::Result<core::Unit> send_notification(const protocol::JsonRpcNotification &notification);

        /// @brief Starts receiving inbound messages for transports that need an active loop.
        /// @param request_handler Callback used for server-to-client JSON-RPC requests.
        /// @param notification_handler Callback used for server-to-client notifications.
        /// @return Unit on success, or a startup error.
        /// @note The default implementation is a no-op for request/response-only transports.
        virtual core::Result<core::Unit> start(TransportRequestHandler request_handler,
                                               TransportNotificationHandler notification_handler = {}) {
            (void) request_handler;
            (void) notification_handler;
            return core::Unit{};
        }
    };

    /// @brief High-level MCP client facade.
    ///
    /// Client owns a Transport and exposes MCP operations such as initialize, discovery,
    /// tool invocation, prompt/resource access, roots, logging, sampling, elicitation,
    /// and task helpers. Methods return core::Result<T>; protocol and transport failures
    /// are reported through that result rather than by changing global state.
    class Client {
    public:
        /// @brief Endpoint options for streamable HTTP and legacy SSE client transports.
        struct StreamableHttpEndpoint {
            /// Remote host name or IP address.
            std::string host;
            /// Remote TCP port.
            std::uint16_t port = 80;
            /// HTTP path for MCP requests or streams.
            std::string path = "/mcp";
            /// Extra headers sent by the transport.
            std::unordered_map<std::string, std::string> headers;
            /// Per-request HTTP timeout.
            std::chrono::milliseconds timeout{30000};
        };

        /// @brief Endpoint options for launching a child process over stdio.
        struct StdioEndpoint {
            /// Executable to launch.
            std::string command;
            /// Command-line arguments passed to the executable.
            std::vector<std::string> args;
            /// Optional working directory for the child process.
            std::string cwd;
            /// Extra environment variables for the child process.
            std::unordered_map<std::string, std::string> env;
        };

        using Root = protocol::Root;

        /// @brief Receives logging messages from the server.
        /// @param level Server-provided logging level.
        /// @param message Human-readable log message.
        using LoggingMessageHandler = std::function<void(std::string_view, std::string_view)>;

        /// @brief Receives notifications that the peer completed initialization.
        using InitializedHandler = std::function<void()>;

        /// @brief Receives cancellation notifications for in-flight requests.
        /// @param request_id Identifier of the cancelled request.
        /// @param reason Optional cancellation reason.
        using CancelledHandler = std::function<void(const protocol::RequestId &, std::string_view)>;

        /// @brief Receives list-change notifications for prompts, resources, tools, or roots.
        using ListChangedHandler = std::function<void()>;

        /// @brief Receives resource update notifications.
        /// @param uri URI of the updated resource.
        using ResourceUpdatedHandler = std::function<void(const std::string &)>;

        /// @brief Receives progress notifications associated with a progress token.
        using ProgressHandler = std::function<void(const protocol::ProgressNotificationParams &)>;

        /// @brief Receives completion notifications for elicitation flows.
        /// @param elicitation_id Identifier of the completed elicitation request.
        using ElicitationCompleteHandler = std::function<void(std::string_view)>;

        /// @brief Receives task status notifications.
        using TaskStatusHandler = std::function<void(const protocol::Task &)>;

        /// @brief Handles a server request for the client's current roots.
        /// @return Roots list result, or an MCP error result.
        using RootsListRequestHandler = std::function<core::Result<protocol::RootsListResult>()>;

        /// @brief Handles a server sampling request.
        /// @param params Sampling parameters supplied by the server.
        /// @return Created message, or an MCP error result.
        using SamplingRequestHandler = std::function<core::Result<protocol::CreateMessageResult>(
                const protocol::CreateMessageParams &)>;

        /// @brief Handles a server elicitation request.
        /// @param params Elicitation request parameters supplied by the server.
        /// @return Elicitation result, or an MCP error result.
        using ElicitationRequestHandler = std::function<core::Result<protocol::CreateElicitationResult>(
                const protocol::CreateElicitationRequestParam &)>;

        /// @brief Handles non-built-in server requests.
        /// @param request Raw JSON-RPC request from the server.
        /// @return JSON value used as the response result, or an MCP error result.
        using CustomRequestHandler = std::function<core::Result<protocol::Json>(const protocol::JsonRpcRequest &)>;
        using ListRootsRequestHandler = RootsListRequestHandler;
        using CreateMessageRequestHandler = SamplingRequestHandler;
        using CreateElicitationRequestHandler = ElicitationRequestHandler;

        /// @brief Observes every inbound notification after built-in dispatch.
        using RawNotificationHandler = std::function<void(const protocol::JsonRpcNotification &)>;

        /// @brief Creates a client connected to a streamable HTTP MCP endpoint.
        /// @param endpoint Remote HTTP endpoint options.
        /// @return Client owning the configured HTTP transport.
        static Client connect_streamable_http(StreamableHttpEndpoint endpoint);

        /// @brief Creates a client connected to a legacy SSE MCP endpoint.
        /// @param endpoint Remote HTTP/SSE endpoint options.
        /// @return Client owning the configured SSE-compatible transport.
        static Client connect_legacy_sse(StreamableHttpEndpoint endpoint);

        /// @brief Creates a client connected to a child process over stdio.
        /// @param endpoint Child process command, arguments, working directory, and environment.
        /// @return Client owning a process stdio transport.
        static Client connect_stdio(StdioEndpoint endpoint);

        /// @brief Constructs a client from a custom transport.
        /// @param transport Transport instance to own. It must not be null.
        explicit Client(std::unique_ptr<Transport> transport);

        /// @brief Sends the MCP initialize request.
        /// @param client_name Name advertised to the server.
        /// @param client_version Version advertised to the server.
        /// @return Raw initialize result JSON, or an MCP/transport error.
        core::Result<protocol::Json> initialize(std::string client_name = "cxxmcp",
                                                std::string client_version = "0");

        /// @brief Sends the initialized notification after a successful initialize exchange.
        core::Result<core::Unit> notify_initialized();

        /// @brief Sends a cancellation notification for a request.
        /// @param request_id Identifier of the request being cancelled.
        /// @param reason Optional cancellation reason.
        core::Result<core::Unit> notify_cancelled(protocol::RequestId request_id,
                                                  std::string reason = {});

        /// @brief Sends a progress notification.
        /// @param progress_token Token that associates progress with a request.
        /// @param progress Current progress value.
        /// @param total Optional total value for determinate progress.
        /// @param message Optional human-readable status message.
        core::Result<core::Unit> notify_progress(protocol::ProgressToken progress_token,
                                                 double progress,
                                                 std::optional<double> total = std::nullopt,
                                                 std::string message = {});

        /// @brief Notifies the server that the client's roots list changed.
        core::Result<core::Unit> notify_roots_list_changed();

        /// @brief Sends an MCP ping request.
        core::Result<core::Unit> ping();

        /// @brief Lists one page of prompts advertised by the server.
        core::Result<std::vector<protocol::Prompt>> list_prompts();

        /// @brief Lists all prompts, following pagination cursors until exhausted.
        core::Result<std::vector<protocol::Prompt>> list_all_prompts();

        /// @brief Gets a prompt by protocol parameter object.
        core::Result<protocol::PromptsGetResult> get_prompt(const protocol::PromptsGetParams &params);

        /// @brief Gets a prompt by name and optional JSON arguments.
        core::Result<protocol::PromptsGetResult> get_prompt(std::string_view name,
                                                            const protocol::Json &arguments = protocol::Json::object());

        /// @brief Lists one page of resources advertised by the server.
        core::Result<std::vector<protocol::Resource>> list_resources();

        /// @brief Lists all resources, following pagination cursors until exhausted.
        core::Result<std::vector<protocol::Resource>> list_all_resources();

        /// @brief Reads a resource by protocol parameter object.
        core::Result<protocol::ResourcesReadResult> read_resource(const protocol::ResourcesReadParams &params);

        /// @brief Reads a resource by URI.
        core::Result<protocol::ResourcesReadResult> read_resource(std::string_view uri);

        /// @brief Lists one page of resource templates advertised by the server.
        core::Result<std::vector<protocol::ResourceTemplate>> list_resource_templates();

        /// @brief Lists all resource templates, following pagination cursors until exhausted.
        core::Result<std::vector<protocol::ResourceTemplate>> list_all_resource_templates();

        /// @brief Lists one page of tool definitions advertised by the server.
        core::Result<std::vector<protocol::ToolDefinition>> list_tools();

        /// @brief Lists all tool definitions, following pagination cursors until exhausted.
        core::Result<std::vector<protocol::ToolDefinition>> list_all_tools();

        /// @brief Calls a tool using a protocol ToolCall object.
        core::Result<protocol::ToolResult> call_tool(const protocol::ToolCall &call);

        /// @brief Calls a tool by name with optional JSON arguments.
        core::Result<protocol::ToolResult> call_raw(std::string_view name,
                                                    const protocol::Json &arguments = protocol::Json::object());

        /// @brief Requests completion using typed protocol parameters.
        core::Result<protocol::CompleteResult> complete(const protocol::CompleteParams &request);

        /// @brief Requests completion using raw JSON parameters.
        core::Result<protocol::Json> complete(const protocol::Json &request);

        /// @brief Sends a sampling createMessage request using typed protocol parameters.
        core::Result<protocol::CreateMessageResult> create_message(const protocol::CreateMessageParams &request);

        /// @brief Sends a sampling createMessage request using raw JSON parameters.
        core::Result<protocol::Json> create_message(const protocol::Json &request);

        /// @brief Sends an elicitation request using typed protocol parameters.
        core::Result<protocol::CreateElicitationResult> create_elicitation(
                const protocol::CreateElicitationRequestParam &request);

        /// @brief Sends an elicitation request using raw JSON parameters.
        core::Result<protocol::Json> create_elicitation(const protocol::Json &request);

        /// @brief Lists one page of tasks advertised by the server.
        core::Result<std::vector<protocol::Task>> list_tasks();

        /// @brief Lists all tasks, following pagination cursors until exhausted.
        core::Result<std::vector<protocol::Task>> list_all_tasks();

        /// @brief Gets a task using typed protocol parameters.
        core::Result<protocol::Task> get_task(const protocol::TaskGetParams &request);

        /// @brief Gets a task by task identifier.
        core::Result<protocol::Task> get_task(std::string_view task_id);

        /// @brief Cancels a task using typed protocol parameters.
        core::Result<protocol::Task> cancel_task(const protocol::TaskCancelParams &request);

        /// @brief Cancels a task by task identifier.
        core::Result<protocol::Task> cancel_task(std::string_view task_id);

        /// @brief Gets a task result using typed protocol parameters.
        core::Result<protocol::Json> task_result(const protocol::TaskResultParams &request);

        /// @brief Gets a task result by task identifier.
        core::Result<protocol::Json> task_result(std::string_view task_id);

        /// @brief Sets the server logging level using typed protocol parameters.
        core::Result<core::Unit> set_level(const protocol::LoggingSetLevelParams &params);

        /// @brief Sets the server logging level by level name.
        core::Result<core::Unit> set_level(std::string_view level);

        /// @brief Subscribes to resource update notifications for a URI.
        core::Result<core::Unit> subscribe(std::string_view uri);

        /// @brief Unsubscribes from resource update notifications for a URI.
        core::Result<core::Unit> unsubscribe(std::string_view uri);

        /// @brief Sends a raw JSON-RPC request and returns its result JSON.
        core::Result<protocol::Json> request(const protocol::JsonRpcRequest &request);

        /// @brief Sends a raw JSON-RPC notification.
        core::Result<core::Unit> notify(const protocol::JsonRpcNotification &notification);

        /// @brief Returns the roots currently advertised by this client.
        std::vector<protocol::Root> list_roots() const;

        /// @brief Replaces the roots advertised by this client.
        /// @param roots New roots collection.
        /// @return Reference to this client for chaining.
        /// @note If a roots-list-changed callback is installed, it is invoked after updating.
        Client &set_roots(std::vector<protocol::Root> roots);

        /// @brief Sets client capabilities used during initialization.
        /// @param capabilities Capabilities to advertise to the server.
        /// @return Reference to this client for chaining.
        Client &set_capabilities(protocol::ClientCapabilities capabilities);

        /// @brief Registers a callback for initialized notifications.
        Client &on_initialized(InitializedHandler handler);

        /// @brief Registers a callback for cancellation notifications.
        Client &on_cancelled(CancelledHandler handler);

        /// @brief Registers a callback for logging message notifications.
        Client &on_logging_message(LoggingMessageHandler handler);

        /// @brief Registers a callback for tool list change notifications.
        Client &on_tool_list_changed(ListChangedHandler handler);

        /// @brief Registers a callback for prompt list change notifications.
        Client &on_prompt_list_changed(ListChangedHandler handler);

        /// @brief Registers a callback for resource list change notifications.
        Client &on_resource_list_changed(ListChangedHandler handler);

        /// @brief Registers a callback for resource update notifications.
        Client &on_resource_updated(ResourceUpdatedHandler handler);

        /// @brief Registers a callback for progress notifications.
        Client &on_progress(ProgressHandler handler);

        /// @brief Registers a callback for elicitation completion notifications.
        Client &on_elicitation_complete(ElicitationCompleteHandler handler);

        /// @brief Registers a callback for task status notifications.
        Client &on_task_status(TaskStatusHandler handler);

        /// @brief Registers a callback for roots list change notifications.
        Client &on_roots_list_changed(ListChangedHandler handler);

        /// @brief Registers a handler for server list-roots requests.
        Client &on_list_roots_request(ListRootsRequestHandler handler);

        /// @brief Registers a handler for server sampling createMessage requests.
        Client &on_create_message_request(CreateMessageRequestHandler handler);

        /// @brief Registers a handler for server elicitation requests.
        Client &on_create_elicitation_request(CreateElicitationRequestHandler handler);

        /// @brief Registers a handler for custom server requests not handled by the facade.
        Client &on_custom_request(CustomRequestHandler handler);

        /// @brief Compatibility alias for on_list_roots_request().
        Client &on_roots_list_request(RootsListRequestHandler handler);

        /// @brief Compatibility alias for on_create_message_request().
        Client &on_sampling_request(SamplingRequestHandler handler);

        /// @brief Compatibility alias for on_create_elicitation_request().
        Client &on_elicitation_request(ElicitationRequestHandler handler);

        /// @brief Registers an observer for raw inbound notifications.
        Client &on_raw_notification(RawNotificationHandler handler);

        /// @brief Compatibility alias for on_raw_notification().
        Client &on_custom_notification(RawNotificationHandler handler);

        /// @brief Installs every non-empty callback from a ClientHandler aggregate.
        /// @param handler Callback aggregate. Empty members leave existing callbacks unchanged.
        /// @return Reference to this client for chaining.
        Client &set_handler(const ClientHandler &handler);

        /// @brief Dispatches an inbound notification through built-in and registered handlers.
        core::Result<core::Unit> handle_notification(const protocol::JsonRpcNotification &notification);

        /// @brief Dispatches an inbound server request through built-in and custom handlers.
        core::Result<protocol::JsonRpcResponse> handle_request(const protocol::JsonRpcRequest &request);

        /// @brief Sends a raw JSON-RPC request and returns the raw result JSON.
        core::Result<protocol::Json> raw_request(const protocol::JsonRpcRequest &request);

        /// @brief Sends a raw JSON-RPC notification.
        core::Result<core::Unit> raw_notification(const protocol::JsonRpcNotification &notification);

    private:
        core::Result<protocol::Json> send_request(std::string method, protocol::Json params);
        core::Result<protocol::JsonRpcResponse> send_rpc_request(protocol::JsonRpcRequest request);
        core::Result<core::Unit> ensure_transport_started();

        std::unique_ptr<Transport> transport_;
        std::int64_t next_request_id_ = 1;
        bool transport_started_ = false;
        std::vector<protocol::Root> roots_;
        std::optional<protocol::Json> last_initialize_params_;
        std::optional<protocol::ClientCapabilities> capabilities_;
        InitializedHandler initialized_handler_;
        CancelledHandler cancelled_handler_;
        LoggingMessageHandler logging_message_handler_;
        ListChangedHandler tool_list_changed_handler_;
        ListChangedHandler prompt_list_changed_handler_;
        ListChangedHandler resource_list_changed_handler_;
        ResourceUpdatedHandler resource_updated_handler_;
        ProgressHandler progress_handler_;
        ElicitationCompleteHandler elicitation_complete_handler_;
        TaskStatusHandler task_status_handler_;
        ListChangedHandler roots_list_changed_handler_;
        RootsListRequestHandler roots_list_request_handler_;
        SamplingRequestHandler sampling_request_handler_;
        ElicitationRequestHandler elicitation_request_handler_;
        CustomRequestHandler custom_request_handler_;
        RawNotificationHandler raw_notification_handler_;
    };

}// namespace mcp::client

#include "cxxmcp/client/handler.hpp"
