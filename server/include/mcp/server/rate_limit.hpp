#pragma once

#include "mcp/core/result.hpp"

#include <chrono>
#include <string>

namespace mcp::server {

struct RateLimitRequest {
    std::string subject;
    std::string method;
    std::size_t request_bytes = 0;
};

struct RateLimitDecision {
    bool allowed = true;
    std::chrono::milliseconds retry_after{0};
};

class RateLimiter {
public:
    virtual ~RateLimiter() = default;
    virtual core::Result<RateLimitDecision> check(const RateLimitRequest& request) = 0;
};

} // namespace mcp::server

