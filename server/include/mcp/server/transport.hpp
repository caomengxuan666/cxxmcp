#pragma once

#include "mcp/core/result.hpp"
#include "mcp/protocol/capabilities.hpp"
#include "mcp/protocol/types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace mcp::server {

class Transport;
class ClientPeer;

struct SessionContext {
    std::string session_id;
    std::string remote_address;
    Transport* transport = nullptr;

    ClientPeer client() const noexcept;
};

using RequestHandler = std::function<core::Result<protocol::JsonRpcResponse>(
        const protocol::JsonRpcRequest&,
        const SessionContext&)>;
using NotificationHandler = std::function<core::Result<core::Unit>(
        const protocol::JsonRpcNotification&,
        const SessionContext&)>;

class Transport {
public:
    virtual ~Transport() = default;
    virtual core::Result<core::Unit> start(RequestHandler handler, NotificationHandler notification_handler = {}) = 0;
    virtual core::Result<protocol::JsonRpcResponse> send_request(const protocol::JsonRpcRequest& request) {
        (void)request;
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::MethodNotFound),
            "transport does not support outbound requests",
            {},
        });
    }
    virtual std::optional<protocol::ClientCapabilities> client_capabilities() const {
        return std::nullopt;
    }
    virtual core::Result<core::Unit> send_notification(const protocol::JsonRpcNotification& notification) = 0;
    virtual void stop() noexcept = 0;
    virtual std::string_view name() const noexcept = 0;
};

} // namespace mcp::server

