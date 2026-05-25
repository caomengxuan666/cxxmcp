// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/protocol.hpp>
#include <utility>

int main() {
  auto schema =
      mcp::protocol::object_schema()
          .required_property("value", mcp::protocol::JsonSchema::string())
          .build();
  auto tool = mcp::protocol::tool_definition("echo")
                  .input_schema(std::move(schema))
                  .build();
  return tool.input_schema.is_object() ? 0 : 1;
}
