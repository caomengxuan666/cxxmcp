// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <tl/expected.hpp>

/// @file
/// @brief Shared result and error primitives used by the public cxxmcp SDK.
///
/// This header keeps SDK APIs independent from exceptions at their public
/// boundaries. Operations that can fail return @ref mcp::core::Result with a
/// structured @ref mcp::core::Error so callers can propagate MCP or transport
/// failures without losing protocol error details.

namespace mcp::core {

/// @brief Creates an unexpected result value for the active expected backend.
template <class E>
constexpr auto unexpected(E&& value) {
  return tl::unexpected<std::decay_t<E> >(std::forward<E>(value));
}

/// @brief Structured error returned by fallible SDK operations.
///
/// The numeric @ref code is normally an MCP JSON-RPC error code or a
/// transport-specific status chosen by the component that failed. @ref message
/// is intended for user-visible or log-friendly summaries, while @ref detail
/// can carry diagnostic text such as a nested exception message, validation
/// failure, or transport response body.
struct Error {
  /// Numeric protocol, transport, or component-specific error code.
  int code = 0;

  /// Short human-readable explanation of the failure.
  std::string message;

  /// Optional extended diagnostic information.
  std::string detail;

  /// Stable SDK error category such as "protocol", "transport", "handler",
  /// "timeout", or "cancellation". Empty means the producer has not opted into
  /// categorized errors yet.
  std::string category;
};

/// @brief Success value for operations that only need to report failure.
///
/// This is used as `Result<Unit>` in APIs whose successful completion has no
/// payload, mirroring `void` while still fitting the `expected` style.
using Unit = std::monostate;

/// @brief Alias for the SDK result type.
///
/// Uses `tl::expected` for every supported C++ standard so compiled SDK
/// libraries and downstream consumers see the same public ABI even when the
/// consumer builds with C++23 or newer. The error side is always
/// mcp::core::Error.
template <typename T>
using Result = tl::expected<T, Error>;

/// @brief Compatibility helper matching `std::string_view::starts_with`.
inline bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

/// @brief Compatibility helper matching `std::string_view::starts_with`.
inline bool starts_with(std::string_view value, char prefix) {
  return !value.empty() && value.front() == prefix;
}

/// @brief Compatibility helper matching `std::string_view::ends_with`.
inline bool ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

/// @brief Compatibility helper matching `std::string_view::ends_with`.
inline bool ends_with(std::string_view value, char suffix) {
  return !value.empty() && value.back() == suffix;
}

}  // namespace mcp::core
