// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/protocol.hpp>

namespace example {

struct SearchArgs {};

struct SearchResult {};

}  // namespace example

namespace mcp::protocol {

template <>
struct SchemaTraits<example::SearchArgs> {
  static Json schema() {
    return object_schema()
        .required_property("query", JsonSchema::string())
        .additional_properties(false)
        .build();
  }
};

template <>
struct SchemaTraits<example::SearchResult> {
  static Json schema() {
    return object_schema()
        .required_property("matches", JsonSchema::array(JsonSchema::string()))
        .build();
  }
};

}  // namespace mcp::protocol

int main() {
  auto tool = mcp::protocol::tool_definition("search")
                  .input<const example::SearchArgs&>()
                  .output<example::SearchResult>()
                  .build();
  if (!tool.input_schema.is_object() || !tool.output_schema_present) {
    return 1;
  }
  if (!tool.input_schema.at("properties").contains("query")) {
    return 1;
  }
  if (!tool.output_schema.at("properties").contains("matches")) {
    return 1;
  }
  return 0;
}
