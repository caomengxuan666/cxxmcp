// Copyright (c) 2025 [caomengxuan666]
//
// Demonstrates typed ServerPeer builder registration with reflected structs.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"

namespace example {

using Json = mcp::protocol::Json;

struct SearchArgs {
  std::string query;
  int limit = 3;
};

struct SearchHit {
  std::string title;
  std::string uri;
};

struct SearchResult {
  std::string session;
  std::vector<SearchHit> hits;
};

}  // namespace example

namespace mcp::protocol {

template <>
struct Reflect<example::SearchArgs> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(field("query", &example::SearchArgs::query),
                           field("limit", &example::SearchArgs::limit));
  }
  static std::vector<std::string> known_keys() { return {"query", "limit"}; }
};

template <>
struct Reflect<example::SearchHit> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(field("title", &example::SearchHit::title),
                           field("uri", &example::SearchHit::uri));
  }
  static std::vector<std::string> known_keys() { return {"title", "uri"}; }
};

template <>
struct Reflect<example::SearchResult> {
  static constexpr bool defined = true;
  static auto fields() {
    return std::make_tuple(field("session", &example::SearchResult::session),
                           field("hits", &example::SearchResult::hits));
  }
  static std::vector<std::string> known_keys() { return {"session", "hits"}; }
};

}  // namespace mcp::protocol

int main() {
  return mcp::ServerPeer::builder()
      .name("cxxmcp-example-typed-stdio-server")
      .version("1.0.0")
      .instructions("Typed MCP server authoring example.")
      .stdio()
      .tool(mcp::server::tool<example::SearchArgs, example::SearchResult>(
                "search")
                .title("Search")
                .description("Search local documents.")
                .annotations(mcp::protocol::Json{{"readOnlyHint", true}})
                .meta(mcp::protocol::Json{{"example", "typed_stdio_server"}})
                .handler([](example::SearchArgs args,
                            const mcp::server::ToolContext& context) {
                  const int limit = std::clamp(args.limit, 1, 5);
                  example::SearchResult result;
                  result.session =
                      context.session_id.empty() ? "stdio" : context.session_id;

                  for (int index = 0; index < limit; ++index) {
                    result.hits.push_back(example::SearchHit{
                        .title =
                            args.query + " result " + std::to_string(index + 1),
                        .uri = "file:///workspace/" + args.query + "/" +
                               std::to_string(index + 1),
                    });
                  }
                  return result;
                }))
      .raw_request([](const mcp::protocol::JsonRpcRequest& request)
                       -> std::optional<mcp::protocol::JsonRpcResponse> {
        if (request.method == "example/health") {
          return mcp::protocol::make_response(
              request.id, mcp::protocol::Json{
                              {"ok", true},
                              {"server", "cxxmcp-example-typed-stdio-server"},
                          });
        }
        return std::nullopt;
      })
      .run();
}
