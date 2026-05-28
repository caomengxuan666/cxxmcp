// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Internal handler dispatch helpers for cxxmcp server authoring APIs.

#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/server/context.hpp"

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

template <class Handler, class Tuple, class = void>
struct callable_arguments_match : std::false_type {};

template <class Handler, class... Args>
struct callable_arguments_match<
    Handler, std::tuple<Args...>,
    std::void_t<typename callable_argument_types<std::decay_t<Handler>>::type>>
    : std::is_same<
          typename callable_argument_types<std::decay_t<Handler>>::type,
          std::tuple<std::decay_t<Args>...>> {};

template <class Handler, class... Args>
inline constexpr bool callable_arguments_match_v =
    callable_arguments_match<Handler, std::tuple<Args...>>::value;

template <class Handler, class = void>
struct has_callable_arguments : std::false_type {};

template <class Handler>
struct has_callable_arguments<
    Handler,
    std::void_t<typename callable_argument_types<std::decay_t<Handler>>::type>>
    : std::true_type {};

template <class Handler>
inline constexpr bool has_callable_arguments_v =
    has_callable_arguments<Handler>::value;

template <bool... Values>
inline constexpr int bool_count_v = (0 + ... + (Values ? 1 : 0));

template <class Handler, class... Args>
inline constexpr bool handler_accepts_v =
    callable_arguments_match_v<Handler, Args...> ||
    (!callable_arguments_match_v<Handler, Args...> &&
     std::is_invocable_v<Handler&, Args...>);

template <class Handler, class... Args>
inline constexpr bool handler_shape_accepts_v =
    has_callable_arguments_v<Handler>
        ? callable_arguments_match_v<Handler, Args...>
        : std::is_invocable_v<Handler&, Args...>;

template <class Handler, class Args>
inline constexpr int tool_handler_match_count_v =
    bool_count_v<handler_accepts_v<Handler, Args, const ToolContext&>,
                 handler_accepts_v<Handler, const ToolContext&, Args>,
                 handler_accepts_v<Handler, Args, CancellationToken>,
                 handler_accepts_v<Handler, CancellationToken, Args>,
                 handler_accepts_v<Handler, Args>,
                 handler_accepts_v<Handler, const ToolContext&>,
                 handler_accepts_v<Handler>>;

template <class Handler, class Args, class Context>
inline constexpr int typed_context_handler_match_count_v =
    bool_count_v<handler_accepts_v<Handler, Args, const Context&>,
                 handler_accepts_v<Handler, const Context&, Args>,
                 handler_accepts_v<Handler, Args, CancellationToken>,
                 handler_accepts_v<Handler, CancellationToken, Args>,
                 handler_accepts_v<Handler, Args>,
                 handler_accepts_v<Handler, const Context&>,
                 handler_accepts_v<Handler, CancellationToken>,
                 handler_accepts_v<Handler>>;

template <class Handler>
inline constexpr int prompt_handler_exact_match_count_v = bool_count_v<
    callable_arguments_match_v<Handler, protocol::Json, PromptContext>,
    callable_arguments_match_v<Handler, PromptContext, protocol::Json>,
    callable_arguments_match_v<Handler, std::string, PromptContext>,
    callable_arguments_match_v<Handler, PromptContext, std::string>,
    callable_arguments_match_v<Handler, protocol::Json>,
    callable_arguments_match_v<Handler, std::string>,
    callable_arguments_match_v<Handler, PromptContext>,
    callable_arguments_match_v<Handler, protocol::Json, CancellationToken>,
    callable_arguments_match_v<Handler, CancellationToken, protocol::Json>,
    callable_arguments_match_v<Handler, std::string, CancellationToken>,
    callable_arguments_match_v<Handler, CancellationToken, std::string>,
    callable_arguments_match_v<Handler, CancellationToken>,
    callable_arguments_match_v<Handler>>;

template <class Handler>
inline constexpr int prompt_handler_invocable_match_count_v = bool_count_v<
    std::is_invocable_v<Handler&, protocol::Json, PromptContext>,
    std::is_invocable_v<Handler&, PromptContext, protocol::Json>,
    std::is_invocable_v<Handler&, std::string, PromptContext>,
    std::is_invocable_v<Handler&, PromptContext, std::string>,
    std::is_invocable_v<Handler&, protocol::Json>,
    std::is_invocable_v<Handler&, std::string>,
    std::is_invocable_v<Handler&, PromptContext>,
    std::is_invocable_v<Handler&, protocol::Json, CancellationToken>,
    std::is_invocable_v<Handler&, CancellationToken, protocol::Json>,
    std::is_invocable_v<Handler&, std::string, CancellationToken>,
    std::is_invocable_v<Handler&, CancellationToken, std::string>,
    std::is_invocable_v<Handler&, CancellationToken>,
    std::is_invocable_v<Handler&>>;

template <class Handler>
inline constexpr int prompt_handler_match_count_v =
    prompt_handler_exact_match_count_v<Handler> > 0
        ? prompt_handler_exact_match_count_v<Handler>
        : prompt_handler_invocable_match_count_v<Handler>;

template <class Handler>
inline constexpr int resource_handler_exact_match_count_v = bool_count_v<
    callable_arguments_match_v<Handler, protocol::Json, ResourceContext>,
    callable_arguments_match_v<Handler, ResourceContext, protocol::Json>,
    callable_arguments_match_v<Handler, std::string, ResourceContext>,
    callable_arguments_match_v<Handler, ResourceContext, std::string>,
    callable_arguments_match_v<Handler, protocol::Json>,
    callable_arguments_match_v<Handler, std::string>,
    callable_arguments_match_v<Handler, ResourceContext>,
    callable_arguments_match_v<Handler, protocol::Json, CancellationToken>,
    callable_arguments_match_v<Handler, CancellationToken, protocol::Json>,
    callable_arguments_match_v<Handler, std::string, CancellationToken>,
    callable_arguments_match_v<Handler, CancellationToken, std::string>,
    callable_arguments_match_v<Handler, CancellationToken>,
    callable_arguments_match_v<Handler>>;

template <class Handler>
inline constexpr int resource_handler_invocable_match_count_v = bool_count_v<
    std::is_invocable_v<Handler&, protocol::Json, ResourceContext>,
    std::is_invocable_v<Handler&, ResourceContext, protocol::Json>,
    std::is_invocable_v<Handler&, std::string, ResourceContext>,
    std::is_invocable_v<Handler&, ResourceContext, std::string>,
    std::is_invocable_v<Handler&, protocol::Json>,
    std::is_invocable_v<Handler&, std::string>,
    std::is_invocable_v<Handler&, ResourceContext>,
    std::is_invocable_v<Handler&, protocol::Json, CancellationToken>,
    std::is_invocable_v<Handler&, CancellationToken, protocol::Json>,
    std::is_invocable_v<Handler&, std::string, CancellationToken>,
    std::is_invocable_v<Handler&, CancellationToken, std::string>,
    std::is_invocable_v<Handler&, CancellationToken>,
    std::is_invocable_v<Handler&>>;

template <class Handler>
inline constexpr int resource_handler_match_count_v =
    resource_handler_exact_match_count_v<Handler> > 0
        ? resource_handler_exact_match_count_v<Handler>
        : resource_handler_invocable_match_count_v<Handler>;

template <class Handler>
inline constexpr int json_extension_handler_match_count_v = bool_count_v<
    handler_accepts_v<Handler, const protocol::Json&, const SessionContext&,
                      CancellationToken>,
    handler_accepts_v<Handler, const protocol::Json&, const SessionContext&>,
    handler_accepts_v<Handler, const SessionContext&, const protocol::Json&,
                      CancellationToken>,
    handler_accepts_v<Handler, const SessionContext&, const protocol::Json&>,
    handler_accepts_v<Handler, const protocol::Json&, CancellationToken>,
    handler_accepts_v<Handler, CancellationToken, const protocol::Json&>,
    handler_accepts_v<Handler, const protocol::Json&>,
    handler_accepts_v<Handler, const SessionContext&, CancellationToken>,
    handler_accepts_v<Handler, CancellationToken, const SessionContext&>,
    handler_accepts_v<Handler, const SessionContext&>,
    handler_accepts_v<Handler, CancellationToken>, handler_accepts_v<Handler>>;

template <class Handler>
inline constexpr int completion_handler_match_count_v = bool_count_v<
    handler_accepts_v<Handler, const protocol::CompleteParams&,
                      const CompletionContext&>,
    handler_accepts_v<Handler, const CompletionContext&,
                      const protocol::CompleteParams&>,
    handler_accepts_v<Handler, const protocol::CompletionArgument&,
                      const CompletionContext&>,
    handler_accepts_v<Handler, const CompletionContext&,
                      const protocol::CompletionArgument&>,
    handler_accepts_v<Handler, std::string, const CompletionContext&>,
    handler_accepts_v<Handler, const CompletionContext&, std::string>,
    handler_accepts_v<Handler, const protocol::CompleteParams&,
                      CancellationToken>,
    handler_accepts_v<Handler, CancellationToken,
                      const protocol::CompleteParams&>,
    handler_accepts_v<Handler, const protocol::CompletionArgument&,
                      CancellationToken>,
    handler_accepts_v<Handler, CancellationToken,
                      const protocol::CompletionArgument&>,
    handler_accepts_v<Handler, std::string, CancellationToken>,
    handler_accepts_v<Handler, CancellationToken, std::string>,
    handler_accepts_v<Handler, const protocol::CompleteParams&>,
    handler_accepts_v<Handler, const protocol::CompletionArgument&>,
    handler_accepts_v<Handler, std::string>,
    handler_accepts_v<Handler, CancellationToken>>;

template <class Handler, class Args>
constexpr void require_unambiguous_tool_handler() {
  static_assert(tool_handler_match_count_v<Handler, Args> <= 1,
                "ambiguous tool handler signature: use one explicit callable "
                "shape instead of a generic/default-argument handler");
}

template <class Handler, class Args, class Context>
constexpr void require_unambiguous_typed_context_handler(
    std::string_view label) {
  (void)label;
  static_assert(
      typed_context_handler_match_count_v<Handler, Args, Context> <= 1,
      "ambiguous typed handler signature: use one explicit callable "
      "shape instead of a generic/default-argument handler");
}

template <class Handler>
constexpr void require_unambiguous_prompt_handler() {
  static_assert(prompt_handler_match_count_v<Handler> <= 1,
                "ambiguous prompt handler signature: use one explicit callable "
                "shape instead of a generic/default-argument handler");
}

template <class Handler>
constexpr void require_unambiguous_resource_handler() {
  static_assert(
      resource_handler_match_count_v<Handler> <= 1,
      "ambiguous resource handler signature: use one explicit callable shape "
      "instead of a generic/default-argument handler");
}

template <class Handler>
constexpr void require_unambiguous_json_extension_handler() {
  static_assert(json_extension_handler_match_count_v<Handler> <= 1,
                "ambiguous JSON extension handler signature: use one explicit "
                "callable shape instead of a generic/default-argument handler");
}

template <class Handler>
constexpr void require_unambiguous_completion_handler() {
  static_assert(
      completion_handler_match_count_v<Handler> <= 1,
      "ambiguous completion handler signature: use one explicit callable shape "
      "instead of a generic/default-argument handler");
}

template <class T>
inline protocol::Json value_to_json(T&& value) {
  using Value = std::decay_t<T>;
  if constexpr (std::is_same_v<Value, protocol::Json>) {
    return std::forward<T>(value);
  } else if constexpr (protocol::has_reflect_v<Value>) {
    return protocol::reflect_to_json(value);
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

inline protocol::PromptsGetResult value_to_prompt_result(
    protocol::PromptMessage message) {
  protocol::PromptsGetResult result;
  result.messages.push_back(std::move(message));
  return result;
}

inline protocol::PromptsGetResult value_to_prompt_result(
    std::vector<protocol::PromptMessage> messages) {
  protocol::PromptsGetResult result;
  result.messages = std::move(messages);
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
    std::vector<protocol::ResourceContents> contents, std::string_view) {
  protocol::ResourcesReadResult result;
  result.contents = std::move(contents);
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

inline protocol::Json value_to_complete_result_json(protocol::Json json) {
  return json;
}

inline protocol::Json value_to_complete_result_json(
    protocol::CompleteResult result) {
  return protocol::complete_result_to_json(result);
}

inline protocol::Json value_to_complete_result_json(
    protocol::CompletionResult result) {
  protocol::CompleteResult envelope;
  envelope.completion = std::move(result);
  return protocol::complete_result_to_json(envelope);
}

inline protocol::Json value_to_complete_result_json(
    std::vector<std::string> values) {
  protocol::CompletionResult completion;
  completion.values = std::move(values);
  return value_to_complete_result_json(std::move(completion));
}

inline protocol::Json value_to_complete_result_json(std::string value) {
  return value_to_complete_result_json(
      std::vector<std::string>{std::move(value)});
}

inline protocol::Json value_to_complete_result_json(const char* value) {
  return value_to_complete_result_json(
      std::string(value == nullptr ? "" : value));
}

template <class T>
inline core::Result<protocol::Json> completion_response_to_json(T&& value) {
  using Value = std::decay_t<T>;
  if constexpr (is_result<Value>::value) {
    if (!value) {
      return mcp::core::unexpected(value.error());
    }
    return completion_response_to_json(std::move(*value));
  } else {
    return value_to_complete_result_json(std::forward<T>(value));
  }
}

template <class Arg>
inline Arg argument_from_json(const protocol::Json& arguments,
                              std::string_view fallback_name = {}) {
  using Decayed = std::decay_t<Arg>;
  if constexpr (std::is_same_v<Decayed, protocol::Json>) {
    return arguments;
  } else if constexpr (protocol::has_reflect_v<Decayed>) {
    protocol::Json effective = arguments;
    if (!fallback_name.empty() && arguments.is_object() &&
        arguments.contains(std::string(fallback_name))) {
      effective = arguments.at(std::string(fallback_name));
    } else if (arguments.is_object() && arguments.size() == 1) {
      effective = arguments.begin().value();
    }
    auto result = protocol::reflect_from_json<Decayed>(effective);
    if (!result) {
      throw std::invalid_argument(result.error().message);
    }
    return std::move(*result);
  } else {
    if (!fallback_name.empty() && arguments.is_object() &&
        arguments.contains(std::string(fallback_name))) {
      return arguments.at(std::string(fallback_name)).template get<Arg>();
    }
    if (arguments.is_object() && arguments.size() == 1) {
      return arguments.begin().value().template get<Arg>();
    }
    return arguments.template get<Arg>();
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
      definition.output_schema_present = true;
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

template <class Handler, class Args, class Context>
decltype(auto) invoke_typed_context_handler(Handler& handler, Args&& args,
                                            const Context& context) {
  using Arg = std::decay_t<Args>;
  if constexpr (std::is_invocable_v<Handler&, Arg, const Context&>) {
    return handler(std::forward<Args>(args), context);
  } else if constexpr (std::is_invocable_v<Handler&, const Context&, Arg>) {
    return handler(context, std::forward<Args>(args));
  } else if constexpr (std::is_invocable_v<Handler&, Arg, CancellationToken>) {
    return handler(std::forward<Args>(args), context.cancellation);
  } else if constexpr (std::is_invocable_v<Handler&, CancellationToken, Arg>) {
    return handler(context.cancellation, std::forward<Args>(args));
  } else if constexpr (std::is_invocable_v<Handler&, Arg>) {
    return handler(std::forward<Args>(args));
  } else if constexpr (std::is_invocable_v<Handler&, const Context&>) {
    return handler(context);
  } else if constexpr (std::is_invocable_v<Handler&, CancellationToken>) {
    return handler(context.cancellation);
  } else if constexpr (std::is_invocable_v<Handler&>) {
    return handler();
  } else {
    static_assert(always_false_v<Handler>,
                  "typed handler must accept Args, Args+Context, "
                  "Context+Args, Args+CancellationToken, "
                  "CancellationToken+Args, Context, CancellationToken, "
                  "or no arguments");
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
  } else if constexpr (callable_arguments_match_v<Handler, Json,
                                                  CancellationToken>) {
    return handler(context.arguments, context.cancellation);
  } else if constexpr (callable_arguments_match_v<Handler, CancellationToken,
                                                  Json>) {
    return handler(context.cancellation, context.arguments);
  } else if constexpr (callable_arguments_match_v<Handler, std::string,
                                                  CancellationToken>) {
    auto text = argument_from_json<std::string>(context.arguments,
                                                std::string_view("text"));
    return handler(std::move(text), context.cancellation);
  } else if constexpr (callable_arguments_match_v<Handler, CancellationToken,
                                                  std::string>) {
    auto text = argument_from_json<std::string>(context.arguments,
                                                std::string_view("text"));
    return handler(context.cancellation, std::move(text));
  } else if constexpr (callable_arguments_match_v<Handler, CancellationToken>) {
    return handler(context.cancellation);
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
                  "PromptContext+string, PromptContext, Json/string plus "
                  "CancellationToken, CancellationToken, or no arguments");
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
  } else if constexpr (callable_arguments_match_v<Handler, Json,
                                                  CancellationToken>) {
    return handler(context.params, context.cancellation);
  } else if constexpr (callable_arguments_match_v<Handler, CancellationToken,
                                                  Json>) {
    return handler(context.cancellation, context.params);
  } else if constexpr (callable_arguments_match_v<Handler, std::string,
                                                  CancellationToken>) {
    return handler(context.uri, context.cancellation);
  } else if constexpr (callable_arguments_match_v<Handler, CancellationToken,
                                                  std::string>) {
    return handler(context.cancellation, context.uri);
  } else if constexpr (callable_arguments_match_v<Handler, CancellationToken>) {
    return handler(context.cancellation);
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
                  "ResourceContext+string, ResourceContext, Json/string plus "
                  "CancellationToken, CancellationToken, or no arguments");
  }
}

template <class Handler>
decltype(auto) invoke_json_extension_handler(Handler& handler,
                                             const protocol::Json& request,
                                             const SessionContext& context,
                                             CancellationToken cancellation) {
  using Json = protocol::Json;
  if constexpr (std::is_invocable_v<Handler&, const Json&,
                                    const SessionContext&, CancellationToken>) {
    return handler(request, context, cancellation);
  } else if constexpr (std::is_invocable_v<Handler&, const Json&,
                                           const SessionContext&>) {
    return handler(request, context);
  } else if constexpr (std::is_invocable_v<Handler&, const SessionContext&,
                                           const Json&, CancellationToken>) {
    return handler(context, request, cancellation);
  } else if constexpr (std::is_invocable_v<Handler&, const SessionContext&,
                                           const Json&>) {
    return handler(context, request);
  } else if constexpr (std::is_invocable_v<Handler&, const Json&,
                                           CancellationToken>) {
    return handler(request, cancellation);
  } else if constexpr (std::is_invocable_v<Handler&, CancellationToken,
                                           const Json&>) {
    return handler(cancellation, request);
  } else if constexpr (std::is_invocable_v<Handler&, const Json&>) {
    return handler(request);
  } else if constexpr (std::is_invocable_v<Handler&, const SessionContext&,
                                           CancellationToken>) {
    return handler(context, cancellation);
  } else if constexpr (std::is_invocable_v<Handler&, CancellationToken,
                                           const SessionContext&>) {
    return handler(cancellation, context);
  } else if constexpr (std::is_invocable_v<Handler&, const SessionContext&>) {
    return handler(context);
  } else if constexpr (std::is_invocable_v<Handler&, CancellationToken>) {
    return handler(cancellation);
  } else if constexpr (std::is_invocable_v<Handler&>) {
    return handler();
  } else {
    static_assert(always_false_v<Handler>,
                  "JSON extension handler must accept Json, "
                  "Json+SessionContext, SessionContext+Json, "
                  "Json/SessionContext plus CancellationToken, "
                  "CancellationToken, or no arguments");
  }
}

template <class Handler>
inline constexpr bool is_typed_completion_handler_v =
    !callable_arguments_match_v<Handler, protocol::Json, SessionContext> &&
    !callable_arguments_match_v<Handler, SessionContext, protocol::Json> &&
    !callable_arguments_match_v<Handler, protocol::Json> &&
    (callable_arguments_match_v<Handler, protocol::CompleteParams,
                                CompletionContext> ||
     callable_arguments_match_v<Handler, CompletionContext,
                                protocol::CompleteParams> ||
     callable_arguments_match_v<Handler, protocol::CompletionArgument,
                                CompletionContext> ||
     callable_arguments_match_v<Handler, CompletionContext,
                                protocol::CompletionArgument> ||
     callable_arguments_match_v<Handler, std::string, CompletionContext> ||
     callable_arguments_match_v<Handler, CompletionContext, std::string> ||
     callable_arguments_match_v<Handler, protocol::CompleteParams,
                                CancellationToken> ||
     callable_arguments_match_v<Handler, CancellationToken,
                                protocol::CompleteParams> ||
     callable_arguments_match_v<Handler, protocol::CompletionArgument,
                                CancellationToken> ||
     callable_arguments_match_v<Handler, CancellationToken,
                                protocol::CompletionArgument> ||
     callable_arguments_match_v<Handler, std::string, CancellationToken> ||
     callable_arguments_match_v<Handler, CancellationToken, std::string> ||
     callable_arguments_match_v<Handler, protocol::CompleteParams> ||
     callable_arguments_match_v<Handler, protocol::CompletionArgument> ||
     callable_arguments_match_v<Handler, std::string> ||
     callable_arguments_match_v<Handler, CancellationToken>);

template <class Handler>
decltype(auto) invoke_completion_handler(Handler& handler,
                                         const CompletionContext& context) {
  if constexpr (std::is_invocable_v<Handler&, const protocol::CompleteParams&,
                                    const CompletionContext&>) {
    return handler(context.params, context);
  } else if constexpr (std::is_invocable_v<Handler&, const CompletionContext&,
                                           const protocol::CompleteParams&>) {
    return handler(context, context.params);
  } else if constexpr (std::is_invocable_v<Handler&,
                                           const protocol::CompletionArgument&,
                                           const CompletionContext&>) {
    return handler(context.params.argument, context);
  } else if constexpr (std::is_invocable_v<
                           Handler&, const CompletionContext&,
                           const protocol::CompletionArgument&>) {
    return handler(context, context.params.argument);
  } else if constexpr (std::is_invocable_v<Handler&, std::string,
                                           const CompletionContext&>) {
    return handler(context.params.argument.value, context);
  } else if constexpr (std::is_invocable_v<Handler&, const CompletionContext&,
                                           std::string>) {
    return handler(context, context.params.argument.value);
  } else if constexpr (std::is_invocable_v<Handler&,
                                           const protocol::CompleteParams&,
                                           CancellationToken>) {
    return handler(context.params, context.cancellation);
  } else if constexpr (std::is_invocable_v<Handler&, CancellationToken,
                                           const protocol::CompleteParams&>) {
    return handler(context.cancellation, context.params);
  } else if constexpr (std::is_invocable_v<Handler&,
                                           const protocol::CompletionArgument&,
                                           CancellationToken>) {
    return handler(context.params.argument, context.cancellation);
  } else if constexpr (std::is_invocable_v<
                           Handler&, CancellationToken,
                           const protocol::CompletionArgument&>) {
    return handler(context.cancellation, context.params.argument);
  } else if constexpr (std::is_invocable_v<Handler&, std::string,
                                           CancellationToken>) {
    return handler(context.params.argument.value, context.cancellation);
  } else if constexpr (std::is_invocable_v<Handler&, CancellationToken,
                                           std::string>) {
    return handler(context.cancellation, context.params.argument.value);
  } else if constexpr (std::is_invocable_v<Handler&,
                                           const protocol::CompleteParams&>) {
    return handler(context.params);
  } else if constexpr (std::is_invocable_v<
                           Handler&, const protocol::CompletionArgument&>) {
    return handler(context.params.argument);
  } else if constexpr (std::is_invocable_v<Handler&, std::string>) {
    return handler(context.params.argument.value);
  } else if constexpr (std::is_invocable_v<Handler&, CancellationToken>) {
    return handler(context.cancellation);
  } else {
    static_assert(always_false_v<Handler>,
                  "completion handler must accept CompleteParams, "
                  "CompletionArgument, string, or those plus "
                  "CompletionContext or CancellationToken");
  }
}

}  // namespace detail

}  // namespace mcp::server
