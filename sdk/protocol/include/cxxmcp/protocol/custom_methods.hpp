// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file cxxmcp/protocol/custom_methods.hpp
/// @brief Generic typed wrappers for MCP extension methods.
///
/// CustomRequest and CustomNotification let library users define new MCP method
/// families without modifying the core protocol headers.  Each type carries a
/// compile-time method name, typed params, and (for requests) a typed result.

#include <cstdint>
#include <string>
#include <string_view>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::protocol {

/// @brief Generic typed MCP request for extension methods.
///
/// @tparam Params  A struct with a `to_json()` free function.
/// @tparam Result  A struct with a `from_json(const Json&) -> Result<T>` free
///                 function.
///
/// Example:
/// @code
///   struct MyParams { int value; };
///   Json my_params_to_json(const MyParams& p) { return {{"value", p.value}}; }
///
///   struct MyResult { int answer; };
///   Json my_result_to_json(const MyResult& r) {
///     return {{"answer", r.answer}};
///   }
///   core::Result<MyResult> my_result_from_json(const Json& j) { ... }
///
///   using MyRequest = CustomRequest<MyParams, MyResult>;
///   constexpr auto method = MyRequest::method_name("my/extension");
/// @endcode
template <class Params, class Result>
struct CustomRequest {
  /// Method name on the wire.
  std::string method;
  /// Typed request parameters.
  Params params;
  /// Request id for JSON-RPC correlation.
  RequestId id;

  /// @brief Creates a JsonRpcRequest envelope from this typed request.
  /// @param serializer Function that converts Params to JSON.
  template <class Serializer>
  JsonRpcRequest to_json_rpc(Serializer serializer) const {
    return make_request(method, id, serializer(params));
  }

  /// @brief Parses a JsonRpcResponse into a typed Result.
  /// @param response JSON-RPC response to parse.
  /// @param deserializer Function that converts JSON to Result.
  template <class Deserializer>
  core::Result<Result> parse_response(const JsonRpcResponse& response,
                                      Deserializer deserializer) const {
    if (response.has_error()) {
      return mcp::core::unexpected(
          core::Error{response.error->code, response.error->message,
                      response.error->data.value_or(Json::object())});
    }
    if (!response.has_result()) {
      return mcp::core::unexpected(
          core::Error{static_cast<int>(ErrorCode::InternalError),
                      "custom request response has neither result nor error",
                      {}});
    }
    return deserializer(*response.result);
  }
};

/// @brief Generic typed MCP notification for extension methods.
///
/// @tparam Params  A struct with a `to_json()` free function.
///
/// Example:
/// @code
///   struct MyEvent { std::string name; };
///   Json my_event_to_json(const MyEvent& e) { return {{"name", e.name}}; }
///
///   using MyNotification = CustomNotification<MyEvent>;
/// @endcode
template <class Params>
struct CustomNotification {
  /// Method name on the wire.
  std::string method;
  /// Typed notification parameters.
  Params params;

  /// @brief Creates a JsonRpcNotification envelope from this typed
  /// notification.
  /// @param serializer Function that converts Params to JSON.
  template <class Serializer>
  JsonRpcNotification to_json_rpc(Serializer serializer) const {
    return make_notification(method, serializer(params));
  }
};

}  // namespace mcp::protocol
