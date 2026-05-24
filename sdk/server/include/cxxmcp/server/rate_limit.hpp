#pragma once

#include "cxxmcp/core/result.hpp"

#include <chrono>
#include <string>

/// @file
/// @brief Rate limiting extension points for server request admission.

namespace mcp::server {

    /// @brief Transport-neutral request facts used for rate limit decisions.
    struct RateLimitRequest {
        /// Subject being limited, typically an authenticated principal or remote address.
        std::string subject;
        /// MCP method name associated with the request.
        std::string method;
        /// Approximate serialized request size in bytes, when available.
        std::size_t request_bytes = 0;
    };

    /// @brief Outcome returned by a RateLimiter.
    struct RateLimitDecision {
        /// True when the request may proceed.
        bool allowed = true;
        /// Suggested retry delay for rejected requests; zero means unspecified.
        std::chrono::milliseconds retry_after{0};
    };

    /// @brief Abstract policy hook for request rate limiting.
    ///
    /// RateLimiter implementations are called before a request is dispatched to the
    /// application handler. They do not own the RateLimitRequest and should return
    /// policy denials as successful decisions with allowed set to false. Backend or
    /// configuration failures should be returned as core::Error.
    class RateLimiter {
    public:
        virtual ~RateLimiter() = default;

        /// @brief Check whether a request should be admitted.
        /// @param request Subject, method, and optional size information.
        /// @return A decision on success, or a core::Error for limiter failures.
        /// @note Implementations may be shared by concurrently running transports.
        virtual core::Result<RateLimitDecision> check(const RateLimitRequest &request) = 0;
    };

}// namespace mcp::server
