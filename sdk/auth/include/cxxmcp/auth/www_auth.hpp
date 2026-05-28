// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/auth/types.hpp"
#include "cxxmcp/core/result.hpp"

/// @file
/// @brief WWW-Authenticate challenge models and parser boundary.

namespace mcp::auth {

namespace detail {

struct WwwAuthenticateResourceMetadataParamTag {};
struct WwwAuthenticateErrorParamTag {};
struct WwwAuthenticateScopeParamTag {};
struct WwwAuthenticateInsufficientScopeErrorTag {};

}  // namespace detail

inline const std::string& WwwAuthenticateResourceMetadataParam =
    detail::static_string_constant<
        detail::WwwAuthenticateResourceMetadataParamTag>("resource_metadata");
inline const std::string& WwwAuthenticateErrorParam =
    detail::static_string_constant<detail::WwwAuthenticateErrorParamTag>(
        "error");
inline const std::string& WwwAuthenticateScopeParam =
    detail::static_string_constant<detail::WwwAuthenticateScopeParamTag>(
        "scope");
inline const std::string& WwwAuthenticateInsufficientScopeError =
    detail::static_string_constant<
        detail::WwwAuthenticateInsufficientScopeErrorTag>("insufficient_scope");

namespace detail {

inline bool is_http_token_char(char value) {
  const auto ch = static_cast<unsigned char>(value);
  return std::isalnum(ch) != 0 || value == '!' || value == '#' ||
         value == '$' || value == '%' || value == '&' || value == '\'' ||
         value == '*' || value == '+' || value == '-' || value == '.' ||
         value == '^' || value == '_' || value == '`' || value == '|' ||
         value == '~';
}

inline bool is_token68_char(char value) {
  const auto ch = static_cast<unsigned char>(value);
  return std::isalnum(ch) != 0 || value == '-' || value == '.' ||
         value == '_' || value == '~' || value == '+' || value == '/' ||
         value == '=';
}

inline void skip_ows(std::string_view input, std::size_t& pos) {
  while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t')) {
    ++pos;
  }
}

inline std::string parse_token(std::string_view input, std::size_t& pos) {
  const auto begin = pos;
  while (pos < input.size() && is_http_token_char(input[pos])) {
    ++pos;
  }
  return std::string(input.substr(begin, pos - begin));
}

inline std::string ascii_lower(std::string value) {
  for (auto& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
}

inline bool ascii_iequals(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    char left = lhs[index];
    char right = rhs[index];
    if (left >= 'A' && left <= 'Z') {
      left = static_cast<char>(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = static_cast<char>(right - 'A' + 'a');
    }
    if (left != right) {
      return false;
    }
  }
  return true;
}

inline core::Error www_auth_parse_error(std::string message) {
  return core::Error{1, std::move(message), {}, std::string(AuthErrorCategory)};
}

inline core::Result<std::string> parse_quoted_string(std::string_view input,
                                                     std::size_t& pos) {
  if (pos >= input.size() || input[pos] != '"') {
    return mcp::core::unexpected(
        www_auth_parse_error("expected quoted WWW-Authenticate value"));
  }
  ++pos;

  std::string result;
  while (pos < input.size()) {
    const auto ch = input[pos++];
    if (ch == '"') {
      return result;
    }
    if (ch == '\\') {
      if (pos >= input.size()) {
        return mcp::core::unexpected(
            www_auth_parse_error("unterminated WWW-Authenticate escape"));
      }
      result.push_back(input[pos++]);
      continue;
    }
    result.push_back(ch);
  }

  return mcp::core::unexpected(
      www_auth_parse_error("unterminated WWW-Authenticate quoted value"));
}

inline bool comma_starts_parameter(std::string_view input, std::size_t pos) {
  if (pos >= input.size() || input[pos] != ',') {
    return false;
  }
  ++pos;
  skip_ows(input, pos);
  const auto name_begin = pos;
  const auto name = parse_token(input, pos);
  if (name.empty() || name_begin >= input.size()) {
    return false;
  }
  skip_ows(input, pos);
  if (pos >= input.size() || input[pos] != '=') {
    return false;
  }
  ++pos;
  skip_ows(input, pos);
  return pos < input.size() &&
         (input[pos] == '"' || is_http_token_char(input[pos]));
}

inline bool starts_parameter(std::string_view input, std::size_t pos) {
  skip_ows(input, pos);
  const auto name_begin = pos;
  auto name = parse_token(input, pos);
  if (name.empty()) {
    return false;
  }
  skip_ows(input, pos);
  if (name_begin >= input.size() || pos >= input.size() || input[pos] != '=') {
    return false;
  }
  ++pos;
  skip_ows(input, pos);
  return pos < input.size() &&
         (input[pos] == '"' || is_http_token_char(input[pos]));
}

}  // namespace detail

/// @brief Parsed authentication challenge from a WWW-Authenticate header.
struct WwwAuthenticateChallenge {
  std::string scheme;
  /// token68 payload for schemes that use it instead of key-value parameters.
  std::optional<std::string> token68;
  MetadataMap parameters;

  std::string parameter(std::string key) const {
    const auto iter = parameters.find(key);
    if (iter != parameters.end()) {
      return iter->second;
    }
    const auto normalized = detail::ascii_lower(std::move(key));
    const auto normalized_iter = parameters.find(normalized);
    return normalized_iter == parameters.end() ? std::string{}
                                               : normalized_iter->second;
  }

  /// @brief Returns true when this is a Bearer challenge.
  bool bearer() const { return detail::ascii_iequals(scheme, "Bearer"); }
};

/// @brief Returns the MCP OAuth protected-resource metadata URL, when present.
inline std::optional<std::string> resource_metadata_url(
    const WwwAuthenticateChallenge& challenge) {
  const auto value =
      challenge.parameter(std::string(WwwAuthenticateResourceMetadataParam));
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

/// @brief Returns true for an OAuth insufficient_scope challenge.
inline bool insufficient_scope(const WwwAuthenticateChallenge& challenge) {
  return challenge.parameter(std::string(WwwAuthenticateErrorParam)) ==
         WwwAuthenticateInsufficientScopeError;
}

/// @brief Returns the first resource_metadata parameter in parsed challenges.
inline std::optional<std::string> first_resource_metadata_url(
    const std::vector<WwwAuthenticateChallenge>& challenges) {
  for (const auto& challenge : challenges) {
    auto value = resource_metadata_url(challenge);
    if (value.has_value()) {
      return value;
    }
  }
  return std::nullopt;
}

/// @brief Parser boundary for HTTP authentication challenges.
///
/// The implementation is cxxmcp-owned because MCP OAuth needs precise handling
/// for `resource_metadata` and `insufficient_scope` challenges.
class WwwAuthenticateParser {
 public:
  virtual ~WwwAuthenticateParser() = default;

  virtual core::Result<std::vector<WwwAuthenticateChallenge>> parse(
      const std::string& header_value) const = 0;
};

/// @brief Default RFC-style parser for WWW-Authenticate challenge headers.
///
/// The parser handles comma-separated challenges, auth-param key/value pairs,
/// quoted strings with backslash escapes, and token68 payloads. Parameter names
/// are normalized to lowercase for case-insensitive lookup.
class DefaultWwwAuthenticateParser final : public WwwAuthenticateParser {
 public:
  core::Result<std::vector<WwwAuthenticateChallenge>> parse(
      const std::string& header_value) const override {
    const std::string_view input(header_value);
    std::vector<WwwAuthenticateChallenge> challenges;
    std::size_t pos = 0;

    while (true) {
      detail::skip_ows(input, pos);
      while (pos < input.size() && input[pos] == ',') {
        ++pos;
        detail::skip_ows(input, pos);
      }
      if (pos >= input.size()) {
        break;
      }

      auto scheme = detail::parse_token(input, pos);
      if (scheme.empty()) {
        return mcp::core::unexpected(detail::www_auth_parse_error(
            "expected WWW-Authenticate auth scheme"));
      }

      WwwAuthenticateChallenge challenge;
      challenge.scheme = std::move(scheme);
      detail::skip_ows(input, pos);

      if (pos < input.size() && input[pos] != ',') {
        if (!detail::starts_parameter(input, pos)) {
          auto token68 = parse_token68(input, pos);
          if (!token68.has_value()) {
            return mcp::core::unexpected(token68.error());
          }
          challenge.token68 = std::move(*token68);
        } else {
          auto parsed = parse_parameters(input, pos, challenge);
          if (!parsed.has_value()) {
            return mcp::core::unexpected(parsed.error());
          }
        }
      }

      challenges.push_back(std::move(challenge));
      if (pos >= input.size()) {
        break;
      }
      if (input[pos] == ',') {
        ++pos;
        continue;
      }
      return mcp::core::unexpected(detail::www_auth_parse_error(
          "expected comma after WWW-Authenticate challenge"));
    }

    return challenges;
  }

 private:
  static core::Result<std::string> parse_token68(std::string_view input,
                                                 std::size_t& pos) {
    const auto begin = pos;
    while (pos < input.size() && detail::is_token68_char(input[pos])) {
      ++pos;
    }
    if (begin == pos) {
      return mcp::core::unexpected(
          detail::www_auth_parse_error("expected WWW-Authenticate token68"));
    }
    const auto token68 = std::string(input.substr(begin, pos - begin));
    detail::skip_ows(input, pos);
    if (pos < input.size() && input[pos] != ',') {
      return mcp::core::unexpected(detail::www_auth_parse_error(
          "unexpected character after WWW-Authenticate token68"));
    }
    return token68;
  }

  static core::Result<core::Unit> parse_parameters(
      std::string_view input, std::size_t& pos,
      WwwAuthenticateChallenge& challenge) {
    while (pos < input.size()) {
      detail::skip_ows(input, pos);

      const auto name = detail::parse_token(input, pos);
      if (name.empty()) {
        return mcp::core::unexpected(detail::www_auth_parse_error(
            "expected WWW-Authenticate parameter name"));
      }

      detail::skip_ows(input, pos);
      if (pos >= input.size() || input[pos] != '=') {
        return mcp::core::unexpected(detail::www_auth_parse_error(
            "expected '=' after WWW-Authenticate parameter name"));
      }
      ++pos;
      detail::skip_ows(input, pos);

      auto value = parse_parameter_value(input, pos);
      if (!value.has_value()) {
        return mcp::core::unexpected(value.error());
      }
      challenge.parameters[detail::ascii_lower(name)] = std::move(*value);

      detail::skip_ows(input, pos);
      if (pos >= input.size()) {
        return core::Unit{};
      }
      if (input[pos] != ',') {
        return mcp::core::unexpected(detail::www_auth_parse_error(
            "expected comma after WWW-Authenticate parameter"));
      }
      if (!detail::comma_starts_parameter(input, pos)) {
        return core::Unit{};
      }
      ++pos;
    }

    return core::Unit{};
  }

  static core::Result<std::string> parse_parameter_value(std::string_view input,
                                                         std::size_t& pos) {
    if (pos >= input.size()) {
      return mcp::core::unexpected(detail::www_auth_parse_error(
          "expected WWW-Authenticate parameter value"));
    }
    if (input[pos] == '"') {
      return detail::parse_quoted_string(input, pos);
    }
    auto value = detail::parse_token(input, pos);
    if (value.empty()) {
      return mcp::core::unexpected(detail::www_auth_parse_error(
          "expected WWW-Authenticate token parameter value"));
    }
    return value;
  }
};

/// @brief Parse a WWW-Authenticate header with the default parser.
inline core::Result<std::vector<WwwAuthenticateChallenge>>
parse_www_authenticate(const std::string& header_value) {
  return DefaultWwwAuthenticateParser{}.parse(header_value);
}

}  // namespace mcp::auth
