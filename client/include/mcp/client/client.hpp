#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/elicitation.hpp"
#include "mcp/protocol/completion.hpp"
#include "mcp/protocol/capabilities.hpp"
#include "mcp/protocol/logging.hpp"
#include "mcp/protocol/serialization.hpp"
#include "mcp/protocol/prompt.hpp"
#include "mcp/protocol/resource.hpp"
#include "mcp/protocol/roots.hpp"
#include "mcp/protocol/sampling.hpp"
#include "mcp/protocol/tool.hpp"
#include "mcp/protocol/types.hpp"

#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcp::client {

struct ClientOptions {
    std::string endpoint;
    std::chrono::milliseconds timeout{30000};
};

using TransportRequestHandler = std::function<core::Result<protocol::JsonRpcResponse>(
    const protocol::JsonRpcRequest&)>;
using TransportNotificationHandler = std::function<core::Result<core::Unit>(
    const protocol::JsonRpcNotification&)>;

class Transport {
public:
    virtual ~Transport() = default;
    virtual core::Result<protocol::JsonRpcResponse> send(const protocol::JsonRpcRequest& request) = 0;
    virtual core::Result<core::Unit> send_notification(const protocol::JsonRpcNotification& notification);
    virtual core::Result<core::Unit> start(TransportRequestHandler request_handler,
                                           TransportNotificationHandler notification_handler = {}) {
        (void)request_handler;
        (void)notification_handler;
        return core::Unit{};
    }
};

class Client {
public:
    struct StreamableHttpEndpoint {
        std::string host;
        std::uint16_t port = 80;
        std::string path = "/mcp";
        std::unordered_map<std::string, std::string> headers;
        std::chrono::milliseconds timeout{30000};
    };

    struct StdioEndpoint {
        std::string command;
        std::vector<std::string> args;
        std::string cwd;
        std::unordered_map<std::string, std::string> env;
    };

    using Root = protocol::Root;
    using LoggingMessageHandler = std::function<void(std::string_view, std::string_view)>;
    using InitializedHandler = std::function<void()>;
    using CancelledHandler = std::function<void(const protocol::RequestId&, std::string_view)>;
    using ListChangedHandler = std::function<void()>;
    using ResourceUpdatedHandler = std::function<void(const std::string&)>;
    using ProgressHandler = std::function<void(const protocol::ProgressNotificationParams&)>;
    using ElicitationCompleteHandler = std::function<void(std::string_view)>;
    using TaskStatusHandler = std::function<void(const protocol::Task&)>;
    using RootsListRequestHandler = std::function<core::Result<protocol::RootsListResult>()>;
    using SamplingRequestHandler = std::function<core::Result<protocol::CreateMessageResult>(
        const protocol::CreateMessageParams&)>;
    using ElicitationRequestHandler = std::function<core::Result<protocol::CreateElicitationResult>(
        const protocol::CreateElicitationRequestParam&)>;
    using CustomRequestHandler = std::function<core::Result<protocol::Json>(const protocol::JsonRpcRequest&)>;
    using ListRootsRequestHandler = RootsListRequestHandler;
    using CreateMessageRequestHandler = SamplingRequestHandler;
    using CreateElicitationRequestHandler = ElicitationRequestHandler;
    using RawNotificationHandler = std::function<void(const protocol::JsonRpcNotification&)>;

    static Client connect_streamable_http(StreamableHttpEndpoint endpoint);
    static Client connect_legacy_sse(StreamableHttpEndpoint endpoint);
    static Client connect_stdio(StdioEndpoint endpoint);

    explicit Client(std::unique_ptr<Transport> transport);

    core::Result<protocol::Json> initialize(std::string client_name = "cxxmcp",
                                            std::string client_version = "0");
    core::Result<core::Unit> notify_initialized();
    core::Result<core::Unit> notify_cancelled(protocol::RequestId request_id,
                                              std::string reason = {});
    core::Result<core::Unit> notify_progress(protocol::ProgressToken progress_token,
                                             double progress,
                                             std::optional<double> total = std::nullopt,
                                             std::string message = {});
    core::Result<core::Unit> notify_roots_list_changed();
    core::Result<core::Unit> ping();

    core::Result<std::vector<protocol::Prompt>> list_prompts();
    core::Result<std::vector<protocol::Prompt>> list_all_prompts();
    core::Result<protocol::PromptsGetResult> get_prompt(const protocol::PromptsGetParams& params);
    core::Result<protocol::PromptsGetResult> get_prompt(std::string_view name,
                                                        const protocol::Json& arguments = protocol::Json::object());
    core::Result<std::vector<protocol::Resource>> list_resources();
    core::Result<std::vector<protocol::Resource>> list_all_resources();
    core::Result<protocol::ResourcesReadResult> read_resource(const protocol::ResourcesReadParams& params);
    core::Result<protocol::ResourcesReadResult> read_resource(std::string_view uri);
    core::Result<std::vector<protocol::ResourceTemplate>> list_resource_templates();
    core::Result<std::vector<protocol::ResourceTemplate>> list_all_resource_templates();
    core::Result<std::vector<protocol::ToolDefinition>> list_tools();
    core::Result<std::vector<protocol::ToolDefinition>> list_all_tools();
    core::Result<protocol::ToolResult> call_tool(const protocol::ToolCall& call);
    core::Result<protocol::ToolResult> call_raw(std::string_view name,
                                                const protocol::Json& arguments = protocol::Json::object());
    core::Result<protocol::CompleteResult> complete(const protocol::CompleteParams& request);
    core::Result<protocol::Json> complete(const protocol::Json& request);
    core::Result<protocol::CreateMessageResult> create_message(const protocol::CreateMessageParams& request);
    core::Result<protocol::Json> create_message(const protocol::Json& request);
    core::Result<protocol::CreateElicitationResult> create_elicitation(
        const protocol::CreateElicitationRequestParam& request);
    core::Result<protocol::Json> create_elicitation(const protocol::Json& request);
    core::Result<std::vector<protocol::Task>> list_tasks();
    core::Result<std::vector<protocol::Task>> list_all_tasks();
    core::Result<protocol::Task> get_task(const protocol::TaskGetParams& request);
    core::Result<protocol::Task> get_task(std::string_view task_id);
    core::Result<protocol::Task> cancel_task(const protocol::TaskCancelParams& request);
    core::Result<protocol::Task> cancel_task(std::string_view task_id);
    core::Result<protocol::Json> task_result(const protocol::TaskResultParams& request);
    core::Result<protocol::Json> task_result(std::string_view task_id);
    core::Result<core::Unit> set_level(const protocol::LoggingSetLevelParams& params);
    core::Result<core::Unit> set_level(std::string_view level);
    core::Result<core::Unit> subscribe(std::string_view uri);
    core::Result<core::Unit> unsubscribe(std::string_view uri);
    core::Result<protocol::Json> request(const protocol::JsonRpcRequest& request);
    core::Result<core::Unit> notify(const protocol::JsonRpcNotification& notification);

    std::vector<protocol::Root> list_roots() const;
    Client& set_roots(std::vector<protocol::Root> roots);
    Client& set_capabilities(protocol::ClientCapabilities capabilities);

    Client& on_initialized(InitializedHandler handler);
    Client& on_cancelled(CancelledHandler handler);
    Client& on_logging_message(LoggingMessageHandler handler);
    Client& on_tool_list_changed(ListChangedHandler handler);
    Client& on_prompt_list_changed(ListChangedHandler handler);
    Client& on_resource_list_changed(ListChangedHandler handler);
    Client& on_resource_updated(ResourceUpdatedHandler handler);
    Client& on_progress(ProgressHandler handler);
    Client& on_elicitation_complete(ElicitationCompleteHandler handler);
    Client& on_task_status(TaskStatusHandler handler);
    Client& on_roots_list_changed(ListChangedHandler handler);
    Client& on_list_roots_request(ListRootsRequestHandler handler);
    Client& on_create_message_request(CreateMessageRequestHandler handler);
    Client& on_create_elicitation_request(CreateElicitationRequestHandler handler);
    Client& on_custom_request(CustomRequestHandler handler);
    Client& on_roots_list_request(RootsListRequestHandler handler);
    Client& on_sampling_request(SamplingRequestHandler handler);
    Client& on_elicitation_request(ElicitationRequestHandler handler);
    Client& on_raw_notification(RawNotificationHandler handler);
    Client& on_custom_notification(RawNotificationHandler handler);
    core::Result<core::Unit> handle_notification(const protocol::JsonRpcNotification& notification);
    core::Result<protocol::JsonRpcResponse> handle_request(const protocol::JsonRpcRequest& request);

    core::Result<protocol::Json> raw_request(const protocol::JsonRpcRequest& request);
    core::Result<core::Unit> raw_notification(const protocol::JsonRpcNotification& notification);
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

} // namespace mcp::client
