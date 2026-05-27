// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/elicitation.hpp"
#include "cxxmcp/protocol/roots.hpp"
#include "cxxmcp/protocol/sampling.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/task.hpp"
#include "cxxmcp/request.hpp"
#include "cxxmcp/server/transport.hpp"

/// @file
/// @brief Server-side handle for sending requests and notifications to a
/// client peer.

namespace mcp::server {

/// @brief Non-owning handle for the client associated with a server session.
///
/// ClientPeer wraps a borrowed Transport pointer from SessionContext. It is
/// cheap to copy and does not extend the lifetime of either the transport or
/// the underlying MCP session. Use available() before optional peer operations
/// when the context may not have come from a live transport.
///
/// Methods return core::Result and never throw intentionally. Transport
/// failures, protocol error responses, unsupported client capabilities, and
/// response deserialization failures are propagated as core::Error values.
class ClientPeer {
 public:
  /// @brief Construct a peer from a borrowed transport pointer.
  /// @param transport Transport used for outbound messages; may be nullptr.
  /// @param session_id Session identifier used for capability lookup.
  explicit ClientPeer(Transport* transport = nullptr,
                      std::string session_id = {}) noexcept
      : transport_(transport), session_id_(std::move(session_id)) {}

  /// @brief Report whether this peer has a transport to send through.
  bool available() const noexcept { return transport_ != nullptr; }

  /// @brief Report whether the client advertised roots/list support.
  bool supports_roots() const noexcept {
    const auto capabilities =
        transport_ ? transport_->client_capabilities_for_session(session_id_)
                   : std::optional<protocol::ClientCapabilities>{};
    return capabilities.has_value() && capabilities->roots.enabled;
  }

  /// @brief Report whether the client advertised sampling support.
  bool supports_sampling_tools() const noexcept {
    const auto capabilities =
        transport_ ? transport_->client_capabilities_for_session(session_id_)
                   : std::optional<protocol::ClientCapabilities>{};
    return capabilities.has_value() && capabilities->sampling.enabled;
  }

  /// @brief Report whether the client advertised form elicitation support.
  bool supports_elicitation_form() const noexcept {
    const auto capabilities =
        transport_ ? transport_->client_capabilities_for_session(session_id_)
                   : std::optional<protocol::ClientCapabilities>{};
    return capabilities.has_value() && capabilities->elicitation.form;
  }

  /// @brief Report whether the client advertised URL elicitation support.
  bool supports_elicitation_url() const noexcept {
    const auto capabilities =
        transport_ ? transport_->client_capabilities_for_session(session_id_)
                   : std::optional<protocol::ClientCapabilities>{};
    return capabilities.has_value() && capabilities->elicitation.url;
  }

  /// @brief Report whether the client supports any elicitation mode.
  bool supports_elicitation() const noexcept {
    return supports_elicitation_form() || supports_elicitation_url();
  }

  /// @brief Report whether the client advertised task listing support.
  bool supports_task_list() const noexcept {
    const auto capabilities =
        transport_ ? transport_->client_capabilities_for_session(session_id_)
                   : std::optional<protocol::ClientCapabilities>{};
    return capabilities.has_value() && capabilities->tasks.has_value() &&
           capabilities->tasks->list;
  }

  /// @brief Report whether the client advertised task cancellation support.
  bool supports_task_cancel() const noexcept {
    const auto capabilities =
        transport_ ? transport_->client_capabilities_for_session(session_id_)
                   : std::optional<protocol::ClientCapabilities>{};
    return capabilities.has_value() && capabilities->tasks.has_value() &&
           capabilities->tasks->cancel;
  }

  /// @brief Report whether the client advertised any task support.
  bool supports_tasks() const noexcept {
    const auto capabilities =
        transport_ ? transport_->client_capabilities_for_session(session_id_)
                   : std::optional<protocol::ClientCapabilities>{};
    return capabilities.has_value() && capabilities->tasks.has_value();
  }

  /// @brief Notify the client that a request was cancelled.
  core::Result<core::Unit> notify_cancelled(protocol::RequestId request_id,
                                            std::string reason = {}) const {
    if (!transport_) {
      return std::unexpected(
          core::Error{static_cast<int>(protocol::ErrorCode::InternalError),
                      "client peer is not available",
                      {}});
    }
    protocol::CancelledNotificationParams params;
    params.request_id = std::move(request_id);
    if (!reason.empty()) {
      params.reason = std::move(reason);
    }
    protocol::JsonRpcNotification notification;
    notification.method = std::string(protocol::CancelledNotificationMethod);
    notification.params =
        protocol::cancelled_notification_params_to_json(params);
    return transport_->send_notification_to_session(session_id_,
                                                    std::move(notification));
  }

  /// @brief Send a raw JSON-RPC request to the client and return a handle.
  /// @param method JSON-RPC method name.
  /// @param params JSON-RPC params payload. Defaults to an empty object.
  /// @param options Optional timeout and metadata.
  /// @return A handle that can await the response or send cancellation.
  RequestHandle<protocol::Json> request_async(
      std::string method, protocol::Json params = protocol::Json::object(),
      RequestOptions options = {}) const {
    const auto request_id = next_request_id();
    ClientPeer peer = *this;
    return RequestHandle<protocol::Json>::spawn(
        request_id, options.timeout, options.cancellation_token,
        [peer, request_id](std::string reason) mutable {
          return peer.notify_cancelled(std::move(request_id),
                                       std::move(reason));
        },
        [peer, method = std::move(method), params = std::move(params),
         request_id, options = std::move(options)]() mutable {
          return peer.request_with_id(std::move(method), std::move(params),
                                      std::move(request_id),
                                      std::move(options));
        });
  }

  /// @brief Send a raw JSON-RPC request to the client.
  /// @param method JSON-RPC method name.
  /// @param params JSON-RPC params payload. Defaults to an empty object.
  /// @return The response result payload, or a core::Error when the peer is
  /// unavailable, the transport fails, the response is an error, or the
  /// response has no result.
  /// @note On HTTP transports this may block until the client posts the
  /// correlated response. The generated request id is process-global.
  core::Result<protocol::Json> request(
      std::string method,
      protocol::Json params = protocol::Json::object()) const {
    return request_with_id(std::move(method), std::move(params),
                           next_request_id());
  }

  /// @brief Request the client's root list.
  /// @return Parsed roots/list result, or a core::Error if the client did not
  /// advertise roots support, transport delivery fails, or parsing fails.
  core::Result<protocol::RootsListResult> list_roots() const {
    if (!supports_roots()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "client does not support roots",
          {},
      });
    }
    auto payload = request(std::string(protocol::RootsListMethod),
                           protocol::Json::object());
    if (!payload) {
      return std::unexpected(payload.error());
    }
    auto result = protocol::roots_list_result_from_json(*payload);
    if (!result) {
      return std::unexpected(result.error());
    }
    return *result;
  }

  /// @brief Request the client's root list asynchronously.
  RequestHandle<protocol::RootsListResult> list_roots_async(
      RequestOptions options = {}) const {
    const auto request_id = next_request_id();
    if (!supports_roots()) {
      return RequestHandle<protocol::RootsListResult>::ready(
          request_id, std::unexpected(core::Error{
                          static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "client does not support roots",
                          {},
                      }));
    }
    ClientPeer peer = *this;
    return RequestHandle<protocol::RootsListResult>::spawn(
        request_id, options.timeout, options.cancellation_token,
        [peer, request_id](std::string reason) mutable {
          return peer.notify_cancelled(std::move(request_id),
                                       std::move(reason));
        },
        [peer, request_id,
         options]() mutable -> core::Result<protocol::RootsListResult> {
          auto payload = peer.request_with_id(
              std::string(protocol::RootsListMethod), protocol::Json::object(),
              request_id, options);
          if (!payload) {
            return std::unexpected(payload.error());
          }
          auto result = protocol::roots_list_result_from_json(*payload);
          if (!result) {
            return std::unexpected(result.error());
          }
          return *result;
        });
  }

  /// @brief Ask the client to create a sampled message.
  /// @param params Sampling request parameters.
  /// @return Parsed sampling result, or a core::Error for unsupported
  /// capability, transport failure, protocol error, or parse failure.
  core::Result<protocol::CreateMessageResult> create_message(
      const protocol::CreateMessageParams& params) const {
    if (!supports_sampling_tools()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "client does not support sampling",
          {},
      });
    }
    auto payload = request(std::string(protocol::SamplingCreateMessageMethod),
                           protocol::create_message_params_to_json(params));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    auto result = protocol::create_message_result_from_json(*payload);
    if (!result) {
      return std::unexpected(result.error());
    }
    return *result;
  }

  /// @brief Ask the client to create a sampled message asynchronously.
  RequestHandle<protocol::CreateMessageResult> create_message_async(
      const protocol::CreateMessageParams& params,
      RequestOptions options = {}) const {
    const auto request_id = next_request_id();
    if (!supports_sampling_tools()) {
      return RequestHandle<protocol::CreateMessageResult>::ready(
          request_id, std::unexpected(core::Error{
                          static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "client does not support sampling",
                          {},
                      }));
    }
    ClientPeer peer = *this;
    return RequestHandle<protocol::CreateMessageResult>::spawn(
        request_id, options.timeout, options.cancellation_token,
        [peer, request_id](std::string reason) mutable {
          return peer.notify_cancelled(std::move(request_id),
                                       std::move(reason));
        },
        [peer, params, request_id,
         options]() mutable -> core::Result<protocol::CreateMessageResult> {
          auto payload = peer.request_with_id(
              std::string(protocol::SamplingCreateMessageMethod),
              protocol::create_message_params_to_json(params), request_id,
              options);
          if (!payload) {
            return std::unexpected(payload.error());
          }
          auto result = protocol::create_message_result_from_json(*payload);
          if (!result) {
            return std::unexpected(result.error());
          }
          return *result;
        });
  }

  /// @brief Ask the client to perform an elicitation flow.
  /// @param params Elicitation request parameters including mode.
  /// @return Parsed elicitation result, or a core::Error when the requested
  /// mode is unsupported, delivery fails, the client returns an error, or the
  /// response cannot be parsed.
  core::Result<protocol::CreateElicitationResult> create_elicitation(
      const protocol::CreateElicitationRequestParam& params) const {
    if (params.mode == protocol::ElicitationMode::Url &&
        !supports_elicitation_url()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::UrlElicitationRequired),
          "client does not support url elicitation",
          {},
      });
    }
    if (params.mode == protocol::ElicitationMode::Form &&
        !supports_elicitation_form()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "client does not support elicitation",
          {},
      });
    }
    auto payload =
        request(std::string(protocol::ElicitationCreateMethod),
                protocol::create_elicitation_request_param_to_json(params));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    auto result = protocol::create_elicitation_result_from_json(*payload);
    if (!result) {
      return std::unexpected(result.error());
    }
    return *result;
  }

  /// @brief Ask the client to perform an elicitation flow asynchronously.
  RequestHandle<protocol::CreateElicitationResult> create_elicitation_async(
      const protocol::CreateElicitationRequestParam& params,
      RequestOptions options = {}) const {
    const auto request_id = next_request_id();
    if (params.mode == protocol::ElicitationMode::Url &&
        !supports_elicitation_url()) {
      return RequestHandle<protocol::CreateElicitationResult>::ready(
          request_id,
          std::unexpected(core::Error{
              static_cast<int>(protocol::ErrorCode::UrlElicitationRequired),
              "client does not support url elicitation",
              {},
          }));
    }
    if (params.mode == protocol::ElicitationMode::Form &&
        !supports_elicitation_form()) {
      return RequestHandle<protocol::CreateElicitationResult>::ready(
          request_id, std::unexpected(core::Error{
                          static_cast<int>(protocol::ErrorCode::MethodNotFound),
                          "client does not support elicitation",
                          {},
                      }));
    }
    ClientPeer peer = *this;
    return RequestHandle<protocol::CreateElicitationResult>::spawn(
        request_id, options.timeout, options.cancellation_token,
        [peer, request_id](std::string reason) mutable {
          return peer.notify_cancelled(std::move(request_id),
                                       std::move(reason));
        },
        [peer, params, request_id,
         options]() mutable -> core::Result<protocol::CreateElicitationResult> {
          auto payload = peer.request_with_id(
              std::string(protocol::ElicitationCreateMethod),
              protocol::create_elicitation_request_param_to_json(params),
              request_id, options);
          if (!payload) {
            return std::unexpected(payload.error());
          }
          auto result = protocol::create_elicitation_result_from_json(*payload);
          if (!result) {
            return std::unexpected(result.error());
          }
          return *result;
        });
  }

  /// @brief Request one page of tasks from the client.
  /// @return Tasks from the first returned page, or a core::Error from
  /// transport, protocol, or parsing.
  core::Result<std::vector<protocol::Task>> list_tasks() const {
    if (!supports_task_list()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "client does not support task listing",
          {},
      });
    }
    auto payload = request(std::string(protocol::TasksListMethod),
                           protocol::Json::object());
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto tasks = protocol::task_list_result_from_json(*payload);
    if (!tasks) {
      return std::unexpected(tasks.error());
    }
    return tasks->tasks;
  }

  /// @brief Request all client task pages by following cursors.
  /// @return Aggregated tasks in server-observed page order, or the first
  /// core::Error encountered.
  core::Result<std::vector<protocol::Task>> list_all_tasks() const {
    if (!supports_task_list()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "client does not support task listing",
          {},
      });
    }
    std::vector<protocol::Task> all;
    std::optional<std::string> cursor;
    do {
      auto payload =
          request(std::string(protocol::TasksListMethod),
                  cursor.has_value() ? protocol::Json{{"cursor", *cursor}}
                                     : protocol::Json::object());
      if (!payload) {
        return std::unexpected(payload.error());
      }
      const auto page = protocol::task_list_result_from_json(*payload);
      if (!page) {
        return std::unexpected(page.error());
      }
      all.insert(all.end(), page->tasks.begin(), page->tasks.end());
      cursor = page->next_cursor;
    } while (cursor.has_value() && !cursor->empty());
    return all;
  }

  /// @brief Request a single task by id.
  /// @param task_id Client task identifier.
  /// @return Parsed task, or a core::Error from transport, protocol, or
  /// parsing.
  core::Result<protocol::Task> get_task(std::string_view task_id) const {
    if (!supports_tasks()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "client does not support tasks",
          {},
      });
    }
    protocol::TaskGetParams params;
    params.task_id = std::string(task_id);
    auto payload = request(std::string(protocol::TasksGetMethod),
                           protocol::task_get_params_to_json(params));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto task = protocol::task_from_json(*payload);
    if (!task) {
      return std::unexpected(task.error());
    }
    return *task;
  }

  /// @brief Request cancellation of a client task.
  /// @param task_id Client task identifier.
  /// @return Parsed task state returned by the client, or a core::Error.
  core::Result<protocol::Task> cancel_task(std::string_view task_id) const {
    if (!supports_task_cancel()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "client does not support task cancellation",
          {},
      });
    }
    protocol::TaskCancelParams params;
    params.task_id = std::string(task_id);
    auto payload = request(std::string(protocol::TasksCancelMethod),
                           protocol::task_cancel_params_to_json(params));
    if (!payload) {
      return std::unexpected(payload.error());
    }
    const auto task = protocol::task_from_json(*payload);
    if (!task) {
      return std::unexpected(task.error());
    }
    return *task;
  }

  /// @brief Request the result payload for a completed client task.
  /// @param task_id Client task identifier.
  /// @return Raw JSON result payload, or a core::Error.
  core::Result<protocol::Json> task_result(std::string_view task_id) const {
    if (!supports_tasks()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::MethodNotFound),
          "client does not support tasks",
          {},
      });
    }
    protocol::TaskResultParams params;
    params.task_id = std::string(task_id);
    return request(std::string(protocol::TasksResultMethod),
                   protocol::task_result_params_to_json(params));
  }

  /// @brief Notify the client that an elicitation flow has completed.
  /// @param elicitation_id Identifier of the elicitation flow.
  /// @return core::Unit on delivery, or a core::Error when the peer is
  /// unavailable or the transport cannot send the notification.
  core::Result<core::Unit> notify_elicitation_complete(
      std::string elicitation_id) const {
    if (!transport_) {
      return std::unexpected(
          core::Error{static_cast<int>(protocol::ErrorCode::InternalError),
                      "client peer is not available",
                      {}});
    }
    protocol::ElicitationCompleteNotificationParams params;
    params.elicitation_id = std::move(elicitation_id);
    protocol::JsonRpcNotification notification;
    notification.method =
        std::string(protocol::ElicitationCompleteNotificationMethod);
    notification.params =
        protocol::elicitation_complete_notification_params_to_json(params);
    return transport_->send_notification_to_session(session_id_,
                                                    std::move(notification));
  }

 private:
  core::Result<protocol::Json> request_with_id(
      std::string method, protocol::Json params, protocol::RequestId request_id,
      RequestOptions options = {}) const {
    if (!transport_) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::InternalError),
          "client peer is not available",
          {},
      });
    }

    protocol::JsonRpcRequest request;
    request.method = std::move(method);
    request.params = std::move(params);
    request.id = std::move(request_id);
    if (options.meta.has_value()) {
      request.meta = std::move(options.meta);
    }

    auto response =
        transport_->send_request_to_session(session_id_, std::move(request));
    if (!response) {
      return std::unexpected(response.error());
    }
    if (response->error.has_value()) {
      return std::unexpected(core::Error{
          response->error->code,
          response->error->message,
          response->error->data.has_value() ? response->error->data->dump()
                                            : std::string{},
      });
    }
    if (!response->result.has_value()) {
      return std::unexpected(core::Error{
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "client peer response did not contain a result",
          {},
      });
    }
    return *response->result;
  }

  static protocol::RequestId next_request_id() {
    static std::atomic<std::int64_t> next{1};
    return next.fetch_add(1);
  }

  Transport* transport_;
  std::string session_id_;
};

/// @brief Create a ClientPeer handle from a session context.
/// @param context Session context whose transport pointer will be borrowed.
/// @return A non-owning ClientPeer.
inline ClientPeer client_peer(const SessionContext& context) noexcept {
  return ClientPeer(context.transport, context.session_id);
}

inline ClientPeer SessionContext::client() const noexcept {
  return ClientPeer(transport, session_id);
}

}  // namespace mcp::server
