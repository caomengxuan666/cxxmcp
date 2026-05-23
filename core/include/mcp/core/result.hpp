#pragma once

#include <expected>
#include <string>
#include <variant>

namespace mcp::core {

struct Error {
    int code = 0;
    std::string message;
    std::string detail;
};

using Unit = std::monostate;

template <typename T>
using Result = std::expected<T, Error>;

} // namespace mcp::core

