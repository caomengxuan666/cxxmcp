#pragma once

#include "cxxmcp/core/result.hpp"

#include <string>
#include <unordered_map>

/// @file
/// @brief Authentication extension points for server transports.

namespace mcp::server {

    /// @brief Transport-neutral authentication input.
    ///
    /// AuthRequest contains only data that can be obtained before dispatching an
    /// MCP request. Header names and remote address formats are transport-specific;
    /// implementations should normalize them before policy checks when needed.
    struct AuthRequest {
        /// Request headers or metadata supplied by the transport.
        std::unordered_map<std::string, std::string> headers;
        /// Best-effort remote address for audit and policy decisions.
        std::string remote_address;
    };

    /// @brief Authenticated principal and associated claims.
    struct AuthIdentity {
        /// Stable principal identifier, such as a user id, service account, or token subject.
        std::string subject;
        /// Provider-specific claims copied by value into the identity.
        std::unordered_map<std::string, std::string> claims;
    };

    /// @brief Abstract authentication provider used by server integrations.
    ///
    /// The provider does not own request data; authenticate() receives a borrowed
    /// AuthRequest for the duration of the call and returns a value identity on
    /// success. Authentication failures and provider errors are propagated through
    /// core::Result so the caller can map them to protocol or HTTP errors.
    class AuthProvider {
    public:
        virtual ~AuthProvider() = default;

        /// @brief Authenticate a transport request.
        /// @param request Headers and remote metadata to evaluate.
        /// @return AuthIdentity on success, or a core::Error describing denial or
        /// provider failure.
        /// @note Implementations may be called concurrently by transports that
        /// dispatch requests on multiple threads.
        virtual core::Result<AuthIdentity> authenticate(const AuthRequest &request) = 0;
    };

}// namespace mcp::server
