// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/server/stdio_transport.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "cxxmcp/protocol/serialization.hpp"

namespace mcp::server {

namespace {

core::Error make_transport_error(int code, std::string message,
                                 std::string detail = {}) {
  return core::Error{code, std::move(message), std::move(detail)};
}

core::Result<core::Unit> write_response(
    std::ostream& output, const protocol::JsonRpcResponse& response) {
  const auto serialized = protocol::serialize_response(response);
  if (!serialized) {
    return std::unexpected(serialized.error());
  }

  output << *serialized << '\n';
  output.flush();
  if (!output.good()) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to write stdio response"));
  }

  return core::Unit{};
}

core::Result<core::Unit> write_notification(
    std::ostream& output, const protocol::JsonRpcNotification& notification) {
  const auto serialized = protocol::serialize_notification(notification);
  if (!serialized) {
    return std::unexpected(serialized.error());
  }

  output << *serialized << '\n';
  output.flush();
  if (!output.good()) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to write stdio notification"));
  }

  return core::Unit{};
}

core::Result<core::Unit> write_error(
    std::ostream& output, int code, std::string message,
    std::optional<protocol::RequestId> id = std::nullopt) {
  return write_response(
      output,
      protocol::make_error_response(
          std::move(id), protocol::make_error(code, std::move(message))));
}

std::optional<protocol::RequestId> request_id_from_message(
    const protocol::JsonRpcMessage& message) {
  if (const auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
    return request->id;
  }

  if (const auto* response = std::get_if<protocol::JsonRpcResponse>(&message)) {
    return response->id;
  }

  return std::nullopt;
}

}  // namespace

StdioTransport::StdioTransport() : StdioTransport(std::cin, std::cout) {}

StdioTransport::StdioTransport(std::istream& input, std::ostream& output)
    : input_(&input), output_(&output) {}

core::Result<core::Unit> StdioTransport::start(
    RequestHandler handler, NotificationHandler notification_handler) {
  if (!input_ || !output_) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "stdio transport streams are not configured"));
  }

  if (!handler) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "stdio transport handler is not configured"));
  }

  running_ = true;

  std::string line;
  while (running_ && std::getline(*input_, line)) {
    if (line.empty()) {
      continue;
    }

    const auto message = protocol::parse_message(line);
    if (!message) {
      const auto written =
          write_error(*output_, message.error().code, message.error().message);
      if (!written) {
        running_ = false;
        return std::unexpected(written.error());
      }
      continue;
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&*message)) {
      if (notification_handler) {
        SessionContext context;
        context.session_id = "stdio";
        context.remote_address = "stdio";
        context.transport = this;
        const auto handled = notification_handler(*notification, context);
        if (!handled) {
          running_ = false;
          return std::unexpected(handled.error());
        }
      }
      continue;
    }

    const auto* request = std::get_if<protocol::JsonRpcRequest>(&*message);
    if (!request) {
      const auto written = write_error(
          *output_, static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "stdio transport expected a JSON-RPC request",
          request_id_from_message(*message));
      if (!written) {
        running_ = false;
        return std::unexpected(written.error());
      }
      continue;
    }

    SessionContext context;
    context.session_id = "stdio";
    context.remote_address = "stdio";
    context.transport = this;
    auto response = handler(*request, context);
    if (!response) {
      response = protocol::make_error_response(
          std::optional<protocol::RequestId>{request->id},
          protocol::make_error(
              response.error().code, response.error().message,
              response.error().detail.empty()
                  ? std::nullopt
                  : std::optional<protocol::Json>{response.error().detail}));
    }

    if (request->method == protocol::InitializeMethod) {
      if (request->params.is_object() &&
          request->params.contains("capabilities")) {
        client_capabilities_ = protocol::client_capabilities_from_json(
            request->params.at("capabilities"));
      } else {
        client_capabilities_.reset();
      }
    }

    const auto written = write_response(*output_, *response);
    if (!written) {
      running_ = false;
      return std::unexpected(written.error());
    }
  }

  running_ = false;
  if (input_->bad()) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to read stdio request"));
  }

  return core::Unit{};
}

core::Result<core::Unit> StdioTransport::send_notification(
    const protocol::JsonRpcNotification& notification) {
  if (!output_) {
    return std::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "stdio transport output stream is not configured"));
  }

  std::lock_guard lock(output_mutex_);
  return write_notification(*output_, notification);
}

std::optional<protocol::ClientCapabilities>
StdioTransport::client_capabilities() const {
  return client_capabilities_;
}

void StdioTransport::stop() noexcept { running_ = false; }

std::string_view StdioTransport::name() const noexcept { return "stdio"; }

}  // namespace mcp::server
