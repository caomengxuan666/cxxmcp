// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/client/stdio_transport.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/serialization.hpp"

namespace mcp::client {

namespace {

core::Error make_transport_error(int code, std::string message,
                                 std::string detail = {}) {
  return core::Error{code, std::move(message), std::move(detail), "transport"};
}

std::string request_id_to_string(const protocol::RequestId& request_id) {
  return std::visit(
      [](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return value;
        } else {
          return std::to_string(value);
        }
      },
      request_id);
}

core::Result<core::Unit> write_response(
    std::ostream& output, const protocol::JsonRpcResponse& response) {
  const auto serialized = protocol::serialize_response(response);
  if (!serialized) {
    return mcp::core::unexpected(serialized.error());
  }

  output << *serialized << '\n';
  output.flush();
  if (!output.good()) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to write stdio response"));
  }

  return core::Unit{};
}

core::Result<protocol::JsonRpcResponse> require_matching_response(
    const protocol::JsonRpcRequest& request,
    const protocol::JsonRpcResponse& response) {
  if (response.id.has_value() && *response.id == request.id) {
    return response;
  }

  return mcp::core::unexpected(make_transport_error(
      static_cast<int>(protocol::ErrorCode::InvalidRequest),
      "stdio transport received an unexpected response",
      response.id.has_value() ? request_id_to_string(*response.id)
                              : std::string{}));
}

}  // namespace

StdioTransport::StdioTransport() : StdioTransport(std::cin, std::cout) {}

StdioTransport::StdioTransport(std::istream& input, std::ostream& output)
    : input_(&input), output_(&output) {}

core::Result<core::Unit> StdioTransport::start(
    TransportRequestHandler request_handler,
    TransportNotificationHandler notification_handler) {
  request_handler_ = std::move(request_handler);
  notification_handler_ = std::move(notification_handler);
  started_ = true;
  return core::Unit{};
}

core::Result<protocol::JsonRpcResponse> StdioTransport::send(
    const protocol::JsonRpcRequest& request) {
  if (!input_ || !output_) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "stdio transport streams are not configured"));
  }

  const auto serialized = protocol::serialize_request(request);
  if (!serialized) {
    return mcp::core::unexpected(serialized.error());
  }

  *output_ << *serialized << '\n';
  output_->flush();
  if (!output_->good()) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to write stdio request"));
  }

  if (!started_) {
    std::string line;
    if (!std::getline(*input_, line)) {
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "failed to read stdio response"));
    }

    const auto response = protocol::parse_response(line);
    if (!response) {
      return mcp::core::unexpected(response.error());
    }
    return require_matching_response(request, *response);
  }

  std::string line;
  while (std::getline(*input_, line)) {
    if (line.empty()) {
      continue;
    }

    const auto message = protocol::parse_message(line);
    if (!message) {
      return mcp::core::unexpected(message.error());
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&*message)) {
      if (notification_handler_) {
        core::Result<core::Unit> handled;
        try {
          handled = notification_handler_(*notification);
        } catch (const std::exception& ex) {
          handled = mcp::core::unexpected(errors::handler_failed(ex.what()));
        } catch (...) {
          handled = mcp::core::unexpected(errors::handler_unknown_exception());
        }
        if (!handled) {
          return mcp::core::unexpected(handled.error());
        }
      }
      continue;
    }

    if (const auto* incoming_request =
            std::get_if<protocol::JsonRpcRequest>(&*message)) {
      if (!request_handler_) {
        return mcp::core::unexpected(make_transport_error(
            static_cast<int>(protocol::ErrorCode::MethodNotFound),
            "stdio transport request handler is not configured",
            incoming_request->method));
      }

      core::Result<protocol::JsonRpcResponse> handled;
      try {
        handled = request_handler_(*incoming_request);
      } catch (const std::exception& ex) {
        handled = mcp::core::unexpected(errors::handler_failed(ex.what()));
      } catch (...) {
        handled = mcp::core::unexpected(errors::handler_unknown_exception());
      }
      if (!handled) {
        handled = protocol::make_error_response(
            std::optional<protocol::RequestId>{incoming_request->id},
            protocol::make_error(
                handled.error().code, handled.error().message,
                handled.error().detail.empty()
                    ? std::nullopt
                    : std::optional<protocol::Json>{handled.error().detail}));
      }

      const auto written = write_response(*output_, *handled);
      if (!written) {
        return mcp::core::unexpected(written.error());
      }
      continue;
    }

    const auto* response = std::get_if<protocol::JsonRpcResponse>(&*message);
    if (!response) {
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InvalidRequest),
          "stdio transport received an unknown message"));
    }

    return require_matching_response(request, *response);
  }

  return mcp::core::unexpected(
      make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                           "failed to read stdio response"));
}

core::Result<core::Unit> StdioTransport::send_notification(
    const protocol::JsonRpcNotification& notification) {
  if (!output_) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "stdio transport output stream is not configured"));
  }

  const auto serialized = protocol::serialize_notification(notification);
  if (!serialized) {
    return mcp::core::unexpected(serialized.error());
  }

  *output_ << *serialized << '\n';
  output_->flush();
  if (!output_->good()) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to write stdio notification"));
  }

  return core::Unit{};
}

}  // namespace mcp::client
