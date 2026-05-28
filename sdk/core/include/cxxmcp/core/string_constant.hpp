// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

/// @file
/// @brief Zero-dynamic-initialization string constants for public headers.

namespace mcp::core {

/// @brief String-like literal wrapper safe for inline public constants.
///
/// `inline const std::string` and `inline const std::string&` constants both
/// require dynamic initialization in each consuming binary. Some MinGW/clang
/// and libc++ configurations are sensitive to that during process startup.
/// This wrapper keeps constants as literal pointers while preserving normal
/// string usage at call sites.
class StringConstant {
 public:
  constexpr explicit StringConstant(const char* value) noexcept
      : value_(value) {}

  constexpr const char* c_str() const noexcept { return value_; }
  constexpr std::string_view view() const noexcept {
    return std::string_view(value_);
  }

  explicit constexpr operator const char*() const noexcept { return value_; }
  constexpr operator std::string_view() const noexcept { return view(); }
  operator std::string() const { return std::string(value_); }

 private:
  const char* value_;
};

inline bool operator==(StringConstant lhs, StringConstant rhs) noexcept {
  return lhs.view() == rhs.view();
}

inline bool operator==(StringConstant lhs, std::string_view rhs) noexcept {
  return lhs.view() == rhs;
}

inline bool operator==(std::string_view lhs, StringConstant rhs) noexcept {
  return lhs == rhs.view();
}

inline bool operator==(StringConstant lhs, const std::string& rhs) noexcept {
  return rhs == lhs.c_str();
}

inline bool operator==(const std::string& lhs, StringConstant rhs) noexcept {
  return lhs == rhs.c_str();
}

inline bool operator==(StringConstant lhs, const char* rhs) noexcept {
  return lhs.view() == std::string_view(rhs == nullptr ? "" : rhs);
}

inline bool operator==(const char* lhs, StringConstant rhs) noexcept {
  return std::string_view(lhs == nullptr ? "" : lhs) == rhs.view();
}

inline bool operator!=(StringConstant lhs, StringConstant rhs) noexcept {
  return !(lhs == rhs);
}

inline bool operator!=(StringConstant lhs, std::string_view rhs) noexcept {
  return !(lhs == rhs);
}

inline bool operator!=(std::string_view lhs, StringConstant rhs) noexcept {
  return !(lhs == rhs);
}

inline bool operator!=(StringConstant lhs, const std::string& rhs) noexcept {
  return !(lhs == rhs);
}

inline bool operator!=(const std::string& lhs, StringConstant rhs) noexcept {
  return !(lhs == rhs);
}

inline bool operator!=(StringConstant lhs, const char* rhs) noexcept {
  return !(lhs == rhs);
}

inline bool operator!=(const char* lhs, StringConstant rhs) noexcept {
  return !(lhs == rhs);
}

namespace detail {

template <typename T, typename = void>
struct IsJsonStringLike : std::false_type {};

template <typename T>
struct IsJsonStringLike<
    T,
    std::void_t<decltype(std::declval<const T&>().is_string()),
                decltype(std::declval<const T&>().template get<std::string>())>>
    : std::true_type {};

}  // namespace detail

template <typename JsonLike,
          std::enable_if_t<detail::IsJsonStringLike<JsonLike>::value, int> = 0>
inline bool operator==(const JsonLike& lhs, StringConstant rhs) {
  return lhs.is_string() && lhs.template get<std::string>() == rhs.c_str();
}

template <typename JsonLike,
          std::enable_if_t<detail::IsJsonStringLike<JsonLike>::value, int> = 0>
inline bool operator==(StringConstant lhs, const JsonLike& rhs) {
  return rhs == lhs;
}

template <typename JsonLike,
          std::enable_if_t<detail::IsJsonStringLike<JsonLike>::value, int> = 0>
inline bool operator!=(const JsonLike& lhs, StringConstant rhs) {
  return !(lhs == rhs);
}

template <typename JsonLike,
          std::enable_if_t<detail::IsJsonStringLike<JsonLike>::value, int> = 0>
inline bool operator!=(StringConstant lhs, const JsonLike& rhs) {
  return !(lhs == rhs);
}

inline std::ostream& operator<<(std::ostream& stream, StringConstant value) {
  return stream << value.c_str();
}

}  // namespace mcp::core
