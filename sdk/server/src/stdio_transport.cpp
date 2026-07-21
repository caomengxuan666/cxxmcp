// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/server/stdio_transport.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#endif

#include "cxxmcp/error.hpp"
#include "cxxmcp/protocol/serialization.hpp"

namespace mcp::server {

namespace {

#ifndef _WIN32
class ScopedSigpipeBlock final {
 public:
  ScopedSigpipeBlock() noexcept {
    sigemptyset(&sigpipe_set_);
    sigaddset(&sigpipe_set_, SIGPIPE);
    if (::pthread_sigmask(SIG_BLOCK, &sigpipe_set_, &previous_mask_) == 0) {
      blocked_ = true;
      sigset_t pending;
      if (::sigpending(&pending) == 0) {
        had_pending_sigpipe_ = sigismember(&pending, SIGPIPE) == 1;
      }
    }
  }

  ~ScopedSigpipeBlock() {
    if (!blocked_) {
      return;
    }

    if (!had_pending_sigpipe_) {
      sigset_t pending;
      if (::sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1) {
        int signal = 0;
        (void)::sigwait(&sigpipe_set_, &signal);
      }
    }

    (void)::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
  }

  ScopedSigpipeBlock(const ScopedSigpipeBlock&) = delete;
  ScopedSigpipeBlock& operator=(const ScopedSigpipeBlock&) = delete;

 private:
  sigset_t sigpipe_set_{};
  sigset_t previous_mask_{};
  bool blocked_ = false;
  bool had_pending_sigpipe_ = false;
};
#endif

core::Error make_transport_error(int code, std::string message,
                                 std::string detail = {}) {
  return core::Error{code, std::move(message), std::move(detail), "transport"};
}

core::Result<core::Unit> write_response(
    std::ostream& output, const protocol::JsonRpcResponse& response) {
  const auto serialized = protocol::serialize_response(response);
  if (!serialized) {
    return mcp::core::unexpected(serialized.error());
  }

#ifndef _WIN32
  const ScopedSigpipeBlock sigpipe_block;
#endif
  output << *serialized << '\n';
  output.flush();
  if (!output.good()) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to write stdio response"));
  }

  return core::Unit{};
}

core::Result<core::Unit> write_notification(
    std::ostream& output, const protocol::JsonRpcNotification& notification) {
  const auto serialized = protocol::serialize_notification(notification);
  if (!serialized) {
    return mcp::core::unexpected(serialized.error());
  }

#ifndef _WIN32
  const ScopedSigpipeBlock sigpipe_block;
#endif
  output << *serialized << '\n';
  output.flush();
  if (!output.good()) {
    return mcp::core::unexpected(make_transport_error(
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

bool is_initialized_notification(
    const protocol::JsonRpcNotification& notification) {
  return notification.method == protocol::InitializedMethod;
}

bool is_allowed_before_initialized(const protocol::JsonRpcRequest& request) {
  return request.method == protocol::InitializeMethod ||
         request.method == protocol::PingMethod;
}

std::string request_id_to_string(const protocol::RequestId& request_id) {
  return std::visit(
      [](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return std::string("s:") + value;
        } else {
          return std::string("i:") + std::to_string(value);
        }
      },
      request_id);
}

}  // namespace

StdioTransport::StdioTransport() : StdioTransport(std::cin, std::cout) {}

StdioTransport::StdioTransport(std::istream& input, std::ostream& output)
    : input_(&input), output_(&output) {}

core::Result<core::Unit> StdioTransport::start(
    RequestHandler handler, NotificationHandler notification_handler) {
  if (!input_ || !output_) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "stdio transport streams are not configured"));
  }

  if (!handler) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InvalidRequest),
        "stdio transport handler is not configured"));
  }

  initialized_ = false;
  running_.store(true, std::memory_order_release);

  std::mutex worker_state_mutex;
  std::unordered_set<std::string> in_flight_request_ids;
  std::optional<core::Error> worker_error;
  std::vector<std::thread> workers;

  auto record_worker_error = [&](core::Error error) {
    std::lock_guard lock(worker_state_mutex);
    if (!worker_error.has_value()) {
      worker_error = std::move(error);
    }
    running_.store(false, std::memory_order_release);
  };

  auto wait_for_workers = [&]() {
    for (auto& worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  };

  auto handle_request = [&](protocol::JsonRpcRequest request,
                            SessionContext context) {
    core::Result<protocol::JsonRpcResponse> response;
    try {
      response = handler(request, context);
    } catch (const std::exception& ex) {
      response = mcp::core::unexpected(errors::handler_failed(ex.what()));
    } catch (...) {
      response = mcp::core::unexpected(errors::handler_unknown_exception());
    }
    if (!response) {
      response = protocol::make_error_response(
          std::optional<protocol::RequestId>{request.id},
          protocol::make_error(
              response.error().code, response.error().message,
              response.error().detail.empty()
                  ? std::nullopt
                  : std::optional<protocol::Json>{response.error().detail}));
    }

    if (request.method == protocol::InitializeMethod) {
      std::lock_guard lock(client_capabilities_mutex_);
      if (request.params.is_object() &&
          request.params.contains("capabilities")) {
        client_capabilities_ = protocol::client_capabilities_from_json(
            request.params.at("capabilities"));
      } else {
        client_capabilities_.reset();
      }
    }

    core::Result<core::Unit> written;
    {
      std::lock_guard lock(output_mutex_);
      written = write_response(*output_, *response);
    }
    if (!written) {
      record_worker_error(written.error());
    }
  };

  std::string line;
  while (running_.load(std::memory_order_acquire) &&
         std::getline(*input_, line)) {
    if (line.empty()) {
      continue;
    }

    const auto message = protocol::parse_message(line);
    if (!message) {
      core::Result<core::Unit> written;
      {
        std::lock_guard lock(output_mutex_);
        written = write_error(*output_, message.error().code,
                              message.error().message);
      }
      if (!written) {
        running_.store(false, std::memory_order_release);
        return mcp::core::unexpected(written.error());
      }
      continue;
    }

    if (const auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&*message)) {
      if (!initialized_ && !is_initialized_notification(*notification)) {
        running_.store(false, std::memory_order_release);
        return mcp::core::unexpected(make_transport_error(
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "stdio transport session is not initialized"));
      }
      if (notification_handler) {
        SessionContext context;
        context.session_id = "stdio";
        context.remote_address = "stdio";
        context.transport = this;
        context.transport_lifetime = lifetime_token();
        core::Result<core::Unit> handled;
        try {
          handled = notification_handler(*notification, context);
        } catch (const std::exception& ex) {
          handled = mcp::core::unexpected(errors::handler_failed(ex.what()));
        } catch (...) {
          handled = mcp::core::unexpected(errors::handler_unknown_exception());
        }
        if (!handled) {
          running_.store(false, std::memory_order_release);
          return mcp::core::unexpected(handled.error());
        }
      }
      if (is_initialized_notification(*notification)) {
        initialized_ = true;
      }
      continue;
    }

    const auto* request = std::get_if<protocol::JsonRpcRequest>(&*message);
    if (!request) {
      core::Result<core::Unit> written;
      {
        std::lock_guard lock(output_mutex_);
        written = write_error(
            *output_, static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "stdio transport expected a JSON-RPC request",
            request_id_from_message(*message));
      }
      if (!written) {
        running_.store(false, std::memory_order_release);
        return mcp::core::unexpected(written.error());
      }
      continue;
    }

    SessionContext context;
    context.session_id = "stdio";
    context.remote_address = "stdio";
    context.transport = this;
    context.transport_lifetime = lifetime_token();
    if (!initialized_ && !is_allowed_before_initialized(*request)) {
      core::Result<core::Unit> written;
      {
        std::lock_guard lock(output_mutex_);
        written = write_error(
            *output_, static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "stdio transport session is not initialized", request->id);
      }
      if (!written) {
        running_.store(false, std::memory_order_release);
        return mcp::core::unexpected(written.error());
      }
      continue;
    }

    if (request->method == protocol::InitializeMethod || !initialized_) {
      handle_request(*request, context);
      continue;
    }

    const auto request_key = request_id_to_string(request->id);
    bool duplicate_request = false;
    {
      std::lock_guard lock(worker_state_mutex);
      const auto [_, inserted] = in_flight_request_ids.insert(request_key);
      duplicate_request = !inserted;
    }
    if (duplicate_request) {
      core::Result<core::Unit> written;
      {
        std::lock_guard output_lock(output_mutex_);
        written = write_error(
            *output_, static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "duplicate in-flight request id", request->id);
      }
      if (!written) {
        running_.store(false, std::memory_order_release);
        wait_for_workers();
        return mcp::core::unexpected(written.error());
      }
      continue;
    }

    try {
      workers.emplace_back(
          [&, request_copy = *request, context, request_key]() mutable {
            handle_request(std::move(request_copy), std::move(context));
            {
              std::lock_guard lock(worker_state_mutex);
              in_flight_request_ids.erase(request_key);
            }
          });
    } catch (const std::system_error& ex) {
      {
        std::lock_guard lock(worker_state_mutex);
        in_flight_request_ids.erase(request_key);
      }
      running_.store(false, std::memory_order_release);
      wait_for_workers();
      return mcp::core::unexpected(make_transport_error(
          static_cast<int>(protocol::ErrorCode::InternalError),
          "failed to start stdio request worker", ex.what()));
    }
  }

  wait_for_workers();
  running_.store(false, std::memory_order_release);
  if (worker_error.has_value()) {
    return mcp::core::unexpected(*worker_error);
  }
  if (input_->bad()) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "failed to read stdio request"));
  }

  return core::Unit{};
}

core::Result<core::Unit> StdioTransport::send_notification(
    const protocol::JsonRpcNotification& notification) {
  if (!output_) {
    return mcp::core::unexpected(make_transport_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "stdio transport output stream is not configured"));
  }

  std::lock_guard lock(output_mutex_);
  return write_notification(*output_, notification);
}

std::optional<protocol::ClientCapabilities>
StdioTransport::client_capabilities() const {
  std::lock_guard lock(client_capabilities_mutex_);
  return client_capabilities_;
}

void StdioTransport::stop() noexcept {
  running_.store(false, std::memory_order_release);
}

std::string_view StdioTransport::name() const noexcept { return "stdio"; }

}  // namespace mcp::server
