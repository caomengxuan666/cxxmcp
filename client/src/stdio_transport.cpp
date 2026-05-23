#include "mcp/client/stdio_transport.hpp"

#include "mcp/protocol/serialization.hpp"

#include <iostream>
#include <string>
#include <utility>

namespace mcp::client {

namespace {

core::Error make_transport_error(int code, std::string message, std::string detail = {}) {
    return core::Error{code, std::move(message), std::move(detail)};
}

} // namespace

StdioTransport::StdioTransport()
    : StdioTransport(std::cin, std::cout) {}

StdioTransport::StdioTransport(std::istream& input, std::ostream& output)
    : input_(&input),
      output_(&output) {}

core::Result<protocol::JsonRpcResponse> StdioTransport::send(const protocol::JsonRpcRequest& request) {
    if (!input_ || !output_) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "stdio transport streams are not configured"));
    }

    const auto serialized = protocol::serialize_request(request);
    if (!serialized) {
        return std::unexpected(serialized.error());
    }

    *output_ << *serialized << '\n';
    output_->flush();
    if (!output_->good()) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "failed to write stdio request"));
    }

    std::string line;
    if (!std::getline(*input_, line)) {
        return std::unexpected(make_transport_error(static_cast<int>(protocol::ErrorCode::InternalError),
                                                    "failed to read stdio response"));
    }

    return protocol::parse_response(line);
}

} // namespace mcp::client
