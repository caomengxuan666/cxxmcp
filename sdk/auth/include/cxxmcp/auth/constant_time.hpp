// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <cstddef>
#include <string_view>

/// @file
/// @brief Constant-time comparison helpers for auth secrets and lookup keys.

namespace mcp::auth {

/// @brief Compare two strings without data-dependent early exit.
///
/// The loop covers the longer input so equal-prefix mismatches and length
/// mismatches do not return early through std::string::operator==.
inline bool constant_time_string_equal(std::string_view lhs,
                                       std::string_view rhs) noexcept {
  const auto max_size = lhs.size() > rhs.size() ? lhs.size() : rhs.size();
  std::size_t diff = lhs.size() ^ rhs.size();
  for (std::size_t index = 0; index < max_size; ++index) {
    const auto lhs_byte =
        index < lhs.size() ? static_cast<unsigned char>(lhs[index]) : 0U;
    const auto rhs_byte =
        index < rhs.size() ? static_cast<unsigned char>(rhs[index]) : 0U;
    diff |= static_cast<std::size_t>(lhs_byte ^ rhs_byte);
  }
  return diff == 0;
}

}  // namespace mcp::auth
