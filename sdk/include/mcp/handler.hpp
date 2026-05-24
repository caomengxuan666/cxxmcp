#pragma once

#include "mcp/client/handler.hpp"
#include "mcp/server/handler.hpp"

namespace mcp {

using ClientHandler = client::ClientHandler;
using ServerHandler = server::ServerHandler;

inline client::Client& attach_handler(client::Client& client, const ClientHandler& handler) {
    return client.set_handler(handler);
}

inline server::Server& attach_handler(server::Server& server, const ServerHandler& handler) {
    return server.set_handler(handler);
}

} // namespace mcp
