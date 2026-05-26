// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/config.hpp>

int main() {
  static_assert(CXXMCP_SDK_MIN_CXX_STANDARD >= 201703L,
                "cxxmcp SDK public headers require at least C++17");
  return 0;
}
