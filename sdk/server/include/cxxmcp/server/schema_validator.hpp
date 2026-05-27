// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Pluggable JSON Schema validation contract for server-side SDK paths.

#include <string>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::server {

/// @brief Server-side SDK location that requested schema validation.
enum class SchemaValidationTarget {
  ToolInput,
  ToolOutput,
  Elicitation,
};

/// @brief Metadata supplied to a user-provided JSON Schema validator.
struct SchemaValidationContext {
  /// Validation site within the SDK.
  SchemaValidationTarget target = SchemaValidationTarget::ToolInput;
  /// Tool name for tool input/output validation.
  std::string tool_name;
};

/// @brief Interface for integrating a JSON Schema validator implementation.
///
/// The SDK owns the validation call sites and error mapping. Implementations
/// can delegate to nlohmann-json-schema-validator, valijson, or another JSON
/// Schema engine without making that dependency part of the core SDK.
class JsonSchemaValidator {
 public:
  virtual ~JsonSchemaValidator() = default;

  /// @brief Validate an instance against a schema.
  /// @return Unit on success, or a diagnostic error on validation failure.
  virtual core::Result<core::Unit> validate(
      const protocol::Json& schema, const protocol::Json& instance,
      const SchemaValidationContext& context) const = 0;
};

}  // namespace mcp::server
