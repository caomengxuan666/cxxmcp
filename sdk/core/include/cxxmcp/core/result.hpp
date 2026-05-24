// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__cpp_lib_expected)
#include <expected>
#else
#include <tl/expected.hpp>
namespace std {
template <class E>
constexpr auto unexpected(E&& value) {
  return tl::unexpected<std::decay_t<E> >(std::forward<E>(value));
}
}  // namespace std
#endif

/// @file
/// @brief Shared result and error primitives used by the public cxxmcp SDK.
///
/// This header keeps SDK APIs independent from exceptions at their public
/// boundaries. Operations that can fail return @ref mcp::core::Result with a
/// structured @ref mcp::core::Error so callers can propagate MCP or transport
/// failures without losing protocol error details.

namespace mcp::core {

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
};

/// @brief Success value for operations that only need to report failure.
///
/// This is used as `Result<Unit>` in APIs whose successful completion has no
/// payload, mirroring `void` while still fitting the `expected` style.
using Unit = std::monostate;

#if defined(__cpp_lib_expected)
/// @brief Alias for the SDK result type.
///
/// Uses `std::expected` when the standard library provides it; otherwise the
/// SDK falls back to `tl::expected` with the same value/error shape. The error
/// side is always @ref Error.
template <typename T>
using Result = std::expected<T, Error>;
#else
/// @brief Alias for the SDK result type.
///
/// Uses `tl::expected` on toolchains that do not yet provide
/// `std::expected`. The error side is always @ref Error.
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
#endif

}  // namespace mcp::core
