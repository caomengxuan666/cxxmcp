// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief High-level server authoring helpers and typed App builder adapters.

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cxxmcp/config.hpp"
#include "cxxmcp/server/detail/handler_dispatch.hpp"
#include "cxxmcp/server/server.hpp"

namespace mcp::server {

namespace detail {

template <class T, class = void>
struct has_tool_title : std::false_type {};

template <class T>
struct has_tool_title<T, std::void_t<decltype(T::title)>> : std::true_type {};

template <class T, class = void>
struct has_tool_description : std::false_type {};

template <class T>
struct has_tool_description<T, std::void_t<decltype(T::description)>>
    : std::true_type {};

template <class T, class = void>
struct has_tool_definition : std::false_type {};

template <class T>
struct has_tool_definition<
    T, std::void_t<decltype(std::declval<const T&>().definition())>>
    : std::true_type {};

template <class Tool, class Text>
inline std::string static_tool_text(Text value) {
  if constexpr (std::is_convertible_v<Text, std::string_view>) {
    return std::string(std::string_view(value));
  } else {
    return std::string(value);
  }
}

}  // namespace detail

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
    definition_ = protocol::tool_definition(std::move(name))
                      .input_schema(protocol::tool_input_schema_for<Args>())
                      .build();
    detail::apply_default_output_schema<Result>(definition_);
  }

  explicit TypedToolBuilder(protocol::ToolDefinition definition)
      : definition_(std::move(definition)) {}

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

  template <class T>
  TypedToolBuilder& input() {
    return input_schema(protocol::tool_input_schema_for<T>());
  }

  TypedToolBuilder& output_schema(protocol::Json schema) {
    definition_.output_schema = std::move(schema);
    definition_.output_schema_present = true;
    return *this;
  }

  template <class T>
  TypedToolBuilder& output() {
    return output_schema(protocol::tool_output_schema_for<T>());
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

  TypedToolBuilder& execution(protocol::ToolExecution value) {
    definition_.execution = std::move(value);
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
    detail::require_unambiguous_tool_handler<Handler, Args>();
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

/// @brief Builds a typed tool registration from a tool object type.
///
/// Tool must expose `using Args`, `using Result`, `static name`, and a callable
/// `operator()` compatible with the typed tool handler rules. Optional static
/// `title` and `description` members are copied into the advertised tool
/// definition.
template <class Tool>
inline auto tool(Tool value) {
  using Args = typename Tool::Args;
  using Result = typename Tool::Result;
  auto definition = [&]() {
    if constexpr (detail::has_tool_definition<Tool>::value) {
      return value.definition();
    } else {
      auto built =
          protocol::tool_definition(detail::static_tool_text<Tool>(Tool::name))
              .build();
      if constexpr (detail::has_tool_title<Tool>::value) {
        built.title = detail::static_tool_text<Tool>(Tool::title);
      }
      if constexpr (detail::has_tool_description<Tool>::value) {
        built.description = detail::static_tool_text<Tool>(Tool::description);
      }
      return built;
    }
  }();
  if (definition.input_schema.empty()) {
    definition.input_schema = protocol::tool_input_schema_for<Args>();
  }
  detail::apply_default_output_schema<Result>(definition);
  TypedToolBuilder<Args, Result> builder(std::move(definition));
  return builder.handler(std::move(value));
}

/// @brief Builds a typed tool registration from a default-constructible tool
/// object type.
template <class Tool>
inline auto tool() {
  return tool(Tool{});
}

/// @brief Typed prompt registration produced by mcp::server::prompt().
template <class Args, class Handler>
struct TypedPromptRegistration {
  protocol::Prompt prompt;
  Handler handler;
};

/// @brief Fluent typed prompt builder for low-boilerplate server authoring.
template <class Args>
class TypedPromptBuilder {
 public:
  explicit TypedPromptBuilder(protocol::Prompt prompt)
      : prompt_(std::move(prompt)) {}

  explicit TypedPromptBuilder(std::string name) {
    prompt_ = protocol::prompt_definition(std::move(name)).build();
  }

  TypedPromptBuilder& title(std::string value) {
    prompt_.title = std::move(value);
    return *this;
  }

  TypedPromptBuilder& description(std::string value) {
    prompt_.description = std::move(value);
    return *this;
  }

  TypedPromptBuilder& argument(std::string name, bool required = false,
                               std::string description = {}) {
    protocol::PromptArgument argument;
    argument.name = std::move(name);
    argument.required = required;
    argument.required_present = true;
    argument.description = std::move(description);
    prompt_.arguments.push_back(std::move(argument));
    return *this;
  }

  TypedPromptBuilder& icon(protocol::Icon value) {
    prompt_.icons.push_back(std::move(value));
    return *this;
  }

  TypedPromptBuilder& annotations(protocol::Json value) {
    prompt_.annotations = std::move(value);
    return *this;
  }

  TypedPromptBuilder& meta(protocol::Json value) {
    prompt_.meta = std::move(value);
    return *this;
  }

  template <class Handler>
  TypedPromptRegistration<Args, Handler> handler(Handler value) {
    detail::require_callable(value, "prompt");
    detail::require_unambiguous_typed_context_handler<Handler, Args,
                                                      PromptContext>("prompt");
    return TypedPromptRegistration<Args, Handler>{std::move(prompt_),
                                                  std::move(value)};
  }

 private:
  protocol::Prompt prompt_;
};

template <class Args>
inline TypedPromptBuilder<Args> prompt(std::string name) {
  return TypedPromptBuilder<Args>(std::move(name));
}

template <class Args>
inline TypedPromptBuilder<Args> prompt(protocol::Prompt prompt) {
  return TypedPromptBuilder<Args>(std::move(prompt));
}

/// @brief Typed resource registration produced by mcp::server::resource().
template <class Args, class Handler>
struct TypedResourceRegistration {
  protocol::Resource resource;
  Handler handler;
};

/// @brief Fluent typed resource builder for low-boilerplate server authoring.
template <class Args>
class TypedResourceBuilder {
 public:
  explicit TypedResourceBuilder(protocol::Resource resource)
      : resource_(std::move(resource)) {}

  TypedResourceBuilder(std::string uri, std::string name) {
    resource_ =
        protocol::resource_definition(std::move(uri), std::move(name)).build();
  }

  TypedResourceBuilder& title(std::string value) {
    resource_.title = std::move(value);
    return *this;
  }

  TypedResourceBuilder& description(std::string value) {
    resource_.description = std::move(value);
    return *this;
  }

  TypedResourceBuilder& mime_type(std::string value) {
    resource_.mime_type = std::move(value);
    return *this;
  }

  TypedResourceBuilder& size(std::int64_t value) {
    resource_.size = value;
    return *this;
  }

  TypedResourceBuilder& icon(protocol::Icon value) {
    resource_.icons.push_back(std::move(value));
    return *this;
  }

  TypedResourceBuilder& annotations(protocol::Json value) {
    resource_.annotations = std::move(value);
    return *this;
  }

  TypedResourceBuilder& meta(protocol::Json value) {
    resource_.meta = std::move(value);
    return *this;
  }

  template <class Handler>
  TypedResourceRegistration<Args, Handler> handler(Handler value) {
    detail::require_callable(value, "resource");
    detail::require_unambiguous_typed_context_handler<Handler, Args,
                                                      ResourceContext>(
        "resource");
    return TypedResourceRegistration<Args, Handler>{std::move(resource_),
                                                    std::move(value)};
  }

 private:
  protocol::Resource resource_;
};

template <class Args>
inline TypedResourceBuilder<Args> resource(std::string uri, std::string name) {
  return TypedResourceBuilder<Args>(std::move(uri), std::move(name));
}

template <class Args>
inline TypedResourceBuilder<Args> resource(protocol::Resource resource) {
  return TypedResourceBuilder<Args>(std::move(resource));
}

inline protocol::ResourceTemplateBuilder resource_template(
    std::string uri_template, std::string name) {
  return protocol::resource_template_definition(std::move(uri_template),
                                                std::move(name));
}

/// @brief Convenience entry point for compact server applications.
///
/// App::builder() exposes a higher-level builder that can create common
/// transports and adapt simple C++ callables into MCP handlers.
class CXXMCP_DEPRECATED(
    "App is a compatibility entry point; use ServerPeer::builder() with "
    "cxxmcp/run.hpp instead") App {
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

#if defined(CXXMCP_ENABLE_HTTP)
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
#endif

    /// @brief Adds a caller-supplied transport.
    /// @param value Transport owned by the built server.
    Builder& transport(std::unique_ptr<Transport> value);

    /// @brief Enables server-side task processing for task-aware tools.
    Builder& tasks(TaskOperationProcessorOptions options = {});

    /// @brief Installs an optional JSON Schema validator.
    Builder& schema_validator(
        std::shared_ptr<const JsonSchemaValidator> validator);

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
    /// prompt text/result or core::Result of either. CancellationToken may be
    /// accepted directly with Json/string where cooperative cancellation is
    /// useful.
    template <class Args, class Handler>
    Builder& prompt(std::string name, Handler handler);

    template <class Handler>
    Builder& prompt(std::string name, Handler handler);

    /// @brief Registers a fully described prompt and low-level handler.
    Builder& prompt(protocol::Prompt prompt, PromptHandler handler);

    /// @brief Registers a resource using a callable adapter.
    /// @param name Resource URI and default display name.
    /// @param handler Callable accepting Json params, requested URI string,
    /// ResourceContext, one of the Json/string plus ResourceContext
    /// combinations, or no argument. Returns resource text/contents/result,
    /// protocol::Resource metadata, or core::Result of these. CancellationToken
    /// may be accepted directly with Json/string where cooperative
    /// cancellation is useful.
    template <class Args, class Handler>
    Builder& resource(std::string name, Handler handler);

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
  CXXMCP_DEPRECATED("use ServerPeer::builder() instead")
  static Builder builder();
};

template <class Args, class Result, class Handler>
App::Builder& App::Builder::tool(std::string name, Handler handler) {
  detail::require_callable(handler, "tool");
  detail::require_unambiguous_tool_handler<Handler, Args>();
  auto definition = protocol::tool_definition(std::move(name))
                        .input_schema(protocol::tool_input_schema_for<Args>())
                        .build();
  detail::apply_default_output_schema<Result>(definition);
  return tool<Args, Result>(std::move(definition), std::move(handler));
}

template <class Args, class Result, class Handler>
App::Builder& App::Builder::tool(protocol::ToolDefinition definition,
                                 Handler handler) {
  detail::require_callable(handler, "tool");
  detail::require_unambiguous_tool_handler<Handler, Args>();
  if (definition.input_schema.empty()) {
    definition.input_schema = protocol::tool_input_schema_for<Args>();
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
              return mcp::core::unexpected(handled.error());
            }
            return detail::value_to_tool_result(*handled);
          } else {
            return detail::value_to_tool_result(std::move(handled));
          }
        } catch (const std::exception& exception) {
          return mcp::core::unexpected(core::Error{
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

template <class Args, class Handler>
App::Builder& App::Builder::prompt(std::string name, Handler handler) {
  detail::require_callable(handler, "prompt");
  detail::require_unambiguous_typed_context_handler<Handler, Args,
                                                    PromptContext>("prompt");
  protocol::Prompt prompt;
  prompt.name = std::move(name);
  return this->prompt(
      std::move(prompt),
      [handler = std::move(handler)](const PromptContext& context)
          -> core::Result<protocol::PromptsGetResult> {
        try {
          auto args = context.arguments.get<Args>();
          auto handled = detail::invoke_typed_context_handler(
              handler, std::move(args), context);
          if constexpr (detail::is_result<decltype(handled)>::value) {
            if (!handled) {
              return mcp::core::unexpected(handled.error());
            }
            return detail::value_to_prompt_result(*handled);
          } else {
            return detail::value_to_prompt_result(std::move(handled));
          }
        } catch (const std::exception& exception) {
          return mcp::core::unexpected(core::Error{
              static_cast<int>(protocol::ErrorCode::InvalidParams),
              "failed to decode prompt arguments",
              exception.what(),
          });
        }
      });
}

template <class Handler>
App::Builder& App::Builder::prompt(std::string name, Handler handler) {
  detail::require_callable(handler, "prompt");
  detail::require_unambiguous_prompt_handler<Handler>();
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
              return mcp::core::unexpected(handled.error());
            }
            return detail::value_to_prompt_result(*handled);
          } else {
            return detail::value_to_prompt_result(std::move(handled));
          }
        } catch (const std::exception& exception) {
          return mcp::core::unexpected(core::Error{
              static_cast<int>(protocol::ErrorCode::InvalidParams),
              "failed to run prompt handler",
              exception.what(),
          });
        }
      });
}

template <class Args, class Handler>
App::Builder& App::Builder::resource(std::string name, Handler handler) {
  detail::require_callable(handler, "resource");
  detail::require_unambiguous_typed_context_handler<Handler, Args,
                                                    ResourceContext>(
      "resource");
  protocol::Resource resource;
  resource.uri = std::move(name);
  resource.name = resource.uri;
  return this->resource(
      std::move(resource),
      [handler = std::move(handler)](const ResourceContext& context)
          -> core::Result<protocol::ResourcesReadResult> {
        try {
          auto args = context.params.get<Args>();
          auto handled = detail::invoke_typed_context_handler(
              handler, std::move(args), context);
          if constexpr (detail::is_result<decltype(handled)>::value) {
            if (!handled) {
              return mcp::core::unexpected(handled.error());
            }
            return detail::value_to_resource_read_result(*handled, context.uri);
          } else {
            return detail::value_to_resource_read_result(std::move(handled),
                                                         context.uri);
          }
        } catch (const std::exception& exception) {
          return mcp::core::unexpected(core::Error{
              static_cast<int>(protocol::ErrorCode::InvalidParams),
              "failed to decode resource parameters",
              exception.what(),
          });
        }
      });
}

template <class Handler>
App::Builder& App::Builder::resource(std::string name, Handler handler) {
  detail::require_callable(handler, "resource");
  detail::require_unambiguous_resource_handler<Handler>();
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
              return mcp::core::unexpected(handled.error());
            }
            return detail::value_to_resource_read_result(*handled, context.uri);
          } else {
            return detail::value_to_resource_read_result(std::move(handled),
                                                         context.uri);
          }
        } catch (const std::exception& exception) {
          return mcp::core::unexpected(core::Error{
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
  if constexpr (detail::is_typed_completion_handler_v<Handler>) {
    detail::require_unambiguous_completion_handler<Handler>();
  } else {
    detail::require_unambiguous_json_extension_handler<Handler>();
  }
  builder_.on_completion(
      [handler = std::move(handler)](const protocol::Json& request,
                                     const SessionContext& context,
                                     CancellationToken cancellation) mutable
          -> core::Result<protocol::Json> {
        if constexpr (detail::is_typed_completion_handler_v<Handler>) {
          const auto params = protocol::complete_params_from_json(request);
          if (!params) {
            return mcp::core::unexpected(core::Error{
                static_cast<int>(protocol::ErrorCode::InvalidParams),
                params.error().message, params.error().detail, "protocol"});
          }
          CompletionContext completion_context;
          static_cast<SessionContext&>(completion_context) = context;
          completion_context.params = *params;
          completion_context.cancellation = cancellation;
          auto handled =
              detail::invoke_completion_handler(handler, completion_context);
          return detail::completion_response_to_json(std::move(handled));
        } else {
          auto handled = detail::invoke_json_extension_handler(
              handler, request, context, cancellation);
          if constexpr (detail::is_result<decltype(handled)>::value) {
            return handled;
          } else {
            return detail::value_to_json(std::move(handled));
          }
        }
      });
  return *this;
}

template <class Handler>
App::Builder& App::Builder::sampling(Handler handler) {
  detail::require_callable(handler, "sampling");
  detail::require_unambiguous_json_extension_handler<Handler>();
  builder_.on_sampling(
      [handler = std::move(handler)](const protocol::Json& request,
                                     const SessionContext& context,
                                     CancellationToken cancellation) mutable
          -> core::Result<protocol::Json> {
        auto handled = detail::invoke_json_extension_handler(
            handler, request, context, cancellation);
        if constexpr (detail::is_result<decltype(handled)>::value) {
          return handled;
        } else {
          return detail::value_to_json(std::move(handled));
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
