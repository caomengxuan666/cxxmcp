// Copyright (c) 2025 [caomengxuan666]
//
// Compatibility example: demonstrates typed App builder registration. New SDK
// server applications should prefer ServerPeer plus Service over App::run().

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "cxxmcp/server.hpp"

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

void from_json(const Json& json, SearchArgs& args) {
  args.query = json.at("query").get<std::string>();
  args.limit = json.value("limit", 3);
}

void to_json(Json& json, const SearchHit& hit) {
  json = Json{
      {"title", hit.title},
      {"uri", hit.uri},
  };
}

void to_json(Json& json, const SearchResult& result) {
  json = Json{
      {"session", result.session},
      {"hits", result.hits},
  };
}

}  // namespace example

namespace mcp::protocol {

template <>
struct SchemaTraits<example::SearchArgs> {
  static Json schema() {
    return object_schema()
        .required_property("query", JsonSchema::string())
        .optional_property("limit", JsonSchema::integer())
        .additional_properties(false)
        .build();
  }
};

template <>
struct SchemaTraits<example::SearchResult> {
  static Json schema() {
    return object_schema()
        .required_property("session", JsonSchema::string())
        .required_property(
            "hits", JsonSchema::array(
                        object_schema()
                            .required_property("title", JsonSchema::string())
                            .required_property("uri", JsonSchema::string())
                            .additional_properties(false)
                            .build()))
        .additional_properties(false)
        .build();
  }
};

}  // namespace mcp::protocol

int main() {
  return mcp::server::App::builder()
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
