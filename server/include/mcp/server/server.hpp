#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/capabilities.hpp"
#include "mcp/protocol/logging.hpp"
#include "mcp/server/auth.hpp"
#include "mcp/server/rate_limit.hpp"
#include "mcp/server/registry.hpp"
#include "mcp/server/transport.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace mcp::server {

namespace detail {

template <class T>
struct is_result : std::false_type {};

template <class T>
struct is_result<core::Result<T>> : std::true_type {};

template <class T>
inline protocol::Json value_to_json(T&& value) {
    using Value = std::decay_t<T>;
    if constexpr (std::is_same_v<Value, protocol::Json>) {
        return std::forward<T>(value);
    } else {
        return protocol::Json(std::forward<T>(value));
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

inline protocol::PromptsGetResult value_to_prompt_result(protocol::PromptsGetResult result) {
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

inline protocol::ResourcesReadResult value_to_resource_read_result(protocol::ResourcesReadResult result,
                                                                   std::string_view) {
    return result;
}

inline protocol::ResourcesReadResult value_to_resource_read_result(protocol::ResourceContents contents,
                                                                   std::string_view) {
    protocol::ResourcesReadResult result;
    result.contents.push_back(std::move(contents));
    return result;
}

inline protocol::ResourcesReadResult value_to_resource_read_result(std::string text, std::string_view uri) {
    protocol::ResourcesReadResult result;
    protocol::ResourceContents contents;
    contents.uri = std::string(uri);
    contents.mime_type = "text/plain";
    contents.text = std::move(text);
    result.contents.push_back(std::move(contents));
    return result;
}

template <class Arg>
inline Arg argument_from_json(const protocol::Json& arguments, std::string_view fallback_name = {}) {
    if constexpr (std::is_same_v<std::decay_t<Arg>, protocol::Json>) {
        return arguments;
    } else {
        if (!fallback_name.empty() && arguments.is_object() && arguments.contains(std::string(fallback_name))) {
            return arguments.at(std::string(fallback_name)).get<Arg>();
        }
        if (arguments.is_object() && arguments.size() == 1) {
            return arguments.begin().value().get<Arg>();
        }
        return arguments.get<Arg>();
    }
}

} // namespace detail

struct ServerOptions {
    protocol::ServerCapabilities capabilities;
    std::string server_name = "cxxmcp";
    std::string server_version = "2.0.0";
    std::string instructions;
};

struct ServerInfo {
    std::string name;
    std::string version;
    std::string instructions;
};

class Server {
public:
    explicit Server(ServerOptions options);

    ServerInfo get_info() const;
    const protocol::ServerCapabilities& capabilities() const noexcept;
    ToolRegistry& tools() noexcept;
    const ToolRegistry& tools() const noexcept;
    std::vector<protocol::ToolDefinition> list_tools() const;
    core::Result<protocol::ToolDefinition> get_tool(std::string_view name) const;
    core::Result<protocol::ToolResult> call_tool(std::string_view name,
                                                 protocol::Json arguments = protocol::Json::object(),
                                                 const std::string& session_id = {}) const;
    PromptRegistry& prompts() noexcept;
    const PromptRegistry& prompts() const noexcept;
    std::vector<protocol::Prompt> list_prompts() const;
    core::Result<protocol::PromptsGetResult> get_prompt(std::string_view name,
                                                        protocol::Json arguments = protocol::Json::object(),
                                                        const std::string& session_id = {}) const;
    ResourceRegistry& resources() noexcept;
    const ResourceRegistry& resources() const noexcept;
    std::vector<protocol::Resource> list_resources() const;
    core::Result<protocol::ResourcesReadResult> read_resource(std::string_view uri,
                                                              protocol::Json params = protocol::Json::object(),
                                                              const std::string& session_id = {}) const;
    ResourceTemplateRegistry& resource_templates() noexcept;
    const ResourceTemplateRegistry& resource_templates() const noexcept;
    std::vector<protocol::ResourceTemplate> list_resource_templates() const;
    core::Result<protocol::Json> ping(const SessionContext& context = {});

    core::Result<protocol::JsonRpcResponse> handle_request(const protocol::JsonRpcRequest& request,
                                                           const SessionContext& context);
    core::Result<core::Unit> handle_notification(const protocol::JsonRpcNotification& notification,
                                                 const SessionContext& context);
    core::Result<core::Unit> notify_roots_list_changed();
    core::Result<core::Unit> notify_tool_list_changed();
    core::Result<core::Unit> notify_prompt_list_changed();
    core::Result<core::Unit> notify_resource_list_changed();
    core::Result<core::Unit> notify_resource_updated(std::string_view uri);
    core::Result<core::Unit> notify_progress(const protocol::ProgressNotificationParams& params);
    core::Result<core::Unit> notify_logging_message(const protocol::LoggingMessageNotificationParams& params);
    core::Result<core::Unit> notify_elicitation_complete(std::string elicitation_id);
    core::Result<core::Unit> notify_task_status(const protocol::Task& task);

    core::Result<core::Unit> add_transport(std::unique_ptr<Transport> transport);
    void set_auth_provider(std::unique_ptr<AuthProvider> auth_provider);
    void set_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter);
    core::Result<core::Unit> start();
    void stop() noexcept;

    using JsonHandler = std::function<core::Result<protocol::Json>(const protocol::Json&)>;
    using LoggingHandler = std::function<void(std::string_view, std::string_view)>;
    using RawRequestHandler = std::function<std::optional<protocol::JsonRpcResponse>(
        const protocol::JsonRpcRequest&,
        const SessionContext&)>;
    using RawNotificationHandler = std::function<core::Result<core::Unit>(const protocol::JsonRpcNotification&,
                                                                          const SessionContext&)>;
    using RootsListChangedHandler = std::function<core::Result<core::Unit>(const SessionContext&)>;
    using ProgressHandler = std::function<core::Result<core::Unit>(const protocol::ProgressNotificationParams&,
                                                                    const SessionContext&)>;
    using ListChangedHandler = std::function<core::Result<core::Unit>(const SessionContext&)>;
    using ResourceUpdatedHandler = std::function<core::Result<core::Unit>(const std::string& uri,
                                                                          const SessionContext&)>;

    void set_completion_handler(JsonHandler handler);
    void set_sampling_handler(JsonHandler handler);
    void set_logging_handler(LoggingHandler handler);
    void set_raw_request_handler(RawRequestHandler handler);
    void set_raw_notification_handler(RawNotificationHandler handler);
    void set_custom_request_handler(RawRequestHandler handler);
    void set_custom_notification_handler(RawNotificationHandler handler);
    void set_progress_handler(ProgressHandler handler);
    void set_roots_list_changed_handler(RootsListChangedHandler handler);
    void set_tool_list_changed_handler(ListChangedHandler handler);
    void set_prompt_list_changed_handler(ListChangedHandler handler);
    void set_resource_list_changed_handler(ListChangedHandler handler);
    void set_resource_updated_handler(ResourceUpdatedHandler handler);

private:
    ServerOptions options_;
    ToolRegistry tools_;
    PromptRegistry prompts_;
    ResourceRegistry resources_;
    ResourceTemplateRegistry resource_templates_;
    std::unique_ptr<AuthProvider> auth_provider_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    std::vector<std::unique_ptr<Transport>> transports_;
    JsonHandler completion_handler_;
    JsonHandler sampling_handler_;
    LoggingHandler logging_handler_;
    RawRequestHandler raw_request_handler_;
    RawNotificationHandler raw_notification_handler_;
    ProgressHandler progress_handler_;
    RootsListChangedHandler roots_list_changed_handler_;
    ListChangedHandler tool_list_changed_handler_;
    ListChangedHandler prompt_list_changed_handler_;
    ListChangedHandler resource_list_changed_handler_;
    ResourceUpdatedHandler resource_updated_handler_;

    core::Result<core::Unit> broadcast_notification(const protocol::JsonRpcNotification& notification);
};

class ServerBuilder {
public:
    ServerBuilder& name(std::string value);
    ServerBuilder& version(std::string value);
    ServerBuilder& instructions(std::string value);
    ServerBuilder& with_capabilities(protocol::ServerCapabilities capabilities);
    ServerBuilder& with_transport(std::unique_ptr<Transport> transport);
    ServerBuilder& with_auth_provider(std::unique_ptr<AuthProvider> auth_provider);
    ServerBuilder& with_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter);
    ServerBuilder& add_tool(protocol::ToolDefinition definition, ToolHandler handler);
    ServerBuilder& add_prompt(protocol::Prompt prompt, PromptHandler handler);
    ServerBuilder& add_resource(protocol::Resource resource, ResourceReadHandler handler);
    ServerBuilder& add_resource_template(protocol::ResourceTemplate resource_template);
    ServerBuilder& on_completion(Server::JsonHandler handler);
    ServerBuilder& on_sampling(Server::JsonHandler handler);
    ServerBuilder& on_logging(Server::LoggingHandler handler);
    ServerBuilder& on_raw_request(Server::RawRequestHandler handler);
    ServerBuilder& on_raw_notification(Server::RawNotificationHandler handler);
    ServerBuilder& on_custom_request(Server::RawRequestHandler handler);
    ServerBuilder& on_custom_notification(Server::RawNotificationHandler handler);
    ServerBuilder& on_progress(Server::ProgressHandler handler);
    ServerBuilder& on_roots_list_changed(Server::RootsListChangedHandler handler);
    ServerBuilder& on_tool_list_changed(Server::ListChangedHandler handler);
    ServerBuilder& on_prompt_list_changed(Server::ListChangedHandler handler);
    ServerBuilder& on_resource_list_changed(Server::ListChangedHandler handler);
    ServerBuilder& on_resource_updated(Server::ResourceUpdatedHandler handler);
    core::Result<std::unique_ptr<Server>> build();

private:
    using ServerRegistration = std::function<core::Result<core::Unit>(Server&)>;

    ServerOptions options_;
    std::unique_ptr<AuthProvider> auth_provider_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    std::vector<std::unique_ptr<Transport>> transports_;
    std::vector<ServerRegistration> registrations_;
    Server::JsonHandler completion_handler_;
    Server::JsonHandler sampling_handler_;
    Server::LoggingHandler logging_handler_;
    Server::RawRequestHandler raw_request_handler_;
    Server::RawNotificationHandler raw_notification_handler_;
    Server::ProgressHandler progress_handler_;
    Server::RootsListChangedHandler roots_list_changed_handler_;
    Server::ListChangedHandler tool_list_changed_handler_;
    Server::ListChangedHandler prompt_list_changed_handler_;
    Server::ListChangedHandler resource_list_changed_handler_;
    Server::ResourceUpdatedHandler resource_updated_handler_;
};

class App {
public:
    class Builder {
    public:
        Builder& name(std::string value);
        Builder& version(std::string value);
        Builder& instructions(std::string value);

        Builder& stdio();
        Builder& streamable_http(std::string host, std::uint16_t port, std::string path = "/mcp");
        Builder& legacy_sse(std::string host, std::uint16_t port, std::string path = "/mcp");
        Builder& transport(std::unique_ptr<Transport> value);

        template <class Args, class Result, class Handler>
        Builder& tool(std::string name, Handler handler);
        Builder& tool(protocol::ToolDefinition definition, ToolHandler handler);

        template <class Handler>
        Builder& prompt(std::string name, Handler handler);
        Builder& prompt(protocol::Prompt prompt, PromptHandler handler);

        template <class Handler>
        Builder& resource(std::string name, Handler handler);
        Builder& resource(protocol::Resource resource, ResourceReadHandler handler);

        template <class Handler>
        Builder& resource_template(std::string name, Handler handler);
        Builder& resource_template(protocol::ResourceTemplate resource_template);

        template <class Handler>
        Builder& completion(Handler handler);
        template <class Handler>
        Builder& sampling(Handler handler);
        template <class Handler>
        Builder& logging(Handler handler);
        template <class Handler>
        Builder& raw_request(Handler handler);

        core::Result<std::unique_ptr<Server>> build();
        int run();

    private:
        ServerBuilder builder_;
    };

    static Builder builder();
};

template <class Args, class Result, class Handler>
App::Builder& App::Builder::tool(std::string name, Handler handler) {
    protocol::ToolDefinition definition;
    definition.name = std::move(name);
    definition.input_schema = protocol::Json{{"type", "object"}};
    return tool(std::move(definition),
                [handler = std::move(handler)](const ToolContext& context)
                    -> core::Result<protocol::ToolResult> {
                    try {
                        auto args = detail::argument_from_json<Args>(context.arguments);
                        if constexpr (detail::is_result<decltype(handler(args))>::value) {
                            auto handled = handler(args);
                            if (!handled) {
                                return std::unexpected(handled.error());
                            }
                            return detail::value_to_tool_result(*handled);
                        } else {
                            return detail::value_to_tool_result(handler(args));
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

template <class Handler>
App::Builder& App::Builder::prompt(std::string name, Handler handler) {
    protocol::Prompt prompt;
    prompt.name = std::move(name);
    return this->prompt(std::move(prompt),
                        [handler = std::move(handler)](const PromptContext& context)
                            -> core::Result<protocol::PromptsGetResult> {
                            try {
                                if constexpr (std::is_invocable_v<Handler, const protocol::Json&>) {
                                    auto handled = handler(context.arguments);
                                    if constexpr (detail::is_result<decltype(handled)>::value) {
                                        if (!handled) {
                                            return std::unexpected(handled.error());
                                        }
                                        return detail::value_to_prompt_result(*handled);
                                    } else {
                                        return detail::value_to_prompt_result(std::move(handled));
                                    }
                                } else if constexpr (std::is_invocable_v<Handler, std::string>) {
                                    auto text = detail::argument_from_json<std::string>(context.arguments, "text");
                                    auto handled = handler(std::move(text));
                                    if constexpr (detail::is_result<decltype(handled)>::value) {
                                        if (!handled) {
                                            return std::unexpected(handled.error());
                                        }
                                        return detail::value_to_prompt_result(*handled);
                                    } else {
                                        return detail::value_to_prompt_result(std::move(handled));
                                    }
                                } else if constexpr (std::is_invocable_v<Handler>) {
                                    auto handled = handler();
                                    if constexpr (detail::is_result<decltype(handled)>::value) {
                                        if (!handled) {
                                            return std::unexpected(handled.error());
                                        }
                                        return detail::value_to_prompt_result(*handled);
                                    } else {
                                        return detail::value_to_prompt_result(std::move(handled));
                                    }
                                } else {
                                    static_assert(std::is_invocable_v<Handler, const protocol::Json&>,
                                                  "prompt handler must accept Json, string, or no arguments");
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
    protocol::Resource resource;
    resource.uri = std::move(name);
    resource.name = resource.uri;
    if constexpr (std::is_invocable_v<Handler>) {
        using Handled = decltype(handler());
        if constexpr (std::is_same_v<std::decay_t<Handled>, protocol::Resource>) {
            resource = handler();
        }
    }
    return this->resource(std::move(resource),
                          [handler = std::move(handler)](const ResourceContext& context)
                              -> core::Result<protocol::ResourcesReadResult> {
                              try {
                                  if constexpr (std::is_invocable_v<Handler, const ResourceContext&>) {
                                      auto handled = handler(context);
                                      if constexpr (detail::is_result<decltype(handled)>::value) {
                                          if (!handled) {
                                              return std::unexpected(handled.error());
                                          }
                                          return detail::value_to_resource_read_result(*handled, context.uri);
                                      } else {
                                          return detail::value_to_resource_read_result(std::move(handled), context.uri);
                                      }
                                  } else if constexpr (std::is_invocable_v<Handler>) {
                                      auto handled = handler();
                                      if constexpr (std::is_same_v<std::decay_t<decltype(handled)>, protocol::Resource>) {
                                          return protocol::ResourcesReadResult{};
                                      } else if constexpr (detail::is_result<decltype(handled)>::value) {
                                          if (!handled) {
                                              return std::unexpected(handled.error());
                                          }
                                          return detail::value_to_resource_read_result(*handled, context.uri);
                                      } else {
                                          return detail::value_to_resource_read_result(std::move(handled), context.uri);
                                      }
                                  } else {
                                      static_assert(std::is_invocable_v<Handler, const ResourceContext&>,
                                                    "resource handler must accept ResourceContext or no arguments");
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
App::Builder& App::Builder::resource_template(std::string name, Handler handler) {
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
        static_assert(std::is_invocable_v<Handler>, "resource_template handler must accept no arguments or string");
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
    builder_.on_completion([handler = std::move(handler)](const protocol::Json& request)
                               -> core::Result<protocol::Json> {
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
    builder_.on_sampling([handler = std::move(handler)](const protocol::Json& request)
                             -> core::Result<protocol::Json> {
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
    builder_.on_logging([handler = std::move(handler)](std::string_view level, std::string_view message) {
        handler(level, message);
    });
    return *this;
}

template <class Handler>
App::Builder& App::Builder::raw_request(Handler handler) {
    builder_.on_raw_request([handler = std::move(handler)](const protocol::JsonRpcRequest& request,
                                                           const SessionContext& context)
                                -> std::optional<protocol::JsonRpcResponse> {
        (void)context;
        if constexpr (std::is_same_v<std::decay_t<decltype(handler(request))>, std::optional<protocol::JsonRpcResponse>>) {
            return handler(request);
        } else if constexpr (std::is_same_v<std::decay_t<decltype(handler(request))>, protocol::JsonRpcResponse>) {
            return handler(request);
        } else {
            handler(request);
            return std::nullopt;
        }
    });
    return *this;
}

} // namespace mcp::server
