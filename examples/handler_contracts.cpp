// Copyright (c) 2025 [caomengxuan666]
//
// SDK contract example: durable applications can keep callbacks in explicit
// handler objects while retaining raw request escape hatches for future MCP
// methods or vendor extensions.

#include <optional>
#include <stdexcept>
#include <string_view>

#include "cxxmcp/client.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/server/handler.hpp"

namespace {

using Json = mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

struct ServerContracts final : mcp::server::ServerHandlerInterface {
  std::optional<mcp::core::Result<Json>> on_completion(
      const Json& params,
      const mcp::server::SessionContext& context) const override {
    const auto prefix = params.value("prefix", std::string{});
    return Json{{"completion", context.session_id + ":" + prefix + "llo"}};
  }

  std::optional<mcp::protocol::JsonRpcResponse> on_raw_request(
      const mcp::protocol::JsonRpcRequest& request,
      const mcp::server::SessionContext&) const override {
    if (request.method == "example/health") {
      return mcp::protocol::make_response(request.id, Json{{"ok", true}});
    }
    return std::nullopt;
  }
};

struct ClientContracts final : mcp::client::ClientHandlerInterface {
  std::optional<mcp::core::Result<mcp::protocol::RootsListResult>>
  on_list_roots_request() const override {
    mcp::protocol::RootsListResult result;
    result.roots.push_back(mcp::protocol::Root{
        .uri = "file:///workspace",
        .name = "workspace",
    });
    return result;
  }
};

}  // namespace

int main() {
  mcp::server::Server server(
      mcp::server::ServerOptions{.server_name = "handler-contracts"});
  ServerContracts server_contracts;
  server.set_handler(server_contracts);

  const auto completed = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = std::string(mcp::protocol::CompletionCompleteMethod),
          .params = Json{{"prefix", "he"}},
          .id = std::int64_t{1},
      },
      mcp::server::SessionContext{.session_id = "session"});
  require(completed.has_value(), "completion request failed");
  require(completed->result.has_value(), "completion result missing");
  require(completed->result->at("completion") == "session:hello",
          "completion result mismatch");

  const auto health = server.handle_request(
      mcp::protocol::JsonRpcRequest{
          .method = "example/health",
          .params = Json::object(),
          .id = std::int64_t{2},
      },
      {});
  require(health.has_value(), "raw fallback request failed");
  require(health->result.has_value(), "raw fallback result missing");
  require(health->result->at("ok"), "raw fallback result mismatch");

  mcp::client::Client client(nullptr);
  ClientContracts client_contracts;
  client.set_handler(client_contracts);
  const auto roots = client.handle_request(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::RootsListMethod),
      .params = Json::object(),
      .id = std::int64_t{3},
  });
  require(roots.has_value(), "client roots request failed");
  require(roots->result.has_value(), "client roots result missing");
  require(roots->result->at("roots").at(0).at("uri") == "file:///workspace",
          "client roots result mismatch");

  return 0;
}
