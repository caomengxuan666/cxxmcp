// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Role-generic newline-delimited stdio transport.

#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/transport/transport.hpp"

namespace mcp::transport {

/// @brief Message-level stdio transport for either MCP role.
///
/// Messages are encoded as one JSON-RPC document per line. receive() reads the
/// next line and parses it as a JsonRpcMessage; EOF is reported as
/// std::nullopt.
template <class Role>
class StdioTransport final : public Transport<Role> {
 public:
  using TxMessage = typename Transport<Role>::TxMessage;
  using RxMessage = typename Transport<Role>::RxMessage;

  StdioTransport(std::istream& input, std::ostream& output)
      : input_(&input), output_(&output) {}

  std::string_view name() const noexcept override { return "stdio"; }

  core::Result<core::Unit> send(TxMessage message) override {
    const auto serialized = protocol::serialize_message(message);
    if (!serialized) {
      return std::unexpected(serialized.error());
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return std::unexpected(transport_error(
          protocol::ErrorCode::InvalidRequest, "transport is closed"));
    }
    (*output_) << *serialized << '\n';
    output_->flush();
    if (!*output_) {
      return std::unexpected(
          transport_error(protocol::ErrorCode::InternalError,
                          "failed to write transport message"));
    }
    return core::Unit{};
  }

  core::Result<std::optional<RxMessage>> receive() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return std::nullopt;
      }
    }

    std::string line;
    if (!std::getline(*input_, line)) {
      return std::nullopt;
    }
    if (line.empty()) {
      return std::unexpected(transport_error(protocol::ErrorCode::ParseError,
                                             "empty transport message"));
    }

    const auto parsed = protocol::parse_message(line);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    return RxMessage{*parsed};
  }

  core::Result<core::Unit> close() override {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    return core::Unit{};
  }

 private:
  static core::Error transport_error(protocol::ErrorCode code,
                                     std::string message) {
    return core::Error{
        static_cast<int>(code),
        std::move(message),
        {},
    };
  }

  std::istream* input_;
  std::ostream* output_;
  std::mutex mutex_;
  bool closed_ = false;
};

using ClientStdioTransport = StdioTransport<RoleClient>;
using ServerStdioTransport = StdioTransport<RoleServer>;

}  // namespace mcp::transport
