#pragma once

#include "mcp/core/result.hpp"

#include <string>
#include <unordered_map>

namespace mcp::server {

struct AuthRequest {
    std::unordered_map<std::string, std::string> headers;
    std::string remote_address;
};

struct AuthIdentity {
    std::string subject;
    std::unordered_map<std::string, std::string> claims;
};

class AuthProvider {
public:
    virtual ~AuthProvider() = default;
    virtual core::Result<AuthIdentity> authenticate(const AuthRequest& request) = 0;
};

} // namespace mcp::server

