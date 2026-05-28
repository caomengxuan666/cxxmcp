// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/server/router.hpp
/// @brief Composable server route sets for low-boilerplate MCP authoring.

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/tool.hpp"
#include "cxxmcp/server/authoring.hpp"
#include "cxxmcp/server/registry.hpp"

namespace mcp::server {

/// @brief Change callback fired when a router's exposed route set changes.
using RouterChangedHandler = std::function<void()>;

/// @brief Composable set of tool routes.
class ToolRouter {
 public:
  struct Route {
    protocol::ToolDefinition definition;
    ToolHandler handler;
    bool enabled = true;
  };

  ToolRouter& add(protocol::ToolDefinition definition, ToolHandler handler) {
    routes_.push_back(Route{std::move(definition), std::move(handler), true});
    notify_changed();
    return *this;
  }

  ToolRouter& tool(protocol::ToolDefinition definition, ToolHandler handler) {
    return add(std::move(definition), std::move(handler));
  }

  template <class Args, class Result, class Handler>
  ToolRouter& tool(TypedToolRegistration<Args, Result, Handler> registration) {
    return add(
        std::move(registration.definition),
        [handler = std::move(registration.handler)](
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

  ToolRouter& merge(const ToolRouter& other) {
    for (const auto& route : other.routes_) {
      routes_.push_back(route);
    }
    notify_changed();
    return *this;
  }

  ToolRouter& remove(std::string_view name) {
    const auto original_size = routes_.size();
    routes_.erase(std::remove_if(routes_.begin(), routes_.end(),
                                 [name](const Route& route) {
                                   return route.definition.name == name;
                                 }),
                  routes_.end());
    if (routes_.size() != original_size) {
      notify_changed();
    }
    return *this;
  }

  ToolRouter& clear() {
    if (!routes_.empty()) {
      routes_.clear();
      notify_changed();
    }
    return *this;
  }

  ToolRouter& enable(std::string_view name, bool enabled = true) {
    for (auto& route : routes_) {
      if (route.definition.name == name) {
        route.enabled = enabled;
      }
    }
    notify_changed();
    return *this;
  }

  ToolRouter& disable(std::string_view name) { return enable(name, false); }

  ToolRouter& on_changed(RouterChangedHandler handler) {
    on_changed_ = std::move(handler);
    return *this;
  }

  template <class Notifier>
  ToolRouter& bind(Notifier& notifier) {
    auto previous = std::move(on_changed_);
    on_changed_ = [previous = std::move(previous), &notifier] {
      if (previous) {
        previous();
      }
      (void)notifier.notify_tool_list_changed();
    };
    return *this;
  }

  const std::vector<Route>& routes() const noexcept { return routes_; }

  ServerBuilder& apply_to(ServerBuilder& builder) const {
    for (const auto& route : routes_) {
      if (route.enabled) {
        builder.add_tool(route.definition, route.handler);
      }
    }
    return builder;
  }

 private:
  void notify_changed() const {
    if (on_changed_) {
      on_changed_();
    }
  }

  std::vector<Route> routes_;
  RouterChangedHandler on_changed_;
};

/// @brief Composable set of prompt routes.
class PromptRouter {
 public:
  struct Route {
    protocol::Prompt prompt;
    PromptHandler handler;
    bool enabled = true;
  };

  PromptRouter& add(protocol::Prompt prompt, PromptHandler handler) {
    routes_.push_back(Route{std::move(prompt), std::move(handler), true});
    notify_changed();
    return *this;
  }

  PromptRouter& prompt(protocol::Prompt prompt, PromptHandler handler) {
    return add(std::move(prompt), std::move(handler));
  }

  PromptRouter& merge(const PromptRouter& other) {
    for (const auto& route : other.routes_) {
      routes_.push_back(route);
    }
    notify_changed();
    return *this;
  }

  PromptRouter& remove(std::string_view name) {
    const auto original_size = routes_.size();
    routes_.erase(std::remove_if(routes_.begin(), routes_.end(),
                                 [name](const Route& route) {
                                   return route.prompt.name == name;
                                 }),
                  routes_.end());
    if (routes_.size() != original_size) {
      notify_changed();
    }
    return *this;
  }

  PromptRouter& clear() {
    if (!routes_.empty()) {
      routes_.clear();
      notify_changed();
    }
    return *this;
  }

  PromptRouter& enable(std::string_view name, bool enabled = true) {
    for (auto& route : routes_) {
      if (route.prompt.name == name) {
        route.enabled = enabled;
      }
    }
    notify_changed();
    return *this;
  }

  PromptRouter& disable(std::string_view name) { return enable(name, false); }

  PromptRouter& on_changed(RouterChangedHandler handler) {
    on_changed_ = std::move(handler);
    return *this;
  }

  template <class Notifier>
  PromptRouter& bind(Notifier& notifier) {
    auto previous = std::move(on_changed_);
    on_changed_ = [previous = std::move(previous), &notifier] {
      if (previous) {
        previous();
      }
      (void)notifier.notify_prompt_list_changed();
    };
    return *this;
  }

  const std::vector<Route>& routes() const noexcept { return routes_; }

  ServerBuilder& apply_to(ServerBuilder& builder) const {
    for (const auto& route : routes_) {
      if (route.enabled) {
        builder.add_prompt(route.prompt, route.handler);
      }
    }
    return builder;
  }

 private:
  void notify_changed() const {
    if (on_changed_) {
      on_changed_();
    }
  }

  std::vector<Route> routes_;
  RouterChangedHandler on_changed_;
};

/// @brief Composable set of resource routes and resource templates.
class ResourceRouter {
 public:
  struct ResourceRoute {
    protocol::Resource resource;
    ResourceReadHandler handler;
    bool enabled = true;
  };

  struct TemplateRoute {
    protocol::ResourceTemplate resource_template;
    bool enabled = true;
  };

  ResourceRouter& add(protocol::Resource resource,
                      ResourceReadHandler handler) {
    resources_.push_back(
        ResourceRoute{std::move(resource), std::move(handler), true});
    notify_changed();
    return *this;
  }

  ResourceRouter& resource(protocol::Resource resource,
                           ResourceReadHandler handler) {
    return add(std::move(resource), std::move(handler));
  }

  ResourceRouter& add_template(protocol::ResourceTemplate resource_template) {
    templates_.push_back(TemplateRoute{std::move(resource_template), true});
    notify_changed();
    return *this;
  }

  ResourceRouter& resource_template(
      protocol::ResourceTemplate resource_template) {
    return add_template(std::move(resource_template));
  }

  ResourceRouter& merge(const ResourceRouter& other) {
    for (const auto& route : other.resources_) {
      resources_.push_back(route);
    }
    for (const auto& route : other.templates_) {
      templates_.push_back(route);
    }
    notify_changed();
    return *this;
  }

  ResourceRouter& remove_resource(std::string_view uri) {
    const auto original_size = resources_.size();
    resources_.erase(std::remove_if(resources_.begin(), resources_.end(),
                                    [uri](const ResourceRoute& route) {
                                      return route.resource.uri == uri;
                                    }),
                     resources_.end());
    if (resources_.size() != original_size) {
      notify_changed();
    }
    return *this;
  }

  ResourceRouter& remove_template(std::string_view uri_template) {
    const auto original_size = templates_.size();
    templates_.erase(
        std::remove_if(templates_.begin(), templates_.end(),
                       [uri_template](const TemplateRoute& route) {
                         return route.resource_template.uri_template ==
                                uri_template;
                       }),
        templates_.end());
    if (templates_.size() != original_size) {
      notify_changed();
    }
    return *this;
  }

  ResourceRouter& clear_resources() {
    if (!resources_.empty()) {
      resources_.clear();
      notify_changed();
    }
    return *this;
  }

  ResourceRouter& clear_templates() {
    if (!templates_.empty()) {
      templates_.clear();
      notify_changed();
    }
    return *this;
  }

  ResourceRouter& clear() {
    if (!resources_.empty() || !templates_.empty()) {
      resources_.clear();
      templates_.clear();
      notify_changed();
    }
    return *this;
  }

  ResourceRouter& enable_resource(std::string_view uri, bool enabled = true) {
    for (auto& route : resources_) {
      if (route.resource.uri == uri) {
        route.enabled = enabled;
      }
    }
    notify_changed();
    return *this;
  }

  ResourceRouter& disable_resource(std::string_view uri) {
    return enable_resource(uri, false);
  }

  ResourceRouter& enable_template(std::string_view uri_template,
                                  bool enabled = true) {
    for (auto& route : templates_) {
      if (route.resource_template.uri_template == uri_template) {
        route.enabled = enabled;
      }
    }
    notify_changed();
    return *this;
  }

  ResourceRouter& disable_template(std::string_view uri_template) {
    return enable_template(uri_template, false);
  }

  ResourceRouter& on_changed(RouterChangedHandler handler) {
    on_changed_ = std::move(handler);
    return *this;
  }

  template <class Notifier>
  ResourceRouter& bind(Notifier& notifier) {
    auto previous = std::move(on_changed_);
    on_changed_ = [previous = std::move(previous), &notifier] {
      if (previous) {
        previous();
      }
      (void)notifier.notify_resource_list_changed();
    };
    return *this;
  }

  const std::vector<ResourceRoute>& resources() const noexcept {
    return resources_;
  }

  const std::vector<TemplateRoute>& templates() const noexcept {
    return templates_;
  }

  ServerBuilder& apply_to(ServerBuilder& builder) const {
    for (const auto& route : resources_) {
      if (route.enabled) {
        builder.add_resource(route.resource, route.handler);
      }
    }
    for (const auto& route : templates_) {
      if (route.enabled) {
        builder.add_resource_template(route.resource_template);
      }
    }
    return builder;
  }

 private:
  void notify_changed() const {
    if (on_changed_) {
      on_changed_();
    }
  }

  std::vector<ResourceRoute> resources_;
  std::vector<TemplateRoute> templates_;
  RouterChangedHandler on_changed_;
};

}  // namespace mcp::server
