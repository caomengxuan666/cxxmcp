// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief JOSE base64url helpers shared by optional OpenSSL auth code.

namespace mcp::auth::openssl {

enum class JoseErrorCode {
  kInvalidBase64Url = 2001,
  kInvalidCompactJws = 2002,
  kInvalidJoseHeader = 2003,
  kInvalidJwk = 2004,
  kUnsupportedJoseAlgorithm = 2005,
  kSignatureVerificationFailed = 2006,
  kJwtClaimValidationFailed = 2007,
};

inline core::Error make_jose_error(JoseErrorCode code, std::string message,
                                   std::string detail = {}) {
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail), std::string(AuthErrorCategory)};
}

inline std::string base64url_encode_bytes(const unsigned char* data,
                                          std::size_t size) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string output;
  output.reserve(((size + 2) / 3) * 4);

  for (std::size_t index = 0; index < size; index += 3) {
    const auto remaining = size - index;
    const auto b0 = data[index];
    const auto b1 = remaining > 1 ? data[index + 1] : 0;
    const auto b2 = remaining > 2 ? data[index + 2] : 0;

    output.push_back(kAlphabet[(b0 >> 2) & 0x3F]);
    output.push_back(kAlphabet[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
    if (remaining > 1) {
      output.push_back(kAlphabet[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)]);
    }
    if (remaining > 2) {
      output.push_back(kAlphabet[b2 & 0x3F]);
    }
  }
  return output;
}

inline std::string base64url_encode(std::string_view data) {
  return base64url_encode_bytes(
      reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

inline std::string base64url_encode(const std::vector<unsigned char>& data) {
  return base64url_encode_bytes(data.data(), data.size());
}

namespace detail {

inline int base64url_decode_value(char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return ch - 'A';
  }
  if (ch >= 'a' && ch <= 'z') {
    return ch - 'a' + 26;
  }
  if (ch >= '0' && ch <= '9') {
    return ch - '0' + 52;
  }
  if (ch == '-') {
    return 62;
  }
  if (ch == '_') {
    return 63;
  }
  return -1;
}

}  // namespace detail

inline core::Result<std::vector<unsigned char>> base64url_decode(
    std::string_view input) {
  if (input.find('=') != std::string_view::npos) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidBase64Url,
                        "base64url input must not contain padding"));
  }
  if (input.size() % 4 == 1) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidBase64Url,
                        "base64url input has invalid length"));
  }

  std::vector<unsigned char> output;
  output.reserve((input.size() * 3) / 4);

  std::uint32_t accumulator = 0;
  int bits = 0;
  for (const char ch : input) {
    const int value = detail::base64url_decode_value(ch);
    if (value < 0) {
      return core::unexpected(make_jose_error(
          JoseErrorCode::kInvalidBase64Url,
          "base64url input contains an invalid character", std::string(1, ch)));
    }

    accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(
          static_cast<unsigned char>((accumulator >> bits) & 0xFF));
    }
  }

  const std::uint32_t unused_mask =
      bits == 0 ? 0U : ((1U << static_cast<unsigned int>(bits)) - 1U);
  if ((accumulator & unused_mask) != 0U) {
    return core::unexpected(
        make_jose_error(JoseErrorCode::kInvalidBase64Url,
                        "base64url input contains non-zero trailing bits"));
  }

  return output;
}

inline core::Result<std::string> base64url_decode_to_string(
    std::string_view input) {
  auto decoded = base64url_decode(input);
  if (!decoded.has_value()) {
    return core::unexpected(decoded.error());
  }
  return std::string(decoded->begin(), decoded->end());
}

}  // namespace mcp::auth::openssl
