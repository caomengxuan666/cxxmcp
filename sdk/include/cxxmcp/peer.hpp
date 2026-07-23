// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Role-aware peer execution boundaries for MCP client and server SDK
/// users.
///
/// Peer<RoleClient> and Peer<RoleServer> are the SDK-facing MCP execution
/// boundary. They expose role-generic message dispatch loops so
/// Transport<Role> can be the public service boundary while concrete Client and
/// Server types remain lower-level convenience APIs.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "cxxmcp/cancellation.hpp"
#include "cxxmcp/client/client.hpp"
#include "cxxmcp/client/session.hpp"
#include "cxxmcp/client/transport_adapter_fwd.hpp"
#include "cxxmcp/config.hpp"
#include "cxxmcp/error.hpp"
#include "cxxmcp/handler.hpp"
#include "cxxmcp/protocol/initialize.hpp"
#include "cxxmcp/roles.hpp"
#include "cxxmcp/server/authoring.hpp"
#include "cxxmcp/server/peer.hpp"
#include "cxxmcp/server/transport_adapter_fwd.hpp"
#if defined(CXXMCP_ENABLE_HTTP)
#include "cxxmcp/transport/http_transport.hpp"
#endif
#if defined(CXXMCP_ENABLE_WEBSOCKET)
#include "cxxmcp/transport/websocket_transport.hpp"
#endif
#include "cxxmcp/transport/process_stdio_transport.hpp"
#include "cxxmcp/transport/stdio_transport.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp {

namespace detail {

inline core::Error peer_dispatch_error(std::string_view message) {
  return errors::make(protocol::ErrorCode::InvalidRequest,
                      std::string(message));
}

inline core::Error peer_transport_error(std::string_view message) {
  return errors::make(protocol::ErrorCode::InvalidRequest, std::string(message),
                      {}, "transport");
}

inline protocol::ErrorObject peer_error_object_from_core_error(
    const core::Error& error) {
  return errors::to_json_rpc_error(error);
}

inline protocol::JsonRpcResponse peer_error_response(
    const protocol::JsonRpcRequest& request, const core::Error& error) {
  return protocol::make_error_response(
      request.id, peer_error_object_from_core_error(error));
}

inline protocol::JsonRpcResponse peer_auth_error_response(
    const protocol::JsonRpcRequest& request, const core::Error& error) {
  protocol::Json data = protocol::Json::object();
  data["category"] = std::string(server::AuthErrorCategory);
  if (!error.detail.empty()) {
    data["detail"] = error.detail;
  } else if (!error.message.empty()) {
    data["detail"] = error.message;
  }
  return protocol::make_error_response(
      request.id,
      protocol::make_error(protocol::ErrorCode::PermissionDenied,
                           "authentication failed", std::move(data)));
}

inline core::Error peer_params_error(core::Error error) {
  if (error.code == static_cast<int>(protocol::ErrorCode::InvalidRequest)) {
    error.code = static_cast<int>(protocol::ErrorCode::InvalidParams);
  }
  return error;
}

inline protocol::JsonRpcResponse peer_params_error_response(
    const protocol::JsonRpcRequest& request, core::Error error) {
  return peer_error_response(request, peer_params_error(std::move(error)));
}

inline std::string peer_request_cancellation_key(
    const protocol::RequestId& request_id) {
  return protocol::request_id_to_json(request_id).dump();
}

inline core::Result<protocol::Json> peer_require_result_payload(
    const protocol::JsonRpcResponse& response) {
  if (response.error.has_value()) {
    return mcp::core::unexpected(core::Error{
        response.error->code,
        response.error->message,
        response.error->data.has_value() ? response.error->data->dump()
                                         : std::string{},
        "protocol",
    });
  }
  if (!response.result.has_value()) {
    return mcp::core::unexpected(
        errors::make(protocol::ErrorCode::InvalidRequest,
                     "response did not contain a result", {}, "protocol"));
  }
  return *response.result;
}

template <class Transport, class Dispatch>
inline core::Result<core::Unit> serve_transport_loop(
    Transport& transport, CancellationToken cancellation, Dispatch dispatch) {
  while (!cancellation.cancelled()) {
    auto received = transport.receive();
    if (!received) {
      return mcp::core::unexpected(received.error());
    }
    if (!received->has_value()) {
      return core::Unit{};
    }

    auto dispatched = dispatch(received->value());
    if (!dispatched) {
      return mcp::core::unexpected(dispatched.error());
    }
    if (dispatched->has_value()) {
      auto sent = transport.send(std::move(dispatched->value()));
      if (!sent) {
        return mcp::core::unexpected(sent.error());
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

inline server::SessionContext context_for_received_server_message(
    transport::ServerTransport& transport,
    const server::SessionContext& fallback) {
  server::SessionContext context = fallback;
#if defined(CXXMCP_ENABLE_HTTP)
  auto* http_transport =
      dynamic_cast<transport::StreamableHttpServerTransport*>(&transport);
  if (http_transport == nullptr) {
    return context;
  }

  const auto http_context = http_transport->last_received_context();
  if (!http_context.has_value()) {
    return context;
  }

  context.session_id = http_context->session_id;
  if (!http_context->remote_address.empty()) {
    context.remote_address = http_context->remote_address;
  }
  context.headers = http_context->headers;
  context.http_method = http_context->http_method;
  context.http_url = http_context->http_url;
#endif
  return context;
}

struct ClientNativeReceiveState {
  std::mutex mutex;
  std::condition_variable cv;
  std::unordered_map<std::string, protocol::JsonRpcResponse> responses;
  std::optional<core::Error> failure;
  bool loop_active = false;
  bool closed = false;
};

inline std::unique_ptr<client::Transport> make_peer_client_transport_adapter(
    std::unique_ptr<transport::ClientTransport>& transport) {
  if (!transport) {
    return client::make_contract_transport_adapter(
        std::unique_ptr<transport::ClientTransport>{});
  }
  return client::make_contract_transport_adapter(*transport);
}

inline protocol::ClientCapabilities default_peer_client_capabilities(
    const std::optional<protocol::ClientCapabilities>& capabilities) {
  if (capabilities.has_value()) {
    return *capabilities;
  }

  protocol::ClientCapabilities defaults;
  defaults.roots.enabled = true;
  defaults.roots.list_changed = true;
  defaults.sampling.enabled = true;
  defaults.sampling.tools = true;
  defaults.sampling.context = true;
  defaults.elicitation.form = true;
  defaults.elicitation.form_schema_validation = true;
  defaults.elicitation.url = true;
  return defaults;
}

inline protocol::Json make_peer_initialize_params(
    std::string client_name, std::string client_version,
    const std::optional<protocol::ClientCapabilities>& capabilities) {
  protocol::Json params = protocol::Json::object();
  params["protocolVersion"] = std::string(protocol::McpProtocolVersion);
  params["capabilities"] = protocol::client_capabilities_to_json(
      default_peer_client_capabilities(capabilities));
  params["clientInfo"] = protocol::Json{
      {"name", std::move(client_name)},
      {"version", std::move(client_version)},
  };
  return params;
}

inline core::Result<protocol::Json> require_peer_initialize_payload(
    const protocol::Json& payload) {
  // Accept minimal responses (e.g. from conformance harness) that don't have
  // all required fields. This allows version negotiation to succeed even when
  // the server returns a simplified response.
  if (payload.is_object() && !payload.contains("protocolVersion")) {
    return payload;
  }
  const auto parsed = protocol::initialize_result_from_json(payload);
  if (!parsed) {
    return mcp::core::unexpected(errors::make(
        protocol::ErrorCode::InvalidRequest, parsed.error().message,
        parsed.error().detail, "protocol"));
  }
  if (!protocol::is_supported_protocol_version(parsed->protocol_version)) {
    return mcp::core::unexpected(
        errors::make(protocol::ErrorCode::InvalidRequest,
                     "unsupported MCP protocol version(\"" +
                         parsed->protocol_version + "\")",
                     {}, "protocol"));
  }
  return payload;
}

inline core::Result<core::Unit> validate_peer_server_initialize_params(
    const protocol::Json& params) {
  if (!params.is_object()) {
    return mcp::core::unexpected(
        errors::make(protocol::ErrorCode::InvalidParams,
                     "initialize params must be an object"));
  }
  if (!params.contains("protocolVersion") ||
      !params.at("protocolVersion").is_string()) {
    return mcp::core::unexpected(
        errors::make(protocol::ErrorCode::InvalidParams,
                     "initialize requires a string protocolVersion"));
  }
  const auto requested_version =
      params.at("protocolVersion").get<std::string>();
  if (!protocol::is_supported_protocol_version(requested_version)) {
    return mcp::core::unexpected(errors::make(
        protocol::ErrorCode::InvalidParams,
        "unsupported MCP protocol version(\"" + requested_version + "\")"));
  }

  return core::Unit{};
}

inline protocol::Json make_peer_server_initialize_result(
    const server::ServerInfo& info,
    const protocol::ServerCapabilities& capabilities,
    std::string_view protocol_version = protocol::McpProtocolVersion) {
  protocol::Json result = protocol::Json::object();
  result["protocolVersion"] = std::string(protocol_version);
  result["capabilities"] = protocol::server_capabilities_to_json(capabilities);
  result["serverInfo"] = protocol::Json{
      {"name", info.name},
      {"version", info.version},
  };
  if (!info.instructions.empty()) {
    result["instructions"] = info.instructions;
  }
  return result;
}

}  // namespace detail

/// @brief Role-specialized MCP peer boundary.
template <class Role>
class Peer;

/// @brief Client-side peer boundary for talking to an MCP server.
///
/// When constructed with a role-generic ClientTransport, request/response
/// helpers serialize native transport access. This preserves the transport
/// contract that receive() is single-consumer and avoids competing receive
/// loops until a dedicated client receive pump is introduced.
template <>
class Peer<RoleClient> {
 public:
  class Builder;
  class TaskHandle {
   public:
    TaskHandle() = default;

    TaskHandle(Peer& peer, protocol::Task task)
        : peer_(&peer), task_(std::move(task)) {}

    const std::string& id() const noexcept { return task_.task_id; }

    protocol::TaskStatus status() const noexcept { return task_.status; }

    const protocol::Task& snapshot() const noexcept { return task_; }

    core::Result<protocol::Task> poll() {
      if (peer_ == nullptr) {
        return mcp::core::unexpected(detached_error());
      }
      auto task = peer_->get_task(task_.task_id);
      if (!task) {
        return mcp::core::unexpected(task.error());
      }
      task_ = *task;
      return task_;
    }

    core::Result<protocol::Task> cancel() {
      if (peer_ == nullptr) {
        return mcp::core::unexpected(detached_error());
      }
      auto task = peer_->cancel_task(task_.task_id);
      if (!task) {
        return mcp::core::unexpected(task.error());
      }
      task_ = *task;
      return task_;
    }

    core::Result<protocol::Json> result() {
      if (peer_ == nullptr) {
        return mcp::core::unexpected(detached_error());
      }
      return peer_->task_result(task_.task_id);
    }

    template <class T>
    core::Result<T> result_as() {
      auto payload = result();
      if (!payload) {
        return mcp::core::unexpected(payload.error());
      }
      try {
        if (payload->is_object() && payload->contains("value")) {
          return payload->at("value").template get<T>();
        }
        return payload->template get<T>();
      } catch (const std::exception& exception) {
        return mcp::core::unexpected(errors::make(
            protocol::ErrorCode::InvalidRequest, "failed to decode task result",
            exception.what(), "protocol"));
      } catch (...) {
        return mcp::core::unexpected(
            errors::make(protocol::ErrorCode::InvalidRequest,
                         "failed to decode task result", {}, "protocol"));
      }
    }

    core::Result<protocol::Task> wait(std::chrono::milliseconds timeout,
                                      std::chrono::milliseconds poll_interval =
                                          std::chrono::milliseconds(100)) {
      if (peer_ == nullptr) {
        return mcp::core::unexpected(detached_error());
      }
      if (poll_interval <= std::chrono::milliseconds::zero()) {
        poll_interval = std::chrono::milliseconds(1);
      }

      const auto started_at = std::chrono::steady_clock::now();
      while (true) {
        if (is_terminal(task_.status)) {
          return task_;
        }
        if (std::chrono::steady_clock::now() - started_at >= timeout) {
          return mcp::core::unexpected(errors::request_timed_out(timeout));
        }

        auto task = poll();
        if (!task) {
          return mcp::core::unexpected(task.error());
        }
        if (is_terminal(task->status)) {
          return *task;
        }

        const auto elapsed = std::chrono::steady_clock::now() - started_at;
        if (elapsed >= timeout) {
          return mcp::core::unexpected(errors::request_timed_out(timeout));
        }
        auto sleep_for = poll_interval;
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout -
                                                                  elapsed);
        if (remaining > std::chrono::milliseconds::zero() &&
            remaining < sleep_for) {
          sleep_for = remaining;
        }
        std::this_thread::sleep_for(sleep_for);
      }
    }

   private:
    static bool is_terminal(protocol::TaskStatus status) noexcept {
      return status == protocol::TaskStatus::Completed ||
             status == protocol::TaskStatus::Failed ||
             status == protocol::TaskStatus::Cancelled;
    }

    static core::Error detached_error() {
      return errors::make(protocol::ErrorCode::InvalidRequest,
                          "task handle is not attached to a client peer", {},
                          "request");
    }

    Peer* peer_ = nullptr;
    protocol::Task task_;
  };

  /// @brief Creates a client peer from an owned transport.
  explicit Peer(std::unique_ptr<client::Transport> transport)
      : client_(std::move(transport)) {}

  /// @brief Creates a client peer from an owned role-generic transport.
  explicit Peer(std::unique_ptr<transport::ClientTransport> transport)
      : native_transport_(std::move(transport)),
        client_(detail::make_peer_client_transport_adapter(native_transport_)) {
  }

  /// @brief Creates a client peer from an existing client implementation.
  explicit Peer(client::Client client) : client_(std::move(client)) {}

#if defined(CXXMCP_ENABLE_HTTP)
  static Peer connect_streamable_http(
      client::Client::StreamableHttpEndpoint endpoint) {
    return Peer(client::Client::connect_streamable_http(std::move(endpoint)));
  }

  static Peer connect_legacy_sse(
      client::Client::StreamableHttpEndpoint endpoint) {
    return Peer(client::Client::connect_legacy_sse(std::move(endpoint)));
  }
#endif

  static Peer connect_stdio(client::Client::StdioEndpoint endpoint) {
    return Peer(client::Client::connect_stdio(std::move(endpoint)));
  }

  /// @brief Starts a fluent builder for common client peer construction.
  static Builder builder();

  CXXMCP_DEPRECATED(
      "client() is a compatibility escape hatch; prefer ClientPeer methods")
  client::Client& client() noexcept { return client_; }

  CXXMCP_DEPRECATED(
      "client() is a compatibility escape hatch; prefer ClientPeer methods")
  const client::Client& client() const noexcept { return client_; }

  bool uses_native_transport() const noexcept {
    return native_transport_ != nullptr;
  }

  core::Result<core::Unit> start(
      CancellationToken cancellation = CancellationToken::none()) {
    if (native_transport_) {
      return start_native_receive_loop(cancellation);
    }
    return client_.start();
  }

  void stop() noexcept {
    if (native_transport_) {
      (void)native_transport_->close();
      return;
    }
    client_.stop();
  }

  core::Result<protocol::Json> initialize(std::string client_name = "cxxmcp",
                                          std::string client_version = "0") {
    if (native_transport_) {
      auto payload =
          request_json(std::string(protocol::InitializeMethod),
                       detail::make_peer_initialize_params(
                           std::move(client_name), std::move(client_version),
                           client_capabilities_));
      if (!payload) {
        return mcp::core::unexpected(payload.error());
      }
      auto checked = detail::require_peer_initialize_payload(*payload);
      if (!checked) {
        return mcp::core::unexpected(checked.error());
      }
      auto recorded = record_server_capabilities(*checked);
      if (!recorded) {
        return mcp::core::unexpected(recorded.error());
      }
      return *checked;
    }
    return client_.initialize(std::move(client_name),
                              std::move(client_version));
  }

  core::Result<protocol::Json> initialize(std::string client_name,
                                          std::string client_version,
                                          RequestOptions options) {
    if (native_transport_) {
      auto params = detail::make_peer_initialize_params(
          std::move(client_name), std::move(client_version),
          client_capabilities_);
      if (options.protocol_version.has_value()) {
        params["protocolVersion"] = *options.protocol_version;
      }
      if (options.meta.has_value()) {
        params["_meta"] = *options.meta;
      }

      auto do_init =
          [&](const protocol::Json& p) -> core::Result<protocol::Json> {
        protocol::JsonRpcRequest request = protocol::make_request(
            std::string(protocol::InitializeMethod), next_peer_request_id(), p);
        request.transport_headers = options.headers;
        request.protocol_version_override = options.protocol_version;
        auto payload = raw_request(request);
        if (!payload) {
          return mcp::core::unexpected(payload.error());
        }
        return *payload;
      };

      auto payload = do_init(params);
      if (!payload) {
        // Version negotiation retry.
        auto& err = payload.error();
        if (!err.detail.empty()) {
          auto detail_json = protocol::Json::parse(err.detail, nullptr, false);
          std::string retry_version;
          if (detail_json.is_object() &&
              detail_json.contains("supportedVersion")) {
            retry_version = detail_json["supportedVersion"].get<std::string>();
          } else if (detail_json.is_object() &&
                     detail_json.contains("supported") &&
                     detail_json["supported"].is_array() &&
                     !detail_json["supported"].empty()) {
            // Server returns supported versions as array
            // (conformance harness format).
            retry_version = detail_json["supported"][0].get<std::string>();
          } else if (detail_json.is_string()) {
            // Server returns unsupported version as string.
            // Fall back to the default protocol version.
            retry_version = std::string(protocol::McpProtocolVersion);
          }
          if (!retry_version.empty()) {
            params["protocolVersion"] = retry_version;
            if (options.meta.has_value()) {
              params["_meta"] = *options.meta;
            }
            payload = do_init(params);
            if (!payload) {
              return mcp::core::unexpected(payload.error());
            }
          } else {
            return mcp::core::unexpected(err);
          }
        } else {
          return mcp::core::unexpected(err);
        }
      }
      auto checked = detail::require_peer_initialize_payload(*payload);
      if (!checked) {
        return mcp::core::unexpected(checked.error());
      }
      auto recorded = record_server_capabilities(*checked);
      if (!recorded) {
        return mcp::core::unexpected(recorded.error());
      }
      return *checked;
    }
    return client_.initialize(std::move(client_name), std::move(client_version),
                              std::move(options));
  }

  std::optional<protocol::ServerCapabilities> server_capabilities() const {
    return native_transport_ ? server_capabilities_
                             : client_.server_capabilities();
  }

  core::Result<core::Unit> notify_initialized() {
    return raw_notification(protocol::make_notification(
        std::string(protocol::InitializedMethod), protocol::Json::object()));
  }

  core::Result<core::Unit> notify_initialized(RequestOptions options) {
    protocol::Json params = protocol::Json::object();
    if (options.meta.has_value()) {
      params["_meta"] = *options.meta;
    }
    protocol::JsonRpcNotification notification;
    notification.method = std::string(protocol::InitializedMethod);
    notification.params = std::move(params);
    return raw_notification(std::move(notification));
  }

  core::Result<core::Unit> notify_cancelled(protocol::RequestId request_id,
                                            std::string reason = {}) {
    protocol::CancelledNotificationParams params;
    params.request_id = std::move(request_id);
    if (!reason.empty()) {
      params.reason = std::move(reason);
    }
    return raw_notification(protocol::make_notification(
        std::string(protocol::CancelledNotificationMethod),
        protocol::cancelled_notification_params_to_json(params)));
  }

  core::Result<core::Unit> notify_progress(
      protocol::ProgressToken progress_token, double progress,
      std::optional<double> total = std::nullopt, std::string message = {}) {
    protocol::ProgressNotificationParams params;
    params.progress_token = std::move(progress_token);
    params.progress = progress;
    params.total = total;
    if (!message.empty()) {
      params.message = std::move(message);
    }
    return raw_notification(protocol::make_notification(
        std::string(protocol::ProgressNotificationMethod),
        protocol::progress_notification_params_to_json(params)));
  }

  core::Result<core::Unit> notify_roots_list_changed() {
    return raw_notification(protocol::make_notification(
        std::string(protocol::RootsListChangedNotificationMethod),
        protocol::Json::object()));
  }

  core::Result<core::Unit> ping() {
    return request_unit(std::string(protocol::PingMethod),
                        protocol::Json::object());
  }

  std::vector<protocol::Root> list_roots() const {
    if (native_transport_) {
      return roots_;
    }
    return client_.list_roots();
  }

  Peer& set_roots(std::vector<protocol::Root> roots) {
    roots_ = roots;
    client_.set_roots(std::move(roots));
    return *this;
  }

  Peer& set_capabilities(protocol::ClientCapabilities capabilities) {
    client_capabilities_ = capabilities;
    client_.set_capabilities(std::move(capabilities));
    return *this;
  }

  Peer& on_initialized(client::Client::InitializedHandler handler) {
    initialized_handler_ = handler;
    client_.on_initialized(std::move(handler));
    return *this;
  }

  Peer& on_cancelled(client::Client::CancelledHandler handler) {
    cancelled_handler_ = handler;
    client_.on_cancelled(std::move(handler));
    return *this;
  }

  Peer& on_logging_message(client::Client::LoggingMessageHandler handler) {
    logging_message_handler_ = handler;
    client_.on_logging_message(std::move(handler));
    return *this;
  }

  Peer& on_tool_list_changed(client::Client::ListChangedHandler handler) {
    tool_list_changed_handler_ = handler;
    client_.on_tool_list_changed(std::move(handler));
    return *this;
  }

  Peer& on_prompt_list_changed(client::Client::ListChangedHandler handler) {
    prompt_list_changed_handler_ = handler;
    client_.on_prompt_list_changed(std::move(handler));
    return *this;
  }

  Peer& on_resource_list_changed(client::Client::ListChangedHandler handler) {
    resource_list_changed_handler_ = handler;
    client_.on_resource_list_changed(std::move(handler));
    return *this;
  }

  Peer& on_resource_updated(client::Client::ResourceUpdatedHandler handler) {
    resource_updated_handler_ = handler;
    client_.on_resource_updated(std::move(handler));
    return *this;
  }

  Peer& on_progress(client::Client::ProgressHandler handler) {
    progress_handler_ = handler;
    client_.on_progress(std::move(handler));
    return *this;
  }

  Peer& on_elicitation_complete(
      client::Client::ElicitationCompleteHandler handler) {
    elicitation_complete_handler_ = handler;
    client_.on_elicitation_complete(std::move(handler));
    return *this;
  }

  Peer& on_task_status(client::Client::TaskStatusHandler handler) {
    task_status_handler_ = handler;
    client_.on_task_status(std::move(handler));
    return *this;
  }

  Peer& on_roots_list_changed(client::Client::ListChangedHandler handler) {
    roots_list_changed_handler_ = handler;
    client_.on_roots_list_changed(std::move(handler));
    return *this;
  }

  Peer& on_list_roots_request(client::Client::ListRootsRequestHandler handler) {
    roots_list_request_handler_ = handler;
    client_.on_list_roots_request(std::move(handler));
    return *this;
  }

  Peer& on_list_roots_request(
      client::Client::RootsListRequestCancellationHandler handler) {
    roots_list_request_cancellation_handler_ = handler;
    client_.on_list_roots_request(std::move(handler));
    return *this;
  }

  Peer& on_create_message_request(
      client::Client::CreateMessageRequestHandler handler) {
    sampling_request_handler_ = handler;
    client_.on_create_message_request(std::move(handler));
    return *this;
  }

  Peer& on_create_message_request(
      client::Client::SamplingRequestCancellationHandler handler) {
    sampling_request_cancellation_handler_ = handler;
    client_.on_create_message_request(std::move(handler));
    return *this;
  }

  Peer& on_create_elicitation_request(
      client::Client::CreateElicitationRequestHandler handler) {
    elicitation_request_handler_ = handler;
    client_.on_create_elicitation_request(std::move(handler));
    return *this;
  }

  Peer& on_create_elicitation_request(
      client::Client::ElicitationRequestCancellationHandler handler) {
    elicitation_request_cancellation_handler_ = handler;
    client_.on_create_elicitation_request(std::move(handler));
    return *this;
  }

  Peer& on_custom_request(client::Client::CustomRequestHandler handler) {
    custom_request_handler_ = handler;
    client_.on_custom_request(std::move(handler));
    return *this;
  }

  Peer& on_custom_request(
      client::Client::CustomRequestCancellationHandler handler) {
    custom_request_cancellation_handler_ = handler;
    client_.on_custom_request(std::move(handler));
    return *this;
  }

  Peer& on_raw_notification(client::Client::RawNotificationHandler handler) {
    raw_notification_handler_ = handler;
    client_.on_raw_notification(std::move(handler));
    return *this;
  }

  Peer& set_handler(const client::ClientHandler& handler) {
    if (handler.on_initialized) {
      on_initialized(handler.on_initialized);
    }
    if (handler.on_cancelled) {
      on_cancelled(handler.on_cancelled);
    }
    if (handler.on_logging_message) {
      on_logging_message(handler.on_logging_message);
    }
    if (handler.on_tool_list_changed) {
      on_tool_list_changed(handler.on_tool_list_changed);
    }
    if (handler.on_prompt_list_changed) {
      on_prompt_list_changed(handler.on_prompt_list_changed);
    }
    if (handler.on_resource_list_changed) {
      on_resource_list_changed(handler.on_resource_list_changed);
    }
    if (handler.on_resource_updated) {
      on_resource_updated(handler.on_resource_updated);
    }
    if (handler.on_progress) {
      on_progress(handler.on_progress);
    }
    if (handler.on_elicitation_complete) {
      on_elicitation_complete(handler.on_elicitation_complete);
    }
    if (handler.on_task_status) {
      on_task_status(handler.on_task_status);
    }
    if (handler.on_roots_list_changed) {
      on_roots_list_changed(handler.on_roots_list_changed);
    }
    if (handler.on_list_roots_request) {
      on_list_roots_request(handler.on_list_roots_request);
    }
    if (handler.on_list_roots_request_with_cancellation) {
      on_list_roots_request(handler.on_list_roots_request_with_cancellation);
    }
    if (handler.on_create_message_request) {
      on_create_message_request(handler.on_create_message_request);
    }
    if (handler.on_create_message_request_with_cancellation) {
      on_create_message_request(
          handler.on_create_message_request_with_cancellation);
    }
    if (handler.on_create_elicitation_request) {
      on_create_elicitation_request(handler.on_create_elicitation_request);
    }
    if (handler.on_create_elicitation_request_with_cancellation) {
      on_create_elicitation_request(
          handler.on_create_elicitation_request_with_cancellation);
    }
    if (handler.on_custom_request) {
      on_custom_request(handler.on_custom_request);
    }
    if (handler.on_custom_request_with_cancellation) {
      on_custom_request(handler.on_custom_request_with_cancellation);
    }
    if (handler.on_roots_list_request) {
      on_list_roots_request(handler.on_roots_list_request);
    }
    if (handler.on_roots_list_request_with_cancellation) {
      on_list_roots_request(handler.on_roots_list_request_with_cancellation);
    }
    if (handler.on_sampling_request) {
      on_create_message_request(handler.on_sampling_request);
    }
    if (handler.on_sampling_request_with_cancellation) {
      on_create_message_request(handler.on_sampling_request_with_cancellation);
    }
    if (handler.on_elicitation_request) {
      on_create_elicitation_request(handler.on_elicitation_request);
    }
    if (handler.on_elicitation_request_with_cancellation) {
      on_create_elicitation_request(
          handler.on_elicitation_request_with_cancellation);
    }
    if (handler.on_raw_notification) {
      on_raw_notification(handler.on_raw_notification);
    }
    if (handler.on_custom_notification) {
      on_raw_notification(handler.on_custom_notification);
    }
    return *this;
  }

  Peer& set_handler(const client::ClientHandlerInterface& handler) {
    on_initialized([&handler]() { handler.on_initialized(); });
    on_cancelled([&handler](const protocol::RequestId& request_id,
                            std::string_view reason) {
      handler.on_cancelled(request_id, reason);
    });
    on_logging_message(
        [&handler](std::string_view level, std::string_view message) {
          handler.on_logging_message(level, message);
        });
    on_tool_list_changed([&handler]() { handler.on_tool_list_changed(); });
    on_prompt_list_changed([&handler]() { handler.on_prompt_list_changed(); });
    on_resource_list_changed(
        [&handler]() { handler.on_resource_list_changed(); });
    on_resource_updated([&handler](const std::string& uri) {
      handler.on_resource_updated(uri);
    });
    on_progress([&handler](const protocol::ProgressNotificationParams& params) {
      handler.on_progress(params);
    });
    on_elicitation_complete([&handler](std::string_view elicitation_id) {
      handler.on_elicitation_complete(elicitation_id);
    });
    on_task_status([&handler](const protocol::Task& task) {
      handler.on_task_status(task);
    });
    on_roots_list_changed([&handler]() { handler.on_roots_list_changed(); });
    on_list_roots_request([&handler](CancellationToken cancellation)
                              -> core::Result<protocol::RootsListResult> {
      const auto response = handler.on_list_roots_request(cancellation);
      if (response.has_value()) {
        return std::move(*response);
      }
      return mcp::core::unexpected(client::handler_method_not_found(
          "client handler does not handle list_roots"));
    });
    on_create_message_request(
        [&handler](const protocol::CreateMessageParams& params,
                   CancellationToken cancellation)
            -> core::Result<protocol::CreateMessageResult> {
          const auto response =
              handler.on_create_message_request(params, cancellation);
          if (response.has_value()) {
            return std::move(*response);
          }
          return mcp::core::unexpected(client::handler_method_not_found(
              "client handler does not handle create_message"));
        });
    on_create_elicitation_request(
        [&handler](const protocol::CreateElicitationRequestParam& params,
                   CancellationToken cancellation)
            -> core::Result<protocol::CreateElicitationResult> {
          const auto response =
              handler.on_create_elicitation_request(params, cancellation);
          if (response.has_value()) {
            return std::move(*response);
          }
          return mcp::core::unexpected(client::handler_method_not_found(
              "client handler does not handle elicitation"));
        });
    on_custom_request([&handler](const protocol::JsonRpcRequest& request,
                                 CancellationToken cancellation)
                          -> core::Result<protocol::Json> {
      const auto response = handler.on_custom_request(request, cancellation);
      if (response.has_value()) {
        return std::move(*response);
      }
      return mcp::core::unexpected(client::handler_method_not_found(
          "client handler does not handle custom request"));
    });
    on_raw_notification(
        [&handler](const protocol::JsonRpcNotification& notification) {
          handler.on_raw_notification(notification);
        });
    return *this;
  }

  core::Result<std::vector<protocol::ToolDefinition>> list_tools() {
    const auto page = list_tools_page();
    if (!page) {
      return mcp::core::unexpected(page.error());
    }
    cache_tool_schemas(page->tools);
    return page->tools;
  }

  core::Result<protocol::ToolsListResult> list_tools_page(
      const protocol::PaginatedRequestParams& params = {}) {
    if (!native_transport_) {
      return client_.list_tools_page(params);
    }
    auto payload =
        request_json(std::string(protocol::ToolsListMethod),
                     protocol::paginated_request_params_to_json(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    const auto result = protocol::tools_list_result_from_json(*payload);
    if (!result) {
      return mcp::core::unexpected(result.error());
    }
    return *result;
  }

  core::Result<std::vector<protocol::ToolDefinition>> list_all_tools() {
    auto result = list_all_pages<protocol::ToolDefinition>(
        std::string(protocol::ToolsListMethod),
        [](const protocol::Json& payload) {
          return protocol::tools_list_result_from_json(payload);
        },
        [](const protocol::ToolsListResult& page,
           std::vector<protocol::ToolDefinition>& all) {
          all.insert(all.end(), page.tools.begin(), page.tools.end());
        });
    if (result) {
      cache_tool_schemas(*result);
    }
    return result;
  }

  core::Result<protocol::ToolResult> call_tool(const protocol::ToolCall& call) {
    auto request = protocol::make_request(
        std::string(protocol::ToolsCallMethod), next_peer_request_id(),
        protocol::tool_call_to_json(call));
    apply_x_mcp_headers(request, call);
    auto payload = raw_request(request);
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return protocol::tool_result_from_json(*payload);
  }

  core::Result<protocol::CreateTaskResult> call_tool_task(
      const protocol::ToolCall& call) {
    if (!call.task.has_value()) {
      return mcp::core::unexpected(errors::make(
          protocol::ErrorCode::InvalidRequest,
          "task-aware tool call requires task parameters", {}, "protocol"));
    }
    if (!supports_server_task_tool_call()) {
      return mcp::core::unexpected(unsupported_server_capability(
          "server does not support task-aware tool calls"));
    }
    auto payload = request_json(std::string(protocol::ToolsCallMethod),
                                protocol::tool_call_to_json(call));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return protocol::create_task_result_from_json(*payload);
  }

  core::Result<TaskHandle> call_tool_task_handle(
      const protocol::ToolCall& call) {
    auto task = call_tool_task(call);
    if (!task) {
      return mcp::core::unexpected(task.error());
    }
    return TaskHandle(*this, task->task);
  }

  core::Result<protocol::ToolResult> call_tool(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object()) {
    protocol::ToolCall call;
    call.name = std::string(name);
    call.arguments = arguments;
    return call_tool(call);
  }

  core::Result<std::vector<protocol::Prompt>> list_prompts() {
    const auto page = list_prompts_page();
    if (!page) {
      return mcp::core::unexpected(page.error());
    }
    return page->prompts;
  }

  core::Result<protocol::PromptsListResult> list_prompts_page(
      const protocol::PaginatedRequestParams& params = {}) {
    if (!native_transport_) {
      return client_.list_prompts_page(params);
    }
    auto payload =
        request_json(std::string(protocol::PromptsListMethod),
                     protocol::paginated_request_params_to_json(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    const auto result = protocol::prompts_list_result_from_json(*payload);
    if (!result) {
      return mcp::core::unexpected(result.error());
    }
    return *result;
  }

  core::Result<std::vector<protocol::Prompt>> list_all_prompts() {
    return list_all_pages<protocol::Prompt>(
        std::string(protocol::PromptsListMethod),
        [](const protocol::Json& payload) {
          return protocol::prompts_list_result_from_json(payload);
        },
        [](const protocol::PromptsListResult& page,
           std::vector<protocol::Prompt>& all) {
          all.insert(all.end(), page.prompts.begin(), page.prompts.end());
        });
  }

  core::Result<protocol::PromptsGetResult> get_prompt(
      const protocol::PromptsGetParams& params) {
    auto payload = request_json(std::string(protocol::PromptsGetMethod),
                                protocol::prompts_get_params_to_json(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return protocol::prompts_get_result_from_json(*payload);
  }

  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object()) {
    protocol::PromptsGetParams params;
    params.name = std::string(name);
    params.arguments = arguments;
    return get_prompt(params);
  }

  core::Result<std::vector<protocol::Resource>> list_resources() {
    const auto page = list_resources_page();
    if (!page) {
      return mcp::core::unexpected(page.error());
    }
    return page->resources;
  }

  core::Result<protocol::ResourcesListResult> list_resources_page(
      const protocol::PaginatedRequestParams& params = {}) {
    if (!native_transport_) {
      return client_.list_resources_page(params);
    }
    auto payload =
        request_json(std::string(protocol::ResourcesListMethod),
                     protocol::paginated_request_params_to_json(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    const auto result = protocol::resources_list_result_from_json(*payload);
    if (!result) {
      return mcp::core::unexpected(result.error());
    }
    return *result;
  }

  core::Result<std::vector<protocol::Resource>> list_all_resources() {
    return list_all_pages<protocol::Resource>(
        std::string(protocol::ResourcesListMethod),
        [](const protocol::Json& payload) {
          return protocol::resources_list_result_from_json(payload);
        },
        [](const protocol::ResourcesListResult& page,
           std::vector<protocol::Resource>& all) {
          all.insert(all.end(), page.resources.begin(), page.resources.end());
        });
  }

  core::Result<std::vector<protocol::ResourceTemplate>>
  list_resource_templates() {
    const auto page = list_resource_templates_page();
    if (!page) {
      return mcp::core::unexpected(page.error());
    }
    return page->resource_templates;
  }

  core::Result<protocol::ResourceTemplatesListResult>
  list_resource_templates_page(
      const protocol::PaginatedRequestParams& params = {}) {
    if (!native_transport_) {
      return client_.list_resource_templates_page(params);
    }
    auto payload =
        request_json(std::string(protocol::ResourcesTemplatesListMethod),
                     protocol::paginated_request_params_to_json(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    const auto result =
        protocol::resource_templates_list_result_from_json(*payload);
    if (!result) {
      return mcp::core::unexpected(result.error());
    }
    return *result;
  }

  core::Result<std::vector<protocol::ResourceTemplate>>
  list_all_resource_templates() {
    return list_all_pages<protocol::ResourceTemplate>(
        std::string(protocol::ResourcesTemplatesListMethod),
        [](const protocol::Json& payload) {
          return protocol::resource_templates_list_result_from_json(payload);
        },
        [](const protocol::ResourceTemplatesListResult& page,
           std::vector<protocol::ResourceTemplate>& all) {
          all.insert(all.end(), page.resource_templates.begin(),
                     page.resource_templates.end());
        });
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      const protocol::ResourcesReadParams& params) {
    auto payload =
        request_json(std::string(protocol::ResourcesReadMethod),
                     protocol::resources_read_params_to_json(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return protocol::resources_read_result_from_json(*payload);
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri) {
    return read_resource(protocol::ResourcesReadParams{std::string(uri)});
  }

  core::Result<protocol::CompleteResult> complete(
      const protocol::CompleteParams& request) {
    if (!supports_server_completion()) {
      return mcp::core::unexpected(
          unsupported_server_capability("server does not support completion"));
    }
    auto payload = request_json(std::string(protocol::CompletionCompleteMethod),
                                protocol::complete_params_to_json(request));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return protocol::complete_result_from_json(*payload);
  }

  core::Result<protocol::Json> complete(const protocol::Json& request) {
    if (!supports_server_completion()) {
      return mcp::core::unexpected(
          unsupported_server_capability("server does not support completion"));
    }
    return request_json(std::string(protocol::CompletionCompleteMethod),
                        request);
  }

  core::Result<protocol::CompletionResult> complete_prompt_argument(
      std::string_view prompt_name, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    protocol::CompletionArgument argument;
    argument.name = std::string(argument_name);
    argument.value = std::move(current_value);
    protocol::CompleteParams params;
    params.ref =
        protocol::prompt_completion_reference(std::string(prompt_name));
    params.argument = std::move(argument);
    params.context = std::move(context);
    const auto result = complete(params);
    if (!result) {
      return mcp::core::unexpected(result.error());
    }
    return result->completion;
  }

  core::Result<protocol::CompletionResult> complete_resource_argument(
      std::string_view uri_template, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    protocol::CompletionArgument argument;
    argument.name = std::string(argument_name);
    argument.value = std::move(current_value);
    protocol::CompleteParams params;
    params.ref =
        protocol::resource_completion_reference(std::string(uri_template));
    params.argument = std::move(argument);
    params.context = std::move(context);
    const auto result = complete(params);
    if (!result) {
      return mcp::core::unexpected(result.error());
    }
    return result->completion;
  }

  core::Result<std::vector<std::string>> complete_prompt_simple(
      std::string_view prompt_name, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    const auto completion =
        complete_prompt_argument(prompt_name, argument_name,
                                 std::move(current_value), std::move(context));
    if (!completion) {
      return mcp::core::unexpected(completion.error());
    }
    return completion->values;
  }

  core::Result<std::vector<std::string>> complete_resource_simple(
      std::string_view uri_template, std::string_view argument_name,
      std::string current_value,
      protocol::Json context = protocol::Json::object()) {
    const auto completion = complete_resource_argument(
        uri_template, argument_name, std::move(current_value),
        std::move(context));
    if (!completion) {
      return mcp::core::unexpected(completion.error());
    }
    return completion->values;
  }

  core::Result<protocol::CreateMessageResult> create_message(
      const protocol::CreateMessageParams& request) {
    auto payload =
        request_json(std::string(protocol::SamplingCreateMessageMethod),
                     protocol::create_message_params_to_json(request));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return protocol::create_message_result_from_json(*payload);
  }

  core::Result<protocol::Json> create_message(const protocol::Json& request) {
    return request_json(std::string(protocol::SamplingCreateMessageMethod),
                        request);
  }

  core::Result<protocol::CreateElicitationResult> create_elicitation(
      const protocol::CreateElicitationRequestParam& request) {
    auto payload = request_json(
        std::string(protocol::ElicitationCreateMethod),
        protocol::create_elicitation_request_param_to_json(request));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return protocol::create_elicitation_result_from_json(*payload);
  }

  core::Result<protocol::Json> create_elicitation(
      const protocol::Json& request) {
    return request_json(std::string(protocol::ElicitationCreateMethod),
                        request);
  }

  core::Result<std::vector<protocol::Task>> list_tasks() {
    const auto page = list_tasks_page();
    if (!page) {
      return mcp::core::unexpected(page.error());
    }
    return page->tasks;
  }

  core::Result<protocol::TaskListResult> list_tasks_page(
      const protocol::TaskListParams& params = {}) {
    if (!supports_server_task_list()) {
      return mcp::core::unexpected(unsupported_server_capability(
          "server does not support task listing"));
    }
    if (!native_transport_) {
      return client_.list_tasks_page(params);
    }
    auto payload = request_json(std::string(protocol::TasksListMethod),
                                protocol::task_list_params_to_json(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    const auto result = protocol::task_list_result_from_json(*payload);
    if (!result) {
      return mcp::core::unexpected(result.error());
    }
    return *result;
  }

  core::Result<std::vector<protocol::Task>> list_all_tasks() {
    if (!supports_server_task_list()) {
      return mcp::core::unexpected(unsupported_server_capability(
          "server does not support task listing"));
    }
    return list_all_pages<protocol::Task>(
        std::string(protocol::TasksListMethod),
        [](const protocol::Json& payload) {
          return protocol::task_list_result_from_json(payload);
        },
        [](const protocol::TaskListResult& page,
           std::vector<protocol::Task>& all) {
          all.insert(all.end(), page.tasks.begin(), page.tasks.end());
        });
  }

  core::Result<protocol::Task> get_task(std::string_view task_id) {
    if (!supports_server_tasks()) {
      return mcp::core::unexpected(
          unsupported_server_capability("server does not support tasks"));
    }
    protocol::TaskGetParams params;
    params.task_id = std::string(task_id);
    auto payload = request_json(std::string(protocol::TasksGetMethod),
                                protocol::task_get_params_to_json(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return protocol::task_from_json(*payload);
  }

  core::Result<protocol::Task> cancel_task(std::string_view task_id) {
    if (!supports_server_task_cancel()) {
      return mcp::core::unexpected(unsupported_server_capability(
          "server does not support task cancellation"));
    }
    protocol::TaskCancelParams params;
    params.task_id = std::string(task_id);
    auto payload = request_json(std::string(protocol::TasksCancelMethod),
                                protocol::task_cancel_params_to_json(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return protocol::task_from_json(*payload);
  }

  core::Result<protocol::Json> task_result(std::string_view task_id) {
    if (!supports_server_tasks()) {
      return mcp::core::unexpected(
          unsupported_server_capability("server does not support tasks"));
    }
    protocol::TaskResultParams params;
    params.task_id = std::string(task_id);
    return request_json(std::string(protocol::TasksResultMethod),
                        protocol::task_result_params_to_json(params));
  }

  core::Result<core::Unit> set_level(std::string_view level) {
    const auto parsed = protocol::logging_level_from_string(std::string(level));
    if (!parsed.has_value()) {
      return mcp::core::unexpected(
          errors::make(protocol::ErrorCode::InvalidRequest,
                       "logging/setLevel level is invalid", {}, "protocol"));
    }
    if (!supports_server_logging()) {
      return mcp::core::unexpected(
          unsupported_server_capability("server does not support logging"));
    }
    protocol::LoggingSetLevelParams params;
    params.level = *parsed;
    return request_unit(std::string(protocol::LoggingSetLevelMethod),
                        protocol::logging_set_level_params_to_json(params));
  }

  core::Result<core::Unit> subscribe(std::string_view uri) {
    if (!supports_server_resource_subscribe()) {
      return mcp::core::unexpected(unsupported_server_capability(
          "server does not support resource subscriptions"));
    }
    protocol::ResourcesSubscribeParams params;
    params.uri = std::string(uri);
    return request_unit(std::string(protocol::ResourcesSubscribeMethod),
                        protocol::resources_subscribe_params_to_json(params));
  }

  core::Result<core::Unit> unsubscribe(std::string_view uri) {
    if (!supports_server_resource_subscribe()) {
      return mcp::core::unexpected(unsupported_server_capability(
          "server does not support resource subscriptions"));
    }
    protocol::ResourcesUnsubscribeParams params;
    params.uri = std::string(uri);
    return request_unit(std::string(protocol::ResourcesUnsubscribeMethod),
                        protocol::resources_unsubscribe_params_to_json(params));
  }

  core::Result<protocol::Json> raw_request(
      const protocol::JsonRpcRequest& request) {
    if (native_transport_) {
      const auto response = send_native_request(request);
      if (!response) {
        return mcp::core::unexpected(response.error());
      }
      return detail::peer_require_result_payload(*response);
    }
    return client_.raw_request(request);
  }

  RequestHandle<protocol::Json> request_async(
      std::string method, protocol::Json params = protocol::Json::object(),
      RequestOptions options = {}) {
    if (native_transport_) {
      protocol::JsonRpcRequest request = protocol::make_request(
          std::move(method), next_peer_request_id(), std::move(params));
      if (options.meta.has_value()) {
        request.meta = std::move(options.meta);
      }
      request.transport_headers = std::move(options.headers);
      request.protocol_version_override = std::move(options.protocol_version);

      const auto request_id = request.id;
      return RequestHandle<protocol::Json>::spawn(
          request_id, options.timeout, options.cancellation_token,
          [this, request_id](std::string reason) mutable {
            return notify_cancelled(std::move(request_id), std::move(reason));
          },
          [this, request = std::move(request)]() mutable {
            return raw_request(request);
          });
    }
    return client_.request_async(std::move(method), std::move(params),
                                 std::move(options));
  }

  template <class T, class Parser>
  RequestHandle<T> request_async(std::string method, protocol::Json params,
                                 Parser parser, RequestOptions options = {}) {
    if (native_transport_) {
      protocol::JsonRpcRequest request = protocol::make_request(
          std::move(method), next_peer_request_id(), std::move(params));
      if (options.meta.has_value()) {
        request.meta = std::move(options.meta);
      }
      request.transport_headers = std::move(options.headers);
      request.protocol_version_override = std::move(options.protocol_version);

      const auto request_id = request.id;
      return RequestHandle<T>::spawn(
          request_id, options.timeout, options.cancellation_token,
          [this, request_id](std::string reason) mutable {
            return notify_cancelled(std::move(request_id), std::move(reason));
          },
          [this, request = std::move(request),
           parser = std::move(parser)]() mutable -> core::Result<T> {
            auto payload = raw_request(request);
            if (!payload) {
              return mcp::core::unexpected(payload.error());
            }
            return parser(*payload);
          });
    }
    return client_.request_async<T>(std::move(method), std::move(params),
                                    std::move(parser), std::move(options));
  }

  RequestHandle<std::vector<protocol::ToolDefinition>> list_tools_async(
      RequestOptions options = {}) {
    return request_async<std::vector<protocol::ToolDefinition>>(
        std::string(protocol::ToolsListMethod), protocol::Json::object(),
        [](const protocol::Json& payload)
            -> core::Result<std::vector<protocol::ToolDefinition>> {
          const auto result = protocol::tools_list_result_from_json(payload);
          if (!result) {
            return mcp::core::unexpected(result.error());
          }
          return result->tools;
        },
        std::move(options));
  }

  RequestHandle<std::vector<protocol::Prompt>> list_prompts_async(
      RequestOptions options = {}) {
    return request_async<std::vector<protocol::Prompt>>(
        std::string(protocol::PromptsListMethod), protocol::Json::object(),
        [](const protocol::Json& payload)
            -> core::Result<std::vector<protocol::Prompt>> {
          const auto result = protocol::prompts_list_result_from_json(payload);
          if (!result) {
            return mcp::core::unexpected(result.error());
          }
          return result->prompts;
        },
        std::move(options));
  }

  RequestHandle<std::vector<protocol::Resource>> list_resources_async(
      RequestOptions options = {}) {
    return request_async<std::vector<protocol::Resource>>(
        std::string(protocol::ResourcesListMethod), protocol::Json::object(),
        [](const protocol::Json& payload)
            -> core::Result<std::vector<protocol::Resource>> {
          const auto result =
              protocol::resources_list_result_from_json(payload);
          if (!result) {
            return mcp::core::unexpected(result.error());
          }
          return result->resources;
        },
        std::move(options));
  }

  RequestHandle<std::vector<protocol::ResourceTemplate>>
  list_resource_templates_async(RequestOptions options = {}) {
    return request_async<std::vector<protocol::ResourceTemplate>>(
        std::string(protocol::ResourcesTemplatesListMethod),
        protocol::Json::object(),
        [](const protocol::Json& payload)
            -> core::Result<std::vector<protocol::ResourceTemplate>> {
          const auto result =
              protocol::resource_templates_list_result_from_json(payload);
          if (!result) {
            return mcp::core::unexpected(result.error());
          }
          return result->resource_templates;
        },
        std::move(options));
  }

  RequestHandle<protocol::ToolResult> call_tool_async(
      const protocol::ToolCall& call, RequestOptions options = {}) {
    return request_async<protocol::ToolResult>(
        std::string(protocol::ToolsCallMethod),
        protocol::tool_call_to_json(call),
        [](const protocol::Json& payload) {
          return protocol::tool_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::ToolResult> call_tool_async(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object(),
      RequestOptions options = {}) {
    protocol::ToolCall call;
    call.name = std::string(name);
    call.arguments = arguments;
    return call_tool_async(call, std::move(options));
  }

  RequestHandle<protocol::CreateTaskResult> call_tool_task_async(
      const protocol::ToolCall& call, RequestOptions options = {}) {
    if (!call.task.has_value()) {
      return RequestHandle<protocol::CreateTaskResult>::ready(
          next_peer_request_id(),
          mcp::core::unexpected(
              errors::make(protocol::ErrorCode::InvalidRequest,
                           "task-aware tool call requires task parameters", {},
                           "protocol")));
    }
    if (!supports_server_task_tool_call()) {
      return RequestHandle<protocol::CreateTaskResult>::ready(
          next_peer_request_id(),
          mcp::core::unexpected(unsupported_server_capability(
              "server does not support task-aware tool calls")));
    }

    return request_async<protocol::CreateTaskResult>(
        std::string(protocol::ToolsCallMethod),
        protocol::tool_call_to_json(call),
        [](const protocol::Json& payload) {
          return protocol::create_task_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::PromptsGetResult> get_prompt_async(
      const protocol::PromptsGetParams& params, RequestOptions options = {}) {
    return request_async<protocol::PromptsGetResult>(
        std::string(protocol::PromptsGetMethod),
        protocol::prompts_get_params_to_json(params),
        [](const protocol::Json& payload) {
          return protocol::prompts_get_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::PromptsGetResult> get_prompt_async(
      std::string_view name,
      const protocol::Json& arguments = protocol::Json::object(),
      RequestOptions options = {}) {
    protocol::PromptsGetParams params;
    params.name = std::string(name);
    params.arguments = arguments;
    return get_prompt_async(params, std::move(options));
  }

  RequestHandle<protocol::ResourcesReadResult> read_resource_async(
      const protocol::ResourcesReadParams& params,
      RequestOptions options = {}) {
    return request_async<protocol::ResourcesReadResult>(
        std::string(protocol::ResourcesReadMethod),
        protocol::resources_read_params_to_json(params),
        [](const protocol::Json& payload) {
          return protocol::resources_read_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::ResourcesReadResult> read_resource_async(
      std::string_view uri, RequestOptions options = {}) {
    return read_resource_async(protocol::ResourcesReadParams{std::string(uri)},
                               std::move(options));
  }

  RequestHandle<protocol::CompleteResult> complete_async(
      const protocol::CompleteParams& request, RequestOptions options = {}) {
    if (!supports_server_completion()) {
      return RequestHandle<protocol::CompleteResult>::ready(
          next_peer_request_id(),
          mcp::core::unexpected(unsupported_server_capability(
              "server does not support completion")));
    }
    return request_async<protocol::CompleteResult>(
        std::string(protocol::CompletionCompleteMethod),
        protocol::complete_params_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::complete_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Json> complete_async(const protocol::Json& request,
                                               RequestOptions options = {}) {
    if (!supports_server_completion()) {
      return RequestHandle<protocol::Json>::ready(
          next_peer_request_id(),
          mcp::core::unexpected(unsupported_server_capability(
              "server does not support completion")));
    }
    return request_async(std::string(protocol::CompletionCompleteMethod),
                         request, std::move(options));
  }

  RequestHandle<protocol::CreateMessageResult> create_message_async(
      const protocol::CreateMessageParams& request,
      RequestOptions options = {}) {
    return request_async<protocol::CreateMessageResult>(
        std::string(protocol::SamplingCreateMessageMethod),
        protocol::create_message_params_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::create_message_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Json> create_message_async(
      const protocol::Json& request, RequestOptions options = {}) {
    return request_async(std::string(protocol::SamplingCreateMessageMethod),
                         request, std::move(options));
  }

  RequestHandle<protocol::CreateElicitationResult> create_elicitation_async(
      const protocol::CreateElicitationRequestParam& request,
      RequestOptions options = {}) {
    return request_async<protocol::CreateElicitationResult>(
        std::string(protocol::ElicitationCreateMethod),
        protocol::create_elicitation_request_param_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::create_elicitation_result_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Json> create_elicitation_async(
      const protocol::Json& request, RequestOptions options = {}) {
    return request_async(std::string(protocol::ElicitationCreateMethod),
                         request, std::move(options));
  }

  RequestHandle<std::vector<protocol::Task>> list_tasks_async(
      RequestOptions options = {}) {
    if (!supports_server_task_list()) {
      return RequestHandle<std::vector<protocol::Task>>::ready(
          next_peer_request_id(),
          mcp::core::unexpected(unsupported_server_capability(
              "server does not support task listing")));
    }
    return request_async<std::vector<protocol::Task>>(
        std::string(protocol::TasksListMethod), protocol::Json::object(),
        [](const protocol::Json& payload)
            -> core::Result<std::vector<protocol::Task>> {
          const auto result = protocol::task_list_result_from_json(payload);
          if (!result) {
            return mcp::core::unexpected(result.error());
          }
          return result->tasks;
        },
        std::move(options));
  }

  RequestHandle<protocol::Task> get_task_async(
      const protocol::TaskGetParams& request, RequestOptions options = {}) {
    if (!supports_server_tasks()) {
      return RequestHandle<protocol::Task>::ready(
          next_peer_request_id(),
          mcp::core::unexpected(
              unsupported_server_capability("server does not support tasks")));
    }
    return request_async<protocol::Task>(
        std::string(protocol::TasksGetMethod),
        protocol::task_get_params_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::task_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Task> get_task_async(std::string_view task_id,
                                               RequestOptions options = {}) {
    protocol::TaskGetParams params;
    params.task_id = std::string(task_id);
    return get_task_async(params, std::move(options));
  }

  RequestHandle<protocol::Task> cancel_task_async(
      const protocol::TaskCancelParams& request, RequestOptions options = {}) {
    if (!supports_server_task_cancel()) {
      return RequestHandle<protocol::Task>::ready(
          next_peer_request_id(),
          mcp::core::unexpected(unsupported_server_capability(
              "server does not support task cancellation")));
    }
    return request_async<protocol::Task>(
        std::string(protocol::TasksCancelMethod),
        protocol::task_cancel_params_to_json(request),
        [](const protocol::Json& payload) {
          return protocol::task_from_json(payload);
        },
        std::move(options));
  }

  RequestHandle<protocol::Task> cancel_task_async(std::string_view task_id,
                                                  RequestOptions options = {}) {
    protocol::TaskCancelParams params;
    params.task_id = std::string(task_id);
    return cancel_task_async(params, std::move(options));
  }

  RequestHandle<protocol::Json> task_result_async(
      const protocol::TaskResultParams& request, RequestOptions options = {}) {
    if (!supports_server_tasks()) {
      return RequestHandle<protocol::Json>::ready(
          next_peer_request_id(),
          mcp::core::unexpected(
              unsupported_server_capability("server does not support tasks")));
    }
    return request_async(std::string(protocol::TasksResultMethod),
                         protocol::task_result_params_to_json(request),
                         std::move(options));
  }

  RequestHandle<protocol::Json> task_result_async(std::string_view task_id,
                                                  RequestOptions options = {}) {
    protocol::TaskResultParams params;
    params.task_id = std::string(task_id);
    return task_result_async(params, std::move(options));
  }

  core::Result<core::Unit> raw_notification(
      const protocol::JsonRpcNotification& notification) {
    if (native_transport_) {
      return native_transport_->send(protocol::JsonRpcMessage{notification});
    }
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
      auto handled = native_transport_ ? handle_native_request(*request)
                                       : client_.handle_request(*request);
      if (!handled) {
        return protocol::JsonRpcMessage{protocol::make_error_response(
            request->id,
            detail::peer_error_object_from_core_error(handled.error()))};
      }
      return protocol::JsonRpcMessage{std::move(*handled)};
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      const auto handled = native_transport_
                               ? handle_native_notification(*notification)
                               : client_.handle_notification(*notification);
      if (!handled) {
        return mcp::core::unexpected(handled.error());
      }
      return std::nullopt;
    }

    if (const auto* response =
            std::get_if<protocol::JsonRpcResponse>(&message)) {
      if (native_transport_) {
        auto stored = store_native_response(*response);
        if (!stored) {
          return mcp::core::unexpected(stored.error());
        }
        return std::nullopt;
      }
    }

    return mcp::core::unexpected(detail::peer_dispatch_error(
        "client peer cannot dispatch an uncorrelated response"));
  }

  /// @brief Runs a sequential receive loop over a role-generic client
  /// transport.
  core::Result<core::Unit> serve_transport(
      transport::ClientTransport& transport,
      CancellationToken cancellation = CancellationToken::none()) {
    return detail::serve_transport_loop(
        transport, cancellation,
        [this](const protocol::JsonRpcMessage& message) {
          return dispatch_message(message);
        });
  }

 private:
  std::int64_t next_peer_request_id() noexcept {
    return next_request_id_->fetch_add(1, std::memory_order_relaxed);
  }

  void cache_tool_schemas(const std::vector<protocol::ToolDefinition>& tools) {
    for (const auto& tool : tools) {
      tool_schema_cache_[tool.name] = tool.input_schema;
    }
  }

  void apply_x_mcp_headers(protocol::JsonRpcRequest& request,
                           const protocol::ToolCall& call) {
    auto it = tool_schema_cache_.find(call.name);
    if (it == tool_schema_cache_.end()) return;
    auto entries = protocol::extract_x_mcp_headers(it->second);
    if (entries.empty()) return;
    auto param_headers =
        protocol::build_tool_param_headers(call.arguments, entries);
    for (auto& [key, value] : param_headers) {
      request.transport_headers.emplace(std::move(key), std::move(value));
    }
  }

  bool native_receive_loop_active() const {
    std::lock_guard<std::mutex> lock(native_receive_state_->mutex);
    return native_receive_state_->loop_active;
  }

  core::Result<core::Unit> start_native_receive_loop(
      CancellationToken cancellation) {
    {
      std::lock_guard<std::mutex> lock(native_receive_state_->mutex);
      if (native_receive_state_->loop_active) {
        return mcp::core::unexpected(detail::peer_transport_error(
            "client peer receive loop is already running"));
      }
      native_receive_state_->loop_active = true;
      native_receive_state_->closed = false;
      native_receive_state_->failure.reset();
      native_receive_state_->responses.clear();
    }

    auto finished = detail::serve_transport_loop(
        *native_transport_, cancellation,
        [this](const protocol::JsonRpcMessage& message) {
          return dispatch_message(message);
        });

    {
      std::lock_guard<std::mutex> lock(native_receive_state_->mutex);
      native_receive_state_->loop_active = false;
      native_receive_state_->closed = true;
      if (!finished) {
        native_receive_state_->failure = finished.error();
      }
    }
    native_receive_state_->cv.notify_all();
    return finished;
  }

  core::Result<core::Unit> store_native_response(
      const protocol::JsonRpcResponse& response) {
    if (!response.id.has_value()) {
      return mcp::core::unexpected(detail::peer_transport_error(
          "client peer received a response without an id"));
    }
    std::lock_guard<std::mutex> lock(native_receive_state_->mutex);
    native_receive_state_
        ->responses[detail::peer_request_cancellation_key(*response.id)] =
        response;
    native_receive_state_->cv.notify_all();
    return core::Unit{};
  }

  core::Result<protocol::JsonRpcResponse> wait_native_response(
      const protocol::RequestId& request_id) {
    const auto key = detail::peer_request_cancellation_key(request_id);
    std::unique_lock<std::mutex> lock(native_receive_state_->mutex);
    native_receive_state_->cv.wait(lock, [&] {
      return native_receive_state_->responses.find(key) !=
                 native_receive_state_->responses.end() ||
             native_receive_state_->failure.has_value() ||
             native_receive_state_->closed;
    });

    const auto found = native_receive_state_->responses.find(key);
    if (found != native_receive_state_->responses.end()) {
      auto response = std::move(found->second);
      native_receive_state_->responses.erase(found);
      return response;
    }
    if (native_receive_state_->failure.has_value()) {
      return mcp::core::unexpected(*native_receive_state_->failure);
    }
    return mcp::core::unexpected(detail::peer_transport_error(
        "client peer transport closed before response"));
  }

  std::optional<protocol::JsonRpcResponse> take_native_response(
      const protocol::RequestId& request_id) {
    const auto key = detail::peer_request_cancellation_key(request_id);
    std::lock_guard<std::mutex> lock(native_receive_state_->mutex);
    const auto found = native_receive_state_->responses.find(key);
    if (found == native_receive_state_->responses.end()) {
      return std::nullopt;
    }
    auto response = std::move(found->second);
    native_receive_state_->responses.erase(found);
    return response;
  }

  static protocol::JsonRpcResponse native_error_response(
      const protocol::JsonRpcRequest& request, const core::Error& error) {
    return protocol::make_error_response(
        std::optional<protocol::RequestId>{request.id},
        protocol::make_error(
            error.code, error.message,
            error.detail.empty()
                ? std::nullopt
                : std::optional<protocol::Json>{error.detail}));
  }

  static protocol::JsonRpcResponse native_error_response(
      const protocol::JsonRpcRequest& request, protocol::ErrorCode code,
      std::string message, std::string detail = {}) {
    return protocol::make_error_response(
        std::optional<protocol::RequestId>{request.id},
        protocol::make_error(
            code, std::move(message),
            detail.empty() ? std::nullopt
                           : std::optional<protocol::Json>{std::move(detail)}));
  }

  CancellationToken begin_client_request_cancellation(
      const protocol::RequestId& request_id) {
    CancellationSource source;
    auto token = source.token();
    std::lock_guard lock(*client_request_cancellations_mutex_);
    (*client_request_cancellations_)[detail::peer_request_cancellation_key(
        request_id)] = std::move(source);
    return token;
  }

  void end_client_request_cancellation(
      const protocol::RequestId& request_id) noexcept {
    std::lock_guard lock(*client_request_cancellations_mutex_);
    client_request_cancellations_->erase(
        detail::peer_request_cancellation_key(request_id));
  }

  void cancel_client_request(const protocol::RequestId& request_id) noexcept {
    std::lock_guard lock(*client_request_cancellations_mutex_);
    const auto it = client_request_cancellations_->find(
        detail::peer_request_cancellation_key(request_id));
    if (it != client_request_cancellations_->end()) {
      it->second.cancel();
    }
  }

  core::Result<core::Unit> handle_native_notification(
      const protocol::JsonRpcNotification& notification) try {
    if (notification.method == std::string(protocol::InitializedMethod) &&
        initialized_handler_) {
      initialized_handler_();
    } else if (notification.method ==
               std::string(protocol::CancelledNotificationMethod)) {
      const auto params = protocol::cancelled_notification_params_from_json(
          notification.params);
      if (!params) {
        return mcp::core::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "cancelled notification requires a requestId"));
      }
      cancel_client_request(params->request_id);
      if (cancelled_handler_) {
        cancelled_handler_(params->request_id,
                           params->reason.value_or(std::string{}));
      }
    } else if (notification.method ==
                   std::string(protocol::LoggingMessageNotificationMethod) &&
               logging_message_handler_) {
      std::string level;
      std::string message;
      if (notification.params.is_object()) {
        if (notification.params.contains("level") &&
            notification.params.at("level").is_string()) {
          level = notification.params.at("level").get<std::string>();
        }
        if (notification.params.contains("data")) {
          if (notification.params.at("data").is_string()) {
            message = notification.params.at("data").get<std::string>();
          } else {
            message = notification.params.at("data").dump();
          }
        }
      }
      logging_message_handler_(level, message);
    } else if (notification.method ==
                   std::string(protocol::ToolsListChangedNotificationMethod) &&
               tool_list_changed_handler_) {
      tool_list_changed_handler_();
    } else if (notification.method ==
                   std::string(
                       protocol::PromptsListChangedNotificationMethod) &&
               prompt_list_changed_handler_) {
      prompt_list_changed_handler_();
    } else if (notification.method ==
                   std::string(
                       protocol::ResourcesListChangedNotificationMethod) &&
               resource_list_changed_handler_) {
      resource_list_changed_handler_();
    } else if (notification.method ==
                   std::string(protocol::ResourcesUpdatedNotificationMethod) &&
               resource_updated_handler_) {
      if (!notification.params.is_object() ||
          !notification.params.contains("uri") ||
          !notification.params.at("uri").is_string()) {
        return mcp::core::unexpected(errors::make(
            protocol::ErrorCode::InvalidParams,
            "resource updated notification requires a string uri"));
      }
      resource_updated_handler_(
          notification.params.at("uri").get<std::string>());
    } else if (notification.method ==
                   std::string(protocol::ProgressNotificationMethod) &&
               progress_handler_) {
      const auto params =
          protocol::progress_notification_params_from_json(notification.params);
      if (!params) {
        return mcp::core::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "progress notification requires valid params"));
      }
      progress_handler_(*params);
    } else if (notification.method ==
                   std::string(
                       protocol::ElicitationCompleteNotificationMethod) &&
               elicitation_complete_handler_) {
      const auto params =
          protocol::elicitation_complete_notification_params_from_json(
              notification.params);
      if (!params) {
        return mcp::core::unexpected(errors::make(
            protocol::ErrorCode::InvalidParams,
            "elicitation completion notification requires valid params"));
      }
      elicitation_complete_handler_(params->elicitation_id);
    } else if (notification.method ==
                   std::string(protocol::TasksStatusNotificationMethod) &&
               task_status_handler_) {
      const auto task = protocol::task_from_json(notification.params);
      if (!task) {
        return mcp::core::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "task status notification requires valid task data"));
      }
      task_status_handler_(*task);
    } else if (notification.method ==
                   std::string(protocol::RootsListChangedNotificationMethod) &&
               roots_list_changed_handler_) {
      roots_list_changed_handler_();
    }

    if (raw_notification_handler_) {
      raw_notification_handler_(notification);
    }
    return core::Unit{};
  } catch (const std::exception& ex) {
    return mcp::core::unexpected(errors::handler_failed(ex.what()));
  } catch (...) {
    return mcp::core::unexpected(errors::handler_unknown_exception());
  }

  core::Result<protocol::JsonRpcResponse> handle_native_request(
      const protocol::JsonRpcRequest& request) try {
    if (request.method == std::string(protocol::PingMethod)) {
      return protocol::make_response(request.id, protocol::Json::object());
    }

    const auto request_cancellation =
        begin_client_request_cancellation(request.id);
    const std::shared_ptr<void> request_cancellation_cleanup(
        nullptr, [this, request_id = request.id](void*) noexcept {
          end_client_request_cancellation(request_id);
        });

    if (request.method == std::string(protocol::RootsListMethod)) {
      if (roots_list_request_cancellation_handler_) {
        const auto roots =
            roots_list_request_cancellation_handler_(request_cancellation);
        if (!roots) {
          return native_error_response(request, roots.error());
        }
        return protocol::make_response(
            request.id, protocol::roots_list_result_to_json(*roots));
      }
      if (roots_list_request_handler_) {
        const auto roots = roots_list_request_handler_();
        if (!roots) {
          return native_error_response(request, roots.error());
        }
        return protocol::make_response(
            request.id, protocol::roots_list_result_to_json(*roots));
      }

      protocol::RootsListResult result;
      result.roots = roots_;
      return protocol::make_response(
          request.id, protocol::roots_list_result_to_json(result));
    }

    if (request.method == std::string(protocol::SamplingCreateMessageMethod)) {
      if (!sampling_request_handler_ &&
          !sampling_request_cancellation_handler_) {
        return native_error_response(
            request, protocol::ErrorCode::MethodNotFound,
            "sampling request handler is not configured");
      }
      const auto params =
          protocol::create_message_params_from_json(request.params);
      if (!params) {
        return detail::peer_params_error_response(request, params.error());
      }
      const auto result = sampling_request_cancellation_handler_
                              ? sampling_request_cancellation_handler_(
                                    *params, request_cancellation)
                              : sampling_request_handler_(*params);
      if (!result) {
        return native_error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::create_message_result_to_json(*result));
    }

    if (request.method == std::string(protocol::ElicitationCreateMethod)) {
      const auto params =
          protocol::create_elicitation_request_param_from_json(request.params);
      if (!params) {
        return detail::peer_params_error_response(request, params.error());
      }
      if (!elicitation_request_handler_ &&
          !elicitation_request_cancellation_handler_) {
        protocol::CreateElicitationResult decline_result;
        decline_result.action = protocol::ElicitationAction::Decline;
        return protocol::make_response(
            request.id,
            protocol::create_elicitation_result_to_json(decline_result));
      }
      const auto result = elicitation_request_cancellation_handler_
                              ? elicitation_request_cancellation_handler_(
                                    *params, request_cancellation)
                              : elicitation_request_handler_(*params);
      if (!result) {
        return native_error_response(request, result.error());
      }
      const auto capabilities =
          detail::default_peer_client_capabilities(client_capabilities_);
      if (params->mode == protocol::ElicitationMode::Form &&
          capabilities.elicitation.form_schema_validation) {
        const auto valid = protocol::validate_elicitation_result_content(
            params->requested_schema, *result);
        if (!valid) {
          return native_error_response(
              request,
              errors::make(protocol::ErrorCode::InternalError,
                           "elicitation result failed schema validation",
                           valid.error().message));
        }
      }
      return protocol::make_response(
          request.id, protocol::create_elicitation_result_to_json(*result));
    }

    if (custom_request_cancellation_handler_ || custom_request_handler_) {
      const auto result = custom_request_cancellation_handler_
                              ? custom_request_cancellation_handler_(
                                    request, request_cancellation)
                              : custom_request_handler_(request);
      if (!result) {
        return native_error_response(request, result.error());
      }
      return protocol::make_response(request.id, std::move(*result));
    }

    return native_error_response(request, protocol::ErrorCode::MethodNotFound,
                                 "method not found", request.method);
  } catch (const std::exception& ex) {
    return native_error_response(request, errors::handler_failed(ex.what()));
  } catch (...) {
    return native_error_response(request, errors::handler_unknown_exception());
  }

  static core::Error unsupported_server_capability(std::string message) {
    return errors::make(protocol::ErrorCode::MethodNotFound, std::move(message),
                        {}, "protocol");
  }

  core::Result<core::Unit> record_server_capabilities(
      const protocol::Json& initialize_payload) {
    // Accept minimal responses (e.g. from conformance harness) that don't have
    // all required fields. This allows version negotiation to succeed even when
    // the server returns a simplified response.
    if (initialize_payload.is_object() &&
        !initialize_payload.contains("protocolVersion")) {
      return core::Unit{};
    }

    const auto parsed =
        protocol::initialize_result_from_json(initialize_payload);
    if (!parsed.has_value()) {
      return mcp::core::unexpected(errors::make(
          protocol::ErrorCode::InvalidRequest, parsed.error().message,
          parsed.error().detail, "protocol"));
    }
    server_capabilities_ = parsed->capabilities;
    return core::Unit{};
  }

  bool supports_server_completion() const noexcept {
    const auto& capabilities = server_capabilities();
    return !capabilities.has_value() || capabilities->completions.enabled;
  }

  bool supports_server_logging() const noexcept {
    const auto& capabilities = server_capabilities();
    return !capabilities.has_value() || capabilities->logging.enabled;
  }

  bool supports_server_resource_subscribe() const noexcept {
    const auto& capabilities = server_capabilities();
    return !capabilities.has_value() || capabilities->resources.subscribe;
  }

  bool supports_server_task_list() const noexcept {
    const auto& capabilities = server_capabilities();
    return !capabilities.has_value() ||
           (capabilities->tasks.has_value() && capabilities->tasks->list);
  }

  bool supports_server_task_cancel() const noexcept {
    const auto& capabilities = server_capabilities();
    return !capabilities.has_value() ||
           (capabilities->tasks.has_value() && capabilities->tasks->cancel);
  }

  bool supports_server_tasks() const noexcept {
    const auto& capabilities = server_capabilities();
    return !capabilities.has_value() || capabilities->tasks.has_value();
  }

  bool supports_server_task_tool_call() const noexcept {
    const auto& capabilities = server_capabilities();
    return !capabilities.has_value() ||
           (capabilities->tasks.has_value() && capabilities->tasks->tools_call);
  }

  core::Result<protocol::Json> request_json(std::string method,
                                            protocol::Json params) {
    return raw_request(protocol::make_request(
        std::move(method), next_peer_request_id(), std::move(params)));
  }

  core::Result<core::Unit> request_unit(std::string method,
                                        protocol::Json params) {
    auto payload = request_json(std::move(method), std::move(params));
    if (!payload) {
      return mcp::core::unexpected(payload.error());
    }
    return core::Unit{};
  }

  static protocol::Json cursor_params(
      const std::optional<std::string>& cursor) {
    protocol::Json params = protocol::Json::object();
    if (cursor.has_value()) {
      params["cursor"] = *cursor;
    }
    return params;
  }

  template <class Item, class Parser, class Append>
  core::Result<std::vector<Item>> list_all_pages(std::string method,
                                                 Parser parser, Append append) {
    std::vector<Item> all;
    std::optional<std::string> cursor;
    do {
      auto payload = request_json(method, cursor_params(cursor));
      if (!payload) {
        return mcp::core::unexpected(payload.error());
      }
      auto page = parser(*payload);
      if (!page) {
        return mcp::core::unexpected(page.error());
      }
      append(*page, all);
      cursor = page->next_cursor;
    } while (cursor.has_value() && !cursor->empty());
    return all;
  }

  core::Result<protocol::JsonRpcResponse> send_native_request(
      const protocol::JsonRpcRequest& request) {
    std::lock_guard<std::mutex> request_lock(*native_request_mutex_);

    auto sent = native_transport_->send(protocol::JsonRpcMessage{request});
    if (!sent) {
      return mcp::core::unexpected(sent.error());
    }

    if (native_receive_loop_active()) {
      return wait_native_response(request.id);
    }

    if (auto buffered = take_native_response(request.id)) {
      return *buffered;
    }

    while (true) {
      auto received = native_transport_->receive();
      if (!received) {
        return mcp::core::unexpected(received.error());
      }
      if (!received->has_value()) {
        return mcp::core::unexpected(detail::peer_transport_error(
            "client peer transport closed before response"));
      }

      if (auto* response =
              std::get_if<protocol::JsonRpcResponse>(&received->value())) {
        if (response->id.has_value() && *response->id == request.id) {
          return *response;
        }
        auto stored = store_native_response(*response);
        if (!stored) {
          return mcp::core::unexpected(stored.error());
        }
        continue;
      }

      auto dispatched = dispatch_message(received->value());
      if (!dispatched) {
        return mcp::core::unexpected(dispatched.error());
      }
      if (dispatched->has_value()) {
        sent = native_transport_->send(std::move(dispatched->value()));
        if (!sent) {
          return mcp::core::unexpected(sent.error());
        }
      }
    }
  }

  std::unique_ptr<transport::ClientTransport> native_transport_;
  std::shared_ptr<std::mutex> native_request_mutex_ =
      std::make_shared<std::mutex>();
  std::shared_ptr<detail::ClientNativeReceiveState> native_receive_state_ =
      std::make_shared<detail::ClientNativeReceiveState>();
  std::shared_ptr<std::atomic<std::int64_t>> next_request_id_ =
      std::make_shared<std::atomic<std::int64_t>>(1);
  std::optional<protocol::ClientCapabilities> client_capabilities_;
  std::optional<protocol::ServerCapabilities> server_capabilities_;
  std::vector<protocol::Root> roots_;
  client::Client::InitializedHandler initialized_handler_;
  client::Client::CancelledHandler cancelled_handler_;
  client::Client::LoggingMessageHandler logging_message_handler_;
  client::Client::ListChangedHandler tool_list_changed_handler_;
  client::Client::ListChangedHandler prompt_list_changed_handler_;
  client::Client::ListChangedHandler resource_list_changed_handler_;
  client::Client::ResourceUpdatedHandler resource_updated_handler_;
  client::Client::ProgressHandler progress_handler_;
  client::Client::ElicitationCompleteHandler elicitation_complete_handler_;
  client::Client::TaskStatusHandler task_status_handler_;
  client::Client::ListChangedHandler roots_list_changed_handler_;
  client::Client::RootsListRequestHandler roots_list_request_handler_;
  client::Client::RootsListRequestCancellationHandler
      roots_list_request_cancellation_handler_;
  client::Client::SamplingRequestHandler sampling_request_handler_;
  client::Client::SamplingRequestCancellationHandler
      sampling_request_cancellation_handler_;
  client::Client::ElicitationRequestHandler elicitation_request_handler_;
  client::Client::ElicitationRequestCancellationHandler
      elicitation_request_cancellation_handler_;
  client::Client::CustomRequestHandler custom_request_handler_;
  client::Client::CustomRequestCancellationHandler
      custom_request_cancellation_handler_;
  client::Client::RawNotificationHandler raw_notification_handler_;
  std::shared_ptr<std::mutex> client_request_cancellations_mutex_ =
      std::make_shared<std::mutex>();
  std::shared_ptr<std::unordered_map<std::string, CancellationSource>>
      client_request_cancellations_ = std::make_shared<
          std::unordered_map<std::string, CancellationSource>>();
  client::Client client_;
  std::unordered_map<std::string, protocol::Json> tool_schema_cache_;
};

/// @brief Fluent builder for common client peer construction.
class Peer<RoleClient>::Builder {
 public:
  Builder() = default;
  Builder(const Builder&) = delete;
  Builder& operator=(const Builder&) = delete;
  Builder(Builder&&) noexcept = default;
  Builder& operator=(Builder&&) noexcept = default;

  Builder& transport(std::unique_ptr<client::Transport> value) {
    reset_transport();
    concrete_transport_ = std::move(value);
    transport_kind_ = TransportKind::Concrete;
    return *this;
  }

  Builder& transport(std::unique_ptr<transport::ClientTransport> value) {
    reset_transport();
    native_transport_ = std::move(value);
    transport_kind_ = TransportKind::Native;
    return *this;
  }

#if defined(CXXMCP_ENABLE_HTTP)
  Builder& streamable_http(client::Client::StreamableHttpEndpoint endpoint) {
    reset_transport();
    http_endpoint_ = std::move(endpoint);
    transport_kind_ = TransportKind::StreamableHttp;
    return *this;
  }

  Builder& streamable_http(std::string uri) {
    client::Client::StreamableHttpEndpoint endpoint;
    endpoint.uri = std::move(uri);
    return streamable_http(std::move(endpoint));
  }

  Builder& legacy_sse(client::Client::StreamableHttpEndpoint endpoint) {
    reset_transport();
    http_endpoint_ = std::move(endpoint);
    transport_kind_ = TransportKind::LegacySse;
    return *this;
  }

  Builder& legacy_sse(std::string uri) {
    client::Client::StreamableHttpEndpoint endpoint;
    endpoint.uri = std::move(uri);
    return legacy_sse(std::move(endpoint));
  }
#endif

#if defined(CXXMCP_ENABLE_WEBSOCKET)
  Builder& websocket(transport::WebSocketClientTransportOptions options) {
    reset_transport();
    ws_options_ = std::move(options);
    transport_kind_ = TransportKind::WebSocket;
    return *this;
  }

  Builder& websocket(std::string uri) {
    transport::WebSocketClientTransportOptions options;
    options.uri = std::move(uri);
    return websocket(std::move(options));
  }
#endif

  Builder& stdio(client::Client::StdioEndpoint endpoint) {
    reset_transport();
    stdio_endpoint_ = std::move(endpoint);
    transport_kind_ = TransportKind::Stdio;
    return *this;
  }

  Builder& process_stdio(client::Client::StdioEndpoint endpoint) {
    return stdio(std::move(endpoint));
  }

  /// @brief Convenience: launch a child process as the MCP server.
  /// @param command Executable path or command string.
  Builder& process_stdio(std::string command) {
    reset_transport();
    transport::ProcessStdioClientTransportOptions options;
    options.command = std::move(command);
    native_transport_ =
        std::make_unique<transport::ProcessStdioClientTransport>(
            std::move(options));
    transport_kind_ = TransportKind::Native;
    return *this;
  }

#if defined(CXXMCP_ENABLE_HTTP) || defined(CXXMCP_ENABLE_WEBSOCKET)
  Builder& header(std::string name, std::string value) {
    http_headers_[std::move(name)] = std::move(value);
    return *this;
  }

  Builder& bearer_token(std::string token) {
    auth_header_ = std::move(token);
    return *this;
  }
#endif

#if defined(CXXMCP_ENABLE_HTTP)
  Builder& auth_refresh_handler(client::HttpAuthRefreshHandler handler) {
    auth_refresh_handler_ = std::move(handler);
    return *this;
  }
#endif

  Builder& timeout(std::chrono::milliseconds value) {
    timeout_ = value;
    return *this;
  }

  Builder& capabilities(protocol::ClientCapabilities value) {
    capabilities_ = std::move(value);
    return *this;
  }

  Builder& roots(std::vector<protocol::Root> value) {
    roots_ = std::move(value);
    return *this;
  }

  Builder& on_initialized(client::Client::InitializedHandler handler) {
    initialized_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_cancelled(client::Client::CancelledHandler handler) {
    cancelled_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_logging_message(client::Client::LoggingMessageHandler handler) {
    logging_message_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_tool_list_changed(client::Client::ListChangedHandler handler) {
    tool_list_changed_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_prompt_list_changed(client::Client::ListChangedHandler handler) {
    prompt_list_changed_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_resource_list_changed(
      client::Client::ListChangedHandler handler) {
    resource_list_changed_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_resource_updated(client::Client::ResourceUpdatedHandler handler) {
    resource_updated_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_progress(client::Client::ProgressHandler handler) {
    progress_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_elicitation_complete(
      client::Client::ElicitationCompleteHandler handler) {
    elicitation_complete_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_task_status(client::Client::TaskStatusHandler handler) {
    task_status_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_roots_list_changed(client::Client::ListChangedHandler handler) {
    roots_list_changed_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_list_roots_request(
      client::Client::ListRootsRequestHandler handler) {
    roots_list_request_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_list_roots_request(
      client::Client::RootsListRequestCancellationHandler handler) {
    roots_list_request_cancellation_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_create_message_request(
      client::Client::CreateMessageRequestHandler handler) {
    sampling_request_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_create_message_request(
      client::Client::SamplingRequestCancellationHandler handler) {
    sampling_request_cancellation_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_create_elicitation_request(
      client::Client::CreateElicitationRequestHandler handler) {
    elicitation_request_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_create_elicitation_request(
      client::Client::ElicitationRequestCancellationHandler handler) {
    elicitation_request_cancellation_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_custom_request(client::Client::CustomRequestHandler handler) {
    custom_request_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_custom_request(
      client::Client::CustomRequestCancellationHandler handler) {
    custom_request_cancellation_handler_ = std::move(handler);
    return *this;
  }

  Builder& on_raw_notification(client::Client::RawNotificationHandler handler) {
    raw_notification_handler_ = std::move(handler);
    return *this;
  }

  Builder& handler(const client::ClientHandler& handler) {
    handler_ = handler;
    return *this;
  }

  core::Result<Peer> build() {
    auto peer = make_peer();
    if (!peer) {
      return mcp::core::unexpected(peer.error());
    }
    apply_to(*peer);
    return std::move(*peer);
  }

  /// @brief Builds the peer, serves it, invokes the callback, then stops.
  ///
  /// Usage:
  /// @code
  /// return ClientPeer::builder()
  ///     .process_stdio("my-server")
  ///     .run([](auto& svc) {
  ///         svc.peer().initialize();
  ///         svc.peer().call_tool("echo", Json{{"value", "hello"}});
  ///     });
  /// @endcode
  ///
  /// @param fn Callback receiving the running service handle.
  /// @return 0 on success, 1 on build or serve error.
  template <class Fn>
  int run(Fn&& fn);

 private:
  enum class TransportKind {
    None,
    Concrete,
    Native,
#if defined(CXXMCP_ENABLE_HTTP)
    StreamableHttp,
    LegacySse,
#endif
#if defined(CXXMCP_ENABLE_WEBSOCKET)
    WebSocket,
#endif
    Stdio,
  };

  void reset_transport() {
    concrete_transport_.reset();
    native_transport_.reset();
#if defined(CXXMCP_ENABLE_HTTP)
    http_endpoint_ = client::Client::StreamableHttpEndpoint{};
#endif
#if defined(CXXMCP_ENABLE_WEBSOCKET)
    ws_options_ = {};
#endif
    stdio_endpoint_ = client::Client::StdioEndpoint{};
    transport_kind_ = TransportKind::None;
  }

#if defined(CXXMCP_ENABLE_HTTP)
  client::Client::StreamableHttpEndpoint configured_http_endpoint() {
    auto endpoint = std::move(http_endpoint_);
    for (auto& header : http_headers_) {
      endpoint.headers[std::move(header.first)] = std::move(header.second);
    }
    if (auth_header_.has_value()) {
      endpoint.auth_header = std::move(auth_header_);
    }
    if (auth_refresh_handler_) {
      endpoint.auth_refresh_handler = std::move(auth_refresh_handler_);
    }
    if (timeout_.has_value()) {
      endpoint.timeout = *timeout_;
    }
    return endpoint;
  }
#endif

  core::Result<Peer> make_peer() {
    switch (transport_kind_) {
      case TransportKind::Concrete:
        if (!concrete_transport_) {
          return missing_transport("client transport is null");
        }
        return Peer(std::move(concrete_transport_));
      case TransportKind::Native:
        if (!native_transport_) {
          return missing_transport("client transport is null");
        }
        return Peer(std::move(native_transport_));
#if defined(CXXMCP_ENABLE_HTTP)
      case TransportKind::StreamableHttp:
        return Peer::connect_streamable_http(configured_http_endpoint());
      case TransportKind::LegacySse:
        return Peer::connect_legacy_sse(configured_http_endpoint());
#endif
#if defined(CXXMCP_ENABLE_WEBSOCKET)
      case TransportKind::WebSocket: {
        auto options = std::move(ws_options_);
        for (auto& [name, value] : http_headers_) {
          options.headers[std::move(name)] = std::move(value);
        }
        if (auth_header_.has_value()) {
          options.auth_header = std::string("Bearer ") + *auth_header_;
          auth_header_.reset();
        }
        if (timeout_.has_value()) {
          options.timeout = *timeout_;
        }
        return Peer(std::make_unique<transport::WebSocketClientTransport>(
            std::move(options)));
      }
#endif
      case TransportKind::Stdio:
        return Peer::connect_stdio(std::move(stdio_endpoint_));
      case TransportKind::None:
        return missing_transport("client peer transport is not configured");
    }
    return missing_transport("client peer transport is not configured");
  }

  static core::Result<Peer> missing_transport(std::string message) {
    return mcp::core::unexpected(core::Error{
        static_cast<int>(protocol::ErrorCode::InvalidParams),
        std::move(message),
        {},
        "configuration",
    });
  }

  void apply_to(Peer& peer) {
    if (capabilities_.has_value()) {
      peer.set_capabilities(std::move(*capabilities_));
    }
    if (roots_.has_value()) {
      peer.set_roots(std::move(*roots_));
    }
    if (handler_.has_value()) {
      peer.set_handler(*handler_);
    }
    if (initialized_handler_) {
      peer.on_initialized(std::move(initialized_handler_));
    }
    if (cancelled_handler_) {
      peer.on_cancelled(std::move(cancelled_handler_));
    }
    if (logging_message_handler_) {
      peer.on_logging_message(std::move(logging_message_handler_));
    }
    if (tool_list_changed_handler_) {
      peer.on_tool_list_changed(std::move(tool_list_changed_handler_));
    }
    if (prompt_list_changed_handler_) {
      peer.on_prompt_list_changed(std::move(prompt_list_changed_handler_));
    }
    if (resource_list_changed_handler_) {
      peer.on_resource_list_changed(std::move(resource_list_changed_handler_));
    }
    if (resource_updated_handler_) {
      peer.on_resource_updated(std::move(resource_updated_handler_));
    }
    if (progress_handler_) {
      peer.on_progress(std::move(progress_handler_));
    }
    if (elicitation_complete_handler_) {
      peer.on_elicitation_complete(std::move(elicitation_complete_handler_));
    }
    if (task_status_handler_) {
      peer.on_task_status(std::move(task_status_handler_));
    }
    if (roots_list_changed_handler_) {
      peer.on_roots_list_changed(std::move(roots_list_changed_handler_));
    }
    if (roots_list_request_handler_) {
      peer.on_list_roots_request(std::move(roots_list_request_handler_));
    }
    if (roots_list_request_cancellation_handler_) {
      peer.on_list_roots_request(
          std::move(roots_list_request_cancellation_handler_));
    }
    if (sampling_request_handler_) {
      peer.on_create_message_request(std::move(sampling_request_handler_));
    }
    if (sampling_request_cancellation_handler_) {
      peer.on_create_message_request(
          std::move(sampling_request_cancellation_handler_));
    }
    if (elicitation_request_handler_) {
      peer.on_create_elicitation_request(
          std::move(elicitation_request_handler_));
    }
    if (elicitation_request_cancellation_handler_) {
      peer.on_create_elicitation_request(
          std::move(elicitation_request_cancellation_handler_));
    }
    if (custom_request_handler_) {
      peer.on_custom_request(std::move(custom_request_handler_));
    }
    if (custom_request_cancellation_handler_) {
      peer.on_custom_request(std::move(custom_request_cancellation_handler_));
    }
    if (raw_notification_handler_) {
      peer.on_raw_notification(std::move(raw_notification_handler_));
    }
  }

  TransportKind transport_kind_ = TransportKind::None;
  std::unique_ptr<client::Transport> concrete_transport_;
  std::unique_ptr<transport::ClientTransport> native_transport_;
#if defined(CXXMCP_ENABLE_HTTP) || defined(CXXMCP_ENABLE_WEBSOCKET)
  std::unordered_map<std::string, std::string> http_headers_;
  std::optional<std::string> auth_header_;
#endif
#if defined(CXXMCP_ENABLE_HTTP)
  client::Client::StreamableHttpEndpoint http_endpoint_;
  client::HttpAuthRefreshHandler auth_refresh_handler_;
#endif
#if defined(CXXMCP_ENABLE_WEBSOCKET)
  transport::WebSocketClientTransportOptions ws_options_;
#endif
  client::Client::StdioEndpoint stdio_endpoint_;
  std::optional<std::chrono::milliseconds> timeout_;
  std::optional<protocol::ClientCapabilities> capabilities_;
  std::optional<std::vector<protocol::Root>> roots_;
  std::optional<client::ClientHandler> handler_;
  client::Client::InitializedHandler initialized_handler_;
  client::Client::CancelledHandler cancelled_handler_;
  client::Client::LoggingMessageHandler logging_message_handler_;
  client::Client::ListChangedHandler tool_list_changed_handler_;
  client::Client::ListChangedHandler prompt_list_changed_handler_;
  client::Client::ListChangedHandler resource_list_changed_handler_;
  client::Client::ResourceUpdatedHandler resource_updated_handler_;
  client::Client::ProgressHandler progress_handler_;
  client::Client::ElicitationCompleteHandler elicitation_complete_handler_;
  client::Client::TaskStatusHandler task_status_handler_;
  client::Client::ListChangedHandler roots_list_changed_handler_;
  client::Client::RootsListRequestHandler roots_list_request_handler_;
  client::Client::RootsListRequestCancellationHandler
      roots_list_request_cancellation_handler_;
  client::Client::SamplingRequestHandler sampling_request_handler_;
  client::Client::SamplingRequestCancellationHandler
      sampling_request_cancellation_handler_;
  client::Client::ElicitationRequestHandler elicitation_request_handler_;
  client::Client::ElicitationRequestCancellationHandler
      elicitation_request_cancellation_handler_;
  client::Client::CustomRequestHandler custom_request_handler_;
  client::Client::CustomRequestCancellationHandler
      custom_request_cancellation_handler_;
  client::Client::RawNotificationHandler raw_notification_handler_;
};

inline Peer<RoleClient>::Builder Peer<RoleClient>::builder() {
  return Builder{};
}

/// @brief Server-side peer boundary for exposing MCP capabilities.
template <>
class Peer<RoleServer> {
 public:
  class Builder;

  /// @brief Creates a server peer from options.
  explicit Peer(server::ServerOptions options = {})
      : server_(std::make_unique<server::Server>(std::move(options))) {}

  /// @brief Creates a server peer from an owned server implementation.
  explicit Peer(std::unique_ptr<server::Server> server)
      : server_(std::move(server)) {}

  static core::Result<Peer> build(server::ServerBuilder builder) {
    auto built = builder.build();
    if (!built) {
      return mcp::core::unexpected(built.error());
    }
    return Peer(std::move(*built));
  }

  /// @brief Starts a fluent builder for common server peer construction.
  static Builder builder();

  CXXMCP_DEPRECATED(
      "server() is a compatibility escape hatch; prefer ServerPeer methods")
  server::Server& server() noexcept { return *server_; }

  CXXMCP_DEPRECATED(
      "server() is a compatibility escape hatch; prefer ServerPeer methods")
  const server::Server& server() const noexcept { return *server_; }

  server::ServerInfo get_info() const { return server_->get_info(); }

  const protocol::ServerCapabilities& capabilities() const noexcept {
    return server_->capabilities();
  }

  Peer& set_completion_handler(server::Server::JsonHandler handler) {
    if (handler) {
      completion_handler_ = [handler](const protocol::Json& params,
                                      const server::SessionContext&) mutable {
        return handler(params);
      };
    } else {
      completion_handler_ = {};
    }
    server_->set_completion_handler(std::move(handler));
    return *this;
  }

  Peer& set_completion_handler(server::Server::JsonContextHandler handler) {
    completion_handler_ = handler;
    server_->set_completion_handler(std::move(handler));
    return *this;
  }

  Peer& set_sampling_handler(server::Server::JsonHandler handler) {
    if (handler) {
      sampling_handler_ = [handler](const protocol::Json& params,
                                    const server::SessionContext&) mutable {
        return handler(params);
      };
    } else {
      sampling_handler_ = {};
    }
    server_->set_sampling_handler(std::move(handler));
    return *this;
  }

  Peer& set_sampling_handler(server::Server::JsonContextHandler handler) {
    sampling_handler_ = handler;
    server_->set_sampling_handler(std::move(handler));
    return *this;
  }

  Peer& set_logging_handler(server::Server::LoggingHandler handler) {
    logging_handler_ = handler;
    server_->set_logging_handler(std::move(handler));
    return *this;
  }

  Peer& set_raw_request_handler(server::Server::RawRequestHandler handler) {
    raw_request_handler_ = handler;
    server_->set_raw_request_handler(std::move(handler));
    return *this;
  }

  Peer& set_raw_request_handler(
      server::Server::RawRequestContextHandler handler) {
    raw_request_context_handler_ = handler;
    server_->set_raw_request_handler(std::move(handler));
    return *this;
  }

  Peer& set_raw_notification_handler(
      server::Server::RawNotificationHandler handler) {
    native_notification_state_ = true;
    raw_notification_handler_ = handler;
    server_->set_raw_notification_handler(std::move(handler));
    return *this;
  }

  Peer& set_custom_request_handler(server::Server::RawRequestHandler handler) {
    raw_request_handler_ = handler;
    server_->set_custom_request_handler(std::move(handler));
    return *this;
  }

  Peer& set_custom_request_handler(
      server::Server::RawRequestContextHandler handler) {
    raw_request_context_handler_ = handler;
    server_->set_custom_request_handler(std::move(handler));
    return *this;
  }

  Peer& set_custom_notification_handler(
      server::Server::RawNotificationHandler handler) {
    native_notification_state_ = true;
    raw_notification_handler_ = handler;
    server_->set_custom_notification_handler(std::move(handler));
    return *this;
  }

  Peer& set_tools_list_handler(server::Server::ToolsListHandler handler) {
    server_->set_tools_list_handler(std::move(handler));
    return *this;
  }

  Peer& set_prompts_list_handler(server::Server::PromptsListHandler handler) {
    server_->set_prompts_list_handler(std::move(handler));
    return *this;
  }

  Peer& set_resources_list_handler(
      server::Server::ResourcesListHandler handler) {
    server_->set_resources_list_handler(std::move(handler));
    return *this;
  }

  Peer& set_resource_templates_list_handler(
      server::Server::ResourceTemplatesListHandler handler) {
    server_->set_resource_templates_list_handler(std::move(handler));
    return *this;
  }

  Peer& set_task_list_handler(server::Server::TaskListHandler handler) {
    task_list_handler_ = handler;
    server_->set_task_list_handler(std::move(handler));
    return *this;
  }

  Peer& set_task_get_handler(server::Server::TaskGetHandler handler) {
    task_get_handler_ = handler;
    server_->set_task_get_handler(std::move(handler));
    return *this;
  }

  Peer& set_task_cancel_handler(server::Server::TaskCancelHandler handler) {
    task_cancel_handler_ = handler;
    server_->set_task_cancel_handler(std::move(handler));
    return *this;
  }

  Peer& set_task_result_handler(server::Server::TaskResultHandler handler) {
    task_result_handler_ = handler;
    server_->set_task_result_handler(std::move(handler));
    return *this;
  }

  Peer& set_progress_handler(server::Server::ProgressHandler handler) {
    native_notification_state_ = true;
    progress_handler_ = handler;
    server_->set_progress_handler(std::move(handler));
    return *this;
  }

  Peer& set_roots_list_changed_handler(
      server::Server::RootsListChangedHandler handler) {
    native_notification_state_ = true;
    roots_list_changed_handler_ = handler;
    server_->set_roots_list_changed_handler(std::move(handler));
    return *this;
  }

  Peer& set_tool_list_changed_handler(
      server::Server::ListChangedHandler handler) {
    native_notification_state_ = true;
    tool_list_changed_handler_ = handler;
    server_->set_tool_list_changed_handler(std::move(handler));
    return *this;
  }

  Peer& set_prompt_list_changed_handler(
      server::Server::ListChangedHandler handler) {
    native_notification_state_ = true;
    prompt_list_changed_handler_ = handler;
    server_->set_prompt_list_changed_handler(std::move(handler));
    return *this;
  }

  Peer& set_resource_list_changed_handler(
      server::Server::ListChangedHandler handler) {
    native_notification_state_ = true;
    resource_list_changed_handler_ = handler;
    server_->set_resource_list_changed_handler(std::move(handler));
    return *this;
  }

  Peer& set_resource_updated_handler(
      server::Server::ResourceUpdatedHandler handler) {
    native_notification_state_ = true;
    resource_updated_handler_ = handler;
    server_->set_resource_updated_handler(std::move(handler));
    return *this;
  }

  Peer& set_handler(const server::ServerHandler& handler) {
    if (handler.on_completion) {
      set_completion_handler(handler.on_completion);
    }
    if (handler.on_sampling) {
      set_sampling_handler(handler.on_sampling);
    }
    if (handler.on_logging) {
      set_logging_handler(handler.on_logging);
    }
    if (handler.on_raw_request) {
      set_raw_request_handler(handler.on_raw_request);
    }
    if (handler.on_raw_notification) {
      set_raw_notification_handler(handler.on_raw_notification);
    }
    if (handler.on_custom_request) {
      set_custom_request_handler(handler.on_custom_request);
    }
    if (handler.on_custom_notification) {
      set_custom_notification_handler(handler.on_custom_notification);
    }
    if (handler.on_tools_list) {
      set_tools_list_handler(handler.on_tools_list);
    }
    if (handler.on_prompts_list) {
      set_prompts_list_handler(handler.on_prompts_list);
    }
    if (handler.on_resources_list) {
      set_resources_list_handler(handler.on_resources_list);
    }
    if (handler.on_resource_templates_list) {
      set_resource_templates_list_handler(handler.on_resource_templates_list);
    }
    if (handler.on_task_list) {
      set_task_list_handler(handler.on_task_list);
    }
    if (handler.on_task_get) {
      set_task_get_handler(handler.on_task_get);
    }
    if (handler.on_task_cancel) {
      set_task_cancel_handler(handler.on_task_cancel);
    }
    if (handler.on_task_result) {
      set_task_result_handler(handler.on_task_result);
    }
    if (handler.on_progress) {
      set_progress_handler(handler.on_progress);
    }
    if (handler.on_roots_list_changed) {
      set_roots_list_changed_handler(handler.on_roots_list_changed);
    }
    if (handler.on_tool_list_changed) {
      set_tool_list_changed_handler(handler.on_tool_list_changed);
    }
    if (handler.on_prompt_list_changed) {
      set_prompt_list_changed_handler(handler.on_prompt_list_changed);
    }
    if (handler.on_resource_list_changed) {
      set_resource_list_changed_handler(handler.on_resource_list_changed);
    }
    if (handler.on_resource_updated) {
      set_resource_updated_handler(handler.on_resource_updated);
    }
    if (handler.on_completion_with_context) {
      set_completion_handler(handler.on_completion_with_context);
    }
    if (handler.on_sampling_with_context) {
      set_sampling_handler(handler.on_sampling_with_context);
    }
    return *this;
  }

  Peer& set_handler(const server::ServerHandlerInterface& handler) {
    server_->set_handler(handler);
    set_raw_request_handler([&handler](const protocol::JsonRpcRequest& request,
                                       const server::SessionContext& context,
                                       CancellationToken cancellation)
                                -> std::optional<protocol::JsonRpcResponse> {
      const auto handler_response = server::dispatch_server_handler_request(
          handler, request, context, cancellation);
      if (handler_response.has_value()) {
        return handler_response;
      }
      return handler.on_custom_request(request, context);
    });
    return *this;
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

  core::Result<protocol::ToolResult> call_tool(
      std::string_view name, protocol::Json arguments,
      const server::SessionContext& context,
      CancellationToken cancellation = CancellationToken::none()) const {
    return server_->call_tool(name, std::move(arguments), context,
                              cancellation);
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

  core::Result<protocol::PromptsGetResult> get_prompt(
      std::string_view name, protocol::Json arguments,
      const server::SessionContext& context,
      CancellationToken cancellation = CancellationToken::none()) const {
    return server_->get_prompt(name, std::move(arguments), context,
                               cancellation);
  }

  std::vector<protocol::Resource> list_resources() const {
    return server_->list_resources();
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri, protocol::Json params = protocol::Json::object(),
      const std::string& session_id = {}) const {
    return server_->read_resource(uri, std::move(params), session_id);
  }

  core::Result<protocol::ResourcesReadResult> read_resource(
      std::string_view uri, protocol::Json params,
      const server::SessionContext& context,
      CancellationToken cancellation = CancellationToken::none()) const {
    return server_->read_resource(uri, std::move(params), context,
                                  cancellation);
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
      const server::SessionContext& input_context = {},
      transport::ServerTransport* native_transport = nullptr) try {
    auto authenticated_context = server_->authenticate_context(input_context);
    if (!authenticated_context) {
      return detail::peer_auth_error_response(request,
                                              authenticated_context.error());
    }
    server::SessionContext context = std::move(*authenticated_context);

    if (request.method == protocol::InitializeMethod) {
      const auto valid =
          detail::validate_peer_server_initialize_params(request.params);
      if (!valid) {
        return detail::peer_error_response(request, valid.error());
      }
      const auto requested_version =
          request.params.at("protocolVersion").get<std::string>();
      const auto negotiated_version =
          protocol::negotiate_protocol_version(requested_version);
      if (!negotiated_version.has_value()) {
        return detail::peer_error_response(
            request, core::Error{
                         static_cast<int>(protocol::ErrorCode::InvalidParams),
                         "unsupported MCP protocol version(\"" +
                             requested_version + "\")",
                         requested_version,
                         "protocol",
                     });
      }
      return protocol::make_response(
          request.id, detail::make_peer_server_initialize_result(
                          get_info(), capabilities(), *negotiated_version));
    }
    if (request.method == protocol::PingMethod) {
      return protocol::make_response(request.id, protocol::Json::object());
    }

    if (raw_request_context_handler_) {
      const auto request_cancellation =
          begin_peer_request_cancellation(request.id);
      const std::shared_ptr<void> request_cancellation_cleanup(
          nullptr, [this, request_id = request.id](void*) noexcept {
            end_peer_request_cancellation(request_id);
          });
      const auto raw_response =
          raw_request_context_handler_(request, context, request_cancellation);
      if (raw_response.has_value()) {
        return *raw_response;
      }
    }

    if (raw_request_handler_) {
      const auto raw_response = raw_request_handler_(request, context);
      if (raw_response.has_value()) {
        return *raw_response;
      }
    }

    if (request.method == protocol::ToolsListMethod) {
      const auto params =
          protocol::paginated_request_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(
            request, errors::make(protocol::ErrorCode::InvalidParams,
                                  "tools/list params must be an object with an "
                                  "optional string cursor"));
      }
      const auto result = server_->list_tools(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::tools_list_result_to_json(*result));
    }

    if (request.method == protocol::ToolsGetMethod) {
      if (!request.params.is_object() || !request.params.contains("name") ||
          !request.params.at("name").is_string()) {
        return detail::peer_error_response(
            request, errors::make(protocol::ErrorCode::InvalidParams,
                                  "tools/get requires a string name"));
      }

      const auto tool = get_tool(request.params.at("name").get<std::string>());
      if (!tool) {
        return detail::peer_error_response(request, tool.error());
      }
      return protocol::make_response(request.id,
                                     protocol::tool_definition_to_json(*tool));
    }

    if (request.method == protocol::ToolsCallMethod) {
      const auto call = protocol::tool_call_from_json(request.params);
      if (!call) {
        return detail::peer_params_error_response(request, call.error());
      }
      if (call->task.has_value()) {
        const auto valid = server_->tools().validate(*call);
        if (!valid) {
          return detail::peer_params_error_response(request, valid.error());
        }
        const auto task_manager = server_->task_manager();
        if (!task_manager) {
          return detail::peer_error_response(
              request, errors::make(protocol::ErrorCode::MethodNotFound,
                                    "task processor is not configured"));
        }
        const auto task =
            task_manager->submit_tool_call(server_->tools(), *call, context,
                                           server_->schema_validator().get());
        if (!task) {
          return detail::peer_params_error_response(request, task.error());
        }
        return protocol::make_response(
            request.id, protocol::create_task_result_to_json(*task));
      }

      const auto request_cancellation =
          begin_peer_request_cancellation(request.id);
      const std::shared_ptr<void> request_cancellation_cleanup(
          nullptr, [this, request_id = request.id](void*) noexcept {
            end_peer_request_cancellation(request_id);
          });
      const auto result =
          server_->tools().call(*call, context, request_cancellation,
                                server_->schema_validator().get());
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }

      return protocol::make_response(request.id,
                                     protocol::tool_result_to_json(*result));
    }

    if (request.method == protocol::PromptsListMethod) {
      const auto params =
          protocol::paginated_request_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(
            request,
            errors::make(protocol::ErrorCode::InvalidParams,
                         "prompts/list params must be an object with an "
                         "optional string cursor"));
      }
      const auto result = server_->list_prompts(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::prompts_list_result_to_json(*result));
    }

    if (request.method == protocol::PromptsGetMethod) {
      const auto params =
          protocol::prompts_get_params_from_json(request.params);
      if (!params) {
        return detail::peer_params_error_response(request, params.error());
      }

      const auto request_cancellation =
          begin_peer_request_cancellation(request.id);
      const std::shared_ptr<void> request_cancellation_cleanup(
          nullptr, [this, request_id = request.id](void*) noexcept {
            end_peer_request_cancellation(request_id);
          });
      const auto result = server_->prompts().get(
          params->name, params->arguments, context, request_cancellation);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }

      return protocol::make_response(
          request.id, protocol::prompts_get_result_to_json(*result));
    }

    if (request.method == protocol::ResourcesListMethod) {
      const auto params =
          protocol::paginated_request_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(
            request,
            errors::make(protocol::ErrorCode::InvalidParams,
                         "resources/list params must be an object with an "
                         "optional string cursor"));
      }
      const auto result = server_->list_resources(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::resources_list_result_to_json(*result));
    }

    if (request.method == protocol::ResourcesReadMethod) {
      const auto params =
          protocol::resources_read_params_from_json(request.params);
      if (!params) {
        return detail::peer_params_error_response(request, params.error());
      }

      const auto request_cancellation =
          begin_peer_request_cancellation(request.id);
      const std::shared_ptr<void> request_cancellation_cleanup(
          nullptr, [this, request_id = request.id](void*) noexcept {
            end_peer_request_cancellation(request_id);
          });
      auto result = server_->resources().read(params->uri, request.params,
                                              context, request_cancellation);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      result->ttl_ms = 300000;
      result->cache_scope = "public";

      return protocol::make_response(
          request.id, protocol::resources_read_result_to_json(*result));
    }

    if (request.method == protocol::ResourcesTemplatesListMethod) {
      const auto params =
          protocol::paginated_request_params_from_json(request.params);
      if (!params) {
        return detail::peer_error_response(
            request,
            errors::make(protocol::ErrorCode::InvalidParams,
                         "resources/templates/list params must be an object "
                         "with an optional string cursor"));
      }
      const auto result = server_->list_resource_templates(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(
          request.id,
          protocol::resource_templates_list_result_to_json(*result));
    }

    if (request.method == protocol::ResourcesSubscribeMethod ||
        request.method == protocol::ResourcesUnsubscribeMethod) {
      if (!capabilities().resources.subscribe) {
        return detail::peer_error_response(
            request, errors::make(protocol::ErrorCode::MethodNotFound,
                                  "resource subscriptions are not enabled"));
      }
      if (!request.params.is_object() || !request.params.contains("uri") ||
          !request.params.at("uri").is_string()) {
        return detail::peer_error_response(
            request,
            errors::make(protocol::ErrorCode::InvalidParams,
                         "resource subscription requires a string uri"));
      }
      const auto subscription = server_->set_resource_subscription(
          subscription_context_for(context, native_transport),
          request.params.at("uri").get<std::string>(),
          request.method == protocol::ResourcesSubscribeMethod);
      if (!subscription) {
        return detail::peer_error_response(request, subscription.error());
      }
      return protocol::make_response(request.id, protocol::Json::object());
    }

    if (request.method == protocol::CompletionCompleteMethod &&
        completion_handler_) {
      const auto result = completion_handler_(request.params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(request.id, *result);
    }

    if (request.method == protocol::SamplingCreateMessageMethod &&
        sampling_handler_) {
      const auto result = sampling_handler_(request.params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(request.id, *result);
    }

    if (request.method == protocol::LoggingSetLevelMethod && logging_handler_) {
      if (!request.params.is_object() || !request.params.contains("level") ||
          !request.params.at("level").is_string()) {
        return detail::peer_error_response(
            request, errors::make(protocol::ErrorCode::InvalidParams,
                                  "logging/setLevel requires a string level"));
      }
      logging_handler_(request.params.at("level").get<std::string>(),
                       "logging level changed");
      return protocol::make_response(request.id, protocol::Json::object());
    }

    if (request.method == protocol::TasksListMethod && task_list_handler_) {
      const auto params = protocol::task_list_params_from_json(request.params);
      if (!params) {
        return detail::peer_params_error_response(request, params.error());
      }
      const auto result = task_list_handler_(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(
          request.id, protocol::task_list_result_to_json(*result));
    }

    if (request.method == protocol::TasksGetMethod && task_get_handler_) {
      const auto params = protocol::task_get_params_from_json(request.params);
      if (!params) {
        return detail::peer_params_error_response(request, params.error());
      }
      const auto result = task_get_handler_(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      protocol::TaskGetResult response_result;
      response_result.task = *result;
      return protocol::make_response(
          request.id, protocol::task_get_result_to_json(response_result));
    }

    if (request.method == protocol::TasksCancelMethod && task_cancel_handler_) {
      const auto params =
          protocol::task_cancel_params_from_json(request.params);
      if (!params) {
        return detail::peer_params_error_response(request, params.error());
      }
      const auto result = task_cancel_handler_(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      protocol::TaskCancelResult response_result;
      response_result.task = *result;
      return protocol::make_response(
          request.id, protocol::task_cancel_result_to_json(response_result));
    }

    if (request.method == protocol::TasksResultMethod && task_result_handler_) {
      const auto params =
          protocol::task_result_params_from_json(request.params);
      if (!params) {
        return detail::peer_params_error_response(request, params.error());
      }
      const auto result = task_result_handler_(*params, context);
      if (!result) {
        return detail::peer_error_response(request, result.error());
      }
      return protocol::make_response(request.id, *result);
    }

    return server_->handle_request(request, context);
  } catch (const std::exception& ex) {
    return detail::peer_error_response(request,
                                       errors::handler_failed(ex.what()));
  } catch (...) {
    return detail::peer_error_response(request,
                                       errors::handler_unknown_exception());
  }

  core::Result<core::Unit> handle_notification(
      const protocol::JsonRpcNotification& notification,
      const server::SessionContext& context = {}) {
    if (notification.method == protocol::CancelledNotificationMethod ||
        native_notification_state_) {
      return handle_native_notification(notification, context);
    }
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
      return mcp::core::unexpected(detail::peer_dispatch_error(
          "server peer transport must not be null"));
    }
    native_transports_.push_back(std::move(transport));
    native_context_transports_.push_back(
        server::make_contract_transport_adapter(*native_transports_.back()));
    const auto attached =
        server_->add_session_transport(*native_context_transports_.back());
    if (!attached) {
      native_context_transports_.pop_back();
      native_transports_.pop_back();
      return mcp::core::unexpected(attached.error());
    }
    return core::Unit{};
  }

  core::Result<core::Unit> start(
      CancellationToken cancellation = CancellationToken::none()) {
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
        if (context_transport_ptr != nullptr) {
          context.transport_lifetime = context_transport_ptr->lifetime_token();
        }
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
      return mcp::core::unexpected(*first_error);
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

  /// @brief Blocks until all native transports are ready to process messages.
  void wait_until_ready() {
    for (auto& transport : native_transports_) {
      transport->wait_until_ready();
    }
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
      const server::SessionContext& context = {},
      transport::ServerTransport* native_transport = nullptr) {
    if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      auto handled = handle_request(*request, context, native_transport);
      if (!handled) {
        return protocol::JsonRpcMessage{protocol::make_error_response(
            request->id,
            detail::peer_error_object_from_core_error(handled.error()))};
      }
      return protocol::JsonRpcMessage{std::move(*handled)};
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      const auto handled = handle_notification(*notification, context);
      if (!handled) {
        return mcp::core::unexpected(handled.error());
      }
      return std::nullopt;
    }

    return mcp::core::unexpected(detail::peer_dispatch_error(
        "server peer cannot dispatch an uncorrelated response"));
  }

  /// @brief Runs a receive loop over a role-generic server transport.
  ///
  /// Initialize handshakes and notifications are dispatched on the receive
  /// thread. After initialization, requests are dispatched on workers so a
  /// long-running tool call does not block the transport from reading later
  /// requests.
  core::Result<core::Unit> serve_transport(
      transport::ServerTransport& transport,
      const server::SessionContext& context = {},
      CancellationToken cancellation = CancellationToken::none()) {
    struct WorkerState {
      std::mutex mutex;
      std::mutex send_mutex;
      std::condition_variable cv;
      std::size_t active = 0;
      std::optional<core::Error> first_error;
    };

    auto worker_state = std::make_shared<WorkerState>();
    bool initialized = false;

    auto record_worker_error = [worker_state,
                                &transport](core::Error error) noexcept {
      {
        std::lock_guard lock(worker_state->mutex);
        if (!worker_state->first_error.has_value()) {
          worker_state->first_error = std::move(error);
        }
      }
      (void)transport.close();
      worker_state->cv.notify_all();
    };

    auto finish_worker = [worker_state]() noexcept {
      {
        std::lock_guard lock(worker_state->mutex);
        if (worker_state->active > 0) {
          --worker_state->active;
        }
      }
      worker_state->cv.notify_all();
    };

    auto wait_for_workers = [worker_state]() -> std::optional<core::Error> {
      std::unique_lock lock(worker_state->mutex);
      worker_state->cv.wait(lock, [&] { return worker_state->active == 0; });
      return worker_state->first_error;
    };

    auto send_message =
        [worker_state, &transport](
            protocol::JsonRpcMessage message) -> core::Result<core::Unit> {
      std::lock_guard lock(worker_state->send_mutex);
      return transport.send(std::move(message));
    };

    auto dispatch_and_send = [this, &transport, &send_message](
                                 const protocol::JsonRpcMessage& message,
                                 const server::SessionContext& message_context)
        -> core::Result<core::Unit> {
      auto dispatched = dispatch_message(message, message_context, &transport);
      if (!dispatched) {
        return mcp::core::unexpected(dispatched.error());
      }
      if (dispatched->has_value()) {
        auto sent = send_message(std::move(dispatched->value()));
        if (!sent) {
          return mcp::core::unexpected(sent.error());
        }
      }
      return core::Unit{};
    };

    auto start_request_worker = [worker_state, &record_worker_error,
                                 &finish_worker, &dispatch_and_send](
                                    protocol::JsonRpcMessage message,
                                    server::SessionContext message_context)
        -> core::Result<core::Unit> {
      {
        std::lock_guard lock(worker_state->mutex);
        ++worker_state->active;
      }

      try {
        std::thread([message = std::move(message),
                     message_context = std::move(message_context),
                     &dispatch_and_send, record_worker_error,
                     finish_worker]() mutable {
          try {
            auto dispatched = dispatch_and_send(message, message_context);
            if (!dispatched) {
              record_worker_error(dispatched.error());
            }
          } catch (const std::exception& ex) {
            record_worker_error(errors::make(
                protocol::ErrorCode::InternalError,
                "server peer request worker failed", ex.what(), "transport"));
          } catch (...) {
            record_worker_error(
                errors::make(protocol::ErrorCode::InternalError,
                             "server peer request worker failed",
                             "unknown exception", "transport"));
          }
          finish_worker();
        }).detach();
      } catch (const std::system_error& ex) {
        finish_worker();
        return mcp::core::unexpected(
            errors::make(protocol::ErrorCode::InternalError,
                         "failed to start server peer request worker",
                         ex.what(), "transport"));
      }
      return core::Unit{};
    };

    auto is_stateless_request = [](const protocol::Json& params) -> bool {
      return params.is_object() && params.contains("_meta") &&
             params.at("_meta").is_object() &&
             params.at("_meta").contains(
                 "io.modelcontextprotocol/protocolVersion");
    };

    while (!cancellation.cancelled()) {
      auto received = transport.receive();
      if (!received) {
        if (const auto worker_error = wait_for_workers()) {
          return mcp::core::unexpected(*worker_error);
        }
        return mcp::core::unexpected(received.error());
      }
      if (!received->has_value()) {
        if (const auto worker_error = wait_for_workers()) {
          return mcp::core::unexpected(*worker_error);
        }
        return core::Unit{};
      }

      auto message = std::move(received->value());
      if (const auto* request =
              std::get_if<protocol::JsonRpcRequest>(&message)) {
        const bool stateless = is_stateless_request(request->params);
        const bool allowed_before_initialized =
            stateless || request->method == protocol::InitializeMethod ||
            request->method == protocol::PingMethod;
        if (!initialized && !allowed_before_initialized) {
          auto sent = send_message(
              protocol::JsonRpcMessage{protocol::make_error_response(
                  request->id,
                  protocol::make_error(protocol::ErrorCode::InvalidRequest,
                                       "server peer transport session is "
                                       "not initialized"))});
          if (!sent) {
            if (const auto worker_error = wait_for_workers()) {
              return mcp::core::unexpected(*worker_error);
            }
            return mcp::core::unexpected(sent.error());
          }
          continue;
        }

        auto message_context =
            detail::context_for_received_server_message(transport, context);
        const bool run_inline = request->method == protocol::InitializeMethod ||
                                (!initialized && !stateless);
        if (run_inline) {
          auto dispatched = dispatch_and_send(message, message_context);
          if (!dispatched) {
            if (const auto worker_error = wait_for_workers()) {
              return mcp::core::unexpected(*worker_error);
            }
            return mcp::core::unexpected(dispatched.error());
          }
          continue;
        }

        auto started = start_request_worker(std::move(message),
                                            std::move(message_context));
        if (!started) {
          if (const auto worker_error = wait_for_workers()) {
            return mcp::core::unexpected(*worker_error);
          }
          return mcp::core::unexpected(started.error());
        }
        continue;
      }

      if (const auto* notification =
              std::get_if<protocol::JsonRpcNotification>(&message)) {
        const bool stateless = is_stateless_request(notification->params);
        if (!initialized && !stateless &&
            notification->method != protocol::InitializedMethod) {
          if (const auto worker_error = wait_for_workers()) {
            return mcp::core::unexpected(*worker_error);
          }
          return mcp::core::unexpected(detail::peer_dispatch_error(
              "server peer transport session is not initialized"));
        }
        auto message_context =
            detail::context_for_received_server_message(transport, context);
        auto dispatched = dispatch_and_send(message, message_context);
        if (!dispatched) {
          if (const auto worker_error = wait_for_workers()) {
            return mcp::core::unexpected(*worker_error);
          }
          return mcp::core::unexpected(dispatched.error());
        }
        if (notification->method == protocol::InitializedMethod) {
          initialized = true;
        }
        continue;
      }

      if (const auto worker_error = wait_for_workers()) {
        return mcp::core::unexpected(*worker_error);
      }
      return mcp::core::unexpected(detail::peer_dispatch_error(
          "server peer cannot dispatch an uncorrelated response"));
    }

    if (const auto worker_error = wait_for_workers()) {
      return mcp::core::unexpected(*worker_error);
    }
    return core::Unit{};
  }

 private:
  server::SessionContext subscription_context_for(
      const server::SessionContext& context,
      const transport::ServerTransport* native_transport) const {
    server::SessionContext subscription_context = context;
    if (subscription_context.transport || native_transport == nullptr) {
      return subscription_context;
    }

    for (std::size_t index = 0; index < native_transports_.size(); ++index) {
      if (native_transports_[index].get() == native_transport &&
          index < native_context_transports_.size()) {
        subscription_context.transport =
            native_context_transports_[index].get();
        subscription_context.transport_lifetime =
            native_context_transports_[index]->lifetime_token();
        break;
      }
    }
    return subscription_context;
  }

  CancellationToken begin_peer_request_cancellation(
      const protocol::RequestId& request_id) {
    CancellationSource source;
    auto token = source.token();
    std::lock_guard lock(*peer_request_cancellations_mutex_);
    (*peer_request_cancellations_)[detail::peer_request_cancellation_key(
        request_id)] = std::move(source);
    return token;
  }

  void end_peer_request_cancellation(
      const protocol::RequestId& request_id) noexcept {
    std::lock_guard lock(*peer_request_cancellations_mutex_);
    peer_request_cancellations_->erase(
        detail::peer_request_cancellation_key(request_id));
  }

  void cancel_peer_request(const protocol::RequestId& request_id) noexcept {
    std::lock_guard lock(*peer_request_cancellations_mutex_);
    const auto it = peer_request_cancellations_->find(
        detail::peer_request_cancellation_key(request_id));
    if (it != peer_request_cancellations_->end()) {
      it->second.cancel();
    }
  }

  core::Result<core::Unit> handle_native_notification(
      const protocol::JsonRpcNotification& notification,
      const server::SessionContext& context) try {
    if (notification.method == protocol::CancelledNotificationMethod) {
      const auto cancelled = protocol::cancelled_notification_params_from_json(
          notification.params);
      if (!cancelled) {
        return mcp::core::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "cancelled notification requires valid params"));
      }
      cancel_peer_request(cancelled->request_id);
      return server_->handle_notification(notification, context);
    }

    if (raw_notification_handler_) {
      const auto raw_result = raw_notification_handler_(notification, context);
      if (!raw_result) {
        return mcp::core::unexpected(raw_result.error());
      }
    }

    if (notification.method == protocol::RootsListChangedNotificationMethod &&
        roots_list_changed_handler_) {
      const auto result = roots_list_changed_handler_(context);
      if (!result) {
        return mcp::core::unexpected(result.error());
      }
    } else if (notification.method == protocol::ProgressNotificationMethod &&
               progress_handler_) {
      const auto params =
          protocol::progress_notification_params_from_json(notification.params);
      if (!params) {
        return mcp::core::unexpected(
            errors::make(protocol::ErrorCode::InvalidParams,
                         "progress notification requires valid params"));
      }
      const auto result = progress_handler_(*params, context);
      if (!result) {
        return mcp::core::unexpected(result.error());
      }
    } else if (notification.method ==
                   protocol::ToolsListChangedNotificationMethod &&
               tool_list_changed_handler_) {
      const auto result = tool_list_changed_handler_(context);
      if (!result) {
        return mcp::core::unexpected(result.error());
      }
    } else if (notification.method ==
                   protocol::PromptsListChangedNotificationMethod &&
               prompt_list_changed_handler_) {
      const auto result = prompt_list_changed_handler_(context);
      if (!result) {
        return mcp::core::unexpected(result.error());
      }
    } else if (notification.method ==
                   protocol::ResourcesListChangedNotificationMethod &&
               resource_list_changed_handler_) {
      const auto result = resource_list_changed_handler_(context);
      if (!result) {
        return mcp::core::unexpected(result.error());
      }
    } else if (notification.method ==
                   protocol::ResourcesUpdatedNotificationMethod &&
               resource_updated_handler_) {
      if (!notification.params.is_object() ||
          !notification.params.contains("uri") ||
          !notification.params.at("uri").is_string()) {
        return mcp::core::unexpected(errors::make(
            protocol::ErrorCode::InvalidParams,
            "resource updated notification requires a string uri"));
      }
      const auto result = resource_updated_handler_(
          notification.params.at("uri").get<std::string>(), context);
      if (!result) {
        return mcp::core::unexpected(result.error());
      }
    }

    return core::Unit{};
  } catch (const std::exception& ex) {
    return mcp::core::unexpected(errors::handler_failed(ex.what()));
  } catch (...) {
    return mcp::core::unexpected(errors::handler_unknown_exception());
  }

  std::unique_ptr<server::Server> server_;
  std::vector<std::unique_ptr<transport::ServerTransport>> native_transports_;
  std::vector<std::unique_ptr<server::Transport>> native_context_transports_;
  std::shared_ptr<std::mutex> peer_request_cancellations_mutex_ =
      std::make_shared<std::mutex>();
  std::shared_ptr<std::unordered_map<std::string, CancellationSource>>
      peer_request_cancellations_ = std::make_shared<
          std::unordered_map<std::string, CancellationSource>>();
  bool native_notification_state_ = false;
  server::Server::RawRequestHandler raw_request_handler_;
  server::Server::RawRequestContextHandler raw_request_context_handler_;
  server::Server::JsonContextHandler completion_handler_;
  server::Server::JsonContextHandler sampling_handler_;
  server::Server::LoggingHandler logging_handler_;
  server::Server::TaskListHandler task_list_handler_;
  server::Server::TaskGetHandler task_get_handler_;
  server::Server::TaskCancelHandler task_cancel_handler_;
  server::Server::TaskResultHandler task_result_handler_;
  server::Server::RawNotificationHandler raw_notification_handler_;
  server::Server::ProgressHandler progress_handler_;
  server::Server::RootsListChangedHandler roots_list_changed_handler_;
  server::Server::ListChangedHandler tool_list_changed_handler_;
  server::Server::ListChangedHandler prompt_list_changed_handler_;
  server::Server::ListChangedHandler resource_list_changed_handler_;
  server::Server::ResourceUpdatedHandler resource_updated_handler_;
};

/// @brief Fluent builder for common server peer construction.
class Peer<RoleServer>::Builder {
 public:
  Builder() = default;
  Builder(const Builder&) = delete;
  Builder& operator=(const Builder&) = delete;
  Builder(Builder&&) noexcept = default;
  Builder& operator=(Builder&&) noexcept = default;

  Builder& name(std::string value) {
    builder_.name(std::move(value));
    return *this;
  }

  Builder& version(std::string value) {
    builder_.version(std::move(value));
    return *this;
  }

  Builder& instructions(std::string value) {
    builder_.instructions(std::move(value));
    return *this;
  }

  Builder& capabilities(protocol::ServerCapabilities value) {
    builder_.with_capabilities(std::move(value));
    return *this;
  }

  Builder& transport(std::unique_ptr<server::Transport> value) {
    builder_.with_transport(std::move(value));
    return *this;
  }

  Builder& transport(std::unique_ptr<transport::ServerTransport> value) {
    native_transports_.push_back(std::move(value));
    return *this;
  }

  Builder& stdio(std::istream& input, std::ostream& output) {
    return transport(
        std::make_unique<transport::ServerStdioTransport>(input, output));
  }

  Builder& stdio() { return stdio(std::cin, std::cout); }

#if defined(CXXMCP_ENABLE_HTTP)
  Builder& streamable_http(
      transport::StreamableHttpServerTransportOptions options) {
    return transport(std::make_unique<transport::StreamableHttpServerTransport>(
        std::move(options)));
  }

  Builder& streamable_http(int listen_port) {
    transport::StreamableHttpServerTransportOptions options;
    options.listen_port = listen_port;
    return streamable_http(std::move(options));
  }

  Builder& streamable_http(std::string listen_host, int listen_port,
                           std::string path = "/mcp") {
    transport::StreamableHttpServerTransportOptions options;
    options.listen_host = std::move(listen_host);
    options.listen_port = listen_port;
    options.path = std::move(path);
    return streamable_http(std::move(options));
  }
#endif

#if defined(CXXMCP_ENABLE_WEBSOCKET)
  Builder& websocket(transport::WebSocketServerTransportOptions options) {
    return transport(std::make_unique<transport::WebSocketServerTransport>(
        std::move(options)));
  }

  Builder& websocket(int listen_port) {
    transport::WebSocketServerTransportOptions options;
    options.listen_port = listen_port;
    return websocket(std::move(options));
  }

  Builder& websocket(std::string listen_host, int listen_port,
                     std::string path = "/mcp") {
    transport::WebSocketServerTransportOptions options;
    options.listen_host = std::move(listen_host);
    options.listen_port = listen_port;
    options.path = std::move(path);
    return websocket(std::move(options));
  }
#endif

  Builder& auth_provider(std::unique_ptr<server::AuthProvider> value) {
    builder_.with_auth_provider(std::move(value));
    return *this;
  }

  Builder& rate_limiter(std::unique_ptr<server::RateLimiter> value) {
    builder_.with_rate_limiter(std::move(value));
    return *this;
  }

  Builder& schema_validator(
      std::shared_ptr<const server::JsonSchemaValidator> value) {
    builder_.with_schema_validator(std::move(value));
    return *this;
  }

  Builder& task_manager(server::TaskOperationProcessorOptions options = {}) {
    builder_.with_task_manager(std::move(options));
    return *this;
  }

  Builder& task_manager(
      std::shared_ptr<server::TaskOperationProcessor> processor) {
    builder_.with_task_manager(std::move(processor));
    return *this;
  }

  Builder& add_tool(protocol::ToolDefinition definition,
                    server::ToolHandler handler) {
    builder_.add_tool(std::move(definition), std::move(handler));
    return *this;
  }

  Builder& tool(protocol::ToolDefinition definition,
                server::ToolHandler handler) {
    return add_tool(std::move(definition), std::move(handler));
  }

  Builder& add_prompt(protocol::Prompt prompt, server::PromptHandler handler) {
    builder_.add_prompt(std::move(prompt), std::move(handler));
    return *this;
  }

  Builder& prompt(protocol::Prompt prompt, server::PromptHandler handler) {
    return add_prompt(std::move(prompt), std::move(handler));
  }

  template <class Args, class Handler>
  Builder& prompt(server::TypedPromptRegistration<Args, Handler> registration) {
    return add_prompt(
        std::move(registration.prompt),
        [handler = std::move(registration.handler)](
            const server::PromptContext& context)
            -> core::Result<protocol::PromptsGetResult> {
          try {
            auto args = context.arguments.get<Args>();
            auto handled = server::detail::invoke_typed_context_handler(
                handler, std::move(args), context);
            if constexpr (server::detail::is_result<decltype(handled)>::value) {
              if (!handled) {
                return mcp::core::unexpected(handled.error());
              }
              return server::detail::value_to_prompt_result(*handled);
            } else {
              return server::detail::value_to_prompt_result(std::move(handled));
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

  Builder& add_resource(protocol::Resource resource,
                        server::ResourceReadHandler handler) {
    builder_.add_resource(std::move(resource), std::move(handler));
    return *this;
  }

  Builder& resource(protocol::Resource resource,
                    server::ResourceReadHandler handler) {
    return add_resource(std::move(resource), std::move(handler));
  }

  template <class Args, class Handler>
  Builder& resource(
      server::TypedResourceRegistration<Args, Handler> registration) {
    return add_resource(
        std::move(registration.resource),
        [handler = std::move(registration.handler)](
            const server::ResourceContext& context)
            -> core::Result<protocol::ResourcesReadResult> {
          try {
            auto args = context.params.get<Args>();
            auto handled = server::detail::invoke_typed_context_handler(
                handler, std::move(args), context);
            if constexpr (server::detail::is_result<decltype(handled)>::value) {
              if (!handled) {
                return mcp::core::unexpected(handled.error());
              }
              return server::detail::value_to_resource_read_result(*handled,
                                                                   context.uri);
            } else {
              return server::detail::value_to_resource_read_result(
                  std::move(handled), context.uri);
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

  Builder& add_resource_template(protocol::ResourceTemplate resource_template) {
    builder_.add_resource_template(std::move(resource_template));
    return *this;
  }

  Builder& resource_template(protocol::ResourceTemplate resource_template) {
    return add_resource_template(std::move(resource_template));
  }

  /// @brief Registers a tool with a typed handler.
  /// @tparam Args Type decoded from the JSON arguments object.
  /// @tparam Result Expected handler result type.
  /// @tparam Handler Callable invoked with Args.
  /// @param name Tool name advertised to clients.
  /// @param handler Callable returning Result or core::Result<Result>.
  template <class Args, class Result, class Handler>
  Builder& tool(std::string name, Handler handler) {
    auto definition = protocol::tool_definition(std::move(name))
                          .input_schema(protocol::tool_input_schema_for<Args>())
                          .build();
    server::detail::apply_default_output_schema<Result>(definition);
    return tool<Args, Result>(std::move(definition), std::move(handler));
  }

  /// @brief Registers a tool with an explicit definition and typed handler.
  template <class Args, class Result, class Handler>
  Builder& tool(protocol::ToolDefinition definition, Handler handler) {
    return add_tool(
        std::move(definition),
        [handler = std::move(handler)](const server::ToolContext& context)
            -> core::Result<protocol::ToolResult> {
          try {
            auto args =
                server::detail::argument_from_json<Args>(context.arguments);
            auto handled = server::detail::invoke_tool_handler(
                handler, std::move(args), context);
            if constexpr (server::detail::is_result<decltype(handled)>::value) {
              if (!handled) {
                return mcp::core::unexpected(handled.error());
              }
              return server::detail::value_to_tool_result(*handled);
            } else {
              return server::detail::value_to_tool_result(std::move(handled));
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

  /// @brief Registers a typed prompt with argument decoding.
  template <class Args, class Handler>
  Builder& prompt(std::string name, Handler handler) {
    server::detail::require_unambiguous_typed_context_handler<
        Handler, Args, server::PromptContext>("prompt");
    protocol::Prompt prompt;
    prompt.name = std::move(name);
    return add_prompt(
        std::move(prompt),
        [handler = std::move(handler)](const server::PromptContext& context)
            -> core::Result<protocol::PromptsGetResult> {
          try {
            auto args = context.arguments.get<Args>();
            auto handled = server::detail::invoke_typed_context_handler(
                handler, std::move(args), context);
            if constexpr (server::detail::is_result<decltype(handled)>::value) {
              if (!handled) {
                return mcp::core::unexpected(handled.error());
              }
              return server::detail::value_to_prompt_result(*handled);
            } else {
              return server::detail::value_to_prompt_result(std::move(handled));
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

  /// @brief Registers a prompt with auto-detected handler signature.
  template <class Handler>
  Builder& prompt(std::string name, Handler handler) {
    server::detail::require_unambiguous_prompt_handler<Handler>();
    protocol::Prompt prompt;
    prompt.name = std::move(name);
    return add_prompt(
        std::move(prompt),
        [handler = std::move(handler)](const server::PromptContext& context)
            -> core::Result<protocol::PromptsGetResult> {
          try {
            auto handled =
                server::detail::invoke_prompt_handler(handler, context);
            if constexpr (server::detail::is_result<decltype(handled)>::value) {
              if (!handled) {
                return mcp::core::unexpected(handled.error());
              }
              return server::detail::value_to_prompt_result(*handled);
            } else {
              return server::detail::value_to_prompt_result(std::move(handled));
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

  /// @brief Registers a typed resource with parameter decoding.
  template <class Args, class Handler>
  Builder& resource(std::string name, Handler handler) {
    server::detail::require_unambiguous_typed_context_handler<
        Handler, Args, server::ResourceContext>("resource");
    protocol::Resource resource;
    resource.uri = std::move(name);
    resource.name = resource.uri;
    return add_resource(
        std::move(resource),
        [handler = std::move(handler)](const server::ResourceContext& context)
            -> core::Result<protocol::ResourcesReadResult> {
          try {
            auto args = context.params.get<Args>();
            auto handled = server::detail::invoke_typed_context_handler(
                handler, std::move(args), context);
            if constexpr (server::detail::is_result<decltype(handled)>::value) {
              if (!handled) {
                return mcp::core::unexpected(handled.error());
              }
              return server::detail::value_to_resource_read_result(*handled,
                                                                   context.uri);
            } else {
              return server::detail::value_to_resource_read_result(
                  std::move(handled), context.uri);
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

  /// @brief Registers a resource with auto-detected handler signature.
  ///
  /// If the handler returns ResourceContents with an empty URI, the URI
  /// registered with .resource(name) is filled in automatically.
  template <class Handler>
  Builder& resource(std::string name, Handler handler) {
    server::detail::require_callable(handler, "resource");
    server::detail::require_unambiguous_resource_handler<Handler>();
    auto uri = name;
    protocol::Resource resource;
    resource.uri = uri;
    resource.name = uri;
    if constexpr (std::is_invocable_v<Handler>) {
      using Handled = decltype(handler());
      if constexpr (std::is_same_v<std::decay_t<Handled>, protocol::Resource>) {
        resource = handler();
      }
    }
    return add_resource(
        std::move(resource),
        [handler = std::move(handler),
         uri = std::move(uri)](const server::ResourceContext& context)
            -> core::Result<protocol::ResourcesReadResult> {
          try {
            auto handled =
                server::detail::invoke_resource_handler(handler, context);
            if constexpr (std::is_same_v<std::decay_t<decltype(handled)>,
                                         protocol::Resource>) {
              return protocol::ResourcesReadResult{};
            } else if constexpr (server::detail::is_result<
                                     decltype(handled)>::value) {
              if (!handled) {
                return mcp::core::unexpected(handled.error());
              }
              auto result = server::detail::value_to_resource_read_result(
                  *handled, context.uri);
              for (auto& c : result.contents) {
                if (c.uri.empty()) {
                  c.uri = uri;
                }
              }
              return result;
            } else {
              auto result = server::detail::value_to_resource_read_result(
                  std::move(handled), context.uri);
              for (auto& c : result.contents) {
                if (c.uri.empty()) {
                  c.uri = uri;
                }
              }
              return result;
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

  /// @brief Registers a resource template with a callable adapter.
  template <class Handler>
  Builder& resource_template(std::string name, Handler handler) {
    server::detail::require_callable(handler, "resource_template");
    protocol::ResourceTemplate tmpl;
    if constexpr (std::is_invocable_v<Handler>) {
      auto handled = handler();
      if constexpr (server::detail::is_result<decltype(handled)>::value) {
        if (!handled) {
          throw std::runtime_error(handled.error().message);
        }
        tmpl = *handled;
      } else {
        tmpl = std::move(handled);
      }
    } else if constexpr (std::is_invocable_v<Handler, std::string>) {
      tmpl = handler({});
    } else {
      static_assert(
          std::is_invocable_v<Handler>,
          "resource_template handler must accept no arguments or string");
    }
    if (tmpl.name.empty()) {
      tmpl.name = name;
    }
    if (tmpl.uri_template.empty()) {
      tmpl.uri_template = std::move(name);
    }
    return add_resource_template(std::move(tmpl));
  }

  /// @brief Registers a completion handler.
  template <class Handler>
  Builder& completion(Handler handler) {
    server::detail::require_callable(handler, "completion");
    if constexpr (server::detail::is_typed_completion_handler_v<Handler>) {
      server::detail::require_unambiguous_completion_handler<Handler>();
    } else {
      server::detail::require_unambiguous_json_extension_handler<Handler>();
    }
    builder_.on_completion([handler = std::move(handler)](
                               const protocol::Json& request,
                               const server::SessionContext& context,
                               CancellationToken cancellation) mutable
                               -> core::Result<protocol::Json> {
      if constexpr (server::detail::is_typed_completion_handler_v<Handler>) {
        const auto params = protocol::complete_params_from_json(request);
        if (!params) {
          return mcp::core::unexpected(core::Error{
              static_cast<int>(protocol::ErrorCode::InvalidParams),
              params.error().message, params.error().detail, "protocol"});
        }
        server::CompletionContext completion_context;
        static_cast<server::SessionContext&>(completion_context) = context;
        completion_context.params = *params;
        completion_context.cancellation = cancellation;
        auto handled = server::detail::invoke_completion_handler(
            handler, completion_context);
        return server::detail::completion_response_to_json(std::move(handled));
      } else {
        auto handled = server::detail::invoke_json_extension_handler(
            handler, request, context, cancellation);
        if constexpr (server::detail::is_result<decltype(handled)>::value) {
          return handled;
        } else {
          return server::detail::value_to_json(std::move(handled));
        }
      }
    });
    return *this;
  }

  /// @brief Registers a sampling handler.
  template <class Handler>
  Builder& sampling(Handler handler) {
    server::detail::require_callable(handler, "sampling");
    server::detail::require_unambiguous_json_extension_handler<Handler>();
    builder_.on_sampling(
        [handler = std::move(handler)](const protocol::Json& request,
                                       const server::SessionContext& context,
                                       CancellationToken cancellation) mutable
            -> core::Result<protocol::Json> {
          auto handled = server::detail::invoke_json_extension_handler(
              handler, request, context, cancellation);
          if constexpr (server::detail::is_result<decltype(handled)>::value) {
            return handled;
          } else {
            return server::detail::value_to_json(std::move(handled));
          }
        });
    return *this;
  }

  /// @brief Registers a logging handler.
  template <class Handler>
  Builder& logging(Handler handler) {
    server::detail::require_callable(handler, "logging");
    builder_.on_logging([handler = std::move(handler)](
                            std::string_view level, std::string_view message) {
      handler(level, message);
    });
    return *this;
  }

  /// @brief Registers a raw request handler.
  template <class Handler>
  Builder& raw_request(Handler handler) {
    server::detail::require_callable(handler, "raw_request");
    builder_.on_raw_request([handler = std::move(handler)](
                                const protocol::JsonRpcRequest& request,
                                const server::SessionContext& /*context*/)
                                -> std::optional<protocol::JsonRpcResponse> {
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

  template <class Router>
  Builder& router(const Router& router) {
    router.apply_to(builder_);
    return *this;
  }

  template <class Args, class Result, class Handler>
  Builder& tool(
      server::TypedToolRegistration<Args, Result, Handler> registration) {
    return add_tool(
        std::move(registration.definition),
        [handler = std::move(registration.handler)](
            const server::ToolContext& context)
            -> core::Result<protocol::ToolResult> {
          try {
            auto args =
                server::detail::argument_from_json<Args>(context.arguments);
            auto handled = server::detail::invoke_tool_handler(
                handler, std::move(args), context);
            if constexpr (server::detail::is_result<decltype(handled)>::value) {
              if (!handled) {
                return mcp::core::unexpected(handled.error());
              }
              return server::detail::value_to_tool_result(*handled);
            } else {
              return server::detail::value_to_tool_result(std::move(handled));
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

  Builder& on_completion(server::Server::JsonHandler handler) {
    builder_.on_completion(std::move(handler));
    return *this;
  }

  Builder& on_completion(server::Server::JsonContextHandler handler) {
    builder_.on_completion(std::move(handler));
    return *this;
  }

  Builder& on_completion(server::Server::JsonRequestContextHandler handler) {
    builder_.on_completion(std::move(handler));
    return *this;
  }

  Builder& on_sampling(server::Server::JsonHandler handler) {
    builder_.on_sampling(std::move(handler));
    return *this;
  }

  Builder& on_sampling(server::Server::JsonContextHandler handler) {
    builder_.on_sampling(std::move(handler));
    return *this;
  }

  Builder& on_sampling(server::Server::JsonRequestContextHandler handler) {
    builder_.on_sampling(std::move(handler));
    return *this;
  }

  Builder& on_logging(server::Server::LoggingHandler handler) {
    builder_.on_logging(std::move(handler));
    return *this;
  }

  Builder& on_raw_request(server::Server::RawRequestHandler handler) {
    builder_.on_raw_request(std::move(handler));
    return *this;
  }

  Builder& on_raw_notification(server::Server::RawNotificationHandler handler) {
    builder_.on_raw_notification(std::move(handler));
    return *this;
  }

  Builder& on_task_list(server::Server::TaskListHandler handler) {
    builder_.on_task_list(std::move(handler));
    return *this;
  }

  Builder& on_tools_list(server::Server::ToolsListHandler handler) {
    builder_.on_tools_list(std::move(handler));
    return *this;
  }

  Builder& on_prompts_list(server::Server::PromptsListHandler handler) {
    builder_.on_prompts_list(std::move(handler));
    return *this;
  }

  Builder& on_resources_list(server::Server::ResourcesListHandler handler) {
    builder_.on_resources_list(std::move(handler));
    return *this;
  }

  Builder& on_resource_templates_list(
      server::Server::ResourceTemplatesListHandler handler) {
    builder_.on_resource_templates_list(std::move(handler));
    return *this;
  }

  Builder& on_task_get(server::Server::TaskGetHandler handler) {
    builder_.on_task_get(std::move(handler));
    return *this;
  }

  Builder& on_task_cancel(server::Server::TaskCancelHandler handler) {
    builder_.on_task_cancel(std::move(handler));
    return *this;
  }

  Builder& on_task_result(server::Server::TaskResultHandler handler) {
    builder_.on_task_result(std::move(handler));
    return *this;
  }

  Builder& on_progress(server::Server::ProgressHandler handler) {
    builder_.on_progress(std::move(handler));
    return *this;
  }

  Builder& on_roots_list_changed(
      server::Server::RootsListChangedHandler handler) {
    builder_.on_roots_list_changed(std::move(handler));
    return *this;
  }

  Builder& on_tool_list_changed(server::Server::ListChangedHandler handler) {
    builder_.on_tool_list_changed(std::move(handler));
    return *this;
  }

  Builder& on_prompt_list_changed(server::Server::ListChangedHandler handler) {
    builder_.on_prompt_list_changed(std::move(handler));
    return *this;
  }

  Builder& on_resource_list_changed(
      server::Server::ListChangedHandler handler) {
    builder_.on_resource_list_changed(std::move(handler));
    return *this;
  }

  Builder& on_resource_updated(server::Server::ResourceUpdatedHandler handler) {
    builder_.on_resource_updated(std::move(handler));
    return *this;
  }

  Builder& handler(server::ServerHandler handler) {
    builder_.with_handler(std::move(handler));
    return *this;
  }

  Builder& handler(const server::ServerHandlerInterface& handler) {
    builder_.with_handler(handler);
    return *this;
  }

  core::Result<Peer> build() {
    auto server = builder_.build();
    if (!server) {
      return mcp::core::unexpected(server.error());
    }

    // Capture handlers from the Server before moving it into the Peer.
    // The Peer's own handler fields must be populated so that
    // ServerPeer::handle_request() dispatches through them.
    auto raw_handler = (*server)->raw_request_handler();
    auto raw_context_handler = (*server)->raw_request_context_handler();

    Peer peer(std::move(*server));
    if (raw_handler) {
      peer.raw_request_handler_ = std::move(raw_handler);
    }
    if (raw_context_handler) {
      peer.raw_request_context_handler_ = std::move(raw_context_handler);
    }
    for (auto& transport : native_transports_) {
      auto added = peer.add_transport(std::move(transport));
      if (!added) {
        return mcp::core::unexpected(added.error());
      }
    }
    native_transports_.clear();
    return peer;
  }

  /// @brief Builds the peer, serves it, and blocks until shutdown.
  /// @return 0 on clean shutdown, 1 on build or serve error.
  int run();

 private:
  server::ServerBuilder builder_;
  std::vector<std::unique_ptr<transport::ServerTransport>> native_transports_;
};

inline Peer<RoleServer>::Builder Peer<RoleServer>::builder() {
  return Builder{};
}

using ClientPeer = Peer<RoleClient>;
using ServerPeer = Peer<RoleServer>;

}  // namespace mcp
