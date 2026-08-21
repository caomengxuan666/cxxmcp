// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Public SDK configuration and compatibility markers.

/// @brief Minimum C++ language level supported by public SDK headers.
///
/// Core SDK headers and exported package targets are expected to compile as
/// C++17 by default. Optional runtime tools may require a newer language mode.
#define CXXMCP_SDK_MIN_CXX_STANDARD 201703L

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(deprecated)
#define CXXMCP_DEPRECATED(message) [[deprecated(message)]]
#endif
#endif

#if !defined(CXXMCP_DEPRECATED)
#if defined(_MSC_VER)
#define CXXMCP_DEPRECATED(message) __declspec(deprecated(message))
#elif defined(__GNUC__) || defined(__clang__)
#define CXXMCP_DEPRECATED(message) __attribute__((deprecated(message)))
#else
#define CXXMCP_DEPRECATED(message)
#endif
#endif

/// @brief Marks compatibility-only APIs as deprecated when explicitly enabled.
///
/// Compatibility entry points stay warning-free by default so consumers using
/// warnings-as-errors, including MSVC C4996 via /WX, can keep building while
/// migrating. Define CXXMCP_ENABLE_COMPAT_DEPRECATION_WARNINGS to opt into the
/// migration warnings.
#if defined(CXXMCP_ENABLE_COMPAT_DEPRECATION_WARNINGS)
#define CXXMCP_COMPAT_DEPRECATED(message) CXXMCP_DEPRECATED(message)
#else
#define CXXMCP_COMPAT_DEPRECATED(message)
#endif
